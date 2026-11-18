#pragma once

#include "backend.h"

#include <algorithm>

// The curves of the three built-in cooling modes, and what the options on
// the Cooling page do to a curve before it goes to the cooler.
namespace Cooler
{
// Balanced is the curve board 10.10 draws; Quiet and Performance are the
// same shape moved later and earlier.
inline Curve builtInCurve(const QString &mode, Channel channel)
{
    const bool fans = channel == Channel::Fans;
    if (mode == QLatin1String("quiet")) {
        return fans ? Curve{{20, 20}, {32, 25}, {40, 35}, {48, 55}, {55, 100}, {60, 100}}
                    : Curve{{20, 50}, {32, 55}, {40, 65}, {48, 80}, {55, 100}, {60, 100}};
    }
    if (mode == QLatin1String("performance")) {
        return fans ? Curve{{20, 35}, {28, 45}, {34, 65}, {40, 85}, {46, 100}, {60, 100}}
                    : Curve{{20, 70}, {28, 80}, {34, 90}, {40, 100}, {46, 100}, {60, 100}};
    }
    return fans ? Curve{{20, 25}, {30, 30}, {38, 45}, {46, 70}, {52, 100}, {60, 100}}
                : Curve{{20, 60}, {30, 65}, {38, 75}, {46, 90}, {52, 100}, {60, 100}};
}

// The speed a curve gives at a temperature, on the straight line between
// the points either side.
inline int speedAt(const Curve &curve, int temperature)
{
    if (curve.isEmpty()) {
        return 100;
    }
    if (temperature <= curve.first().temperature) {
        return curve.first().percent;
    }
    for (qsizetype i = 1; i < curve.size(); ++i) {
        const CurvePoint &left = curve.at(i - 1);
        const CurvePoint &right = curve.at(i);
        if (temperature <= right.temperature) {
            const int span = right.temperature - left.temperature;
            return span <= 0 ? right.percent : left.percent + (right.percent - left.percent) * (temperature - left.temperature) / span;
        }
    }
    return curve.last().percent;
}

// "Never slower than": no point of the curve is below the floor.
inline Curve withFloor(Curve curve, int floor)
{
    for (CurvePoint &point : curve) {
        point.percent = std::max(point.percent, floor);
    }
    return curve;
}

// "Stop the fans when cool": nothing below fanStopBelow, then straight onto
// the curve as it was.
inline Curve stoppedWhenCool(const Curve &curve)
{
    if (curve.isEmpty() || curve.first().temperature >= fanStopBelow) {
        return curve;
    }
    Curve stopped{{curve.first().temperature, 0}, {fanStopBelow - 1, 0}, {fanStopBelow, speedAt(curve, fanStopBelow)}};
    for (const CurvePoint &point : curve) {
        if (point.temperature > fanStopBelow) {
            stopped << point;
        }
    }
    return stopped;
}
}
