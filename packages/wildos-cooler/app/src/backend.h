#pragma once

#include <QList>
#include <QObject>
#include <QString>

// What the Cooler window needs from the cooler. The window is written
// against this and nothing else, so the cooler service's D-Bus interface can
// be written to match it member for member.
//
// Getters report what the cooler is doing now. The window keeps the user's
// own choices in ~/.config/wildoscoolerrc and hands each one to the setter
// as it changes, and the choices of earlier runs once at start-up; a getter
// is what it shows for anything the user has not chosen yet. Setters return
// at once and never block on the device.
//
// Temperatures are degrees Celsius, speeds are per cent of full speed.

namespace Cooler
{
// The liquid temperature below which "Stop the fans when cool" stops them.
inline constexpr int fanStopBelow = 28;

struct Readings {
    // The processor's temperature, and the highest it has been since midnight.
    // Negative when this PC has no sensor Cooler can read.
    double cpuTemperature = 0;
    double cpuHighestToday = 0;
    // The liquid's temperature, from the sensor in the pump. Negative until
    // the cooler has been read; the pump and fan figures mean nothing then.
    double liquidTemperature = 0;
    // An estimate of the room's temperature (the liquid at rest), or a
    // negative number when there is none to give.
    double roomTemperature = -1;
    // The pump's speed in turns a minute, and the per cent it is driven at.
    int pumpRpm = 0;
    int pumpPercent = 0;
    // The fans' speed and per cent, for fans plugged into the cooler; both 0
    // when none are.
    int fanRpm = 0;
    int fanPercent = 0;
    // How busy the processor is, 0 to 100.
    double cpuLoad = 0;
    // The graphics card's temperature, or a negative number without one.
    double gpuTemperature = -1;
    // How busy each core is, and the last minute of the two temperatures, a
    // value a second, oldest first: what the Cores, Last minute and Both
    // faces draw. Empty where the temperature is not known.
    QList<double> coreLoads;
    QList<double> cpuHistory;
    QList<double> gpuHistory;

    bool operator==(const Readings &) const = default;
};

// One point of a speed curve: at this liquid temperature, run at this speed.
struct CurvePoint {
    int temperature = 0;
    int percent = 0;

    bool operator==(const CurvePoint &) const = default;
};
// Sorted by temperature, speeds never falling. The first and last points
// are the ends of the chart (20 and 60 degrees).
using Curve = QList<CurvePoint>;

enum class Channel { Fans, Pump };

class Backend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // A cooler Cooler can drive is connected and answering. Everything
    // below is only meaningful while this is true.
    virtual bool present() const = 0;

    // The cooler's name as the window shows it, such as "NZXT Kraken Z".
    virtual QString model() const = 0;
    // The round screen's width and height in pixels: 240, 320 or 640.
    virtual int screenSize() const = 0;
    // The firmware's version, such as "2.1.0"; empty when it cannot be read.
    virtual QString firmware() const = 0;
    // How many fans are plugged into the cooler itself, so it can read and
    // drive them. 0 when the fans are on the motherboard.
    virtual int fanCount() const = 0;
    // The cooler has lights of its own (a ring around the screen).
    virtual bool hasLighting() const = 0;

    // The newest readings; readingsChanged() follows each new set.
    virtual Readings readings() const = 0;

    // The face on the screen, by its id in WildOS.Cooler.Faces: liquid,
    // clock, last-minute, both, cores, warp, tunnel, reactor, hive, matrix,
    // neon or rgb-ring. "limit", the red warning, is never set from here:
    // the program that draws the faces shows it by itself.
    virtual QString face() const = 0;
    virtual void setFace(const QString &face) = 0;

    // The screen's brightness, 0 to 100.
    virtual int brightness() const = 0;
    virtual void setBrightness(int percent) = 0;

    // How far the picture is turned, clockwise: 0, 90, 180 or 270.
    virtual int rotation() const = 0;
    virtual void setRotation(int degrees) = 0;

    // The small Wild OS W is drawn on the face.
    virtual bool showMark() const = 0;
    virtual void setShowMark(bool show) = 0;

    // Faces take their colours from the Wild OS theme instead of their own.
    virtual bool themeColours() const = 0;
    virtual void setThemeColours(bool use) = 0;

    // The screen dims once the PC has been idle for ten minutes.
    virtual bool dimWhenIdle() const = 0;
    virtual void setDimWhenIdle(bool dim) = 0;

    // The red warning face takes over while the liquid is above this
    // temperature.
    virtual int warnLiquidAbove() const = 0;
    virtual void setWarnLiquidAbove(int temperature) = 0;

    // The red warning face takes over once the processor has been held at
    // its limit for this many seconds.
    virtual int warnCpuLimitSeconds() const = 0;
    virtual void setWarnCpuLimitSeconds(int seconds) = 0;

    // The cooling mode: quiet, balanced, performance or custom ("Your own").
    virtual QString mode() const = 0;
    virtual void setMode(const QString &mode) = 0;

    // The curve a mode runs a channel at. For custom it is the user's own.
    virtual Curve curve(const QString &mode, Channel channel) const = 0;
    // Replaces the user's own curve, the one mode custom runs.
    virtual void setCustomCurve(Channel channel, const Curve &curve) = 0;

    // The fans stop while the liquid is below fanStopBelow.
    virtual bool stopFansWhenCool() const = 0;
    virtual void setStopFansWhenCool(bool stop) = 0;

    // The pump never runs slower than this, whatever its curve says.
    virtual int pumpMinimum() const = 0;
    virtual void setPumpMinimum(int percent) = 0;

    // Fans and pump run at full speed while a game is running.
    virtual bool fullSpeedInGames() const = 0;
    virtual void setFullSpeedInGames(bool full) = 0;

    // "Look again" and "Check again": search for a cooler afresh.
    // presentChanged() or deviceChanged() follows if the answer differs.
    virtual void lookAgain() = 0;

    // "Put everything back": the face, speeds and lighting return to how
    // Cooler first set them. settingsChanged() follows.
    virtual void putEverythingBack() = 0;

    // "Save a report": what the cooler says about itself, as plain text for
    // a problem report (model, firmware, readings, raw status). The window
    // adds the user's settings and writes the file.
    virtual QString report() const = 0;

Q_SIGNALS:
    void presentChanged();
    // The model, screen size, firmware, fans or lighting changed.
    void deviceChanged();
    void readingsChanged();
    // A setting changed on the cooler without the window asking for it.
    void settingsChanged();
};
}
