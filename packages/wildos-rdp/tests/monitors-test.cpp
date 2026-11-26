#include "monitors.h"

#include <QTest>

namespace
{
Monitor screen(const char *name, QRect logical, double scale, QSize pixels = {})
{
    Monitor m;
    m.name = QString::fromLatin1(name);
    m.logical = logical;
    m.scale = scale;
    m.pixels = pixels;
    return m;
}

bool overlapping(const QList<Monitor> &layout)
{
    for (int i = 0; i < layout.size(); ++i) {
        for (int j = i + 1; j < layout.size(); ++j) {
            if (layout[i].rect.intersects(layout[j].rect)) {
                return true;
            }
        }
    }
    return false;
}
}

class MonitorsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void oneScreen()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].rect, QRect(0, 0, 3840, 2160));
    }

    // A 4K laptop at 2.25 with a 1080p screen at 1.0 to its right: side by
    // side here, side by side in Windows, each at its own size.
    void laptopWithScreenRight()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160)),
                                          screen("DP-1", QRect(1707, 0, 1920, 1080), 1.0, QSize(1920, 1080))});
        QCOMPARE(l[0].rect, QRect(0, 0, 3840, 2160));
        QCOMPARE(l[1].rect, QRect(3840, 0, 1920, 1080));
        QVERIFY(!overlapping(l));
    }

    // The external screen is the primary and the laptop sits lower, to its
    // right: the offset along the edge is in the primary's pixels.
    void primaryLeftLaptopRightAndLower()
    {
        const auto l = monitors::arrange({screen("DP-1", QRect(0, 0, 1920, 1080), 1.0, QSize(1920, 1080)),
                                          screen("eDP-1", QRect(1920, 120, 1707, 960), 2.25, QSize(3840, 2160))});
        QCOMPARE(l[0].rect, QRect(0, 0, 1920, 1080));
        QCOMPARE(l[1].rect, QRect(1920, 120, 3840, 2160));
    }

    // The primary is on the right: the other screen goes left of 0,0.
    void primaryOnTheRight()
    {
        const auto l = monitors::arrange({screen("DP-1", QRect(1707, 0, 1920, 1080), 1.0, QSize(1920, 1080)),
                                          screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        QCOMPARE(l[0].rect, QRect(0, 0, 1920, 1080));
        QCOMPARE(l[1].rect, QRect(-3840, 0, 3840, 2160));
        QCOMPARE(monitors::bounds(l), QRect(-3840, 0, 5760, 2160));
    }

    void screenAbove()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160)),
                                          screen("DP-1", QRect(-100, -1080, 1920, 1080), 1.0, QSize(1920, 1080))});
        QCOMPARE(l[1].rect, QRect(-225, -1080, 1920, 1080));
        QVERIFY(!overlapping(l));
    }

    // Three in a row, the primary in the middle.
    void threeInARow()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(1920, 0, 1707, 960), 2.25, QSize(3840, 2160)),
                                          screen("DP-1", QRect(0, 0, 1920, 1080), 1.0),
                                          screen("DP-2", QRect(3627, 0, 2560, 1440), 1.0)});
        QCOMPARE(l[0].rect, QRect(0, 0, 3840, 2160));
        QCOMPARE(l[1].rect, QRect(-1920, 0, 1920, 1080));
        QCOMPARE(l[2].rect, QRect(3840, 0, 2560, 1440));
    }

    // A screen that touches none of the others goes to the right of them.
    void gap()
    {
        const auto l = monitors::arrange({screen("DP-1", QRect(0, 0, 1920, 1080), 1.0), screen("DP-2", QRect(2500, 300, 1280, 1024), 1.0)});
        QCOMPARE(l[1].rect, QRect(1920, 0, 1280, 1024));
    }

    // Without the mode, the size comes from the scale, made even.
    void pixelsWorkedOut()
    {
        const auto l = monitors::arrange({screen("A", QRect(0, 0, 1536, 864), 1.25), screen("B", QRect(1536, 0, 1707, 960), 2.25)});
        QCOMPARE(l[0].rect.size(), QSize(1920, 1080));
        QCOMPARE(l[1].rect.size(), QSize(3840, 2160));
    }

    void windowsMonitor()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160)),
                                          screen("DP-1", QRect(1707, 0, 1920, 1080), 1.0, QSize(1920, 1080))});
        QCOMPARE(monitors::at(l, QRect(100, 100, 800, 600)), 0);
        QCOMPARE(monitors::at(l, QRect(4000, 100, 800, 600)), 1);
        // Centre on neither (below the external screen): the one it
        // overlaps most.
        QCOMPARE(monitors::at(l, QRect(3900, 900, 600, 400)), 1);
        // Off every monitor: the nearest.
        QCOMPARE(monitors::at(l, QRect(7000, 100, 200, 200)), 1);
        QCOMPARE(monitors::named(l, QStringLiteral("DP-1")), 1);
        QCOMPARE(monitors::named(l, QStringLiteral("HDMI-1")), -1);
    }

    void sameLayout()
    {
        const auto a = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        auto b = monitors::arrange({screen("other", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        QVERIFY(monitors::same(a, b));
        b = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.0, QSize(3840, 2160))});
        QVERIFY(!monitors::same(a, b));
    }

    // A 52 unit taskbar along the bottom of each screen: Windows keeps the
    // same strip clear, in each monitor's own pixels.
    void workAreaLessTaskbar()
    {
        const auto l = monitors::arrange({screen("DP-1", QRect(0, 0, 1920, 1080), 1.0, QSize(1920, 1080)),
                                          screen("eDP-1", QRect(1920, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        QCOMPARE(monitors::workArea(l[0], QRectF(0, 0, 1920, 1028)), QRect(0, 0, 1920, 1028));
        QCOMPARE(monitors::workArea(l[1], QRectF(1920, 0, 1707, 908)), QRect(1920, 0, 3840, 2043));
    }

    // A full screen window is the monitor's rectangle, or a little more; a
    // window that leaves any of it uncovered is not full screen.
    void coveredMonitor()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160)),
                                          screen("DP-1", QRect(1707, 0, 1920, 1080), 1.0, QSize(1920, 1080))});
        QCOMPARE(monitors::covered(l, QRect(0, 0, 3840, 2160)), 0);
        QCOMPARE(monitors::covered(l, QRect(3840, 0, 1920, 1080)), 1);
        QCOMPARE(monitors::covered(l, QRect(3832, -8, 1936, 1096)), 1);
        QCOMPARE(monitors::covered(l, QRect(3840, 0, 1920, 1028)), -1);
        QCOMPARE(monitors::covered(l, QRect(0, 0, 1730, 1000)), -1);
    }

    void workAreaWholeScreenAndTopPanel()
    {
        const auto l = monitors::arrange({screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        QCOMPARE(monitors::workArea(l[0], QRectF(0, 0, 1707, 960)), QRect(0, 0, 3840, 2160));
        QCOMPARE(monitors::workArea(l[0], QRectF(0, 40, 1707, 920)), QRect(0, 90, 3840, 2070));
        QVERIFY(monitors::workArea(l[0], QRectF(2000, 0, 100, 100)).isEmpty());
    }

    // A screen left of the primary is at negative pixels in Windows.
    void workAreaLeftOfPrimary()
    {
        const auto l = monitors::arrange({screen("DP-1", QRect(1707, 0, 1920, 1080), 1.0, QSize(1920, 1080)),
                                          screen("eDP-1", QRect(0, 0, 1707, 960), 2.25, QSize(3840, 2160))});
        QCOMPARE(monitors::workArea(l[1], QRectF(0, 0, 1707, 908)), QRect(-3840, 0, 3840, 2043));
    }

    void rdpScales()
    {
        QCOMPARE(monitors::desktopScale(2.25), 225u);
        QCOMPARE(monitors::desktopScale(0.5), 100u);
        QCOMPARE(monitors::deviceScale(1.0), 100u);
        QCOMPARE(monitors::deviceScale(1.25), 140u);
        QCOMPARE(monitors::deviceScale(2.25), 180u);
    }
};

QTEST_APPLESS_MAIN(MonitorsTest)
#include "monitors-test.moc"
