#include "MainWindow.h"
#include "FileUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryFile>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const char *kSaveDirKey = "saveDir";

QString defaultDownloadsDir()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (d.isEmpty())
        d = QDir::homePath();
    return QDir::toNativeSeparators(d);
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_downloader = new Downloader(this);
    connect(m_downloader, &Downloader::infoReady, this, &MainWindow::onInfoReady);
    connect(m_downloader, &Downloader::infoFailed, this, &MainWindow::onInfoFailed);
    connect(m_downloader, &Downloader::progressChanged, this, &MainWindow::onProgress);
    connect(m_downloader, &Downloader::stageChanged, this, &MainWindow::onStageChanged);
    connect(m_downloader, &Downloader::downloadFinished, this, &MainWindow::onFinished);
    connect(m_downloader, &Downloader::downloadFailed, this, &MainWindow::onFailed);
    connect(m_downloader, &Downloader::downloadCancelled, this, &MainWindow::onCancelled);

    buildUi();
    applyStyle();

    // Restore the last used folder, falling back to the user's Downloads folder.
    QSettings settings;
    QString dir = settings.value(QLatin1String(kSaveDirKey)).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = defaultDownloadsDir();
    m_folderEdit->setText(QDir::toNativeSeparators(dir));

    setStatus(tr("Paste a YouTube link to get started."), StatusKind::Neutral);
    updateControls();

    const QString toolError = Downloader::checkTools();
    if (!toolError.isEmpty())
        setStatus(tr("Missing yt-dlp.exe or ffmpeg.exe in the tools folder."), StatusKind::Error);
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Tube Downloader"));
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 18, 20, 14);
    root->setSpacing(12);

    auto *header = new QLabel(tr("Tube Downloader"), central);
    header->setObjectName(QStringLiteral("header"));
    root->addWidget(header);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);

    auto makeCaption = [central](const QString &t) {
        auto *l = new QLabel(t, central);
        l->setObjectName(QStringLiteral("caption"));
        return l;
    };

    // Row 0: URL
    m_urlEdit = new QLineEdit(central);
    m_urlEdit->setPlaceholderText(tr("https://www.youtube.com/watch?v=..."));
    m_urlEdit->setClearButtonEnabled(true);
    m_pasteBtn = new QPushButton(tr("Paste"), central);
    m_infoBtn = new QPushButton(tr("Get Video Info"), central);
    m_infoBtn->setObjectName(QStringLiteral("secondary"));
    auto *urlRow = new QHBoxLayout;
    urlRow->setSpacing(8);
    urlRow->addWidget(m_urlEdit, 1);
    urlRow->addWidget(m_pasteBtn);
    urlRow->addWidget(m_infoBtn);
    grid->addWidget(makeCaption(tr("Video URL")), 0, 0);
    grid->addLayout(urlRow, 0, 1);

    // Row 1: Title
    m_titleLabel = new QLabel(QStringLiteral("—"), central);
    m_titleLabel->setObjectName(QStringLiteral("title"));
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setMinimumWidth(100);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    grid->addWidget(makeCaption(tr("Title")), 1, 0);
    grid->addWidget(m_titleLabel, 1, 1);

    // Row 2: Quality + format
    m_qualityCombo = new QComboBox(central);
    m_qualityCombo->setMinimumWidth(200);
    m_formatLabel = new QLabel(tr("Format:  <b>MP4</b>"), central);
    m_formatLabel->setObjectName(QStringLiteral("format"));
    auto *qualRow = new QHBoxLayout;
    qualRow->setSpacing(16);
    qualRow->addWidget(m_qualityCombo);
    qualRow->addWidget(m_formatLabel);
    qualRow->addStretch(1);
    grid->addWidget(makeCaption(tr("Resolution")), 2, 0);
    grid->addLayout(qualRow, 2, 1);

    // Row 3: Folder
    m_folderEdit = new QLineEdit(central);
    m_browseBtn = new QPushButton(tr("Browse"), central);
    auto *folderRow = new QHBoxLayout;
    folderRow->setSpacing(8);
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(m_browseBtn);
    grid->addWidget(makeCaption(tr("Save to")), 3, 0);
    grid->addLayout(folderRow, 3, 1);

    root->addLayout(grid);

    // Progress
    m_progress = new QProgressBar(central);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("%p%"));
    m_progress->setTextVisible(true);
    m_progress->setAlignment(Qt::AlignCenter);
    root->addSpacing(4);
    root->addWidget(m_progress);

    m_detailsLabel = new QLabel(QStringLiteral(" "), central);
    m_detailsLabel->setObjectName(QStringLiteral("details"));
    root->addWidget(m_detailsLabel);

    // Status + buttons
    m_statusLabel = new QLabel(central);
    m_statusLabel->setObjectName(QStringLiteral("status"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_downloadBtn = new QPushButton(tr("Download"), central);
    m_downloadBtn->setObjectName(QStringLiteral("primary"));
    m_cancelBtn = new QPushButton(tr("Cancel"), central);
    m_openBtn = new QPushButton(tr("Open Folder"), central);
    m_openBtn->setObjectName(QStringLiteral("secondary"));
    m_openBtn->setVisible(false);

    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(8);
    bottom->addWidget(m_statusLabel, 1);
    bottom->addWidget(m_openBtn);
    bottom->addWidget(m_cancelBtn);
    bottom->addWidget(m_downloadBtn);
    root->addLayout(bottom);

    auto *note = new QLabel(tr("Only download videos you own or have permission to save."), central);
    note->setObjectName(QStringLiteral("note"));
    root->addWidget(note);

    connect(m_pasteBtn, &QPushButton::clicked, this, &MainWindow::onPaste);
    connect(m_infoBtn, &QPushButton::clicked, this, &MainWindow::onGetInfo);
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &MainWindow::onGetInfo);
    connect(m_urlEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (m_hasInfo && t.trimmed() != m_info.url)
            resetVideoInfo();
        updateControls();
    });
    connect(m_browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowse);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onDownload);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::onOpenFolder);

    resize(680, 0);
    setMinimumWidth(560);
}

void MainWindow::applyStyle()
{
    setStyleSheet(QStringLiteral(R"(
        #central { background: #f6f7fb; }
        QLabel { color: #1f2430; font-size: 10pt; }
        #header { font-size: 15pt; font-weight: 600; color: #111827; padding-bottom: 2px; }
        #caption { color: #5b6475; font-weight: 600; min-width: 78px; }
        #title { font-weight: 600; color: #111827; }
        #format { color: #374151; }
        #details { color: #5b6475; font-size: 9pt; }
        #note { color: #9aa1ad; font-size: 8pt; }
        QLineEdit, QComboBox {
            background: #ffffff; border: 1px solid #d5d9e2; border-radius: 6px;
            padding: 6px 8px; font-size: 10pt; color: #1f2430;
            selection-background-color: #2563eb;
        }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #2563eb; }
        QLineEdit:disabled, QComboBox:disabled { background: #eef0f4; color: #8b93a1; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background: #ffffff; border: 1px solid #d5d9e2; selection-background-color: #e0e9ff;
            selection-color: #111827; outline: 0;
        }
        QPushButton {
            background: #ffffff; border: 1px solid #d5d9e2; border-radius: 6px;
            padding: 6px 14px; font-size: 10pt; color: #1f2430; min-height: 20px;
        }
        QPushButton:hover { background: #f0f3f9; border-color: #b9c0cd; }
        QPushButton:pressed { background: #e4e8f0; }
        QPushButton:disabled { color: #a3aab6; background: #f1f2f5; border-color: #e1e4ea; }
        QPushButton#secondary { color: #1d4ed8; border-color: #bcd0fb; background: #eef3ff; }
        QPushButton#secondary:hover { background: #e0e9ff; }
        QPushButton#secondary:disabled { color: #a3aab6; background: #f1f2f5; border-color: #e1e4ea; }
        QPushButton#primary {
            background: #2563eb; border: 1px solid #2563eb; color: #ffffff; font-weight: 600;
            padding: 6px 22px;
        }
        QPushButton#primary:hover { background: #1d4ed8; }
        QPushButton#primary:pressed { background: #1e40af; }
        QPushButton#primary:disabled { background: #a9c0f5; border-color: #a9c0f5; color: #eef3ff; }
        QProgressBar {
            background: #e6e9f0; border: none; border-radius: 7px; height: 22px;
            text-align: center; color: #1f2430; font-weight: 600; font-size: 9pt;
        }
        QProgressBar::chunk { background: #2563eb; border-radius: 7px; }
    )"));
}

void MainWindow::setStatus(const QString &text, StatusKind kind)
{
    QString color;
    switch (kind) {
    case StatusKind::Neutral: color = QStringLiteral("#5b6475"); break;
    case StatusKind::Busy:    color = QStringLiteral("#1d4ed8"); break;
    case StatusKind::Success: color = QStringLiteral("#15803d"); break;
    case StatusKind::Warning: color = QStringLiteral("#b45309"); break;
    case StatusKind::Error:   color = QStringLiteral("#b91c1c"); break;
    }
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(color));
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(text);
}

void MainWindow::updateControls()
{
    const Downloader::Stage stage = m_downloader->stage();
    const bool busy = stage != Downloader::Stage::Idle;
    const bool downloading = stage == Downloader::Stage::Downloading || stage == Downloader::Stage::Merging;

    m_urlEdit->setEnabled(!busy);
    m_pasteBtn->setEnabled(!busy);
    m_infoBtn->setEnabled(!busy && !m_urlEdit->text().trimmed().isEmpty());
    m_qualityCombo->setEnabled(!busy && m_hasInfo);
    m_folderEdit->setEnabled(!busy);
    m_browseBtn->setEnabled(!busy);
    m_downloadBtn->setEnabled(!busy && m_hasInfo);
    m_cancelBtn->setEnabled(downloading || stage == Downloader::Stage::FetchingInfo);
}

void MainWindow::resetVideoInfo()
{
    m_hasInfo = false;
    m_info = VideoInfo{};
    m_titleLabel->setText(QStringLiteral("—"));
    m_titleLabel->setToolTip(QString());
    m_qualityCombo->clear();
}

bool MainWindow::ensureTools()
{
    const QString err = Downloader::checkTools();
    if (err.isEmpty())
        return true;
    setStatus(tr("Failed: required tools are missing."), StatusKind::Error);
    QMessageBox::critical(this, tr("Missing tools"), err);
    return false;
}

void MainWindow::onPaste()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        setStatus(tr("The clipboard is empty."), StatusKind::Warning);
        return;
    }
    m_urlEdit->setText(text);
    m_urlEdit->setFocus();
}

void MainWindow::onGetInfo()
{
    if (m_downloader->isBusy())
        return;
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    if (!Downloader::isSupportedUrl(url)) {
        setStatus(tr("Please enter a valid YouTube video link."), StatusKind::Error);
        return;
    }
    if (!ensureTools())
        return;

    resetVideoInfo();
    m_openBtn->setVisible(false);
    m_progress->setValue(0);
    m_detailsLabel->setText(QStringLiteral(" "));
    setStatus(tr("Getting video information…"), StatusKind::Busy);
    m_downloader->fetchInfo(url);
    updateControls();
}

void MainWindow::onInfoReady(const VideoInfo &info)
{
    m_info = info;
    m_hasInfo = true;
    const QString elided = m_titleLabel->fontMetrics().elidedText(info.title, Qt::ElideRight, 480);
    m_titleLabel->setText(elided);
    m_titleLabel->setToolTip(info.title);

    m_qualityCombo->clear();
    for (const QualityOption &o : info.options)
        m_qualityCombo->addItem(o.label);
    m_qualityCombo->setCurrentIndex(0);

    QString msg = tr("Ready. Choose a resolution and click Download.");
    if (info.durationSec > 0)
        msg = tr("Ready (%1). Choose a resolution and click Download.")
                  .arg(FileUtils::formatDuration(info.durationSec));
    setStatus(msg, StatusKind::Neutral);
    updateControls();
    m_downloadBtn->setFocus();
}

void MainWindow::onInfoFailed(const QString &error)
{
    setStatus(tr("Failed to get video information: %1").arg(error), StatusKind::Error);
    updateControls();
}

void MainWindow::onBrowse()
{
    QString start = m_folderEdit->text().trimmed();
    if (!QFileInfo(start).isDir())
        start = defaultDownloadsDir();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose save folder"), start);
    if (dir.isEmpty())
        return;
    m_folderEdit->setText(QDir::toNativeSeparators(dir));
    QSettings().setValue(QLatin1String(kSaveDirKey), QDir::toNativeSeparators(dir));
}

void MainWindow::onDownload()
{
    if (m_downloader->isBusy() || !m_hasInfo)
        return;
    if (!ensureTools())
        return;

    const QString dir = QDir::toNativeSeparators(QDir::cleanPath(m_folderEdit->text().trimmed()));
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        setStatus(tr("The save folder does not exist. Please choose another folder."), StatusKind::Error);
        return;
    }
    // Verify that the folder is writable before starting.
    {
        QTemporaryFile probe(QDir(dir).filePath(QStringLiteral(".tdl-write-test-XXXXXX")));
        if (!probe.open()) {
            setStatus(tr("Cannot write to the save folder. Please choose another folder."), StatusKind::Error);
            return;
        }
    }
    m_folderEdit->setText(dir);
    QSettings().setValue(QLatin1String(kSaveDirKey), dir);

    const int idx = std::max(0, m_qualityCombo->currentIndex());
    if (idx >= m_info.options.size())
        return;

    // Never overwrite: pick "Name.mp4", "Name (1).mp4", ...
    const QString safe = FileUtils::sanitizeFileName(m_info.title);
    const QString path = FileUtils::uniqueFilePath(dir, safe, QStringLiteral("mp4"));
    const QString baseName = QFileInfo(path).completeBaseName();

    m_lastFile.clear();
    m_openBtn->setVisible(false);
    m_progress->setValue(0);
    m_detailsLabel->setText(tr("Starting…"));
    setStatus(tr("Downloading…"), StatusKind::Busy);
    m_downloader->startDownload(m_info.url, m_info.options.at(idx), dir, baseName);
    updateControls();
}

void MainWindow::onCancel()
{
    if (!m_downloader->isBusy())
        return;
    setStatus(tr("Cancelling…"), StatusKind::Warning);
    m_cancelBtn->setEnabled(false);
    m_downloader->cancel();
}

void MainWindow::onProgress(const DownloadProgress &p)
{
    m_progress->setValue(int(p.percent));
    if (m_downloader->stage() == Downloader::Stage::Merging)
        return;

    QStringList parts;
    parts << QStringLiteral("%1%").arg(p.percent, 0, 'f', 1);
    if (p.speed > 0)
        parts << QStringLiteral("%1/s").arg(FileUtils::formatBytes(p.speed));
    if (p.downloaded >= 0 && p.total > 0)
        parts << QStringLiteral("%1 / %2").arg(FileUtils::formatBytes(p.downloaded), FileUtils::formatBytes(p.total));
    else if (p.downloaded >= 0)
        parts << FileUtils::formatBytes(p.downloaded);
    if (p.eta >= 0)
        parts << tr("ETA %1").arg(FileUtils::formatDuration(p.eta));
    m_detailsLabel->setText(parts.join(QStringLiteral("   ·   ")));
}

void MainWindow::onStageChanged(Downloader::Stage stage)
{
    if (stage == Downloader::Stage::Merging) {
        m_progress->setValue(100);
        m_detailsLabel->setText(tr("Finalizing MP4 file…"));
        setStatus(tr("Merging audio and video…"), StatusKind::Busy);
    }
    updateControls();
}

void MainWindow::onFinished(const QString &path)
{
    m_lastFile = path;
    m_progress->setValue(100);
    const QFileInfo fi(path);
    m_detailsLabel->setText(tr("Saved as %1  (%2)").arg(fi.fileName(), FileUtils::formatBytes(double(fi.size()))));
    m_detailsLabel->setToolTip(QDir::toNativeSeparators(path));
    setStatus(tr("Completed"), StatusKind::Success);
    m_openBtn->setVisible(true);
    updateControls();
    m_openBtn->setFocus();
}

void MainWindow::onFailed(const QString &error)
{
    m_detailsLabel->setText(QStringLiteral(" "));
    setStatus(tr("Failed: %1").arg(error), StatusKind::Error);
    updateControls();
}

void MainWindow::onCancelled()
{
    m_progress->setValue(0);
    m_detailsLabel->setText(QStringLiteral(" "));
    setStatus(tr("Cancelled"), StatusKind::Warning);
    updateControls();
}

void MainWindow::onOpenFolder()
{
    if (!m_lastFile.isEmpty() && QFileInfo::exists(m_lastFile)) {
        // Open Explorer with the downloaded file selected. The path comes from our own
        // sanitized file name (no quotes), and explorer.exe is started directly (no shell).
        QProcess p;
        p.setProgram(QStringLiteral("explorer.exe"));
        p.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(m_lastFile)));
        if (p.startDetached())
            return;
    }
    const QString dir = m_lastFile.isEmpty() ? m_folderEdit->text() : QFileInfo(m_lastFile).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const Downloader::Stage stage = m_downloader->stage();
    if (stage == Downloader::Stage::Downloading || stage == Downloader::Stage::Merging) {
        const auto answer = QMessageBox::question(
            this, tr("Download in progress"),
            tr("A download is still running. Cancel it and exit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    m_downloader->cancelAndWait();
    event->accept();
}
