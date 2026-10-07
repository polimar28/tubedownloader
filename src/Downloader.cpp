#include "Downloader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

// Standard quality tiers: {label height, nominal long side}.
struct Tier { int height; int longSide; };
constexpr Tier kTiers[] = {
    {4320, 7680}, {2160, 3840}, {1440, 2560}, {1080, 1920},
    {720, 1280},  {480, 854},   {360, 640},
};

// Maps a width x height to a standard tier. Works for landscape, ultra-wide,
// 4:3 and vertical (Shorts) videos. Returns 0 when below 360p.
int tierFor(int w, int h)
{
    const int longSide = std::max(w, h);
    const int shortSide = std::min(w, h);
    for (const Tier &t : kTiers) {
        if (longSide >= t.longSide * 0.85 || shortSide >= t.height * 0.85)
            return t.height;
    }
    return 0;
}

int codecScore(const QString &vcodec)
{
    // Prefer H.264 for maximum MP4 compatibility, then VP9, then AV1.
    if (vcodec.startsWith(QLatin1String("avc1")) || vcodec.startsWith(QLatin1String("h264")))
        return 3;
    if (vcodec.startsWith(QLatin1String("vp09")) || vcodec.startsWith(QLatin1String("vp9")))
        return 2;
    if (vcodec.startsWith(QLatin1String("av01")))
        return 1;
    return 0;
}

double estimatedSize(const QJsonObject &f, qint64 duration)
{
    double s = f.value(QLatin1String("filesize")).toDouble(0);
    if (s <= 0)
        s = f.value(QLatin1String("filesize_approx")).toDouble(0);
    if (s <= 0 && duration > 0)
        s = f.value(QLatin1String("tbr")).toDouble(0) * 125.0 * double(duration); // kbit/s -> bytes
    return s;
}

struct Candidate {
    QString id;
    double size = 0;
    QList<double> key; // lexicographic score, higher is better
};

bool better(const Candidate &a, const Candidate &b)
{
    return std::lexicographical_compare(b.key.begin(), b.key.end(), a.key.begin(), a.key.end());
}

QString escapeTemplate(QString s)
{
    // yt-dlp output templates treat '%' specially.
    return s.replace(QLatin1Char('%'), QLatin1String("%%"));
}

} // namespace

Downloader::Downloader(QObject *parent) : QObject(parent) {}

Downloader::~Downloader()
{
    cancelAndWait();
}

QString Downloader::toolsDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools"));
}

QString Downloader::ytDlpPath()
{
    return QDir(toolsDir()).filePath(QStringLiteral("yt-dlp.exe"));
}

QString Downloader::ffmpegPath()
{
    return QDir(toolsDir()).filePath(QStringLiteral("ffmpeg.exe"));
}

QStringList Downloader::jsRuntimeArgs()
{
    // Optional: YouTube extraction works best with a JavaScript runtime. If deno.exe is
    // bundled in tools/, tell yt-dlp to use it.
    const QString deno = QDir(toolsDir()).filePath(QStringLiteral("deno.exe"));
    if (QFileInfo(deno).isFile())
        return {QStringLiteral("--js-runtimes"), QStringLiteral("deno:") + QDir::toNativeSeparators(deno)};
    return {};
}

QString Downloader::checkTools()
{
    QStringList missing;
    if (!QFileInfo(ytDlpPath()).isFile())
        missing << QDir::toNativeSeparators(ytDlpPath());
    if (!QFileInfo(ffmpegPath()).isFile())
        missing << QDir::toNativeSeparators(ffmpegPath());
    if (missing.isEmpty())
        return {};
    return tr("Required tool(s) not found:\n\n%1\n\nPlace yt-dlp.exe and ffmpeg.exe in the "
              "\"tools\" folder next to TubeDownloader.exe.")
        .arg(missing.join(QLatin1Char('\n')));
}

bool Downloader::isSupportedUrl(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
        return false;
    const QString host = url.host().toLower();
    static const QStringList domains = {
        QStringLiteral("youtube.com"), QStringLiteral("youtu.be"), QStringLiteral("youtube-nocookie.com")};
    for (const QString &d : domains) {
        if (host == d || host.endsWith(QLatin1Char('.') + d))
            return true;
    }
    return false;
}

bool Downloader::parseInfoJson(const QByteArray &json, VideoInfo &info, QString *error)
{
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = tr("Could not read video information.");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("_type")).toString() == QLatin1String("playlist")) {
        if (error)
            *error = tr("This link is a playlist. Please paste a link to a single video.");
        return false;
    }

    info.title = root.value(QLatin1String("title")).toString().trimmed();
    info.durationSec = qint64(root.value(QLatin1String("duration")).toDouble(-1));
    info.options.clear();

    const qint64 dur = info.durationSec;
    QMap<int, Candidate> bestVideoOnly;   // tier -> best video-only format
    QMap<int, Candidate> bestProgressive; // tier -> best format with audio+video
    Candidate bestAudio;

    const QJsonArray formats = root.value(QLatin1String("formats")).toArray();
    for (const QJsonValue &v : formats) {
        const QJsonObject f = v.toObject();
        const QString id = f.value(QLatin1String("format_id")).toString();
        const QString ext = f.value(QLatin1String("ext")).toString();
        const QString vcodec = f.value(QLatin1String("vcodec")).toString(QStringLiteral("none"));
        const QString acodec = f.value(QLatin1String("acodec")).toString(QStringLiteral("none"));
        const QString protocol = f.value(QLatin1String("protocol")).toString();
        if (id.isEmpty() || ext == QLatin1String("mhtml") || f.value(QLatin1String("has_drm")).toBool())
            continue;

        const bool hasVideo = vcodec != QLatin1String("none") && !vcodec.isEmpty();
        const bool hasAudio = acodec != QLatin1String("none") && !acodec.isEmpty();
        // Direct HTTPS (DASH) gives accurate progress; HLS is a fallback.
        const double protoScore = protocol.startsWith(QLatin1String("http")) ? 1 : 0;
        const double size = estimatedSize(f, dur);

        if (hasVideo) {
            const int w = f.value(QLatin1String("width")).toInt();
            const int h = f.value(QLatin1String("height")).toInt();
            const int tier = tierFor(w, h);
            if (tier == 0)
                continue;
            Candidate c;
            c.id = id;
            c.size = size;
            c.key = {protoScore, double(codecScore(vcodec)), f.value(QLatin1String("fps")).toDouble(),
                     double(std::max(w, h)), f.value(QLatin1String("tbr")).toDouble()};
            QMap<int, Candidate> &map = hasAudio ? bestProgressive : bestVideoOnly;
            if (!map.contains(tier) || better(c, map[tier]))
                map[tier] = c;
        } else if (hasAudio) {
            Candidate c;
            c.id = id;
            c.size = size;
            const QString note = f.value(QLatin1String("format_note")).toString().toLower();
            const double original = note.contains(QLatin1String("original")) ? 1 : 0;
            const double langPref = f.value(QLatin1String("language_preference")).toDouble(0);
            const double isM4a = (ext == QLatin1String("m4a") || acodec.startsWith(QLatin1String("mp4a"))) ? 1 : 0;
            // Avoid YouTube's dynamic-range-compressed ("-drc") variants.
            const double notDrc = (id.endsWith(QLatin1String("-drc")) || note.contains(QLatin1String("drc"))) ? 0 : 1;
            c.key = {original, langPref, notDrc, protoScore, isM4a, f.value(QLatin1String("abr")).toDouble()};
            if (bestAudio.id.isEmpty() || better(c, bestAudio))
                bestAudio = c;
        }
    }

    // Build one option per available tier, highest first.
    for (const Tier &t : kTiers) {
        QualityOption opt;
        opt.tier = t.height;
        opt.label = QStringLiteral("%1p").arg(t.height);
        if (bestVideoOnly.contains(t.height) && !bestAudio.id.isEmpty()) {
            const Candidate &vid = bestVideoOnly[t.height];
            // Exact pick first; fall back to yt-dlp's own choice at this height if it fails.
            opt.formatSpec = QStringLiteral("%1+%2/%1+ba/bv*[height<=%3]+ba/b[height<=%3]")
                                 .arg(vid.id, bestAudio.id).arg(t.height);
            opt.streamSizes = {vid.size, bestAudio.size};
        } else if (bestProgressive.contains(t.height)) {
            const Candidate &p = bestProgressive[t.height];
            opt.formatSpec = QStringLiteral("%1/b[height<=%2]").arg(p.id).arg(t.height);
            opt.streamSizes = {p.size};
        } else if (bestVideoOnly.contains(t.height)) {
            // Video without any audio track available at all.
            const Candidate &vid = bestVideoOnly[t.height];
            opt.formatSpec = vid.id;
            opt.streamSizes = {vid.size};
        } else {
            continue;
        }
        info.options.append(opt);
    }

    if (info.options.isEmpty()) {
        // Nothing parsable (e.g. very low quality only): let yt-dlp decide.
        QualityOption opt;
        opt.label = tr("Best Available");
        opt.formatSpec = QStringLiteral("bv*+ba/b");
        info.options.append(opt);
    } else {
        QualityOption best = info.options.first();
        best.label = tr("Best Available (%1p)").arg(best.tier);
        info.options.prepend(best);
    }

    if (info.title.isEmpty())
        info.title = tr("Untitled video");
    return true;
}

void Downloader::setStage(Stage s)
{
    if (m_stage == s)
        return;
    m_stage = s;
    emit stageChanged(s);
}

QProcess *Downloader::createProcess()
{
    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    p->setProcessEnvironment(env);
    p->setProgram(ytDlpPath());
    p->setWorkingDirectory(toolsDir());
    connect(p, &QProcess::readyReadStandardOutput, this, &Downloader::onReadyRead);
    connect(p, &QProcess::readyReadStandardError, this, &Downloader::onReadyRead);
    connect(p, &QProcess::finished, this, &Downloader::onFinished);
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && p == m_proc)
            onFinished(-1, QProcess::CrashExit);
    });
    return p;
}

void Downloader::fetchInfo(const QString &url)
{
    if (isBusy())
        return;
    m_url = url.trimmed();
    m_cancelRequested = false;
    m_buffer.clear();
    m_infoJson.clear();
    m_errorLines.clear();

    m_proc = createProcess();
    // Arguments are passed directly (no shell). "--" ensures the URL can never be read as an option.
    QStringList args = jsRuntimeArgs();
    args << QStringLiteral("-J") << QStringLiteral("--no-playlist") << QStringLiteral("--no-warnings")
         << QStringLiteral("--encoding") << QStringLiteral("utf-8") << QStringLiteral("--") << m_url;
    m_proc->setArguments(args);
    setStage(Stage::FetchingInfo);
    m_proc->start();
}

QString Downloader::startDownload(const QString &url, const QualityOption &option,
                                  const QString &saveDir, const QString &baseName)
{
    if (isBusy())
        return {};
    m_url = url.trimmed();
    m_option = option;
    m_saveDir = saveDir;
    m_baseName = baseName;
    m_attempt = 0;
    m_lastPercent = 0;
    m_finalPath = QDir::toNativeSeparators(QDir(saveDir).filePath(baseName + QStringLiteral(".mp4")));
    launchDownload();
    return m_finalPath;
}

void Downloader::launchDownload()
{
    const QualityOption &option = m_option;
    const QString &saveDir = m_saveDir;
    const QString &baseName = m_baseName;
    m_cancelRequested = false;
    m_buffer.clear();
    m_errorLines.clear();
    m_streamSizes = option.streamSizes;
    m_streamIndex = -1;

    const QDir dir(saveDir);
    // Private temporary folder on the same drive so that the final move is instant
    // and partial files can be removed reliably on cancel/failure.
    m_tempDir = dir.filePath(QStringLiteral(".tubedownloader-tmp-%1")
                                 .arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(m_tempDir);

    QStringList args = {
        QStringLiteral("-f"), option.formatSpec,
        QStringLiteral("--merge-output-format"), QStringLiteral("mp4"),
        QStringLiteral("--remux-video"), QStringLiteral("mp4"),
        QStringLiteral("--ffmpeg-location"), QDir::toNativeSeparators(ffmpegPath()),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--no-overwrites"),
        QStringLiteral("--no-mtime"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--encoding"), QStringLiteral("utf-8"),
        QStringLiteral("--newline"),
        QStringLiteral("--progress"),
        QStringLiteral("--progress-template"),
        QStringLiteral("download:TDLPROG|%(progress.status)s|%(progress.downloaded_bytes)s|"
                       "%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|"
                       "%(progress.speed)s|%(progress.eta)s"),
        QStringLiteral("-P"), QStringLiteral("home:") + QDir::toNativeSeparators(saveDir),
        QStringLiteral("-P"), QStringLiteral("temp:") + QDir::toNativeSeparators(m_tempDir),
        QStringLiteral("-o"), escapeTemplate(baseName) + QStringLiteral(".%(ext)s"),
        QStringLiteral("--"), m_url,
    };

    args = jsRuntimeArgs() + args;
    m_proc = createProcess();
    m_proc->setArguments(args);
    setStage(Stage::Downloading);
    if (m_attempt == 0)
        emit progressChanged(DownloadProgress{});
    m_proc->start();
}

void Downloader::onReadyRead()
{
    if (!m_proc)
        return;
    const QByteArray out = m_proc->readAllStandardOutput();
    if (m_stage == Stage::FetchingInfo)
        m_infoJson += out; // JSON goes to stdout
    else
        m_buffer += out;
    m_buffer += m_proc->readAllStandardError();

    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(nl)).trimmed();
        m_buffer.remove(0, nl + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

void Downloader::handleLine(const QString &line)
{
    if (line.startsWith(QLatin1String("ERROR:"))) {
        m_errorLines << line;
        return;
    }
    if (m_stage == Stage::FetchingInfo)
        return;

    if (line.startsWith(QLatin1String("[download] Destination:"))) {
        // A new stream (video, then audio) starts downloading.
        ++m_streamIndex;
        while (m_streamSizes.size() <= m_streamIndex)
            m_streamSizes.append(0);
        return;
    }
    if (line.startsWith(QLatin1String("[Merger]")) || line.startsWith(QLatin1String("[VideoRemuxer]"))
        || line.startsWith(QLatin1String("[FixupM3u8]"))) {
        setStage(Stage::Merging);
        DownloadProgress p;
        p.percent = 100;
        emit progressChanged(p);
        return;
    }
    if (!line.startsWith(QLatin1String("TDLPROG|")))
        return;

    const QStringList parts = line.split(QLatin1Char('|'));
    if (parts.size() < 7)
        return;
    auto num = [](const QString &s) {
        bool ok = false;
        const double v = s.toDouble(&ok);
        return ok && std::isfinite(v) ? v : -1.0;
    };
    const double downloaded = num(parts[2]);
    double total = num(parts[3]);
    if (total <= 0)
        total = num(parts[4]);
    const double speed = num(parts[5]);
    const double eta = num(parts[6]);

    if (m_streamIndex < 0) {
        m_streamIndex = 0;
        if (m_streamSizes.isEmpty())
            m_streamSizes.append(0);
    }
    if (total > 0)
        m_streamSizes[m_streamIndex] = total;

    double done = 0, all = 0;
    for (int i = 0; i < m_streamSizes.size(); ++i) {
        const double s = std::max(0.0, m_streamSizes[i]);
        all += s;
        if (i < m_streamIndex)
            done += s;
    }
    if (downloaded > 0)
        done += downloaded;

    DownloadProgress p;
    p.speed = speed;
    p.downloaded = done;
    p.total = all > 0 ? all : -1;
    if (all > 0) {
        p.percent = std::clamp(done * 100.0 / all, 0.0, 100.0);
        // Never let the bar jump backwards when size estimates change.
        p.percent = std::max(p.percent, m_lastPercent);
        m_lastPercent = p.percent;
        if (speed > 0)
            p.eta = qint64(std::max(0.0, all - done) / speed);
    } else {
        p.percent = m_lastPercent;
    }
    if (p.eta < 0 && eta >= 0 && m_streamIndex == m_streamSizes.size() - 1)
        p.eta = qint64(eta);
    emit progressChanged(p);
}

QString Downloader::extractError() const
{
    if (m_errorLines.isEmpty())
        return tr("yt-dlp exited unexpectedly.");
    QString e = m_errorLines.last();
    e.remove(QRegularExpression(QStringLiteral("^ERROR:\\s*(\\[[^\\]]+\\]\\s*)?([\\w-]{6,}:\\s*)?")));
    return e.trimmed().isEmpty() ? m_errorLines.last() : e.trimmed();
}

void Downloader::onFinished(int exitCode, QProcess::ExitStatus status)
{
    QProcess *proc = m_proc;
    if (!proc)
        return;
    onReadyRead();
    if (!m_buffer.trimmed().isEmpty())
        handleLine(QString::fromUtf8(m_buffer).trimmed());
    m_buffer.clear();
    m_proc = nullptr;
    proc->deleteLater();

    const Stage stage = m_stage;
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    const QString startError = proc->error() == QProcess::FailedToStart
        ? tr("Could not start yt-dlp.exe:\n%1").arg(proc->errorString()) : QString();

    // YouTube occasionally rejects stream URLs (HTTP 403 / 5xx). A fresh attempt gets
    // new URLs and usually succeeds, so retry once without bothering the user.
    if (stage == Stage::Downloading && !ok && !m_cancelRequested && startError.isEmpty() && m_attempt < 1) {
        const QString err = m_errorLines.join(QLatin1Char('\n'));
        if (err.contains(QLatin1String("HTTP Error 403")) || err.contains(QLatin1String("HTTP Error 5"))) {
            ++m_attempt;
            cleanupTemp();
            launchDownload();
            return;
        }
    }
    setStage(Stage::Idle);

    if (stage == Stage::FetchingInfo) {
        if (m_cancelRequested)
            return;
        if (!ok) {
            emit infoFailed(startError.isEmpty() ? extractError() : startError);
            return;
        }
        VideoInfo info;
        info.url = m_url;
        QString err;
        if (!parseInfoJson(m_infoJson, info, &err))
            emit infoFailed(err);
        else
            emit infoReady(info);
        return;
    }

    // Download finished
    if (m_cancelRequested) {
        // The final file did not exist before we started (unique name), so any file there is ours.
        if (!m_finalPath.isEmpty() && QFileInfo::exists(m_finalPath))
            QFile::remove(m_finalPath);
        cleanupTemp();
        emit downloadCancelled();
        return;
    }
    if (ok && QFileInfo(m_finalPath).isFile()) {
        cleanupTemp();
        emit downloadFinished(m_finalPath);
        return;
    }
    cleanupTemp();
    emit downloadFailed(startError.isEmpty()
                            ? (ok ? tr("The downloaded file could not be found.") : extractError())
                            : startError);
}

void Downloader::killTree()
{
    if (!m_proc || m_proc->state() == QProcess::NotRunning)
        return;
    // yt-dlp spawns ffmpeg; kill the whole process tree so nothing keeps running.
    const qint64 pid = m_proc->processId();
    if (pid > 0) {
        QProcess::execute(QStringLiteral("taskkill"),
                          {QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"), QStringLiteral("/F")});
    }
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
}

void Downloader::cancel()
{
    if (!m_proc)
        return;
    m_cancelRequested = true;
    killTree();
}

void Downloader::cancelAndWait()
{
    if (!m_proc)
        return;
    m_cancelRequested = true;
    killTree();
    if (m_proc)
        m_proc->waitForFinished(5000);
    if (m_proc) // finished signal may not have been delivered yet
        onFinished(-1, QProcess::CrashExit);
}

void Downloader::cleanupTemp()
{
    if (m_tempDir.isEmpty())
        return;
    const QString dir = m_tempDir;
    m_tempDir.clear();
    // Files can stay locked for a moment after the processes exit; retry briefly.
    for (int attempt = 0; attempt < 10; ++attempt) {
        QDir d(dir);
        if (!d.exists() || d.removeRecursively())
            return;
        QThread::msleep(100);
    }
}
