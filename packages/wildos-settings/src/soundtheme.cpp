#include "soundtheme.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QProcess>
#include <QStandardPaths>

namespace
{
KConfigGroup sounds()
{
    return KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->group(QStringLiteral("Sounds"));
}

// The shutdown sound's own switch. Off unless turned on: shut down and
// restart wait for the sound to finish, and Windows 11 plays none.
KConfigGroup wildosSound()
{
    return KSharedConfig::openConfig(QStringLiteral("wildosrc"))->group(QStringLiteral("Sound"));
}

KConfigGroup sessionEvent(const QString &event)
{
    return KSharedConfig::openConfig(QStringLiteral("plasma_workspace.notifyrc"))->group(QStringLiteral("Event/") + event);
}
}

SoundTheme::SoundTheme(QObject *parent)
    : QObject(parent)
{
}

QString SoundTheme::scheme() const
{
    const auto g = sounds();
    if (!g.readEntry("Enable", true)) {
        return QStringLiteral("none");
    }
    return g.readEntry("Theme", QStringLiteral("ocean")) == QLatin1String("wildos") ? QStringLiteral("wildos") : QStringLiteral("ocean");
}

bool SoundTheme::startupSound() const
{
    return sessionEvent(QStringLiteral("startkde")).readEntry("Action", QString()).contains(QLatin1String("Sound"));
}

bool SoundTheme::shutdownSound() const
{
    KSharedConfig::openConfig(QStringLiteral("wildosrc"))->reparseConfiguration();
    return wildosSound().readEntry("ShutdownSound", false);
}

void SoundTheme::setScheme(const QString &scheme)
{
    auto g = sounds();
    if (scheme == QLatin1String("none")) {
        g.writeEntry("Enable", false, KConfig::Notify);
    } else {
        // A true Enable is the default, so it isn't written down.
        g.revertToDefault("Enable", KConfig::Notify);
        g.writeEntry("Theme", scheme == QLatin1String("ocean") ? QStringLiteral("ocean") : QStringLiteral("wildos"), KConfig::Notify);
    }
    g.sync();
    Q_EMIT changed();
    if (scheme != QLatin1String("none")) {
        preview(QStringLiteral("message-new-instant"));
    }
}

// Kept on Plasma's start-up event, which plays it.
void SoundTheme::setStartupSound(bool on)
{
    auto g = sessionEvent(QStringLiteral("startkde"));
    g.writeEntry("Action", on ? QStringLiteral("Sound") : QString(), KConfig::Notify);
    g.sync();
    Q_EMIT changed();
}

// Read by /usr/libexec/wildos/logout-sound, which plays the sound as the
// session ends. Plasma's own logout event would hold the shutdown back
// until it had finished.
void SoundTheme::setShutdownSound(bool on)
{
    auto g = wildosSound();
    if (on) {
        g.writeEntry("ShutdownSound", true, KConfig::Notify);
    } else {
        g.revertToDefault("ShutdownSound", KConfig::Notify);
    }
    g.sync();
    Q_EMIT changed();
}

void SoundTheme::preview(const QString &name)
{
    // The file from the scheme chosen here, played straight through
    // PipeWire: canberra-gtk-play would follow GTK's sound theme setting
    // rather than KDE's.
    const QString theme = sounds().readEntry("Theme", QStringLiteral("ocean"));
    const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                QStringLiteral("sounds/%1/stereo/%2.oga").arg(theme, name));
    if (file.isEmpty()) {
        return;
    }
    const QString player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty() ? QStringLiteral("pw-play")
                                                                                                  : QStringLiteral("paplay");
    QProcess::startDetached(player, {file});
}
