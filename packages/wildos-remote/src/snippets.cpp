#include "snippets.h"
#include "paths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace
{
QString snippetsFile()
{
    return Paths::data() + QStringLiteral("/snippets.json");
}

QString historyFile()
{
    return Paths::data() + QStringLiteral("/history.json");
}

// How many commands each host keeps; the least used go first.
constexpr int historyLimit = 500;

void writeJson(const QString &path, const QJsonDocument &doc)
{
    QDir().mkpath(Paths::data());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(doc.toJson());
        f.commit();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}
}

Snippets *Snippets::instance()
{
    static Snippets *s = new Snippets;
    return s;
}

Snippets *Snippets::create(QQmlEngine *, QJSEngine *)
{
    Snippets *s = instance();
    QJSEngine::setObjectOwnership(s, QJSEngine::CppOwnership);
    return s;
}

Snippets::Snippets(QObject *parent)
    : QObject(parent)
{
    QFile s(snippetsFile());
    if (s.open(QIODevice::ReadOnly)) {
        m_snippets = QJsonDocument::fromJson(s.readAll()).array().toVariantList();
    }
    QFile h(historyFile());
    if (h.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(h.readAll()).object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            QHash<QString, int> &commands = m_history[it.key()];
            const QJsonObject o = it.value().toObject();
            for (auto c = o.begin(); c != o.end(); ++c) {
                commands.insert(c.key(), c.value().toInt());
            }
        }
    }
}

void Snippets::writeSnippets()
{
    writeJson(snippetsFile(), QJsonDocument(QJsonArray::fromVariantList(m_snippets)));
    Q_EMIT changed();
}

void Snippets::writeHistory()
{
    QJsonObject root;
    for (auto it = m_history.cbegin(); it != m_history.cend(); ++it) {
        QJsonObject o;
        for (auto c = it->cbegin(); c != it->cend(); ++c) {
            o.insert(c.key(), c.value());
        }
        root.insert(it.key(), o);
    }
    writeJson(historyFile(), QJsonDocument(root));
}

QString Snippets::save(const QVariantMap &input)
{
    QVariantMap s = input;
    QString id = s.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        s.insert(QStringLiteral("id"), id);
        m_snippets.append(s);
    } else {
        for (QVariant &v : m_snippets) {
            if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                v = s;
            }
        }
    }
    writeSnippets();
    return id;
}

void Snippets::remove(const QString &id)
{
    m_snippets.erase(std::remove_if(m_snippets.begin(), m_snippets.end(),
                                    [&](const QVariant &v) { return v.toMap().value(QStringLiteral("id")).toString() == id; }),
                     m_snippets.end());
    writeSnippets();
}

void Snippets::record(const QString &hostKey, const QString &command)
{
    const QString c = command.trimmed();
    // A command typed with a space first is one the person asked the shell
    // not to remember; Remote doesn't either.
    if (c.size() < 3 || command.startsWith(QLatin1Char(' '))) {
        return;
    }
    QHash<QString, int> &commands = m_history[hostKey];
    ++commands[c];
    if (commands.size() > historyLimit) {
        auto least = std::min_element(commands.begin(), commands.end());
        commands.erase(least);
    }
    writeHistory();
}

void Snippets::clearHistory(const QString &hostKey)
{
    if (hostKey.isEmpty()) {
        m_history.clear();
    } else {
        m_history.remove(hostKey);
    }
    writeHistory();
}

QVariantList Snippets::suggest(const QString &hostKey, const QString &typed) const
{
    QVariantList out;
    if (typed.trimmed().size() < 3) {
        return out;
    }
    QList<QPair<QString, int>> matches;
    const QHash<QString, int> commands = m_history.value(hostKey);
    for (auto it = commands.cbegin(); it != commands.cend(); ++it) {
        if (it.key().size() > typed.size() && it.key().startsWith(typed)) {
            matches.append({it.key(), it.value()});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    for (int i = 0; i < matches.size() && i < 2; ++i) {
        out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("history")},
                               {QStringLiteral("text"), matches[i].first},
                               {QStringLiteral("completion"), matches[i].first},
                               {QStringLiteral("detail"), matches[i].second == 1 ? tr("Used once") : tr("Used %1 times").arg(matches[i].second)}});
    }
    // A snippet whose command starts the same way, or whose name has the
    // last word typed in it.
    const QString lastWord = typed.section(QLatin1Char(' '), -1).trimmed();
    for (const QVariant &v : m_snippets) {
        const QVariantMap s = v.toMap();
        const QString command = s.value(QStringLiteral("command")).toString();
        const QString title = s.value(QStringLiteral("title")).toString();
        const bool byCommand = command.startsWith(typed) && command.size() > typed.size();
        const bool byWord = lastWord.size() >= 3 && title.contains(lastWord, Qt::CaseInsensitive);
        if (byCommand || byWord) {
            const QString note = s.value(QStringLiteral("note")).toString();
            out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("snippet")},
                                   {QStringLiteral("text"), title},
                                   {QStringLiteral("completion"), command},
                                   {QStringLiteral("replace"), !byCommand},
                                   {QStringLiteral("detail"), note.isEmpty() ? tr("Snippet") : tr("Snippet: %1").arg(note)}});
            break;
        }
    }
    return out;
}
