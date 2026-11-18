#pragma once

#include "backend.h"
#include "coolerclient.h"
#include "sensors.h"

#include <QTimer>

// The real cooler: wildos-coolerd over D-Bus for the cooler itself, and
// this PC's own sensors for the processor and graphics card.
//
// The service knows the cooler's brightness, rotation and the two speed
// curves it runs; it knows nothing of modes or options. So the Cooling
// page's choices become curves here: the mode picks the curve, "never
// slower than" and "stop the fans when cool" reshape it, and the result is
// what the service is given and puts back at start-up.
//
// The face and its options are not sent anywhere: wildos-cooler-faces, the
// program that draws the cooler's screen, reads them from wildoscoolerrc as
// the window writes them. It also does "full speed in games", because only
// a program in the person's session can see a game start.
namespace Cooler
{
class ServiceBackend : public Backend
{
    Q_OBJECT

public:
    explicit ServiceBackend(QObject *parent = nullptr);

    bool present() const override { return m_client.device().present; }
    QString model() const override;
    int screenSize() const override { return m_client.device().screen; }
    QString firmware() const override { return m_client.device().firmware; }
    // The cooler has one fan connector and says only whether anything on
    // it is turning, not how many fans share it.
    int fanCount() const override { return m_client.device().fansOnCooler ? 1 : 0; }
    // The service drives no lights, so none are offered.
    bool hasLighting() const override { return false; }
    Readings readings() const override;

    QString face() const override;
    void setFace(const QString &) override {}
    int brightness() const override;
    void setBrightness(int percent) override;
    int rotation() const override;
    void setRotation(int degrees) override;
    bool showMark() const override;
    void setShowMark(bool) override {}
    bool themeColours() const override;
    void setThemeColours(bool) override {}
    bool dimWhenIdle() const override;
    void setDimWhenIdle(bool) override {}
    int warnLiquidAbove() const override;
    void setWarnLiquidAbove(int) override {}
    int warnCpuLimitSeconds() const override;
    void setWarnCpuLimitSeconds(int) override {}

    QString mode() const override { return m_mode; }
    void setMode(const QString &mode) override;
    Curve curve(const QString &mode, Channel channel) const override;
    void setCustomCurve(Channel channel, const Curve &curve) override;
    bool stopFansWhenCool() const override { return m_stopFansWhenCool; }
    void setStopFansWhenCool(bool stop) override;
    int pumpMinimum() const override { return m_pumpMinimum; }
    void setPumpMinimum(int percent) override;
    bool fullSpeedInGames() const override;
    void setFullSpeedInGames(bool) override {}

    void lookAgain() override { m_client.lookAgain(); }
    void putEverythingBack() override;
    QString report() const override;

private:
    void defaults();
    void coolerChanged();
    void send();

    Client m_client;
    Sensors m_sensors;
    bool m_wasPresent = false;
    // Dragging a slider or a point of a curve makes many changes a second;
    // the cooler is told once they pause.
    QTimer m_sendSoon;

    // What the window asked for, or -1 for whatever the cooler has.
    int m_brightness = -1;
    int m_rotation = -1;
    // The window has said which mode and options hold, so the curves that
    // follow from them may be sent.
    bool m_cooling = false;
    QString m_mode;
    Curve m_customFans;
    Curve m_customPump;
    bool m_stopFansWhenCool = false;
    int m_pumpMinimum = 0;
};
}
