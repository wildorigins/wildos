#include "format.h"

#include "clockformat.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>
#include <QCoreApplication>

namespace Format
{

QString size(qint64 bytes)
{
    if (bytes < 0) {
        return QString();
    }
    const QLocale numbers = QLocale::system();
    // Windows rounds small files up, so a 200 byte note is 1 KB, not 0.
    if (bytes < 1024 * 1024) {
        const qint64 kb = bytes == 0 ? 0 : (bytes + 1023) / 1024;
        return QCoreApplication::translate("Format", "%1 KB").arg(numbers.toString(kb));
    }
    static const char *units[] = {QT_TRANSLATE_NOOP("Format", "%1 MB"), QT_TRANSLATE_NOOP("Format", "%1 GB"), QT_TRANSLATE_NOOP("Format", "%1 TB")};
    double value = bytes / (1024.0 * 1024.0);
    int unit = 0;
    while (value >= 1024 && unit < 2) {
        value /= 1024;
        ++unit;
    }
    return QCoreApplication::translate("Format", units[unit]).arg(numbers.toString(value, 'f', value < 100 ? 1 : 0));
}

QString date(const QDateTime &when)
{
    if (!when.isValid()) {
        return QString();
    }
    const QDateTime local = when.toLocalTime();
    const QLocale format = QLocale::system();
    return format.toString(local.date(), format.dateFormat(QLocale::ShortFormat)) + QLatin1Char(' ') + ClockFormat::time(local.time());
}

QString typeName(const QString &fileName, bool isDir)
{
    if (isDir) {
        return QCoreApplication::translate("Format", "Folder");
    }
    static QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
    static const QHash<QString, QString> plain = {
        {QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"), QCoreApplication::translate("Format", "Spreadsheet")},
        {QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"), QCoreApplication::translate("Format", "Spreadsheet")},
        {QStringLiteral("application/vnd.ms-excel"), QCoreApplication::translate("Format", "Spreadsheet")},
        {QStringLiteral("text/csv"), QCoreApplication::translate("Format", "Spreadsheet")},
        {QStringLiteral("application/vnd.oasis.opendocument.text"), QCoreApplication::translate("Format", "Document")},
        {QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"), QCoreApplication::translate("Format", "Document")},
        {QStringLiteral("application/msword"), QCoreApplication::translate("Format", "Document")},
        {QStringLiteral("application/rtf"), QCoreApplication::translate("Format", "Document")},
        {QStringLiteral("application/vnd.oasis.opendocument.presentation"), QCoreApplication::translate("Format", "Presentation")},
        {QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"), QCoreApplication::translate("Format", "Presentation")},
        {QStringLiteral("application/pdf"), QStringLiteral("PDF")},
        {QStringLiteral("text/plain"), QCoreApplication::translate("Format", "Text")},
        {QStringLiteral("text/markdown"), QCoreApplication::translate("Format", "Text")},
        {QStringLiteral("application/gpx+xml"), QCoreApplication::translate("Format", "GPX route")},
        {QStringLiteral("application/zip"), QCoreApplication::translate("Format", "ZIP archive")},
        {QStringLiteral("application/x-7z-compressed"), QCoreApplication::translate("Format", "7z archive")},
        {QStringLiteral("application/vnd.rar"), QCoreApplication::translate("Format", "RAR archive")},
        {QStringLiteral("application/x-ms-dos-executable"), QCoreApplication::translate("Format", "Windows app")},
        {QStringLiteral("application/x-msi"), QCoreApplication::translate("Format", "Windows installer")},
        {QStringLiteral("application/octet-stream"), QCoreApplication::translate("Format", "File")},
    };
    const auto it = plain.constFind(mime.name());
    if (it != plain.constEnd()) {
        return it.value();
    }
    QString comment = mime.comment();
    if (comment.isEmpty()) {
        return QCoreApplication::translate("Format", "File");
    }
    // Shared MIME info writes "plain text document" and "JPEG image"; the
    // first letter is raised and acronyms stay as they are.
    comment[0] = comment[0].toUpper();
    return comment;
}

bool isImage(const QString &fileName)
{
    static const QStringList endings = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp"),
                                        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"),
                                        QStringLiteral("heic"), QStringLiteral("avif"), QStringLiteral("svg")};
    return endings.contains(QFileInfo(fileName).suffix().toLower());
}

QString duration(double seconds)
{
    if (seconds < 1) {
        return QCoreApplication::translate("Format", "less than a second");
    }
    if (seconds < 60) {
        const int s = qRound(seconds);
        return s == 1 ? QCoreApplication::translate("Format", "about 1 second") : QCoreApplication::translate("Format", "about %1 seconds").arg(s);
    }
    const int m = qRound(seconds / 60);
    if (m < 60) {
        return m == 1 ? QCoreApplication::translate("Format", "about 1 minute") : QCoreApplication::translate("Format", "about %1 minutes").arg(m);
    }
    const int h = qRound(seconds / 3600);
    return h == 1 ? QCoreApplication::translate("Format", "about 1 hour") : QCoreApplication::translate("Format", "about %1 hours").arg(h);
}

QString folder(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    const QString home = QDir::homePath();
    if (clean == home) {
        return QCoreApplication::translate("Format", "Home");
    }
    const QString rest = clean.startsWith(home + QLatin1Char('/')) ? clean.mid(home.size() + 1) : clean.mid(1);
    if (rest.isEmpty()) {
        return QStringLiteral("/");
    }
    return rest.split(QLatin1Char('/'), Qt::SkipEmptyParts).join(QStringLiteral(" › "));
}

QString archiveBaseName(const QString &fileName)
{
    static const QStringList endings = {
        QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.zst"),
        QStringLiteral(".tar.lz"), QStringLiteral(".tar.lzma"), QStringLiteral(".tar.z"), QStringLiteral(".tgz"),
        QStringLiteral(".tbz2"), QStringLiteral(".tbz"), QStringLiteral(".txz"), QStringLiteral(".tzst"),
        QStringLiteral(".zip"), QStringLiteral(".7z"), QStringLiteral(".rar"), QStringLiteral(".tar"),
        QStringLiteral(".gz"), QStringLiteral(".bz2"), QStringLiteral(".xz"), QStringLiteral(".zst"),
        QStringLiteral(".lz"), QStringLiteral(".lzma"), QStringLiteral(".cpio"), QStringLiteral(".cab"),
        QStringLiteral(".lzh"), QStringLiteral(".lha"),
    };
    for (const QString &ending : endings) {
        if (fileName.size() > ending.size() && fileName.endsWith(ending, Qt::CaseInsensitive)) {
            return fileName.left(fileName.size() - ending.size());
        }
    }
    return fileName;
}

QString uniqueName(const QString &folder, const QString &base, const QString &suffix)
{
    const QDir dir(folder);
    QString name = base + suffix;
    for (int n = 2; QFileInfo::exists(dir.filePath(name)) || QFileInfo(dir.filePath(name)).isSymLink(); ++n) {
        name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
    }
    return name;
}

}
