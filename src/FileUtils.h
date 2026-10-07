#pragma once

#include <QString>

namespace FileUtils {

// Turns an arbitrary video title into a safe Windows file name (without extension).
QString sanitizeFileName(const QString &title);

// Returns a path inside `dir` for `baseName.ext` that does not exist yet,
// e.g. "Video.mp4", "Video (1).mp4", "Video (2).mp4", ...
QString uniqueFilePath(const QString &dir, const QString &baseName, const QString &ext);

// Human-readable byte size, e.g. "45.2 MiB".
QString formatBytes(double bytes);

// Human-readable duration, e.g. "1:05:07" or "0:08".
QString formatDuration(qint64 seconds);

} // namespace FileUtils
