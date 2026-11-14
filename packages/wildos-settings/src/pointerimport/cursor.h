#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QString>
#include <QStringList>

// One picture of a pointer: premultiplied ARGB, its hotspot in the
// picture's own pixels, and for an animation how long it shows.
struct Frame {
    QImage image;
    QPoint hotspot;
    int delay = 0;
};

// A pointer: one frame, or the steps of an animation in order.
using Pointer = QList<Frame>;

// Windows pointer files, read as untrusted data: every length and offset is
// checked against the bytes actually there before it is used.
namespace WinCursor
{
// A .cur or .ico file: its largest, deepest picture.
bool readCur(const QByteArray &data, Frame &out);
// A .ani file (RIFF ACON): every step, with the rate and sequence applied.
bool readAni(const QByteArray &data, Pointer &out);
// Either of the two, told apart by the first bytes.
bool read(const QByteArray &data, Pointer &out);

// The roles of a Windows pointer scheme, in the order the scheme's registry
// value lists them (Control Panel\Cursors\Schemes).
enum Role {
    Arrow,
    Help,
    AppStarting,
    Wait,
    Crosshair,
    IBeam,
    NWPen,
    No,
    SizeNS,
    SizeWE,
    SizeNWSE,
    SizeNESW,
    SizeAll,
    UpArrow,
    Hand,
    RoleCount
};

// The file name install.inf gives each role, without any folder. Roles it
// does not name are missing from the map.
QMap<int, QString> readInf(const QByteArray &data);
}

namespace Xcursor
{
// The Xcursor file for one pointer at the given nominal sizes, each frame
// scaled so its longer side is fill times the nominal size.
QByteArray write(const Pointer &pointer, const QList<int> &sizes, qreal fill);

// Reads an Xcursor file strictly and writes the same pictures back in a
// fresh file, so nothing but well-formed pictures passes through.
bool rewrite(const QByteArray &data, QByteArray &out);
}
