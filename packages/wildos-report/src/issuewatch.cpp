#include "issuewatch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace IssueWatch
{
Posted find(const QByteArray &json, const QString &zipName, const QDateTime &since)
{
    if (zipName.isEmpty()) {
        return {};
    }
    const QDateTime earliest = since.toUTC().addSecs(-120);
    // Only ever opened in the browser, so it must be an issue page of ours.
    static const QString prefix = QStringLiteral("https://github.com/wildorigins/wildos/issues/");
    for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject issue = value.toObject();
        // The issues list includes pull requests.
        if (issue.contains(QLatin1String("pull_request"))) {
            continue;
        }
        const QDateTime created = QDateTime::fromString(issue.value(QLatin1String("created_at")).toString(), Qt::ISODate);
        if (!created.isValid() || created < earliest) {
            continue;
        }
        const bool named = issue.value(QLatin1String("body")).toString().contains(zipName);
        const QString url = issue.value(QLatin1String("html_url")).toString();
        const int number = issue.value(QLatin1String("number")).toInt();
        if (named && number > 0 && url == prefix + QString::number(number)) {
            return {number, url};
        }
    }
    return {};
}
}
