#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QProcess;

// Settings > Privacy and security > Firewall. Reads the choices the root
// helper settings-firewall keeps (/etc/wildos/firewall.json, rules that last
// until a restart in /run), what it last loaded, and the list of blocked
// connections wildos-network keeps; asks for every change through the
// helper, behind the admin password.
class Firewall : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    // The rules are in place (settings-firewall's mark); false while on
    // means they failed to load.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool discovery READ discovery NOTIFY changed)
    // Connection uuids marked public, and those marked home. A network in
    // neither has not been chosen yet: only discovery is let in there.
    Q_PROPERTY(QVariantMap publicNetworks READ publicNetworks NOTIFY changed)
    Q_PROPERTY(QVariantMap homeNetworks READ homeNetworks NOTIFY changed)
    // ssh, files, remote: {on, installed}; remote also {running}, KRdp's
    // server being up in this session (it stops until an account is added).
    Q_PROPERTY(QVariantMap services READ services NOTIFY changed)
    // Rules the person added, oldest first: {id, name, port, proto, scope,
    // address, until (date, invalid when none), on, session}.
    Q_PROPERTY(QVariantList rules READ rules NOTIFY changed)
    // Newest first: {when (date), source, proto, port, kind}, kind being
    // router, local, internet or other.
    Q_PROPERTY(QVariantList blocked READ blocked NOTIFY blockedChanged)
    // Programs waiting for connections that the firewall turns away:
    // {name, port, proto}.
    Q_PROPERTY(QVariantList listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // What is being changed: "enabled", "trust", "discovery", "ssh",
    // "files", "remote", "allow", or a rule id.
    Q_PROPERTY(QString busyWhat READ busyWhat NOTIFY busyChanged)
    // While a server installs: 0 to 100, or -1 before apt says.
    Q_PROPERTY(int progress READ progress NOTIFY busyChanged)
    Q_PROPERTY(QString step READ step NOTIFY busyChanged)

public:
    explicit Firewall(QObject *parent = nullptr);

    bool available() const;
    bool enabled() const { return m_enabled; }
    bool loaded() const { return m_loaded; }
    bool discovery() const { return m_discovery; }
    QVariantMap publicNetworks() const { return m_public; }
    QVariantMap homeNetworks() const { return m_home; }
    QVariantMap services() const { return m_services; }
    QVariantList rules() const { return m_rules; }
    QVariantList blocked() const { return m_blocked; }
    QVariantList listening() const { return m_listening; }
    bool busy() const { return m_process != nullptr; }
    QString busyWhat() const { return m_busyWhat; }
    int progress() const { return m_progress; }
    QString step() const { return m_step; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshListening();
    Q_INVOKABLE void setEnabled(bool on);
    Q_INVOKABLE void setPublic(const QString &uuid, bool isPublic);
    Q_INVOKABLE void setDiscovery(bool on);
    // ssh, files or remote. The first time on, the server is downloaded.
    Q_INVOKABLE void setService(const QString &name, bool on);
    // scope network, device or anyone; duration forever, restart or hour.
    Q_INVOKABLE void allow(const QString &name, int port, const QString &proto, const QString &scope, const QString &address,
                           const QString &duration);
    Q_INVOKABLE void setRuleOn(const QString &id, bool on);
    Q_INVOKABLE void removeRule(const QString &id);
    Q_INVOKABLE void clearBlocked();
    // Empty when the address is fine, otherwise why not, in words.
    Q_INVOKABLE QString checkAddress(const QString &address) const;
    // What usually waits on a port ("SSH", "Remote Desktop"), or "".
    Q_INVOKABLE QString serviceName(int port, const QString &proto) const;
    // A row that already lets the port in, or a rule that does.
    Q_INVOKABLE bool allowed(int port, const QString &proto) const;

Q_SIGNALS:
    void changed();
    void blockedChanged();
    void listeningChanged();
    void busyChanged();
    // A change did not happen; the reason, in words.
    void failed(const QString &message);
    // A change went through.
    void done(const QString &what);

private:
    void run(const QStringList &args, const QString &what, const QString &program = {});
    void readBlocked();
    void watch();
    void readOutput();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QProcess *m_process = nullptr;
    QString m_busyWhat;
    int m_progress = -1;
    QString m_step;
    QString m_lastLine;
    bool m_enabled = true;
    bool m_loaded = false;
    bool m_discovery = true;
    QVariantMap m_public;
    QVariantMap m_home;
    QVariantMap m_services;
    QVariantList m_rules;
    QVariantList m_blocked;
    QVariantList m_listening;
};
