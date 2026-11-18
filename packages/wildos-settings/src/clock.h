#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

// The date, time and time zone, through systemd's timedated (the service
// KDE's clock module uses too), which asks for the administrator password
// through polkit when the account needs one.
class Clock : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString timeZone READ timeZone NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic NOTIFY changed)
    Q_PROPERTY(bool canAutomatic READ canAutomatic NOTIFY changed)
    Q_PROPERTY(bool synchronised READ synchronised NOTIFY changed)
    // sdwdate sets the clock through Tor; timedated does not know it, so
    // automatic says off while it runs.
    Q_PROPERTY(bool secureTime READ secureTime NOTIFY changed)
    // sdwdate has set the clock since this start-up.
    Q_PROPERTY(bool secureTimeSet READ secureTimeSet NOTIFY changed)
    // Where the clock is set from: "tor" (sdwdate) or "server"
    // (systemd-timesyncd), and the server named for it, empty for Debian's.
    Q_PROPERTY(QString timeSource READ timeSource NOTIFY changed)
    Q_PROPERTY(QString timeServer READ timeServer NOTIFY changed)
    Q_PROPERTY(bool sourceBusy READ sourceBusy NOTIFY changed)

public:
    explicit Clock(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString timeZone() const { return m_timeZone; }
    bool automatic() const { return m_automatic; }
    bool canAutomatic() const { return m_canAutomatic; }
    bool synchronised() const { return m_synchronised; }
    bool secureTime() const { return m_secureTime; }
    bool secureTimeSet() const { return m_secureTimeSet; }
    QString timeSource() const { return m_timeSource; }
    QString timeServer() const { return m_timeServer; }
    bool sourceBusy() const { return m_sourceBusy; }

    // Every zone as { value: "Europe/London", text: "(UTC+00:00) London, Europe" },
    // in order of offset as Windows lists them.
    Q_INVOKABLE QVariantList timeZones() const;
    Q_INVOKABLE void setTimeZone(const QString &id);
    Q_INVOKABLE void setAutomatic(bool on);
    // Asks for the administrator password. server is only used with
    // "server"; whether it is a name a time server could have is checked
    // here and again by the helper.
    Q_INVOKABLE void setTimeSource(const QString &source, const QString &server);
    Q_INVOKABLE bool serverNameValid(const QString &server) const;
    // day, month, year, hour, minute in local time.
    Q_INVOKABLE void setDateTime(int day, int month, int year, int hour, int minute);

Q_SIGNALS:
    void changed();
    void failed(const QString &reason);

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void read();
    void readSecureTime();
    void readTimeSource();
    void call(const QString &method, const QVariantList &arguments);

    bool m_available = false;
    QString m_timeZone;
    bool m_automatic = false;
    bool m_canAutomatic = false;
    bool m_synchronised = false;
    bool m_secureTime = false;
    bool m_secureTimeSet = false;
    QString m_timeSource = QStringLiteral("tor");
    QString m_timeServer;
    bool m_sourceBusy = false;
};
