#pragma once

#include <QDateTime>
#include <QMimeType>
#include <QString>

// The house style for names, types, dates and sizes in Files: extensions
// hidden for known types, plain type words, day-first dates, sizes the way
// Windows writes them.
namespace Format
{
QString visibleName(const QString &fileName, bool isDir, bool showExtensions);
QString typeName(const QMimeType &mime, bool isDir);
QString date(const QDateTime &when);
QString longDate(const QDateTime &when);
QString size(qint64 bytes);
QString exactSize(qint64 bytes);
// one is the whole phrase for 1 ("1 item"); many is a "%Ln items" numerus
// translation, so languages with more than two plural forms get theirs.
QString count(qint64 n, const QString &one, const QString &many);
}
