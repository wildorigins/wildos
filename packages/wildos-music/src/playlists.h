#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

// Playlists and liked songs, each an .m3u8 file in Music/Playlists so any
// other player can open them; liked songs are "Liked songs.m3u8". Songs are
// written as paths relative to the playlist, so the Music folder can move
// to another PC with its playlists intact. Edits made outside Music are
// picked up as they happen.
class Playlists : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Bumped on every change, so a binding can read it alongside isLiked().
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // [{ name, count }], in name order.
    Q_PROPERTY(QVariantList all READ all NOTIFY changed)
    Q_PROPERTY(int likedCount READ likedCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    explicit Playlists(QObject *parent);
    static Playlists *instance();
    static Playlists *create(QQmlEngine *, QJSEngine *);

    int revision() const { return m_revision; }
    QVariantList all() const;
    int likedCount() const { return int(m_liked.size()); }
    QString lastError() const { return m_error; }

    Q_INVOKABLE bool isLiked(const QString &path) const;
    Q_INVOKABLE void setLiked(const QString &path, bool liked);
    Q_INVOKABLE void setLikedAll(const QStringList &paths, bool liked);
    Q_INVOKABLE bool allLiked(const QStringList &paths) const;
    Q_INVOKABLE QStringList liked() const { return m_liked; }

    Q_INVOKABLE QStringList songs(const QString &name) const;
    Q_INVOKABLE bool exists(const QString &name) const;
    // A name not taken yet: "New playlist", then "New playlist 2" and so on.
    Q_INVOKABLE QString freeName(const QString &base) const;
    // Each returns false and sets lastError when the file cannot be written.
    Q_INVOKABLE bool create(const QString &name, const QStringList &paths);
    Q_INVOKABLE bool rename(const QString &name, const QString &newName);
    Q_INVOKABLE bool remove(const QString &name);
    // Undoes the last remove(): the playlist's file comes back out of the
    // bin, or is written again if the bin no longer has it.
    Q_INVOKABLE bool restore(const QString &name);
    Q_INVOKABLE bool add(const QString &name, const QStringList &paths);
    // Takes one copy of the song out; a song can be in a playlist twice.
    Q_INVOKABLE bool removeSong(const QString &name, const QString &path);
    // Puts a playlist's songs back as they were, to undo taking some out.
    Q_INVOKABLE bool setSongs(const QString &name, const QStringList &paths);
    Q_INVOKABLE QString fileOf(const QString &name) const;

    // Songs listed in any .m3u or .m3u8 file, for one opened from Files.
    static QStringList read(const QString &file);

Q_SIGNALS:
    void changed();

private:
    void load();
    bool write(const QString &file, const QStringList &paths);
    bool validName(const QString &name);
    void touched();

    // The playlist remove() took away last: its name, its file, where
    // that went in the bin and the songs it held.
    struct Removed
    {
        QString name;
        QString file;
        QString binned;
        QStringList songs;
    };

    QHash<QString, QStringList> m_lists;
    Removed m_removed;
    QStringList m_liked;
    QSet<QString> m_likedSet;
    QString m_error;
    int m_revision = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
};
