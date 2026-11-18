#include "effects.h"
#include "seen.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Effects;

namespace
{
const QColor glacier(0x8f, 0xb4, 0xff);

Look look(Effect effect)
{
    Look l;
    l.effect = effect;
    l.accent = glacier;
    l.taskbarColour = QColor(0x10, 0x20, 0x30);
    return l;
}

bool near(QRgb a, const QColor &b, int within = 3)
{
    return qAbs(qRed(a) - b.red()) <= within && qAbs(qGreen(a) - b.green()) <= within && qAbs(qBlue(a) - b.blue()) <= within;
}

void write(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}
}

class EffectsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void namesGoThereAndBack()
    {
        for (const Effect e : {Effect::Theme, Effect::Taskbar, Effect::Temperature, Effect::Breathe, Effect::Steady, Effect::Off}) {
            QCOMPARE(fromId(id(e), Effect::Off == e ? Effect::Theme : Effect::Off), e);
        }
        QCOMPARE(fromId(QStringLiteral("disco"), Effect::Steady), Effect::Steady);
        QCOMPARE(shapeFromId(id(Shape::Ring), Shape::Strip), Shape::Ring);
        QCOMPARE(shapeFromId(QStringLiteral("cube"), Shape::Single), Shape::Single);
    }

    void themeIsTheBoardsGradient()
    {
        const QList<QColor> stops = themeStops(glacier);
        QCOMPARE(stops[0], glacier);
        QVERIFY2(near(stops[1].rgb(), QColor(0x9d, 0x8f, 0xf0), 4), qPrintable(stops[1].name()));
        QVERIFY2(near(stops[2].rgb(), QColor(0xf0, 0xa3, 0xae), 4), qPrintable(stops[2].name()));
    }

    void aStripShowsTheThemeFromEndToEnd()
    {
        Look l = look(Effect::Theme);
        l.speed = 0;
        QList<QRgb> frame;
        fill(frame, l, {{12, Shape::Strip}}, 100.0);
        QCOMPARE(frame.size(), 12);
        QVERIFY(near(frame.first(), glacier));
        QVERIFY(near(frame[6], themeStops(glacier)[1]));
        QVERIFY(!near(frame.last(), glacier, 30));
    }

    void aRingMeetsItself()
    {
        Look l = look(Effect::Theme);
        for (const double seconds : {0.0, 1.3, 7.7}) {
            QCOMPARE(colourAt(l, Shape::Ring, 0.1, seconds), colourAt(l, Shape::Ring, 1.1, seconds));
        }
        QVERIFY(near(colourAt(l, Shape::Ring, 0.999, 0), glacier, 6));
    }

    void aSingleLightIsOneColour()
    {
        Look l = look(Effect::Theme);
        QList<QRgb> frame;
        fill(frame, l, {{6, Shape::Single}}, 3.0);
        QCOMPARE(QSet<QRgb>(frame.begin(), frame.end()).size(), 1);
    }

    void brightnessScales()
    {
        Look l = look(Effect::Steady);
        l.brightness = 50;
        QCOMPARE(colourAt(l, Shape::Strip, 0, 0), qRgb(72, 90, 128));
        l.brightness = 0;
        QCOMPARE(colourAt(l, Shape::Strip, 0, 0), qRgb(0, 0, 0));
        QCOMPARE(colourAt(look(Effect::Off), Shape::Strip, 0.5, 2), qRgb(0, 0, 0));
    }

    void onlyMovingEffectsMove()
    {
        QVERIFY(moves(look(Effect::Theme)));
        QVERIFY(moves(look(Effect::Breathe)));
        QVERIFY(!moves(look(Effect::Steady)));
        QVERIFY(!moves(look(Effect::Off)));
        QVERIFY(!moves(look(Effect::Temperature)));
        Look l = look(Effect::Theme);
        l.speed = 0;
        QVERIFY(!moves(l));
        l = look(Effect::Breathe);
        l.still = true;
        QVERIFY(!moves(l));
        QCOMPARE(colourAt(l, Shape::Strip, 0, 1.0), colourAt(l, Shape::Strip, 0, 2.5));

        l = look(Effect::Taskbar);
        QVERIFY(!moves(l));
        l.taskbar = Plan::Look::Breathing;
        QVERIFY(moves(l));
        l.taskbar = Plan::Look::Spectrum;
        QVERIFY(moves(l));
        l.taskbar = Plan::Look::FrozenSpectrum;
        QVERIFY(!moves(l));
    }

    void taskbarCopiesTheUnderglow()
    {
        Look l = look(Effect::Taskbar);
        QCOMPARE(colourAt(l, Shape::Strip, 0.4, 9), qRgb(0x10, 0x20, 0x30));
        l.taskbar = Plan::Look::FrozenSpectrum;
        QVERIFY(near(colourAt(l, Shape::Strip, 0, 9), Plan::spectrum().first()));
    }

    void breathNeverGoesDark()
    {
        double lowest = 1;
        double highest = 0;
        for (int i = 0; i < 400; ++i) {
            const double b = breath(i * 0.05, 35);
            lowest = qMin(lowest, b);
            highest = qMax(highest, b);
        }
        QVERIFY(lowest > 0.05 && lowest < 0.1);
        QVERIFY(highest > 0.99 && highest <= 1.0);
    }

    void temperatureRunsFromCoolToHot()
    {
        QCOMPARE(temperatureColour(20), QColor(0x5f, 0xa8, 0xff));
        QCOMPARE(temperatureColour(40), QColor(0x5f, 0xa8, 0xff));
        QCOMPARE(temperatureColour(85), QColor(0xff, 0x6b, 0x6b));
        QCOMPARE(temperatureColour(120), QColor(0xff, 0x6b, 0x6b));
    }

    void whiteFollowsBrightnessOnly()
    {
        Look l = look(Effect::Theme);
        l.brightness = 60;
        QCOMPARE(whiteLevel(l, 5), 0.6);
        l.effect = Effect::Off;
        QCOMPARE(whiteLevel(l, 5), 0.0);
        l.effect = Effect::Breathe;
        QVERIFY(whiteLevel(l, 0) < 0.1);
    }

    void splitsEvenly()
    {
        QCOMPARE(split(30, 3), QList<int>({10, 10, 10}));
        QCOMPARE(split(10, 3), QList<int>({4, 3, 3}));
        QCOMPARE(split(2, 5), QList<int>({1, 1}));
        QCOMPARE(split(0, 3), QList<int>({0}));
    }

    void seesAKnownGraphicsCardAndNothingElse()
    {
        QTemporaryDir root;
        const QString devices = root.path() + QStringLiteral("/bus/pci/devices/");
        const auto card = [&](const QString &slot, const char *cls, const char *device, const char *sub) {
            write(devices + slot + QStringLiteral("/class"), cls);
            write(devices + slot + QStringLiteral("/vendor"), "0x10de\n");
            write(devices + slot + QStringLiteral("/device"), device);
            write(devices + slot + QStringLiteral("/subsystem_vendor"), "0x10de\n");
            write(devices + slot + QStringLiteral("/subsystem_device"), sub);
        };
        card(QStringLiteral("0000:01:00.0"), "0x030000\n", "0x2204\n", "0x147d\n");
        // The same card's sound function, and a card from another maker.
        card(QStringLiteral("0000:01:00.1"), "0x040300\n", "0x2204\n", "0x147d\n");
        card(QStringLiteral("0000:02:00.0"), "0x030000\n", "0x2204\n", "0x3881\n");

        const QList<Seen::Part> parts = Seen::graphicsCards(root.path());
        QCOMPARE(parts.size(), 1);
        QCOMPARE(parts[0].name, QStringLiteral("GeForce RTX 3090"));
        QCOMPARE(parts[0].lights.size(), 2);
        QVERIFY(!parts[0].lights[0].white);
        QVERIFY(parts[0].lights[1].white);
        QCOMPARE(Seen::graphicsCards(root.path() + QStringLiteral("/nowhere")).size(), 0);
    }

    void readsTheProcessorSensor()
    {
        QTemporaryDir root;
        QCOMPARE(Seen::processorTemperature(root.path()), -1.0);
        write(root.path() + QStringLiteral("/class/hwmon/hwmon0/name"), "nvme\n");
        write(root.path() + QStringLiteral("/class/hwmon/hwmon0/temp1_input"), "30000\n");
        write(root.path() + QStringLiteral("/class/hwmon/hwmon1/name"), "k10temp\n");
        write(root.path() + QStringLiteral("/class/hwmon/hwmon1/temp1_input"), "61500\n");
        QCOMPARE(Seen::processorTemperature(root.path()), 61.5);
    }
};

QTEST_GUILESS_MAIN(EffectsTest)

#include "effectstest.moc"
