#pragma once

#include "Downloader.h"

#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class StatusKind { Neutral, Busy, Success, Warning, Error };

    void buildUi();
    void applyStyle();
    void updateControls();
    void setStatus(const QString &text, StatusKind kind);
    void resetVideoInfo();
    bool ensureTools();

    void onPaste();
    void onGetInfo();
    void onBrowse();
    void onDownload();
    void onCancel();
    void onOpenFolder();

    void onInfoReady(const VideoInfo &info);
    void onInfoFailed(const QString &error);
    void onProgress(const DownloadProgress &p);
    void onStageChanged(Downloader::Stage stage);
    void onFinished(const QString &path);
    void onFailed(const QString &error);
    void onCancelled();

    Downloader *m_downloader = nullptr;
    VideoInfo m_info;
    bool m_hasInfo = false;
    QString m_lastFile;

    QLineEdit *m_urlEdit = nullptr;
    QPushButton *m_pasteBtn = nullptr;
    QPushButton *m_infoBtn = nullptr;
    QLabel *m_titleLabel = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLineEdit *m_folderEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_detailsLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_downloadBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
};
