#include "startupdisk.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace
{
const QString statusHelper = QStringLiteral("/usr/libexec/wildos/startup-status");
const QString helper = QStringLiteral("/usr/libexec/wildos/startup-ctl");
}

StartupDisk::StartupDisk(QObject *parent)
    : QObject(parent)
    , m_installed(QFileInfo::exists(helper))
{
}

void StartupDisk::refresh()
{
    if (!m_installed) {
        return;
    }
    if (m_process) {
        m_again = true;
        return;
    }
    start({statusHelper}, true);
}

void StartupDisk::request(const QStringList &args)
{
    if (!m_installed) {
        return;
    }
    setChanging(true);
    if (m_process) {
        m_pending = args;
        return;
    }
    start(QStringList{helper} + args, false);
}

void StartupDisk::start(const QStringList &command, bool reading)
{
    m_process = new QProcess(this);
    // The helper's JSON is on stdout; pkexec and Python put their own words
    // on stderr.
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this, [this, reading](int code, QProcess::ExitStatus status) {
        // 126 and 127: the password prompt was dismissed.
        done(reading, status == QProcess::NormalExit && code == 0, code == 126 || code == 127);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, reading](QProcess::ProcessError error) {
        // Nothing ran, so finished() never comes.
        if (error == QProcess::FailedToStart) {
            done(reading, false, false);
        }
    });
    m_process->start(QStringLiteral("pkexec"), command);
}

void StartupDisk::done(bool reading, bool ok, bool dismissed)
{
    const QByteArray output = m_process->readAllStandardOutput();
    m_process->deleteLater();
    m_process = nullptr;

    if (reading) {
        if (ok) {
            read(output);
        } else if (!m_loaded) {
            m_unreadable = true;
        }
        // Even when nothing differs: a control that moved before a
        // dismissed password prompt goes back to what the helper reports.
        Q_EMIT changed();
    } else if (!ok && !dismissed) {
        Q_EMIT failed(tr("Something went wrong. Nothing was changed."));
    }

    if (!m_pending.isEmpty()) {
        const QStringList args = m_pending;
        m_pending.clear();
        start(QStringList{helper} + args, false);
    } else if (!reading || m_again) {
        m_again = false;
        start({statusHelper}, true);
    } else {
        setChanging(false);
    }
}

void StartupDisk::read(const QByteArray &json)
{
    const QJsonObject status = QJsonDocument::fromJson(json).object();
    if (status.isEmpty()) {
        m_unreadable = !m_loaded;
        return;
    }
    m_loaded = true;
    m_unreadable = false;
    m_efi = status.value(QStringLiteral("efi")).toBool();
    m_mode = status.value(QStringLiteral("mode")).toString(QStringLiteral("hold"));
    m_wait = status.value(QStringLiteral("wait")).toInt(5);
    m_restore = status.value(QStringLiteral("restore")).toBool();
    m_restoreAvailable = status.value(QStringLiteral("restore_available")).toBool();
    m_password = status.value(QStringLiteral("password")).toBool();
    m_encrypted = status.value(QStringLiteral("encrypted")).toBool();
    m_systems = status.value(QStringLiteral("systems")).toArray().toVariantList();
}

void StartupDisk::setChanging(bool changing)
{
    if (m_changing != changing) {
        m_changing = changing;
        Q_EMIT busyChanged();
    }
}

void StartupDisk::setDefault(const QString &id)
{
    request({QStringLiteral("default"), id});
}

void StartupDisk::setMode(const QString &mode)
{
    request({QStringLiteral("mode"), mode});
}

void StartupDisk::setWait(int seconds)
{
    request({QStringLiteral("wait"), QString::number(seconds)});
}

void StartupDisk::setRestore(bool on)
{
    request({QStringLiteral("restore"), on ? QStringLiteral("on") : QStringLiteral("off")});
}

void StartupDisk::setPassword(bool on)
{
    request({QStringLiteral("password"), on ? QStringLiteral("on") : QStringLiteral("off")});
}

void StartupDisk::restartInto(const QString &id)
{
    request({QStringLiteral("restart"), id});
}

void StartupDisk::restartToFirmware()
{
    request({QStringLiteral("firmware")});
}
