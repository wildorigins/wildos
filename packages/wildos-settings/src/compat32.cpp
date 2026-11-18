#include "compat32.h"

#include <QFile>
#include <QDir>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QStandardPaths>

namespace
{
const QString wildosDir = QStringLiteral("/etc/wildos");
const QString choiceFile = QStringLiteral("/etc/wildos/32bit");
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-32bit");
const QString overlayHelper = QStringLiteral("/usr/libexec/wildos/settings-game-overlay");
const QString steamId = QStringLiteral("com.valvesoftware.Steam");

QString userFlatpak()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/flatpak");
}
}

Compat32::Compat32(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    // The choice and the protection level both live in /etc/wildos, written
    // by root helpers (the 32-bit prompt can turn it on too), so the switch
    // follows them without Settings asking.
    if (QFile::exists(wildosDir)) {
        m_watcher->addPath(wildosDir);
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Compat32::changed);
    // The overlay lives only while 32-bit apps are on: turned off here, or
    // at Strict and Maximum, it goes too, the next time Settings sees it.
    connect(this, &Compat32::changed, this, [this] {
        if (!enabled() && overlay() && !m_overlayBusy) {
            setOverlay(false);
        }
    });
    if (!enabled() && overlay()) {
        setOverlay(false);
    }
}

bool Compat32::steamInstalled() const
{
    return QFile::exists(userFlatpak() + QStringLiteral("/app/") + steamId)
        || QFile::exists(QStringLiteral("/var/lib/flatpak/app/") + steamId);
}

// Read straight from Steam's override file, as settings-game-overlay status
// does through flatpak, without starting a process for every look.
bool Compat32::overlay() const
{
    QFile file(userFlatpak() + QStringLiteral("/overrides/") + steamId);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (line.trimmed() == "MANGOHUD=1") {
            return true;
        }
    }
    return false;
}

void Compat32::setOverlay(bool on)
{
    if (m_overlayBusy || on == overlay() || (on && (!enabled() || locked()))) {
        return;
    }
    m_overlayBusy = true;
    Q_EMIT changed();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        m_overlayBusy = false;
        if (code == 3) {
            Q_EMIT failed(tr("Install Steam from the Store first."));
        } else if (code != 0) {
            Q_EMIT failed(tr("The overlay could not be downloaded. Check you're online and try again."));
        }
        Q_EMIT changed();
    });
    process->start(overlayHelper, {on ? QStringLiteral("on") : QStringLiteral("off")});
}

bool Compat32::available() const
{
    return QFile::exists(helper);
}

bool Compat32::enabled() const
{
    return QFile::exists(choiceFile);
}

// The kernel takes the last ia32_emulation= it was started with; without
// one, 32-bit programs run.
bool Compat32::active() const
{
    QFile file(QStringLiteral("/proc/cmdline"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray setting;
    for (const QByteArray &word : file.readAll().simplified().split(' ')) {
        if (word.startsWith("ia32_emulation=")) {
            setting = word.mid(15);
        }
    }
    static const QList<QByteArray> off{"0", "n", "N", "no", "off", "false"};
    return !off.contains(setting);
}

bool Compat32::locked() const
{
    QFile file(QStringLiteral("/etc/wildos/protection-level"));
    const QByteArray level = file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
    return level == "strict" || level == "maximum";
}

void Compat32::setEnabled(bool on)
{
    if (m_busy || on == enabled() || (on && locked())) {
        return;
    }
    m_busy = true;
    Q_EMIT changed();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        m_busy = false;
        if (code == 126 || code == 127) {
            Q_EMIT failed(tr("Nothing was changed because the password was not given."));
        } else if (code == 3) {
            Q_EMIT failed(tr("Strict and Maximum protection keep 32-bit apps off."));
        } else if (code != 0) {
            Q_EMIT failed(tr("The setting could not be changed."));
        }
        Q_EMIT changed();
    });
    process->start(QStringLiteral("pkexec"), {helper, on ? QStringLiteral("on") : QStringLiteral("off")});
}
