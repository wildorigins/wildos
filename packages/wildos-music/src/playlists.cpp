#include "playlists.h"
#include "library.h"
#include "paths.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace
{
Playlists *s_instance = nullptr;

QString likedName()
{
    return QStringLiteral("Liked songs");
}
}

Playlists::Playlists(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_reload.setSingleShot(true);
    m_reload.setInterval(300);
    connect(&m_reload, &QTimer::timeout, this, &Playlists::load);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reload, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reload, qOverload<>(&QTimer::start));
    load();
}

Playlists *Playlists::instance()
{
    if (!s_instance) {
        new Playlists(QCoreApplication::instance());
    }
    return s_instance;
}

Playlists *Playlists::create(QQmlEngine *, QJSEngine *)
{
    Playlists *playlists = instance();
    QQmlEngine::setObjectOwnership(playlists, QQmlEngine::CppOwnership);
    return playlists;
}

QStringList Playlists::read(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QDir base = QFileInfo(file).absoluteDir();
    QStringList paths;
    QTextStream stream(&in);
    // .m3u files from Windows players are often in the old code page;
    // .m3u8 is UTF-8 by definition, and so is anything Music writes.
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1String("file:"))) {
            line = QUrl(line).toLocalFile();
        } else if (line.contains(QLatin1Char('\\')) && !line.contains(QLatin1Char('/'))) {
            line.replace(QLatin1Char('\\'), QLatin1Char('/'));
        }
        if (line.contains(QLatin1String("://"))) {
            continue;
        }
        paths.append(QDir::cleanPath(base.absoluteFilePath(line)));
    }
    return paths;
}

void Playlists::load()
{
    const QDir folder(MusicPaths::playlistFolder());
    m_lists.clear();
    m_liked.clear();
    if (folder.exists()) {
        const QFileInfoList files = folder.entryInfoList({QStringLiteral("*.m3u8"), QStringLiteral("*.m3u")}, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &file : files) {
            const QString name = file.completeBaseName();
            // X.m3u8 wins over an X.m3u beside it.
            if (m_lists.contains(name) && file.suffix().compare(QLatin1String("m3u"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            if (name == likedName()) {
                m_liked = read(file.absoluteFilePath());
            } else {
                m_lists.insert(name, read(file.absoluteFilePath()));
            }
        }
    }
    m_likedSet = QSet<QString>(m_liked.cbegin(), m_liked.cend());

    if (!m_watcher.files().isEmpty()) {
        m_watcher.removePaths(m_watcher.files());
    }
    if (folder.exists()) {
        if (m_watcher.directories().isEmpty()) {
            m_watcher.addPath(folder.absolutePath());
        }
        QStringList files;
        for (const QFileInfo &file : folder.entryInfoList({QStringLiteral("*.m3u8"), QStringLiteral("*.m3u")}, QDir::Files)) {
            files.append(file.absoluteFilePath());
        }
        if (!files.isEmpty()) {
            m_watcher.addPaths(files);
        }
    }
    ++m_revision;
    Q_EMIT changed();
}

void Playlists::touched()
{
    ++m_revision;
    Q_EMIT changed();
    // Our own write shows up as a change on disk too; reading it back
    // keeps the watch on the new file.
    m_reload.start();
}

QVariantList Playlists::all() const
{
    QStringList names = m_lists.keys();
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), [&collator](const QString &a, const QString &b) {
        return collator.compare(a, b) < 0;
    });
    QVariantList out;
    for (const QString &name : names) {
        out.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("count"), m_lists.value(name).size()}});
    }
    return out;
}

bool Playlists::isLiked(const QString &path) const
{
    return m_likedSet.contains(path);
}

void Playlists::setLiked(const QString &path, bool liked)
{
    setLikedAll({path}, liked);
}

void Playlists::setLikedAll(const QStringList &paths, bool liked)
{
    QStringList list = m_liked;
    bool changed = false;
    for (const QString &path : paths) {
        if (liked && !m_likedSet.contains(path)) {
            // Newest like first, as the Liked songs page shows them.
            list.prepend(path);
            m_likedSet.insert(path);
            changed = true;
        } else if (!liked && m_likedSet.contains(path)) {
            list.removeAll(path);
            m_likedSet.remove(path);
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    if (!write(likedName(), list)) {
        m_likedSet = QSet<QString>(m_liked.cbegin(), m_liked.cend());
        Q_EMIT this->changed();
        return;
    }
    m_liked = list;
    touched();
}

bool Playlists::allLiked(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return false;
    }
    return std::all_of(paths.cbegin(), paths.cend(), [this](const QString &p) {
        return m_likedSet.contains(p);
    });
}

QStringList Playlists::songs(const QString &name) const
{
    return name == likedName() ? m_liked : m_lists.value(name);
}

bool Playlists::exists(const QString &name) const
{
    return m_lists.contains(name);
}

QString Playlists::freeName(const QString &base) const
{
    if (!m_lists.contains(base) && base != likedName()) {
        return base;
    }
    for (int n = 2;; ++n) {
        const QString name = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!m_lists.contains(name)) {
            return name;
        }
    }
}

QString Playlists::fileOf(const QString &name) const
{
    const QDir folder(MusicPaths::playlistFolder());
    const QString m3u = folder.filePath(name + QStringLiteral(".m3u"));
    if (!QFile::exists(folder.filePath(name + QStringLiteral(".m3u8"))) && QFile::exists(m3u)) {
        return m3u;
    }
    return folder.filePath(name + QStringLiteral(".m3u8"));
}

bool Playlists::validName(const QString &name)
{
    if (name.trimmed().isEmpty() || name.startsWith(QLatin1Char('.'))) {
        m_error = tr("Give the playlist a name.");
        return false;
    }
    if (name.contains(QLatin1Char('/'))) {
        m_error = tr("A playlist name can't have a / in it.");
        return false;
    }
    if (name == likedName() || m_lists.contains(name)) {
        m_error = tr("There's already a playlist called %1.").arg(name);
        return false;
    }
    return true;
}

bool Playlists::write(const QString &name, const QStringList &paths)
{
    const QString path = fileOf(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QDir base = QFileInfo(path).absoluteDir();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_error = tr("Couldn't save %1: %2").arg(name, file.errorString());
        Q_EMIT changed();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "#EXTM3U\n";
    Library *library = Library::instance();
    for (const QString &song : paths) {
        if (const Track *t = library->find(song)) {
            out << "#EXTINF:" << (t->duration > 0 ? (t->duration + 500) / 1000 : -1) << ',' << (t->artist.isEmpty() ? t->title : t->artist + QStringLiteral(" - ") + t->title) << '\n';
        }
        out << base.relativeFilePath(song) << '\n';
    }
    out.flush();
    if (!file.commit()) {
        m_error = tr("Couldn't save %1: %2").arg(name, file.errorString());
        Q_EMIT changed();
        return false;
    }
    if (m_watcher.directories().isEmpty()) {
        m_watcher.addPath(base.absolutePath());
    }
    return true;
}

bool Playlists::create(const QString &name, const QStringList &paths)
{
    const QString clean = name.trimmed();
    if (!validName(clean) || !write(clean, paths)) {
        return false;
    }
    m_lists.insert(clean, paths);
    touched();
    return true;
}

bool Playlists::rename(const QString &name, const QString &newName)
{
    const QString clean = newName.trimmed();
    if (clean == name) {
        return true;
    }
    if (!m_lists.contains(name) || !validName(clean)) {
        return false;
    }
    const QString from = fileOf(name);
    const QString to = QFileInfo(from).absoluteDir().filePath(clean + QLatin1Char('.') + QFileInfo(from).suffix());
    if (!QFile::rename(from, to)) {
        m_error = tr("Couldn't rename %1.").arg(name);
        Q_EMIT changed();
        return false;
    }
    m_lists.insert(clean, m_lists.take(name));
    touched();
    return true;
}

bool Playlists::remove(const QString &name)
{
    if (!m_lists.contains(name)) {
        return false;
    }
    const QString path = fileOf(name);
    QString binned;
    if (!QFile::moveToTrash(path, &binned) && !QFile::remove(path)) {
        m_error = tr("Couldn't delete %1.").arg(name);
        Q_EMIT changed();
        return false;
    }
    m_removed = {name, path, QFileInfo::exists(path) ? QString() : binned, m_lists.value(name)};
    m_lists.remove(name);
    touched();
    return true;
}

bool Playlists::restore(const QString &name)
{
    if (name.isEmpty() || m_removed.name != name) {
        m_error = tr("Couldn't bring %1 back.").arg(name);
        Q_EMIT changed();
        return false;
    }
    if (!validName(name)) {
        Q_EMIT changed();
        return false;
    }
    const Removed removed = m_removed;
    m_removed = {};
    bool back = false;
    if (!removed.binned.isEmpty() && QFileInfo::exists(removed.binned) && !QFileInfo::exists(removed.file)) {
        back = QFile::rename(removed.binned, removed.file);
        if (back) {
            // The bin keeps a note beside each file it holds.
            const QFileInfo held(removed.binned);
            QFile::remove(held.absoluteDir().absoluteFilePath(QStringLiteral("../info/") + held.fileName() + QStringLiteral(".trashinfo")));
        }
    }
    if (!back && !write(name, removed.songs)) {
        return false;
    }
    m_lists.insert(name, removed.songs);
    touched();
    return true;
}

bool Playlists::add(const QString &name, const QStringList &paths)
{
    if (name == likedName()) {
        setLikedAll(paths, true);
        return true;
    }
    if (!m_lists.contains(name)) {
        return false;
    }
    const QStringList list = m_lists.value(name) + paths;
    if (!write(name, list)) {
        return false;
    }
    m_lists.insert(name, list);
    touched();
    return true;
}

bool Playlists::setSongs(const QString &name, const QStringList &paths)
{
    const bool liked = name == likedName();
    if (!liked && !m_lists.contains(name)) {
        return false;
    }
    if (!write(name, paths)) {
        return false;
    }
    if (liked) {
        m_liked = paths;
        m_likedSet = QSet<QString>(paths.cbegin(), paths.cend());
    } else {
        m_lists.insert(name, paths);
    }
    touched();
    return true;
}

bool Playlists::removeSong(const QString &name, const QString &path)
{
    if (name == likedName()) {
        setLiked(path, false);
        return true;
    }
    QStringList list = m_lists.value(name);
    if (!list.removeOne(path)) {
        return false;
    }
    if (!write(name, list)) {
        return false;
    }
    m_lists.insert(name, list);
    touched();
    return true;
}
