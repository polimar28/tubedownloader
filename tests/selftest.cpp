// Small console self-test for filename handling and yt-dlp JSON parsing.
// Usage: tdl_selftest [path/to/info.json]
#include "Downloader.h"
#include "FileUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int g_failures = 0;

static void check(bool cond, const char *what, const QString &detail = {})
{
    std::printf("[%s] %s %s\n", cond ? "PASS" : "FAIL", what, qPrintable(detail));
    if (!cond)
        ++g_failures;
}

int runDownload(const QString &url, int tier, const QString &dir, double cancelAt)
{
    Downloader d;
    QEventLoop loop;
    int rc = 1;
    QObject::connect(&d, &Downloader::infoFailed, [&](const QString &e) {
        std::printf("INFO FAILED: %s\n", qPrintable(e));
        loop.quit();
    });
    QObject::connect(&d, &Downloader::infoReady, [&](const VideoInfo &info) {
        QualityOption opt = info.options.first();
        for (const QualityOption &o : info.options)
            if (o.tier == tier) { opt = o; break; }
        const QString base = FileUtils::sanitizeFileName(info.title);
        const QString path = FileUtils::uniqueFilePath(dir, base, QStringLiteral("mp4"));
        std::printf("Title: %s\nQuality: %s\nTarget: %s\n", info.title.toUtf8().constData(),
                    qPrintable(opt.label), path.toUtf8().constData());
        // Start on the next event-loop turn (the info process is finishing).
        const QString url = info.url;
        QTimer::singleShot(0, [&d, url, opt, path, dir] { d.startDownload(url, opt, dir, QFileInfo(path).completeBaseName()); });
    });
    QObject::connect(&d, &Downloader::stageChanged, [&](Downloader::Stage s) {
        if (s == Downloader::Stage::Merging)
            std::printf("\nSTAGE: Merging audio and video\n");
    });
    double lastShown = -10;
    QObject::connect(&d, &Downloader::progressChanged, [&](const DownloadProgress &p) {
        if (p.percent - lastShown >= 10 || p.percent >= 100) {
            lastShown = p.percent;
            std::printf("  %5.1f%%  %s/s  %s / %s  ETA %s\n", p.percent, qPrintable(FileUtils::formatBytes(p.speed)),
                        qPrintable(FileUtils::formatBytes(p.downloaded)), qPrintable(FileUtils::formatBytes(p.total)),
                        qPrintable(FileUtils::formatDuration(p.eta)));
            std::fflush(stdout);
        }
        if (cancelAt >= 0 && p.percent >= cancelAt && d.stage() == Downloader::Stage::Downloading) {
            std::printf("CANCELLING at %.1f%%\n", p.percent);
            d.cancel();
        }
    });
    QObject::connect(&d, &Downloader::downloadFinished, [&](const QString &p) {
        std::printf("COMPLETED: %s (%lld bytes)\n", p.toUtf8().constData(), QFileInfo(p).size());
        rc = 0;
        loop.quit();
    });
    QObject::connect(&d, &Downloader::downloadFailed, [&](const QString &e) {
        std::printf("FAILED: %s\n", qPrintable(e));
        loop.quit();
    });
    QObject::connect(&d, &Downloader::downloadCancelled, [&] {
        std::printf("CANCELLED\n");
        rc = 0;
        loop.quit();
    });
    d.fetchInfo(url);
    loop.exec();
    return rc;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    using namespace FileUtils;

    // tdl_selftest --download <url> <tier> <dir> [cancelAtPercent]
    if (argc >= 5 && QByteArray(argv[1]) == "--download")
        return runDownload(QString::fromLocal8Bit(argv[2]), QByteArray(argv[3]).toInt(),
                           QString::fromLocal8Bit(argv[4]), argc > 5 ? QByteArray(argv[5]).toDouble() : -1);

    check(sanitizeFileName(QStringLiteral("a<b>c:d\"e/f\\g|h?i*j")) == QStringLiteral("a b c d e f g h i j"), "illegal chars",
          sanitizeFileName(QStringLiteral("a<b>c:d\"e/f\\g|h?i*j")));
    check(sanitizeFileName(QStringLiteral("  Title...  ")) == QStringLiteral("Title"), "trailing dots/spaces");
    check(sanitizeFileName(QStringLiteral("CON")) == QStringLiteral("_CON"), "reserved name");
    check(sanitizeFileName(QStringLiteral("nul.txt")) == QStringLiteral("_nul.txt"), "reserved name w/ ext");
    check(sanitizeFileName(QStringLiteral("???")) == QStringLiteral("video"), "empty fallback");
    check(sanitizeFileName(QStringLiteral("Tab\tNew\nLine")) == QStringLiteral("TabNewLine"), "control chars");
    check(sanitizeFileName(QString(300, QLatin1Char('x'))).size() == 150, "length limit");
    check(sanitizeFileName(QStringLiteral("日本語 🎬 100% fun")) == QStringLiteral("日本語 🎬 100% fun"), "unicode kept");
    check(sanitizeFileName(QStringLiteral("../../evil")) == QStringLiteral("evil"), "no traversal",
          sanitizeFileName(QStringLiteral("../../evil")));

    QTemporaryDir tmp;
    const QString p0 = uniqueFilePath(tmp.path(), QStringLiteral("Video"), QStringLiteral("mp4"));
    check(p0.endsWith(QStringLiteral("Video.mp4")), "unique: first", p0);
    QFile(p0).open(QIODevice::WriteOnly);
    const QString p1 = uniqueFilePath(tmp.path(), QStringLiteral("Video"), QStringLiteral("mp4"));
    check(p1.endsWith(QStringLiteral("Video (1).mp4")), "unique: second", p1);
    QFile(p1).open(QIODevice::WriteOnly);
    const QString p2 = uniqueFilePath(tmp.path(), QStringLiteral("Video"), QStringLiteral("mp4"));
    check(p2.endsWith(QStringLiteral("Video (2).mp4")), "unique: third", p2);

    check(Downloader::isSupportedUrl(QStringLiteral("https://www.youtube.com/watch?v=abc")), "url: youtube.com");
    check(Downloader::isSupportedUrl(QStringLiteral("https://youtu.be/abc")), "url: youtu.be");
    check(!Downloader::isSupportedUrl(QStringLiteral("https://evil-youtube.com/x")), "url: lookalike rejected");
    check(!Downloader::isSupportedUrl(QStringLiteral("--exec calc")), "url: option rejected");
    check(!Downloader::isSupportedUrl(QStringLiteral("file:///C:/x")), "url: file rejected");

    if (argc > 1) {
        QFile f(QString::fromLocal8Bit(argv[1]));
        if (f.open(QIODevice::ReadOnly)) {
            VideoInfo info;
            QString err;
            const bool ok = Downloader::parseInfoJson(f.readAll(), info, &err);
            check(ok, "parse json", err);
            std::printf("Title: %s\n", info.title.toUtf8().constData());
            for (const QualityOption &o : info.options) {
                QStringList sizes;
                for (double s : o.streamSizes)
                    sizes << formatBytes(s);
                std::printf("  %-24s -f %-45s sizes: %s\n", qPrintable(o.label), qPrintable(o.formatSpec),
                            qPrintable(sizes.join(QStringLiteral(" + "))));
            }
        }
    }

    std::printf("\n%s (%d failure(s))\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
