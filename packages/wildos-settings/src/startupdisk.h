#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

class QProcess;

// Settings > System > Start-up disk: what this PC can start, which of them
// starts by default, when the start-up manager shows and restarting
// straight into another system. Everything is wildos-startup's: one helper
// that only reads (startup-status, no password) and one that takes the
// page's fixed requests behind the admin password (startup-ctl). After any
// request the status is read again, so the page shows what the helper
// reports and a dismissed password prompt puts the control back.
class StartupDisk : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // wildos-startup is on this PC. Without it the page does not exist.
    Q_PROPERTY(bool installed READ installed CONSTANT)
    // The status has been read at least once.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    // The first read failed, so there is nothing to show.
    Q_PROPERTY(bool unreadable READ unreadable NOTIFY changed)
    Q_PROPERTY(bool efi READ efi NOTIFY changed)
    // "hold" (only while Space is held), "always" or "off".
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(int wait READ wait NOTIFY changed)
    Q_PROPERTY(bool restore READ restore NOTIFY changed)
    Q_PROPERTY(bool restoreAvailable READ restoreAvailable NOTIFY changed)
    Q_PROPERTY(bool password READ password NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY changed)
    // As the helper lists them: {kind, name, id, disk_rank, disk_bytes,
    // removable, bitlocker, default, current}.
    Q_PROPERTY(QVariantList systems READ systems NOTIFY changed)
    // A request is with the helper, or its result is being read back.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit StartupDisk(QObject *parent = nullptr);

    bool installed() const { return m_installed; }
    bool loaded() const { return m_loaded; }
    bool unreadable() const { return m_unreadable; }
    bool efi() const { return m_efi; }
    QString mode() const { return m_mode; }
    int wait() const { return m_wait; }
    bool restore() const { return m_restore; }
    bool restoreAvailable() const { return m_restoreAvailable; }
    bool password() const { return m_password; }
    bool encrypted() const { return m_encrypted; }
    QVariantList systems() const { return m_systems; }
    bool busy() const { return m_changing; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setDefault(const QString &id);
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void setWait(int seconds);
    Q_INVOKABLE void setRestore(bool on);
    Q_INVOKABLE void setPassword(bool on);
    Q_INVOKABLE void restartInto(const QString &id);
    Q_INVOKABLE void restartToFirmware();

Q_SIGNALS:
    void changed();
    void busyChanged();
    // A request the helper refused or could not carry out.
    void failed(const QString &message);

private:
    void request(const QStringList &args);
    void start(const QStringList &command, bool reading);
    void done(bool reading, bool ok, bool dismissed);
    void read(const QByteArray &json);
    void setChanging(bool changing);

    const bool m_installed;
    QProcess *m_process = nullptr;
    // A request made while the helper was busy, and a read asked for
    // meanwhile. The newest request wins: each is one control's new value.
    QStringList m_pending;
    bool m_again = false;
    bool m_changing = false;
    bool m_loaded = false;
    bool m_unreadable = false;
    bool m_efi = false;
    QString m_mode = QStringLiteral("hold");
    int m_wait = 5;
    bool m_restore = true;
    bool m_restoreAvailable = false;
    bool m_password = false;
    bool m_encrypted = false;
    QVariantList m_systems;
};
