#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

// One entry in the resolution dropdown.
struct QualityOption {
    QString label;        // e.g. "1080p" or "Best Available (2160p)"
    int tier = 0;         // 2160, 1440, 1080, ...
    QString formatSpec;   // yt-dlp -f argument
    QList<double> streamSizes; // estimated bytes per downloaded stream (video, audio)
};

struct VideoInfo {
    QString url;
    QString title;
    qint64 durationSec = -1;
    QList<QualityOption> options; // first entry is "Best Available"
};

struct DownloadProgress {
    double percent = 0;     // 0..100
    double speed = -1;      // bytes/s, -1 = unknown
    double downloaded = -1; // bytes
    double total = -1;      // bytes
    qint64 eta = -1;        // seconds, -1 = unknown
};

class Downloader : public QObject
{
    Q_OBJECT
public:
    enum class Stage { Idle, FetchingInfo, Downloading, Merging };
    Q_ENUM(Stage)

    explicit Downloader(QObject *parent = nullptr);
    ~Downloader() override;

    static QString toolsDir();
    static QString ytDlpPath();
    static QString ffmpegPath();
    static QStringList jsRuntimeArgs();
    // Returns an empty string when everything is fine, otherwise a user-facing error.
    static QString checkTools();
    static bool isSupportedUrl(const QString &url);

    // Parses `yt-dlp -J` output. Exposed for testing.
    static bool parseInfoJson(const QByteArray &json, VideoInfo &info, QString *error);

    bool isBusy() const { return m_stage != Stage::Idle; }
    Stage stage() const { return m_stage; }

    void fetchInfo(const QString &url);
    // `saveDir` must exist; `baseName` must already be sanitized (no extension).
    // Returns the final file path that will be produced.
    QString startDownload(const QString &url, const QualityOption &option,
                          const QString &saveDir, const QString &baseName);
    void cancel();
    // Cancel and block until the process tree is gone (used when closing the app).
    void cancelAndWait();

signals:
    void stageChanged(Downloader::Stage stage);
    void infoReady(const VideoInfo &info);
    void infoFailed(const QString &error);
    void progressChanged(const DownloadProgress &progress);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);
    void downloadCancelled();

private:
    void setStage(Stage s);
    void launchDownload();
    QProcess *createProcess();
    void onReadyRead();
    void handleLine(const QString &line);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void killTree();
    void cleanupTemp();
    QString extractError() const;

    QProcess *m_proc = nullptr;
    Stage m_stage = Stage::Idle;
    bool m_cancelRequested = false;
    QByteArray m_buffer;
    QByteArray m_infoJson;
    QStringList m_errorLines;
    QString m_url;

    // Download state
    QualityOption m_option;
    QString m_saveDir;
    QString m_baseName;
    int m_attempt = 0;
    QString m_finalPath;
    QString m_tempDir;
    QList<double> m_streamSizes;
    int m_streamIndex = -1;
    double m_lastPercent = 0;
};
