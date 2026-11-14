#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>

class QFileSystemWatcher;

// The Advanced protection switches Files acts on (Settings > Privacy and
// security > Advanced protection), read from the system-wide store
// /etc/wildos/advanced-protection that Settings' helper writes, and watched
// so a change shows in open windows straight away. A key that is missing
// has the board's default: both are on.
//
// Also the person's own choice of what "Remove personal details" removes,
// kept in wildosfilesrc.
class Protection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool scrubFiles READ scrubFiles NOTIFY changed)
    Q_PROPERTY(bool safeBox READ safeBox NOTIFY changed)
    Q_PROPERTY(bool removeLocation READ removeLocation WRITE setRemoveLocation NOTIFY choicesChanged)
    Q_PROPERTY(bool removeDevice READ removeDevice WRITE setRemoveDevice NOTIFY choicesChanged)
    Q_PROPERTY(bool removeAuthors READ removeAuthors WRITE setRemoveAuthors NOTIFY choicesChanged)
    // Whether the safe box's limits have been explained once.
    Q_PROPERTY(bool safeBoxExplained READ safeBoxExplained WRITE setSafeBoxExplained NOTIFY choicesChanged)

public:
    static Protection *instance();
    static Protection *create(QQmlEngine *, QJSEngine *);

    bool scrubFiles() const { return m_scrubFiles; }
    bool safeBox() const { return m_safeBox; }
    bool removeLocation() const;
    void setRemoveLocation(bool on);
    bool removeDevice() const;
    void setRemoveDevice(bool on);
    bool removeAuthors() const;
    void setRemoveAuthors(bool on);
    bool safeBoxExplained() const;
    void setSafeBoxExplained(bool done);

Q_SIGNALS:
    void changed();
    void choicesChanged();

private:
    explicit Protection(QObject *parent = nullptr);
    void read();
    void write(const char *key, bool value);

    QFileSystemWatcher *m_watcher;
    KSharedConfigPtr m_config;
    bool m_scrubFiles = true;
    bool m_safeBox = true;
};
