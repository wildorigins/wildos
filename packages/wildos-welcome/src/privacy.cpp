#include "privacy.h"
#include "setup.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace
{
const QString notifierFile = QStringLiteral("org.kde.discover.notifier.desktop");
// apt-daily refreshes the package lists from the update servers once a day
// and apt-daily-upgrade installs security fixes from them.
const QStringList updateTimers = {QStringLiteral("apt-daily.timer"), QStringLiteral("apt-daily-upgrade.timer")};

QString systemNotifier()
{
    const QString path = QStringLiteral("/etc/xdg/autostart/") + notifierFile;
    return QFileInfo::exists(path) ? path : QString();
}

QString userNotifier()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart/") + notifierFile;
}

bool timerExists(const QString &timer)
{
    for (const QString &dir : {QStringLiteral("/usr/lib/systemd/system/"), QStringLiteral("/lib/systemd/system/"), QStringLiteral("/etc/systemd/system/")}) {
        if (QFileInfo::exists(dir + timer)) {
            return true;
        }
    }
    return false;
}

bool timerEnabled(const QString &timer)
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), {QStringLiteral("is-enabled"), timer});
    if (!process.waitForFinished(3000)) {
        return false;
    }
    return process.readAllStandardOutput().trimmed() == "enabled";
}
}

Privacy::Privacy(QObject *parent)
    : QObject(parent)
{
    m_updateChecks = readUpdateChecks();
}

bool Privacy::updateChecksAvailable() const
{
    return !systemNotifier().isEmpty() || timerExists(updateTimers.first());
}

bool Privacy::readUpdateChecks() const
{
    bool timers = false;
    for (const QString &timer : updateTimers) {
        if (timerExists(timer) && timerEnabled(timer)) {
            timers = true;
        }
    }
    bool notifier = false;
    if (!systemNotifier().isEmpty()) {
        KConfigGroup entry = KSharedConfig::openConfig(userNotifier(), KConfig::SimpleConfig)->group(QStringLiteral("Desktop Entry"));
        notifier = !entry.readEntry("Hidden", false);
    }
    return timers || notifier;
}

void Privacy::setNotifierHidden(bool hidden)
{
    if (systemNotifier().isEmpty() || Setup::planning()) {
        return;
    }
    if (!hidden) {
        QFile::remove(userNotifier());
        return;
    }
    // A Hidden copy in the person's own autostart folder switches the
    // system-wide entry off for this account only.
    QDir().mkpath(QFileInfo(userNotifier()).absolutePath());
    QFile::copy(systemNotifier(), userNotifier());
    QFile(userNotifier()).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    KSharedConfigPtr config = KSharedConfig::openConfig(userNotifier(), KConfig::SimpleConfig);
    config->group(QStringLiteral("Desktop Entry")).writeEntry("Hidden", true);
    config->sync();
}

void Privacy::setUpdateChecks(bool on)
{
    if (m_busy || on == m_updateChecks) {
        return;
    }
    QStringList timers;
    for (const QString &timer : updateTimers) {
        if (timerExists(timer)) {
            timers << timer;
        }
    }
    m_error.clear();
    if (timers.isEmpty()) {
        setNotifierHidden(!on);
        m_updateChecks = on;
        Q_EMIT changed();
        Q_EMIT busyChanged();
        return;
    }

    // The daily check is the system's, not this account's, so changing it
    // asks for the administrator password.
    m_busy = true;
    Q_EMIT busyChanged();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, on](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        m_busy = false;
        if (status == QProcess::NormalExit && exitCode == 0) {
            setNotifierHidden(!on);
            m_updateChecks = on;
        } else {
            m_error = exitCode == 126 || exitCode == 127 ? tr("Changing this needs the administrator password, and it was not given.")
                                                         : tr("Wild OS couldn't change this. Try again in Settings.");
        }
        Q_EMIT changed();
        Q_EMIT busyChanged();
    });
    // Wild OS's own helper switches the update timers and nothing else, so
    // the password prompt names that one change (polkit action
    // org.wildos.update-checks) rather than a general systemctl call.
    process->start(QStringLiteral("pkexec"), {QStringLiteral("/usr/libexec/wildos/update-checks"), on ? QStringLiteral("on") : QStringLiteral("off")});
}

bool Privacy::crashReportsAvailable() const
{
    const QStringList candidates = {QStringLiteral("/usr/lib/x86_64-linux-gnu/libexec/drkonqi"), QStringLiteral("/usr/lib/aarch64-linux-gnu/libexec/drkonqi"),
                                    QStringLiteral("/usr/libexec/drkonqi")};
    for (const QString &path : candidates) {
        if (QFileInfo(path).isExecutable()) {
            return true;
        }
    }
    return false;
}

bool Privacy::crashReports() const
{
    if (Setup::planning()) {
        return m_plannedCrashReports;
    }
    // The crash handler's own "send automatically" choice. With it on, a
    // crash report goes off without the crash dialog asking first.
    return KSharedConfig::openConfig(QStringLiteral("drkonqirc"))->group(QStringLiteral("General")).readEntry("Sentry", false);
}

void Privacy::setCrashReports(bool on)
{
    if (Setup::planning()) {
        m_plannedCrashReports = on;
        Q_EMIT changed();
        return;
    }
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("drkonqirc"));
    config->group(QStringLiteral("General")).writeEntry("Sentry", on);
    config->sync();
    Q_EMIT changed();
}

bool Privacy::location() const
{
    // The same test Settings > Privacy and security uses: with the location
    // service installed, apps ask before using it; without it nothing can.
    return QFileInfo::exists(QStringLiteral("/usr/libexec/geoclue"));
}

QVariantMap Privacy::plan() const
{
    return {
        {QStringLiteral("updateChecks"), m_updateChecks},
        {QStringLiteral("crashReports"), crashReports()},
    };
}

void Privacy::apply(const QVariantMap &plan)
{
    setNotifierHidden(!plan.value(QStringLiteral("updateChecks"), true).toBool());
    setCrashReports(plan.value(QStringLiteral("crashReports")).toBool());
}
