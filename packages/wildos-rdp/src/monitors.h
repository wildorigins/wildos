#pragma once

#include <QList>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

// One of this PC's screens as Windows is told about it. Windows lays its
// desktop out in pixels, each monitor at its own scale; the desktop here
// is laid out in logical units, so the two layouts differ wherever the
// scales do and the places have to be worked out.
struct Monitor {
    // The screen's connector name (eDP-1, DP-2), which ties it to a QScreen.
    QString name;
    // Where the screen is on this desktop, in logical units.
    QRect logical;
    double scale = 1.0;
    // Its size in its own pixels; worked out from `logical` and `scale`
    // when empty.
    QSize pixels;
    QSize millimetres;
    // Where it is on Windows' desktop, in pixels, the first monitor (the
    // primary) at 0,0. Filled in by monitors::arrange().
    QRect rect;
};

namespace monitors
{
// Windows' desktop for these screens, the primary first: every screen at
// its own size in pixels, next to the same neighbour on the same side as
// here, none overlapping. A screen with no neighbour here (a gap between
// screens) goes to the right of the rest.
QList<Monitor> arrange(QList<Monitor> screens);

// The monitor a window on Windows' desktop is on: the one under its centre,
// else the one it overlaps most, else the nearest. -1 for no monitors.
int at(const QList<Monitor> &layout, const QRect &frame);
int named(const QList<Monitor> &layout, const QString &name);
// The monitor a window on Windows' desktop covers the whole of, as a full
// screen window does; -1 when it covers none.
int covered(const QList<Monitor> &layout, const QRect &frame);

// The part of a monitor that windows maximise into, in Windows' pixels,
// for `area`, that part of its screen here in logical units (the screen
// less the taskbar). Empty when the two do not overlap.
QRect workArea(const Monitor &monitor, const QRectF &area);

// The smallest rectangle round them all.
QRect bounds(const QList<Monitor> &layout);
// Whether Windows would see any difference between the two.
bool same(const QList<Monitor> &a, const QList<Monitor> &b);

// The scale as RDP carries it: any whole percentage from 100 to 500, and the
// nearest of the three device scales.
quint32 desktopScale(double scale);
quint32 deviceScale(double scale);

// One line for the log: "eDP-1 3840x2160+0+0 at 2.25".
QString describe(const QList<Monitor> &layout);
}
