#pragma once

#include "lights.h"

#include <KConfigWatcher>

#include <QDBusObjectPath>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include <functional>

// Keeps the keyboard and mouse lighting in step with the taskbar underglow.
//
// OpenRGB's server (wildos-openrgb.service) runs only while Settings has
// the lighting on (wildosrc [Lighting] Rgb) and this session is the one in
// front: on a switch to another account it stops and lets go of the
// devices, so this account keeps no hold on them meanwhile. The colours are
// set through openrgb's command line, which joins the running server as a
// client instead of opening the devices itself.
//
// Once something has been chosen on Cooler's Lighting page, the colours
// come from Lights instead (lights.h), which keeps the server's hours and
// the underglow's colours from here.
class Lighting : public QObject
{
    Q_OBJECT

public:
    explicit Lighting(QObject *parent = nullptr);

private Q_SLOTS:
    void upowerChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void sessionChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void prepareForSleep(bool sleeping);

private:
    void readSettings();
    void readBattery();
    void findSession();
    void readSessionActive();
    void sync();
    void setServer(bool on);
    void waitForServer();
    void apply();
    void tellLights();
    void run(const QStringList &arguments, std::function<void(const QString &)> done);

    KConfigWatcher::Ptr m_wildos;
    KConfigWatcher::Ptr m_globals;
    QTimer m_sync;
    QTimer m_retry;

    bool m_rgb = false;
    QString m_colour;
    QString m_custom;
    QString m_effect;
    QString m_accent;
    bool m_reducedMotion = false;
    bool m_onBattery = false;
    bool m_sessionActive = true;
    QDBusObjectPath m_session;

    bool m_serverOn = false;
    bool m_serverKnown = false;
    bool m_serverReady = false;
    int m_waits = 0;
    QProcess *m_running = nullptr;
    bool m_applyAgain = false;

    Lights m_lights;
};
