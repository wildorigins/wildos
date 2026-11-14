#pragma once

#include <KConfigWatcher>

#include <QObject>
#include <QQmlEngine>

// The taskbar style chosen in Settings > Personalisation: docked along the
// bottom of the screen (the default, as Windows does it) or floating glass
// panels. Settings writes wildosrc [Taskbar] Style; the panel follows the
// change straight away. The same group holds the show desktop sliver at the
// bar's right end (ShowDesktop) and whether hovering it peeks (PeekDesktop),
// the clock and battery choices (ClockSeconds, ClockLongDate,
// BatteryPercent), and whether an app's taskbar menu offers End task
// (EndTask, off by default as on Windows 11, where it sits in the developer
// settings: ending an app loses whatever it had unsaved).
class TaskbarStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool floating READ floating NOTIFY floatingChanged)
    Q_PROPERTY(bool showDesktop READ showDesktop NOTIFY showDesktopChanged)
    Q_PROPERTY(bool peek READ peek NOTIFY showDesktopChanged)
    Q_PROPERTY(bool clockSeconds READ clockSeconds NOTIFY itemsChanged)
    Q_PROPERTY(bool clockLongDate READ clockLongDate NOTIFY itemsChanged)
    Q_PROPERTY(bool batteryPercent READ batteryPercent NOTIFY itemsChanged)
    Q_PROPERTY(bool endTask READ endTask NOTIFY endTaskChanged)

public:
    explicit TaskbarStyle(QObject *parent = nullptr);

    bool floating() const { return m_floating; }
    bool showDesktop() const { return m_showDesktop; }
    bool peek() const { return m_peek; }
    bool clockSeconds() const { return m_clockSeconds; }
    bool clockLongDate() const { return m_clockLongDate; }
    bool batteryPercent() const { return m_batteryPercent; }
    bool endTask() const { return m_endTask; }

Q_SIGNALS:
    void floatingChanged();
    void showDesktopChanged();
    void itemsChanged();
    void endTaskChanged();

private:
    void read();

    KConfigWatcher::Ptr m_watcher;
    bool m_floating = false;
    bool m_showDesktop = true;
    bool m_peek = false;
    bool m_clockSeconds = false;
    bool m_clockLongDate = false;
    bool m_batteryPercent = false;
    bool m_endTask = false;
};
