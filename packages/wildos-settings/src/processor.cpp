#include "processor.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QProcess>

namespace
{
const QString wildosDir = QStringLiteral("/etc/wildos");
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-cpu-threads");

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

QByteArray readSmt(const char *name)
{
    return readFile(QStringLiteral("/sys/devices/system/cpu/smt/") + QLatin1String(name));
}

// "on", "off", or empty while the kernel decides.
QByteArray threadChoice()
{
    return readFile(QStringLiteral("/etc/wildos/cpu-threads"));
}

bool strictLevel()
{
    const QByteArray level = readFile(QStringLiteral("/etc/wildos/protection-level"));
    return level == "strict" || level == "maximum";
}

// 40_wildos.cfg takes spectre_v2=on off the start-up options unless extra
// protection applies, so its presence says which the running kernel got.
bool extraProtectionRunning()
{
    const QByteArray cmdline = readFile(QStringLiteral("/proc/cmdline"));
    return (" " + cmdline + " ").contains(" spectre_v2=on ");
}
}

Processor::Processor(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    // The choices and the protection level all live in /etc/wildos, written
    // by root helpers, so the switches follow them without Settings asking.
    if (QFile::exists(wildosDir)) {
        m_watcher->addPath(wildosDir);
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Processor::changed);
}

// "notsupported" and "notimplemented" mean a processor with one thread per core.
bool Processor::threadsAvailable() const
{
    const QByteArray control = readSmt("control");
    return QFile::exists(helper) && !control.isEmpty() && control != "notsupported" && control != "notimplemented";
}

bool Processor::everyThread() const
{
    if (everyThreadLocked()) {
        return false;
    }
    const QByteArray choice = threadChoice();
    if (choice == "on") {
        return true;
    }
    if (choice == "off") {
        return false;
    }
    return everyThreadActive();
}

bool Processor::everyThreadActive() const
{
    return readSmt("active") == "1";
}

bool Processor::threadsPending() const
{
    if (everyThreadLocked()) {
        return everyThreadActive();
    }
    const QByteArray choice = threadChoice();
    if (choice != "on" && choice != "off") {
        return false;
    }
    return (choice == "on") != everyThreadActive();
}

bool Processor::everyThreadLocked() const
{
    return extraProtection();
}

bool Processor::extraProtection() const
{
    return strictLevel() || readFile(QStringLiteral("/etc/wildos/cpu-protection")) == "extra";
}

bool Processor::extraProtectionLocked() const
{
    return strictLevel();
}

bool Processor::extraProtectionPending() const
{
    return extraProtection() != extraProtectionRunning();
}

void Processor::setEveryThread(bool on)
{
    if (m_busy || on == everyThread() || (on && everyThreadLocked())) {
        return;
    }
    run({helper, on ? QStringLiteral("on") : QStringLiteral("off")});
}

void Processor::setExtraProtection(bool on)
{
    if (m_busy || on == extraProtection() || (!on && extraProtectionLocked())) {
        return;
    }
    run({helper, QStringLiteral("protect"), on ? QStringLiteral("on") : QStringLiteral("off")});
}

void Processor::run(const QStringList &args)
{
    m_busy = true;
    Q_EMIT changed();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        m_busy = false;
        if (code == 126 || code == 127) {
            Q_EMIT failed(QStringLiteral("Nothing was changed because the password was not given."));
        } else if (code == 3) {
            Q_EMIT failed(QStringLiteral("Strict and Maximum protection keep every processor protection on."));
        } else if (code != 0) {
            Q_EMIT failed(QStringLiteral("The setting could not be changed."));
        }
        Q_EMIT changed();
    });
    process->start(QStringLiteral("pkexec"), args);
}
