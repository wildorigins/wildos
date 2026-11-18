#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

// Every note under ~/Notes: one folder per notebook, one Markdown file per
// note. The folder is the only store; this reads it, watches it for changes
// made outside Notes, and lists the notes the sidebar has picked.
class NoteStore : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString root READ root CONSTANT)
    Q_PROPERTY(QVariantList notebooks READ notebooks NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY libraryChanged)
    Q_PROPERTY(int allCount READ allCount NOTIFY libraryChanged)
    Q_PROPERTY(int pinnedCount READ pinnedCount NOTIFY libraryChanged)
    Q_PROPERTY(QString defaultNotebook READ defaultNotebook NOTIFY libraryChanged)
    Q_PROPERTY(QString viewKind READ viewKind WRITE setViewKind NOTIFY viewChanged)
    Q_PROPERTY(QString viewKey READ viewKey WRITE setViewKey NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY viewChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        NotebookRole,
        PinnedRole,
        ColourRole,
        DateTextRole,
        TagsRole,
    };

    struct Note {
        QString path;
        QString notebook;
        QString title;
        QString preview;
        QString search;
        QStringList tags;
        QString colour;
        bool pinned = false;
        QDateTime modified;
        QDateTime created;
    };

    // No default constructor: QML would make a second one instead of
    // calling create().
    explicit NoteStore(QObject *parent);
    static NoteStore *instance();
    static NoteStore *create(QQmlEngine *, QJSEngine *);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString root() const;
    QVariantList notebooks() const;
    QVariantList tags() const;
    int allCount() const;
    int pinnedCount() const;
    QString defaultNotebook() const;
    QString viewKind() const;
    void setViewKind(const QString &kind);
    QString viewKey() const;
    void setViewKey(const QString &key);
    QString search() const;
    void setSearch(const QString &search);
    QString sort() const;
    void setSort(const QString &sort);
    int count() const;
    QString lastError() const;

    // A new, empty note in the notebook; returns its path.
    Q_INVOKABLE QString createNote(const QString &notebook);
    Q_INVOKABLE bool trashNote(const QString &path);
    // Returns the note's new path, with any pictures it shows copied along.
    Q_INVOKABLE QString moveNote(const QString &path, const QString &notebook);
    // Returns the name used, or an empty string (lastError says why).
    Q_INVOKABLE QString createNotebook(const QString &name);
    Q_INVOKABLE QString renameNotebook(const QString &from, const QString &to);
    Q_INVOKABLE bool trashNotebook(const QString &name);
    // Brings back the note or notebook last moved to the bin from here, for
    // Undo. Returns where it is again, or an empty string (lastError says
    // why).
    Q_INVOKABLE QString restoreTrashed();
    Q_INVOKABLE int notebookCount(const QString &name) const;
    Q_INVOKABLE int indexOfPath(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE void showInFiles(const QString &path) const;
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void copyText(const QString &text) const;

    Q_INVOKABLE QString notebookDir(const QString &notebook) const;
    Q_INVOKABLE QString notebookOf(const QString &path) const;
    // A free path for a note called name in dir, other than ignore.
    static QString freePath(const QString &dir, const QString &name, const QString &ignore = {});
    // The note was saved or renamed by Notes itself.
    void noteWritten(const QString &oldPath, const QString &newPath);
    QDateTime createdAt(const QString &path);
    static QString dateText(const QDateTime &when);
    static QString editedText(const QDateTime &when);

Q_SIGNALS:
    void libraryChanged();
    void viewChanged();
    void countChanged();
    void lastErrorChanged();
    // A note's file changed on disk, by Notes or by anything else.
    void fileChanged(const QString &path);
    // Notes moved a file: a note, or every note in a renamed notebook.
    void pathMoved(const QString &from, const QString &to);

private:
    void ensureRoot();
    void scheduleRescan();
    void readAll();
    void refilter();
    void fail(const QString &message);
    void loadCreated();
    void saveCreatedSoon();
    void saveCreatedNow();
    bool matches(const Note &note) const;
    void rememberTrashed(const QString &from, const QString &inBin);

    struct Trashed {
        QString from;
        QString inBin;
        // When its notes were first seen, which a rescan forgets once the
        // files are gone.
        QHash<QString, qint64> created;
    };
    Trashed m_trashed;

    QString m_root;
    QList<Note> m_all;
    QList<int> m_rows;
    QStringList m_rowPaths;
    QStringList m_notebooks;
    QString m_viewKind = QStringLiteral("all");
    QString m_viewKey;
    QString m_search;
    QString m_sort = QStringLiteral("edited");
    QString m_lastError;
    QFileSystemWatcher m_watcher;
    QTimer m_rescan;
    QTimer m_saveCreated;
    QHash<QString, qint64> m_created;
    QHash<QString, QDateTime> m_seenModified;
};
