#pragma once

#include "plan.h"

#include <QColor>
#include <QList>
#include <QString>

// The effects Cooler's Lighting page offers, worked out on this PC one
// frame at a time: what colour each light of a zone takes at a moment.
// Kept apart from sockets and timers so it can be tested alone.
//
// Nothing here knows where a light sits in the PC. A zone is a run of
// lights; the effect is laid along the run from its first light to its
// last, and once round for a zone the person says is a ring.
namespace Effects
{
enum class Effect {
    Theme,
    Taskbar,
    Temperature,
    Breathe,
    Steady,
    Off,
};

Effect fromId(const QString &id, Effect fallback);
QString id(Effect effect);

enum class Shape {
    Strip,
    Ring,
    Single,
};

Shape shapeFromId(const QString &id, Shape fallback);
QString id(Shape shape);

struct Look {
    Effect effect = Effect::Taskbar;
    // Both 0 to 100.
    int brightness = 100;
    int speed = 35;
    QColor accent;
    // What the taskbar underglow is doing, for Effect::Taskbar.
    Plan::Look taskbar = Plan::Look::Colour;
    QColor taskbarColour;
    // The processor's temperature in degrees C, below zero when unknown.
    double temperature = -1;
    // On battery or with Reduce motion on: nothing moves.
    bool still = false;
    // The colours are for drawing on a screen, not for sending to a
    // light. A light at half power still looks most of the way to full,
    // and a screen shows that only if the brightness is lifted the way
    // screens encode it; drawn as sent, a dimmed light looks nearly off.
    bool forScreen = false;
};

// One run of lights the effect is laid across.
struct Run {
    int lights = 0;
    Shape shape = Shape::Strip;
};

// Whether the look changes from one moment to the next, so frames need
// sending at all.
bool moves(const Look &look);

// The three colours "Follow my theme" runs through, from the accent.
QList<QColor> themeStops(const QColor &accent);
// Cool blue at 40 degrees and below, through violet, to red at 85.
QColor temperatureColour(double celsius);
// How bright a breath is at a moment, 0 to 1.
double breath(double seconds, int speed);

// The colour of the light at `position` (0 at the run's first light, up
// to but not reaching 1) at `seconds`.
QRgb colourAt(const Look &look, Shape shape, double position, double seconds);

// Appends every light of the runs.
void fill(QList<QRgb> &out, const Look &look, const QList<Run> &runs, double seconds);

// For a part that can only do white: how bright, 0 to 1.
double whiteLevel(const Look &look, double seconds);
// Splits `lights` into `sections` runs as evenly as they go, the longer
// ones first.
QList<int> split(int lights, int sections);
}
