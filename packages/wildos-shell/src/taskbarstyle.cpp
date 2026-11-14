#include "taskbarstyle.h"

#include <KConfigGroup>
#include <KSharedConfig>

TaskbarStyle::TaskbarStyle(QObject *parent)
    : QObject(parent)
    , m_watcher(KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("wildosrc"))))
{
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("Taskbar")) {
            read();
        }
    });
    read();
}

void TaskbarStyle::read()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    config->reparseConfiguration();
    const KConfigGroup taskbar = config->group(QStringLiteral("Taskbar"));
    const bool floating = taskbar.readEntry("Style") == QLatin1String("floating");
    if (floating != m_floating) {
        m_floating = floating;
        Q_EMIT floatingChanged();
    }
    const bool showDesktop = taskbar.readEntry("ShowDesktop", true);
    const bool peek = taskbar.readEntry("PeekDesktop", false);
    if (showDesktop != m_showDesktop || peek != m_peek) {
        m_showDesktop = showDesktop;
        m_peek = peek;
        Q_EMIT showDesktopChanged();
    }
    const bool seconds = taskbar.readEntry("ClockSeconds", false);
    const bool longDate = taskbar.readEntry("ClockLongDate", false);
    const bool battery = taskbar.readEntry("BatteryPercent", false);
    if (seconds != m_clockSeconds || longDate != m_clockLongDate || battery != m_batteryPercent) {
        m_clockSeconds = seconds;
        m_clockLongDate = longDate;
        m_batteryPercent = battery;
        Q_EMIT itemsChanged();
    }
    const bool endTask = taskbar.readEntry("EndTask", false);
    if (endTask != m_endTask) {
        m_endTask = endTask;
        Q_EMIT endTaskChanged();
    }
}
