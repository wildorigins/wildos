#pragma once

#include "backend.h"

// A cooler that is not there, for rendering the pages and for tests:
// WILDOS_COOLER_FAKE=kraken is the cooler the boards draw (three fans,
// 640 pixel screen, lighting), =krakenz a Kraken Z as it is often fitted
// (320 pixels, fans on the motherboard, no lights), =none no cooler at all.
// It answers with the boards' example values and keeps every change it is
// given until the window closes.
namespace Cooler
{
class FakeBackend : public Backend
{
    Q_OBJECT

public:
    explicit FakeBackend(const QString &kind, QObject *parent = nullptr);

    // An example curve for each built-in mode.
    static Curve builtInCurve(const QString &mode, Channel channel);

    bool present() const override { return m_present; }
    QString model() const override { return m_model; }
    int screenSize() const override { return m_screenSize; }
    QString firmware() const override { return m_firmware; }
    int fanCount() const override { return m_fanCount; }
    bool hasLighting() const override { return m_lighting; }
    Readings readings() const override { return m_readings; }

    QString face() const override { return m_face; }
    void setFace(const QString &face) override { m_face = face; }
    int brightness() const override { return m_brightness; }
    void setBrightness(int percent) override { m_brightness = percent; }
    int rotation() const override { return m_rotation; }
    void setRotation(int degrees) override { m_rotation = degrees; }
    bool showMark() const override { return m_showMark; }
    void setShowMark(bool show) override { m_showMark = show; }
    bool themeColours() const override { return m_themeColours; }
    void setThemeColours(bool use) override { m_themeColours = use; }
    bool dimWhenIdle() const override { return m_dimWhenIdle; }
    void setDimWhenIdle(bool dim) override { m_dimWhenIdle = dim; }
    int warnLiquidAbove() const override { return m_warnLiquidAbove; }
    void setWarnLiquidAbove(int temperature) override { m_warnLiquidAbove = temperature; }
    int warnCpuLimitSeconds() const override { return m_warnCpuLimitSeconds; }
    void setWarnCpuLimitSeconds(int seconds) override { m_warnCpuLimitSeconds = seconds; }

    QString mode() const override { return m_mode; }
    void setMode(const QString &mode) override { m_mode = mode; }
    Curve curve(const QString &mode, Channel channel) const override;
    void setCustomCurve(Channel channel, const Curve &curve) override;
    bool stopFansWhenCool() const override { return m_stopFansWhenCool; }
    void setStopFansWhenCool(bool stop) override { m_stopFansWhenCool = stop; }
    int pumpMinimum() const override { return m_pumpMinimum; }
    void setPumpMinimum(int percent) override { m_pumpMinimum = percent; }
    bool fullSpeedInGames() const override { return m_fullSpeedInGames; }
    void setFullSpeedInGames(bool full) override { m_fullSpeedInGames = full; }

    void lookAgain() override {}
    void putEverythingBack() override;
    QString report() const override;

private:
    void defaults();

    bool m_present = false;
    QString m_model;
    int m_screenSize = 0;
    QString m_firmware;
    int m_fanCount = 0;
    bool m_lighting = false;
    Readings m_readings;

    QString m_face;
    int m_brightness = 0;
    int m_rotation = 0;
    bool m_showMark = false;
    bool m_themeColours = false;
    bool m_dimWhenIdle = false;
    int m_warnLiquidAbove = 0;
    int m_warnCpuLimitSeconds = 0;
    QString m_mode;
    Curve m_customFans;
    Curve m_customPump;
    bool m_stopFansWhenCool = false;
    int m_pumpMinimum = 0;
    bool m_fullSpeedInGames = false;
};
}
