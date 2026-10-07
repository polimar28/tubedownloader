#include "MainWindow.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TubeDownloader"));
    QApplication::setApplicationName(QStringLiteral("TubeDownloader"));
    QApplication::setApplicationDisplayName(QStringLiteral("Tube Downloader"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    qRegisterMetaType<VideoInfo>();
    qRegisterMetaType<DownloadProgress>();

    MainWindow w;
    w.show();

    // Developer option for automated UI checks:
    //   TubeDownloader.exe --grab out.png [youtube-url]
    // Renders the window to a PNG (after loading video info if a URL is given) and exits.
    const QStringList args = QApplication::arguments();
    const int gi = args.indexOf(QStringLiteral("--grab"));
    if (gi > 0 && gi + 1 < args.size()) {
        const QString out = args.at(gi + 1);
        const QString url = gi + 2 < args.size() ? args.at(gi + 2) : QString();
        int delay = 800;
        if (!url.isEmpty()) {
            if (auto *edit = w.findChild<QLineEdit *>())
                edit->setText(url);
            for (auto *b : w.findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("Get Video Info"))
                    b->click();
            delay = 12000;
        }
        QTimer::singleShot(delay, &w, [&w, out] {
            w.grab().save(out);
            QApplication::exit(0);
        });
    }
    return app.exec();
}
