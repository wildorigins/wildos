#include "history.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace
{
constexpr int keptFiles = 30;
constexpr int shownFiles = 5;

const QString filesKey = QStringLiteral("Recent/files");

// A path has slashes, which settings read as groups, so a file's place is
// kept under a digest of its path.
QString placeKey(const QString &path)
{
    return QStringLiteral("Places/") + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString shownFolder(const QString &folder)
{
    const QString home = QDir::homePath();
    if (folder == home) {
        return QStringLiteral("~");
    }
    return folder.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + folder.mid(home.size()) : folder;
}
}

History *History::instance()
{
    static History *history = new History;
    return history;
}

History *History::create(QQmlEngine *, QJSEngine *)
{
    History *history = instance();
    QJSEngine::setObjectOwnership(history, QJSEngine::CppOwnership);
    return history;
}

History::History(QObject *parent)
    : QObject(parent)
{
}

QVariantList History::recent() const
{
    QSettings settings;
    const QStringList paths = settings.value(filesKey).toStringList();
    QVariantList list;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            continue;
        }
        list.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("url"), QUrl::fromLocalFile(path)},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("folder"), shownFolder(info.absolutePath())},
        });
        if (list.size() == shownFiles) {
            break;
        }
    }
    return list;
}

void History::opened(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QSettings settings;
    QStringList paths = settings.value(filesKey).toStringList();
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > keptFiles) {
        settings.remove(placeKey(paths.takeLast()));
    }
    settings.setValue(filesKey, paths);
    Q_EMIT recentChanged();
}

void History::removeRecent(const QString &path)
{
    QSettings settings;
    QStringList paths = settings.value(filesKey).toStringList();
    if (paths.removeAll(path) == 0) {
        return;
    }
    settings.setValue(filesKey, paths);
    settings.remove(placeKey(path));
    Q_EMIT recentChanged();
}

QVariantMap History::place(const QString &path) const
{
    QSettings settings;
    const QString key = placeKey(path);
    if (path.isEmpty() || !settings.contains(key + QStringLiteral("/zoom"))) {
        return {};
    }
    return {
        {QStringLiteral("page"), settings.value(key + QStringLiteral("/page")).toInt()},
        {QStringLiteral("offset"), settings.value(key + QStringLiteral("/offset")).toDouble()},
        {QStringLiteral("zoom"), settings.value(key + QStringLiteral("/zoom")).toDouble()},
        {QStringLiteral("turns"), settings.value(key + QStringLiteral("/turns")).toInt()},
    };
}

void History::setPlace(const QString &path, int page, double offset, double zoom, int turns)
{
    if (path.isEmpty() || zoom <= 0) {
        return;
    }
    QSettings settings;
    // Only for files on the list, so places never pile up without end.
    if (!settings.value(filesKey).toStringList().contains(path)) {
        return;
    }
    const QString key = placeKey(path);
    settings.setValue(key + QStringLiteral("/page"), page);
    settings.setValue(key + QStringLiteral("/offset"), offset);
    settings.setValue(key + QStringLiteral("/zoom"), zoom);
    settings.setValue(key + QStringLiteral("/turns"), turns);
}
