#pragma once

#include <QImage>
#include <QString>

// What "Straighten and crop each page" and "Black and white" do to a
// scanned page, on the scan's own thread.
namespace ScanProcess
{
// Turns the page level by the lines of its text (or its own edges, on a
// dark lid), then trims the dark bands left round it. `note` says what
// was done, for the line above the page: "straightened, edges trimmed".
QImage straightenAndCrop(const QImage &page, QString *note);
// Ink and paper only, split where the page's own histogram parts them
// (Otsu), for scanners without a mode of their own for it.
QImage blackAndWhite(const QImage &page);
// The angle, in degrees, the page's lines lean by; 0 when there is too
// little to go on.
double skewAngle(const QImage &grey);
}
