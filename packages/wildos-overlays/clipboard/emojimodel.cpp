#include "emojimodel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

EmojiData::EmojiData(QObject *parent)
    : QObject(parent)
{
    QFile file(QStringLiteral(":/wildos/emoji.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("The emoji list is missing from this build");
        return;
    }
    const QJsonArray groups = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("groups")).toArray();
    for (const QJsonValue &group : groups) {
        const QJsonArray emoji = group.toObject().value(QStringLiteral("emoji")).toArray();
        for (const QJsonValue &entry : emoji) {
            m_all.append(entry);
        }
    }
    m_groups = groups.toVariantList();
}

QVariantList EmojiData::search(const QString &query, int limit) const
{
    const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVariantList hits;
    if (words.isEmpty()) {
        return hits;
    }
    for (const QJsonValue &value : m_all) {
        const QJsonArray entry = value.toArray();
        const QString haystack = QLatin1Char(' ') + entry.at(1).toString().toLower() + QLatin1Char(' ') + entry.at(2).toString().toLower();
        bool all = true;
        for (const QString &word : words) {
            if (!haystack.contains(QLatin1Char(' ') + word)) {
                all = false;
                break;
            }
        }
        if (all) {
            hits.append(QVariant(entry.toVariantList()));
            if (hits.size() >= limit) {
                break;
            }
        }
    }
    return hits;
}

QVariantList EmojiData::find(const QStringList &texts) const
{
    QVariantList found;
    for (const QString &text : texts) {
        for (const QJsonValue &value : m_all) {
            const QJsonArray entry = value.toArray();
            if (entry.at(0).toString() == text) {
                found.append(QVariant(entry.toVariantList()));
                break;
            }
        }
    }
    return found;
}
