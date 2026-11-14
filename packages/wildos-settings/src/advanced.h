#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class QProcess;

// Settings > Privacy and security > Advanced protection. Reads the store the
// root helper settings-advanced keeps (/etc/wildos/advanced-protection,
// world-readable, a missing key meaning its default) and the list of keys
// set by hand; asks for every change through the helper, behind the admin
// password. Each change runs that key's hook in
// /usr/lib/wildos/advanced-protection.d, and a hook being installed is how
// the page knows the part of Wild OS that carries a choice out is there.
class Advanced : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    // Every key, with its value now: "on"/"off", hours as a number, or
    // dns_provider "quad9", "mullvad", "cloudflare".
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    // Keys set by hand, which no protection level changes.
    Q_PROPERTY(QStringList chosen READ chosen NOTIFY changed)
    // Key to true where its hook is installed.
    Q_PROPERTY(QVariantMap installed READ installed NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // The key being changed, or "reset".
    Q_PROPERTY(QString busyKey READ busyKey NOTIFY busyChanged)

public:
    explicit Advanced(QObject *parent = nullptr);

    bool available() const;
    QVariantMap values() const { return m_values; }
    QStringList chosen() const { return m_chosen; }
    QVariantMap installed() const { return m_installed; }
    bool busy() const { return m_process != nullptr; }
    QString busyKey() const { return m_busyKey; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    // Back to the defaults, forgetting what was set by hand.
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();
    void busyChanged();
    // A change did not happen; the reason, in words.
    void failed(const QString &key, const QString &message);
    // A change went through.
    void done(const QString &key);

private:
    void run(const QStringList &args, const QString &key);

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QProcess *m_process = nullptr;
    QString m_busyKey;
    QVariantMap m_values;
    QStringList m_chosen;
    QVariantMap m_installed;
};
