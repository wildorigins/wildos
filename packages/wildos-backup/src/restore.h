#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QVariantList>

class Restic;

// Restore files: the backups on the drive by date, the folders in one,
// search, and getting picked files and folders back, to where they were or
// to a folder of the person's choosing.
//
// restic restores into a hidden folder beside where things are going, and
// they are moved into place from there: nothing is overwritten unless the
// person said to replace, and by default both copies are kept.
class Restore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "idle", "opening", "noDrive", "needPassword", "ready" or "failed"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY stateChanged)
    Q_PROPERTY(QString driveName READ driveName NOTIFY stateChanged)
    // [{ id, time, dataAdded, files }], newest first
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY snapshotsChanged)
    Q_PROPERTY(QString snapshot READ snapshot NOTIFY folderChanged)
    Q_PROPERTY(QDateTime snapshotTime READ snapshotTime NOTIFY folderChanged)
    Q_PROPERTY(QString root READ root NOTIFY folderChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    // [{ name, path, dir, size, mtime }]
    Q_PROPERTY(QVariantList entries READ entries NOTIFY folderChanged)
    Q_PROPERTY(bool listing READ listing NOTIFY folderChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY folderChanged)

    Q_PROPERTY(QStringList selected READ selected NOTIFY selectionChanged)

    Q_PROPERTY(bool restoring READ restoring NOTIFY restoreChanged)
    Q_PROPERTY(double restorePercent READ restorePercent NOTIFY restoreChanged)

public:
    explicit Restore(QObject *parent = nullptr);
    ~Restore() override;
    static Restore *create(QQmlEngine *, QJSEngine *);

    QString state() const
    {
        return m_state;
    }
    QString error() const
    {
        return m_error;
    }
    // The backup tool's own words, for Details, when `error` is only the
    // general sentence.
    QString errorDetail() const
    {
        return m_errorDetail;
    }
    QString driveName() const
    {
        return m_driveName;
    }
    QVariantList snapshots() const
    {
        return m_snapshots;
    }
    QString snapshot() const
    {
        return m_snapshot;
    }
    QDateTime snapshotTime() const
    {
        return m_snapshotTime;
    }
    QString root() const
    {
        return m_root;
    }
    QString folder() const
    {
        return m_folder;
    }
    QVariantList entries() const
    {
        return m_entries;
    }
    bool listing() const
    {
        return m_listing;
    }
    QString searchText() const
    {
        return m_searchText;
    }
    QStringList selected() const;
    bool restoring() const
    {
        return m_restoring;
    }
    double restorePercent() const
    {
        return m_restorePercent;
    }

    // Opens the backups on the person's drive (the default if plugged in).
    Q_INVOKABLE void open();
    Q_INVOKABLE void givePassword(const QString &password);
    Q_INVOKABLE void chooseSnapshot(const QString &id);
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE void up();
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void toggle(const QString &path);
    Q_INVOKABLE bool isSelected(const QString &path) const;
    Q_INVOKABLE void clearSelection();
    // How many picked items already exist where they would go ("" for
    // where they were).
    Q_INVOKABLE int clashes(const QString &destination) const;
    Q_INVOKABLE void restore(const QString &destination, bool replace);
    Q_INVOKABLE void cancelRestore();
    Q_INVOKABLE void showFolder(const QString &path);

Q_SIGNALS:
    void stateChanged();
    void snapshotsChanged();
    void folderChanged();
    void selectionChanged();
    void restoreChanged();
    // count of items put back, and the folder to show them in.
    void restored(int count, const QString &where);
    void restoreFailed(const QString &reason, const QString &detail);

private:
    void setState(const QString &state, const QString &error = QString(), const QString &detail = QString());
    void loadSnapshots();
    void list(const QString &path);
    void moveIntoPlace(const QString &stage, const QStringList &items, const QString &destination, bool replace);
    Restic *newRestic();
    QString target(const QString &item, const QString &destination) const;

    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_errorDetail;
    QString m_driveName;
    QString m_repo;
    QByteArray m_password;
    QVariantList m_snapshots;
    QString m_snapshot;
    QDateTime m_snapshotTime;
    QString m_root;
    QString m_folder;
    QVariantList m_entries;
    bool m_listing = false;
    QString m_searchText;
    QSet<QString> m_selected;
    bool m_restoring = false;
    double m_restorePercent = -1;
    QString m_stage;
    QPointer<Restic> m_restic;
    QPointer<Restic> m_restoreRun;
};
