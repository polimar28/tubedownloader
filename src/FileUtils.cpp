#include "FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace FileUtils {

QString sanitizeFileName(const QString &title)
{
    QString out;
    out.reserve(title.size());
    for (const QChar c : title) {
        const ushort u = c.unicode();
        if (u < 32 || u == 127)
            continue; // control characters
        switch (u) {
        case '<': case '>': case ':': case '"': case '/':
        case '\\': case '|': case '?': case '*':
            out += QLatin1Char(' ');
            break;
        default:
            out += c;
        }
    }

    // Collapse whitespace.
    out = out.simplified();

    // Limit length (leave room for " (999).mp4" and long folder paths).
    constexpr int kMaxLen = 150;
    if (out.size() > kMaxLen) {
        out.truncate(kMaxLen);
        // Do not split a surrogate pair.
        if (!out.isEmpty() && out.back().isHighSurrogate())
            out.chop(1);
    }

    // Windows forbids trailing dots and spaces.
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' '))))
        out.chop(1);
    // Avoid leading dots/spaces (hidden-looking or odd names).
    while (!out.isEmpty() && (out.startsWith(QLatin1Char('.')) || out.startsWith(QLatin1Char(' '))))
        out.remove(0, 1);

    // Reserved device names (with or without extension-like suffix).
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(out).hasMatch())
        out.prepend(QLatin1Char('_'));

    if (out.isEmpty())
        out = QStringLiteral("video");
    return out;
}

QString uniqueFilePath(const QString &dir, const QString &baseName, const QString &ext)
{
    const QDir d(dir);
    QString candidate = d.filePath(baseName + QLatin1Char('.') + ext);
    for (int i = 1; QFileInfo::exists(candidate); ++i)
        candidate = d.filePath(QStringLiteral("%1 (%2).%3").arg(baseName).arg(i).arg(ext));
    return QDir::toNativeSeparators(candidate);
}

QString formatBytes(double bytes)
{
    if (bytes < 0)
        return QStringLiteral("?");
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        ++u;
    }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', u == 0 ? 0 : 1).arg(QLatin1String(units[u]));
}

QString formatDuration(qint64 s)
{
    if (s < 0)
        return QStringLiteral("?");
    const qint64 h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
}

} // namespace FileUtils
