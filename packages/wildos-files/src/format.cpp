#include "format.h"

#include "clockformat.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>

namespace Format
{

namespace
{
// Day and month names in the display language. The translation names its
// locale, so a language without one keeps the English names.
QLocale wordsLocale()
{
    return QLocale(QCoreApplication::translate("Format", "en_GB", "locale for day and month names"));
}
}

QString visibleName(const QString &fileName, bool isDir, bool showExtensions)
{
    if (isDir || showExtensions) {
        return fileName;
    }
    // Only suffixes the system knows a type for are hidden, so a file with
    // an unknown or misleading ending still shows it, as Windows does.
    static QMimeDatabase db;
    const QString suffix = db.suffixForFileName(fileName);
    if (suffix.isEmpty() || fileName.size() <= suffix.size() + 1) {
        return fileName;
    }
    return fileName.left(fileName.size() - suffix.size() - 1);
}

QString typeName(const QMimeType &mime, bool isDir)
{
    if (isDir) {
        return QCoreApplication::translate("Format", "Folder");
    }
    static const QHash<QString, const char *> plain = {
        {QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"), QT_TRANSLATE_NOOP("Format", "Spreadsheet")},
        {QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"), QT_TRANSLATE_NOOP("Format", "Spreadsheet")},
        {QStringLiteral("application/vnd.ms-excel"), QT_TRANSLATE_NOOP("Format", "Spreadsheet")},
        {QStringLiteral("text/csv"), QT_TRANSLATE_NOOP("Format", "Spreadsheet")},
        {QStringLiteral("application/vnd.oasis.opendocument.text"), QT_TRANSLATE_NOOP("Format", "Document")},
        {QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"), QT_TRANSLATE_NOOP("Format", "Document")},
        {QStringLiteral("application/msword"), QT_TRANSLATE_NOOP("Format", "Document")},
        {QStringLiteral("application/rtf"), QT_TRANSLATE_NOOP("Format", "Document")},
        {QStringLiteral("application/vnd.oasis.opendocument.presentation"), QT_TRANSLATE_NOOP("Format", "Presentation")},
        {QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"), QT_TRANSLATE_NOOP("Format", "Presentation")},
        {QStringLiteral("application/vnd.ms-powerpoint"), QT_TRANSLATE_NOOP("Format", "Presentation")},
        {QStringLiteral("application/pdf"), QT_TRANSLATE_NOOP("Format", "PDF")},
        {QStringLiteral("text/plain"), QT_TRANSLATE_NOOP("Format", "Text")},
        {QStringLiteral("text/markdown"), QT_TRANSLATE_NOOP("Format", "Text")},
        {QStringLiteral("application/zip"), QT_TRANSLATE_NOOP("Format", "Compressed folder")},
        {QStringLiteral("application/x-desktop"), QT_TRANSLATE_NOOP("Format", "Shortcut")},
        {QStringLiteral("application/x-ms-dos-executable"), QT_TRANSLATE_NOOP("Format", "Windows app")},
        {QStringLiteral("application/x-msi"), QT_TRANSLATE_NOOP("Format", "Windows installer")},
        {QStringLiteral("application/x-ms-shortcut"), QT_TRANSLATE_NOOP("Format", "Windows shortcut")},
        {QStringLiteral("application/x-iso9660-image"), QT_TRANSLATE_NOOP("Format", "Disc image")},
        {QStringLiteral("application/vnd.debian.binary-package"), QT_TRANSLATE_NOOP("Format", "App package")},
        {QStringLiteral("application/octet-stream"), QT_TRANSLATE_NOOP("Format", "File")},
        {QStringLiteral("inode/symlink"), QT_TRANSLATE_NOOP("Format", "Shortcut")},
    };
    const auto it = plain.constFind(mime.name());
    if (it != plain.constEnd()) {
        return QCoreApplication::translate("Format", it.value());
    }
    QString comment = mime.comment();
    if (comment.isEmpty()) {
        return QCoreApplication::translate("Format", "File");
    }
    // Shared MIME info writes "plain text document" and "JPEG image";
    // the first letter is raised and acronyms stay as they are.
    comment[0] = comment[0].toUpper();
    return comment;
}

QString date(const QDateTime &when)
{
    if (!when.isValid()) {
        return QString();
    }
    // The regional format chosen in Settings decides the order and the
    // clock, unless a 12 or 24-hour clock is chosen; the words follow the
    // display language.
    const QLocale names = wordsLocale();
    const QLocale format = QLocale::system();
    const QDateTime local = when.toLocalTime();
    const QDate today = QDate::currentDate();
    const QString time = ClockFormat::time(local.time());
    const qint64 days = local.date().daysTo(today);
    if (days == 0) {
        return QCoreApplication::translate("Format", "Today %1").arg(time);
    }
    if (days == 1) {
        return QCoreApplication::translate("Format", "Yesterday %1").arg(time);
    }
    if (days > 1 && days < 7) {
        return names.dayName(local.date().dayOfWeek()) + QLatin1Char(' ') + time;
    }
    return names.toString(local.date(), format.dateFormat(QLocale::ShortFormat));
}

QString longDate(const QDateTime &when)
{
    if (!when.isValid()) {
        return QString();
    }
    const QDateTime local = when.toLocalTime();
    const QLocale names = wordsLocale();
    const QLocale format = QLocale::system();
    // Qt writes UK dates as "Monday, 9 November"; Wild OS has no comma.
    QString day = format.dateFormat(QLocale::LongFormat);
    if (day.startsWith(QLatin1String("dddd, d MMMM"))) {
        day.remove(4, 1);
    }
    return names.toString(local.date(), day) + QStringLiteral(", ")
        + ClockFormat::time(local.time());
}

QString size(qint64 bytes)
{
    if (bytes < 0) {
        return QString();
    }
    // Separators from the regional format chosen in Settings (LC_NUMERIC).
    const QLocale numbers = QLocale::system();
    // Windows rounds small files up, so a 200 byte note is 1 KB, not 0.
    if (bytes < 1024 * 1024) {
        const qint64 kb = bytes == 0 ? 0 : (bytes + 1023) / 1024;
        return QCoreApplication::translate("Format", "%1 KB").arg(numbers.toString(kb));
    }
    static const char *units[] = {QT_TRANSLATE_NOOP("Format", "MB"), QT_TRANSLATE_NOOP("Format", "GB"), QT_TRANSLATE_NOOP("Format", "TB"),
                                  QT_TRANSLATE_NOOP("Format", "PB")};
    double value = bytes / (1024.0 * 1024.0);
    int unit = 0;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    // Three significant figures: 4.21 MB, 12.3 MB, 123 MB.
    const int decimals = value < 10 ? 2 : value < 100 ? 1 : 0;
    QString text = numbers.toString(value, 'f', decimals);
    if (text.contains(numbers.decimalPoint())) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(numbers.decimalPoint())) {
            text.chop(1);
        }
    }
    return text + QLatin1Char(' ') + QCoreApplication::translate("Format", units[unit]);
}

QString exactSize(qint64 bytes)
{
    const QString exact = QLocale::system().toString(bytes);
    return bytes == 1 ? QCoreApplication::translate("Format", "%1 (%2 byte)").arg(size(bytes), exact)
                      : QCoreApplication::translate("Format", "%1 (%2 bytes)").arg(size(bytes), exact);
}

QString count(qint64 n, const QString &one, const QString &many)
{
    return n == 1 ? one : many;
}

}
