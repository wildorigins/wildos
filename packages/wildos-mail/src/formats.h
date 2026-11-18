#pragma once

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

#include "clockformat.h"

// Dates and times in the regional format chosen in Settings (LC_TIME, which
// QLocale::system() follows) on the clock chosen in Settings > Date and
// time, with day and month names in the display language chosen in Settings
// (LANGUAGE), as WildOS.UI picks translations. qml/Formats.js does the same
// for the QML side.
namespace Formats
{
inline QLocale names()
{
    const QStringList languages = QLocale::system().uiLanguages();
    if (languages.isEmpty() || languages.first().startsWith(QLatin1String("en")) || languages.first() == QLatin1String("C"))
        return QLocale(QLocale::English, QLocale::UnitedKingdom);
    return QLocale(languages.first());
}

inline QString time(const QDateTime &when)
{
    return ClockFormat::time(when.time());
}

inline QString shortDate(const QDate &day)
{
    return names().toString(day, QLocale::system().dateFormat(QLocale::ShortFormat));
}

// "Mon 9 Nov", "Mon, Nov 9": the long date without its year, with short names.
inline QString shortDayAndMonth(const QDate &day)
{
    QString format = QLocale::system().dateFormat(QLocale::LongFormat);
    format.remove(QRegularExpression(QStringLiteral("[,.\s]*y+[.\u5e74]?")));
    format.replace(QStringLiteral("dddd, d MMMM"), QStringLiteral("dddd d MMMM"));
    format.replace(QStringLiteral("dddd"), QStringLiteral("ddd"));
    format.replace(QStringLiteral("MMMM"), QStringLiteral("MMM"));
    return names().toString(day, format.trimmed());
}
}
