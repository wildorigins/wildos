#include "monitors.h"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
enum class Side { None, Right, Left, Below, Above };

bool overlapsVertically(const QRect &a, const QRect &b)
{
    return a.top() <= b.bottom() && b.top() <= a.bottom();
}

bool overlapsHorizontally(const QRect &a, const QRect &b)
{
    return a.left() <= b.right() && b.left() <= a.right();
}

// Which side of `placed` `screen` touches, on this desktop.
Side sideOf(const QRect &placed, const QRect &screen)
{
    if (screen.left() == placed.right() + 1 && overlapsVertically(placed, screen)) {
        return Side::Right;
    }
    if (screen.right() + 1 == placed.left() && overlapsVertically(placed, screen)) {
        return Side::Left;
    }
    if (screen.top() == placed.bottom() + 1 && overlapsHorizontally(placed, screen)) {
        return Side::Below;
    }
    if (screen.bottom() + 1 == placed.top() && overlapsHorizontally(placed, screen)) {
        return Side::Above;
    }
    return Side::None;
}

qint64 area(const QRect &r)
{
    return r.isEmpty() ? 0 : qint64(r.width()) * r.height();
}
}

namespace monitors
{
QList<Monitor> arrange(QList<Monitor> screens)
{
    for (Monitor &m : screens) {
        if (m.scale <= 0) {
            m.scale = 1.0;
        }
        if (!m.pixels.isValid() || m.pixels.isEmpty()) {
            m.pixels = QSize(int(std::lround(m.logical.width() * m.scale)), int(std::lround(m.logical.height() * m.scale)));
        }
        // Windows' display control takes only even widths; a layout it
        // would change is one the windows here would be out of step with.
        m.pixels.setWidth(std::max(200, m.pixels.width() & ~1));
        m.pixels.setHeight(std::max(200, m.pixels.height()));
        m.rect = QRect();
    }
    if (screens.isEmpty()) {
        return screens;
    }
    QList<bool> placed(screens.size(), false);
    screens[0].rect = QRect(QPoint(0, 0), screens[0].pixels);
    placed[0] = true;

    auto fits = [&](const QRect &candidate) {
        for (int i = 0; i < screens.size(); ++i) {
            if (placed[i] && screens[i].rect.intersects(candidate)) {
                return false;
            }
        }
        return true;
    };

    // Each screen goes beside a neighbour already placed. The offset along
    // the shared edge is measured in the neighbour's pixels, as that is the
    // screen the edge belongs to.
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < screens.size(); ++i) {
            if (placed[i]) {
                continue;
            }
            for (int j = 0; j < screens.size() && !placed[i]; ++j) {
                if (!placed[j]) {
                    continue;
                }
                const Monitor &n = screens[j];
                const QRect &a = n.logical;
                const QRect &b = screens[i].logical;
                const QSize size = screens[i].pixels;
                const int alongX = n.rect.left() + int(std::lround((b.left() - a.left()) * n.scale));
                const int alongY = n.rect.top() + int(std::lround((b.top() - a.top()) * n.scale));
                QRect candidate;
                switch (sideOf(a, b)) {
                case Side::Right: candidate = QRect(QPoint(n.rect.right() + 1, alongY), size); break;
                case Side::Left: candidate = QRect(QPoint(n.rect.left() - size.width(), alongY), size); break;
                case Side::Below: candidate = QRect(QPoint(alongX, n.rect.bottom() + 1), size); break;
                case Side::Above: candidate = QRect(QPoint(alongX, n.rect.top() - size.height()), size); break;
                case Side::None: break;
                }
                if (candidate.isValid() && fits(candidate)) {
                    screens[i].rect = candidate;
                    placed[i] = true;
                    progress = true;
                }
            }
        }
    }

    // What is left touches nothing here, or would overlap: it goes in a row
    // to the right, in the order it is in here from left to right.
    QList<int> left;
    for (int i = 0; i < screens.size(); ++i) {
        if (!placed[i]) {
            left.append(i);
        }
    }
    std::stable_sort(left.begin(), left.end(), [&](int x, int y) { return screens[x].logical.left() < screens[y].logical.left(); });
    for (int i : left) {
        QRect all;
        for (int j = 0; j < screens.size(); ++j) {
            if (placed[j]) {
                all |= screens[j].rect;
            }
        }
        screens[i].rect = QRect(QPoint(all.right() + 1, 0), screens[i].pixels);
        placed[i] = true;
    }
    return screens;
}

int at(const QList<Monitor> &layout, const QRect &frame)
{
    if (layout.isEmpty()) {
        return -1;
    }
    const QPoint centre = frame.center();
    for (int i = 0; i < layout.size(); ++i) {
        if (layout[i].rect.contains(centre)) {
            return i;
        }
    }
    int best = -1;
    qint64 most = 0;
    for (int i = 0; i < layout.size(); ++i) {
        const qint64 shared = area(layout[i].rect.intersected(frame));
        if (shared > most) {
            most = shared;
            best = i;
        }
    }
    if (best >= 0) {
        return best;
    }
    qint64 nearest = std::numeric_limits<qint64>::max();
    for (int i = 0; i < layout.size(); ++i) {
        const QPoint d = layout[i].rect.center() - centre;
        const qint64 distance = qint64(d.x()) * d.x() + qint64(d.y()) * d.y();
        if (distance < nearest) {
            nearest = distance;
            best = i;
        }
    }
    return best;
}

int named(const QList<Monitor> &layout, const QString &name)
{
    for (int i = 0; i < layout.size(); ++i) {
        if (layout[i].name == name) {
            return i;
        }
    }
    return -1;
}

int covered(const QList<Monitor> &layout, const QRect &frame)
{
    const int under = at(layout, frame);
    if (under >= 0 && !layout[under].rect.isEmpty() && frame.contains(layout[under].rect)) {
        return under;
    }
    for (int i = 0; i < layout.size(); ++i) {
        if (!layout[i].rect.isEmpty() && frame.contains(layout[i].rect)) {
            return i;
        }
    }
    return -1;
}

QRect workArea(const Monitor &monitor, const QRectF &area)
{
    const QRectF screen(monitor.logical);
    const QRectF part = area.intersected(screen);
    if (part.isEmpty() || monitor.rect.isEmpty()) {
        return {};
    }
    // By the monitor's own pixels per unit rather than its scale, so the
    // whole screen comes out as exactly the whole monitor: its size in
    // pixels is rounded.
    auto x = [&](double at) {
        return monitor.rect.left() + std::clamp(int(std::lround((at - screen.left()) * monitor.rect.width() / screen.width())), 0, monitor.rect.width());
    };
    auto y = [&](double at) {
        return monitor.rect.top() + std::clamp(int(std::lround((at - screen.top()) * monitor.rect.height() / screen.height())), 0, monitor.rect.height());
    };
    const int left = x(part.left());
    const int top = y(part.top());
    return QRect(left, top, x(part.right()) - left, y(part.bottom()) - top);
}

QRect bounds(const QList<Monitor> &layout)
{
    QRect all;
    for (const Monitor &m : layout) {
        all |= m.rect;
    }
    return all;
}

bool same(const QList<Monitor> &a, const QList<Monitor> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].rect != b[i].rect || desktopScale(a[i].scale) != desktopScale(b[i].scale)) {
            return false;
        }
    }
    return true;
}

quint32 desktopScale(double scale)
{
    return quint32(qBound(100.0, scale * 100.0 + 0.5, 500.0));
}

quint32 deviceScale(double scale)
{
    return scale >= 1.6 ? 180 : scale >= 1.2 ? 140 : 100;
}

QString describe(const QList<Monitor> &layout)
{
    QStringList parts;
    for (const Monitor &m : layout) {
        parts.append(QStringLiteral("%1 %2x%3%4%5%6%7 at %8")
                         .arg(m.name.isEmpty() ? QStringLiteral("?") : m.name)
                         .arg(m.rect.width())
                         .arg(m.rect.height())
                         .arg(m.rect.x() < 0 ? QString() : QStringLiteral("+"))
                         .arg(m.rect.x())
                         .arg(m.rect.y() < 0 ? QString() : QStringLiteral("+"))
                         .arg(m.rect.y())
                         .arg(m.scale, 0, 'g', 3));
    }
    return parts.join(QStringLiteral(", "));
}
}
