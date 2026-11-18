#include "effects.h"

#include <cmath>

namespace Effects
{
namespace
{
struct Name {
    Effect effect;
    const char *id;
};
const Name names[] = {
    {Effect::Theme, "theme"},       {Effect::Taskbar, "taskbar"}, {Effect::Temperature, "temperature"},
    {Effect::Breathe, "breathe"},   {Effect::Steady, "steady"},   {Effect::Off, "off"},
};

double frac(double x)
{
    return x - std::floor(x);
}

// 0 at whole numbers, 1 half way between: there and back again.
double triangle(double x)
{
    return 1.0 - std::abs(2.0 * frac(x) - 1.0);
}

QColor mix(const QColor &a, const QColor &b, double f)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                            a.blueF() + (b.blueF() - a.blueF()) * f);
}

QColor along(const QList<QColor> &stops, double t)
{
    const double scaled = qBound(0.0, t, 1.0) * (stops.size() - 1);
    const int i = qBound(0, int(scaled), int(stops.size()) - 2);
    return mix(stops[i], stops[i + 1], scaled - i);
}

QRgb scaled(const QColor &c, double level)
{
    const double l = qBound(0.0, level, 1.0);
    return qRgb(qRound(c.red() * l), qRound(c.green() * l), qRound(c.blue() * l));
}

double shown(const Look &look, double level)
{
    return look.forScreen ? std::pow(qBound(0.0, level, 1.0), 1.0 / 2.2) : level;
}

// How far the colours have travelled: 0 to 1 is once through. The fastest
// is ten times the slowest; at 0 they stand still.
double travel(double seconds, int speed)
{
    if (speed <= 0) {
        return 0;
    }
    const double period = 30.0 * std::pow(0.1, qBound(0, speed, 100) / 100.0);
    return seconds / period;
}
}

Effect fromId(const QString &id, Effect fallback)
{
    for (const Name &n : names) {
        if (id == QLatin1String(n.id)) {
            return n.effect;
        }
    }
    return fallback;
}

QString id(Effect effect)
{
    for (const Name &n : names) {
        if (n.effect == effect) {
            return QString::fromLatin1(n.id);
        }
    }
    return QString();
}

Shape shapeFromId(const QString &id, Shape fallback)
{
    if (id == QLatin1String("strip")) {
        return Shape::Strip;
    }
    if (id == QLatin1String("ring")) {
        return Shape::Ring;
    }
    if (id == QLatin1String("single")) {
        return Shape::Single;
    }
    return fallback;
}

QString id(Shape shape)
{
    return shape == Shape::Ring ? QStringLiteral("ring") : shape == Shape::Single ? QStringLiteral("single") : QStringLiteral("strip");
}

bool moves(const Look &look)
{
    if (look.still) {
        return false;
    }
    switch (look.effect) {
    case Effect::Theme:
        return look.speed > 0;
    case Effect::Breathe:
        return true;
    case Effect::Taskbar:
        return look.taskbar == Plan::Look::Breathing || (look.taskbar == Plan::Look::Spectrum && look.speed > 0);
    default:
        return false;
    }
}

QList<QColor> themeStops(const QColor &accent)
{
    // Board 10.11 draws Dawn's glacier accent running through violet to
    // rose: the hue turned by 29 and then 131 degrees, a little less
    // saturated. Other accents take the same turns.
    float h = 0;
    float s = 0;
    float l = 0;
    accent.getHslF(&h, &s, &l);
    if (h < 0) {
        return {accent, accent, accent};
    }
    const auto turned = [&](double degrees, double saturation, double lightness) {
        return QColor::fromHslF(float(frac(h + degrees / 360.0)), float(qBound(0.0, s * saturation, 1.0)), float(qBound(0.0, l * lightness, 1.0)));
    };
    return {accent, turned(29, 0.76, 0.96), turned(131, 0.72, 1.01)};
}

QColor temperatureColour(double celsius)
{
    static const QList<QColor> stops = {QColor(0x5f, 0xa8, 0xff), QColor(0xb5, 0x8c, 0xff), QColor(0xff, 0x6b, 0x6b)};
    return along(stops, (celsius - 40.0) / 45.0);
}

double breath(double seconds, int speed)
{
    const double period = 8.0 * std::pow(0.1875, qBound(0, speed, 100) / 100.0);
    // Never quite dark, so a breathing light is not mistaken for one
    // that is off.
    return 0.08 + 0.92 * (0.5 - 0.5 * std::cos(2.0 * M_PI * seconds / period));
}

QRgb colourAt(const Look &look, Shape shape, double position, double seconds)
{
    const double level = shown(look, qBound(0, look.brightness, 100) / 100.0);
    const double t = look.still ? 0.0 : seconds;
    const double p = shape == Shape::Single ? 0.0 : position;
    const auto breathing = [&] {
        return shown(look, qBound(0, look.brightness, 100) / 100.0 * breath(t, look.speed));
    };

    switch (look.effect) {
    case Effect::Off:
        return qRgb(0, 0, 0);
    case Effect::Steady:
        return scaled(look.accent, level);
    case Effect::Breathe:
        return scaled(look.accent, look.still ? level : breathing());
    case Effect::Temperature:
        return scaled(look.temperature < 0 ? look.accent : temperatureColour(look.temperature), level);
    case Effect::Theme: {
        // A strip shows the colours once from end to end and slides them
        // back and forth; a ring shows them there and back so its last
        // light meets its first.
        const double moved = travel(t, look.speed);
        const double u = shape == Shape::Ring ? triangle(p + moved) : triangle(p * 0.5 + moved);
        return scaled(along(themeStops(look.accent), u), level);
    }
    case Effect::Taskbar:
        switch (look.taskbar) {
        case Plan::Look::Colour:
            return scaled(look.taskbarColour, level);
        case Plan::Look::Breathing:
            return scaled(look.taskbarColour, look.still ? level : breathing());
        case Plan::Look::Spectrum:
            return scaled(along(Plan::spectrum(), frac(p + travel(t, look.speed))), level);
        case Plan::Look::FrozenSpectrum:
            return scaled(along(Plan::spectrum(), frac(p)), level);
        }
    }
    return qRgb(0, 0, 0);
}

void fill(QList<QRgb> &out, const Look &look, const QList<Run> &runs, double seconds)
{
    for (const Run &run : runs) {
        for (int i = 0; i < run.lights; ++i) {
            out.append(colourAt(look, run.shape, double(i) / run.lights, seconds));
        }
    }
}

double whiteLevel(const Look &look, double seconds)
{
    const double level = qBound(0, look.brightness, 100) / 100.0;
    if (look.effect == Effect::Off) {
        return 0;
    }
    const bool breathing = look.effect == Effect::Breathe || (look.effect == Effect::Taskbar && look.taskbar == Plan::Look::Breathing);
    return shown(look, breathing && !look.still ? level * breath(seconds, look.speed) : level);
}

QList<int> split(int lights, int sections)
{
    const int n = qBound(1, sections, qMax(1, lights));
    QList<int> out;
    for (int i = 0; i < n; ++i) {
        out.append(lights / n + (i < lights % n ? 1 : 0));
    }
    return out;
}
}
