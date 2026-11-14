#include "scanprocess.h"

#include <QColor>
#include <QList>
#include <QStringList>
#include <QPainter>
#include <QPoint>
#include <QTransform>

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <random>

namespace
{
int otsu(const QImage &grey)
{
    std::array<qint64, 256> hist{};
    for (int y = 0; y < grey.height(); ++y) {
        const uchar *line = grey.constScanLine(y);
        for (int x = 0; x < grey.width(); ++x) {
            ++hist[line[x]];
        }
    }
    const qint64 total = qint64(grey.width()) * grey.height();
    double sum = 0;
    for (int i = 0; i < 256; ++i) {
        sum += double(i) * hist[i];
    }
    double sumB = 0, best = -1;
    qint64 wB = 0;
    int threshold = 128;
    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0) {
            continue;
        }
        const qint64 wF = total - wB;
        if (wF == 0) {
            break;
        }
        sumB += double(t) * hist[t];
        const double mB = sumB / wB;
        const double mF = (sum - sumB) / wF;
        const double between = double(wB) * double(wF) * (mB - mF) * (mB - mF);
        if (between > best) {
            best = between;
            threshold = t;
        }
    }
    return threshold;
}

QImage greyOf(const QImage &image)
{
    return image.format() == QImage::Format_Grayscale8 ? image : image.convertToFormat(QImage::Format_Grayscale8);
}

// Lines of text, at the angle they really run at, stack their ink into a
// few rows with clean gaps between, so the row counts change sharply from
// one row to the next. That sharpness is the score.
double score(const QList<QPoint> &points, double degrees, int height, int width)
{
    const double a = degrees * M_PI / 180.0;
    const double s = std::sin(a), c = std::cos(a);
    const int offset = width + 2;
    std::vector<int> rows(size_t(height + 2 * offset), 0);
    for (const QPoint &p : points) {
        const int r = int(std::lround(p.y() * c - p.x() * s)) + offset;
        if (r >= 0 && r < int(rows.size())) {
            ++rows[size_t(r)];
        }
    }
    double total = 0;
    for (size_t i = 1; i < rows.size(); ++i) {
        const double d = rows[i] - rows[i - 1];
        total += d * d;
    }
    return total;
}

int meanOf(const QImage &grey, const QRect &area)
{
    qint64 sum = 0, n = 0;
    const QRect r = area.intersected(grey.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const uchar *line = grey.constScanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            sum += line[x];
            ++n;
        }
    }
    return n ? int(sum / n) : 255;
}
}

double ScanProcess::skewAngle(const QImage &image)
{
    const QImage grey = greyOf(image);
    const int threshold = otsu(grey);
    // Strokes, not areas: a dark pixel counts only with paper a few pixels
    // to one side, so a dark lid round a crooked page adds its edges,
    // which lean with the page, and not its bulk, which leans nowhere.
    QList<QPoint> ink;
    const int reach = 3;
    for (int y = reach; y < grey.height() - reach; ++y) {
        const uchar *line = grey.constScanLine(y);
        const uchar *above = grey.constScanLine(y - reach);
        const uchar *below = grey.constScanLine(y + reach);
        for (int x = reach; x < grey.width() - reach; ++x) {
            if (line[x] < threshold
                && (line[x - reach] >= threshold || line[x + reach] >= threshold || above[x] >= threshold || below[x] >= threshold)) {
                ink << QPoint(x, y);
            }
        }
    }
    if (ink.size() < 300) {
        return 0;
    }
    if (ink.size() > 120000) {
        std::mt19937 random(7);
        std::shuffle(ink.begin(), ink.end(), random);
        ink.resize(120000);
    }
    const double flat = score(ink, 0, grey.height(), grey.width());
    double best = 0, bestScore = flat;
    for (double a = -6; a <= 6.001; a += 0.25) {
        const double s = score(ink, a, grey.height(), grey.width());
        if (s > bestScore) {
            best = a;
            bestScore = s;
        }
    }
    const double coarse = best;
    for (double a = coarse - 0.25; a <= coarse + 0.25; a += 0.02) {
        const double s = score(ink, a, grey.height(), grey.width());
        if (s > bestScore) {
            best = a;
            bestScore = s;
        }
    }
    // Too slight a gain is noise, not a lean.
    return bestScore > flat * 1.03 ? best : 0;
}

QImage ScanProcess::straightenAndCrop(const QImage &page, QString *note)
{
    QStringList done;
    const QImage small = greyOf(page.width() > 1000 ? page.scaledToWidth(1000, Qt::SmoothTransformation) : page);
    const int ring = std::max(2, small.width() / 100);
    const int border = (meanOf(small, QRect(0, 0, small.width(), ring)) + meanOf(small, QRect(0, small.height() - ring, small.width(), ring))
                        + meanOf(small, QRect(0, 0, ring, small.height())) + meanOf(small, QRect(small.width() - ring, 0, ring, small.height())))
        / 4;
    // Corners a turn uncovers take the colour round the edge, so a white
    // lid stays white and a dark one is trimmed with the rest.
    const QColor fill(border, border, border);

    QImage result = page;
    const double angle = skewAngle(small);
    if (std::abs(angle) >= 0.1) {
        QImage turned(page.size(), QImage::Format_RGB32);
        turned.fill(fill);
        {
            QPainter painter(&turned);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            const QPointF centre(page.width() / 2.0, page.height() / 2.0);
            painter.translate(centre);
            painter.rotate(-angle);
            painter.translate(-centre);
            painter.drawImage(0, 0, page);
        }
        result = page.format() == QImage::Format_Grayscale8 ? turned.convertToFormat(QImage::Format_Grayscale8) : turned.convertToFormat(QImage::Format_RGB888);
        done << QStringLiteral("straightened");
    }

    // Dark bands round the paper: the lid's shadow, the glass's edge, or
    // the corners a turned page leaves. A row or column that is mostly
    // darker than the paper is trimmed, from the outside in.
    const QImage look = greyOf(result.width() > 1000 ? result.scaledToWidth(1000, Qt::SmoothTransformation) : result);
    const int paper = meanOf(look, QRect(look.width() / 4, look.height() / 4, look.width() / 2, look.height() / 2));
    const int dark = std::min((border + paper) / 2, paper * 6 / 10);
    auto darkRow = [&look, dark](int y) {
        const uchar *line = look.constScanLine(y);
        int n = 0;
        for (int x = 0; x < look.width(); ++x) {
            n += line[x] < dark;
        }
        return n * 2 > look.width();
    };
    auto darkColumn = [&look, dark](int x, int top, int bottom) {
        int n = 0;
        for (int y = top; y <= bottom; ++y) {
            n += look.constScanLine(y)[x] < dark;
        }
        return n * 2 > bottom - top + 1;
    };
    int top = 0, bottom = look.height() - 1;
    while (top < look.height() / 4 && darkRow(top)) {
        ++top;
    }
    while (bottom > look.height() * 3 / 4 && darkRow(bottom)) {
        --bottom;
    }
    int left = 0, right = look.width() - 1;
    while (left < look.width() / 4 && darkColumn(left, top, bottom)) {
        ++left;
    }
    while (right > look.width() * 3 / 4 && darkColumn(right, top, bottom)) {
        --right;
    }
    const double scale = double(result.width()) / look.width();
    const int edge = std::max(2, look.width() / 250);
    if (top > 0 || left > 0 || bottom < look.height() - 1 || right < look.width() - 1) {
        // A little more, so no grey line is left along the cut.
        const QRect keep = QRect(QPoint(int(std::ceil((left + (left > 0 ? edge : 0)) * scale)), int(std::ceil((top + (top > 0 ? edge : 0)) * scale))),
                                 QPoint(int(std::floor((right - (right < look.width() - 1 ? edge : 0)) * scale)),
                                        int(std::floor((bottom - (bottom < look.height() - 1 ? edge : 0)) * scale))))
                               .intersected(result.rect());
        if (keep.width() > result.width() / 2 && keep.height() > result.height() / 2 && keep != result.rect()) {
            result = result.copy(keep);
            done << QStringLiteral("edges trimmed");
        }
    }
    if (note) {
        *note = done.join(QStringLiteral(", "));
    }
    return result;
}

QImage ScanProcess::blackAndWhite(const QImage &page)
{
    QImage grey = greyOf(page);
    const int threshold = otsu(grey);
    for (int y = 0; y < grey.height(); ++y) {
        uchar *line = grey.scanLine(y);
        for (int x = 0; x < grey.width(); ++x) {
            line[x] = line[x] < threshold ? 0 : 255;
        }
    }
    return grey;
}
