#include "sdk.h"

#include <QTest>
#include <QtEndian>

using namespace Sdk;

namespace
{
// A controller's description built by hand from OpenRGB's protocol
// document, apart from the code under test.
struct Block {
    QByteArray bytes;

    Block &u32(quint32 v)
    {
        char raw[4];
        qToLittleEndian<quint32>(v, raw);
        bytes.append(raw, 4);
        return *this;
    }

    Block &u16(quint16 v)
    {
        char raw[2];
        qToLittleEndian<quint16>(v, raw);
        bytes.append(raw, 2);
        return *this;
    }

    Block &text(const char *s)
    {
        const QByteArray raw(s);
        u16(quint16(raw.size() + 1));
        bytes.append(raw);
        bytes.append('\0');
        return *this;
    }
};

QByteArray board(quint32 protocol)
{
    Block b;
    b.u32(0);
    b.u32(0).text("Z690 GAMING X DDR4");
    if (protocol >= 1) {
        b.text("Gigabyte");
    }
    b.text("RGB Fusion 2 USB").text("1.0").text("ABC123").text("HID: /dev/hidraw4");
    b.u16(1).u32(0);
    b.text("Direct").u32(7).u32(HasPerLedColour).u32(0).u32(0);
    if (protocol >= 3) {
        b.u32(0).u32(0);
    }
    b.u32(0).u32(0).u32(0);
    if (protocol >= 3) {
        b.u32(0);
    }
    b.u32(0).u32(ColoursPerLed).u16(0);

    b.u16(2);
    b.text("Motherboard").u32(ZoneLinear).u32(2).u32(2).u32(2);
    // A matrix map, which the client has no use for and must step over.
    b.u16(16).u32(1).u32(2).u32(0).u32(1);
    if (protocol >= 4) {
        b.u16(1).text("Left").u32(ZoneLinear).u32(0).u32(1);
    }
    if (protocol >= 5) {
        b.u32(0);
    }
    b.text("D_LED1").u32(ZoneLinear).u32(0).u32(1024).u32(0).u16(0);
    if (protocol >= 4) {
        b.u16(0);
    }
    if (protocol >= 5) {
        b.u32(1 << 1);
    }

    b.u16(2).text("LED 1").u32(0).text("LED 2").u32(1);
    b.u16(2).u32(0x00030201).u32(0x00060504);
    if (protocol >= 5) {
        b.u16(0).u32(1);
    }
    return b.bytes;
}
}

class SdkTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsAControllerInEveryVersion_data()
    {
        QTest::addColumn<quint32>("protocol");
        for (quint32 v = oldestProtocol; v <= newestProtocol; ++v) {
            QTest::newRow(qPrintable(QString::number(v))) << v;
        }
    }

    void readsAControllerInEveryVersion()
    {
        QFETCH(quint32, protocol);
        Controller c;
        QVERIFY(parseController(board(protocol), protocol, &c));
        QCOMPARE(c.name, QStringLiteral("Z690 GAMING X DDR4"));
        QCOMPARE(c.vendor, QStringLiteral("Gigabyte"));
        QCOMPARE(c.serial, QStringLiteral("ABC123"));
        QCOMPARE(c.modes.size(), 1);
        QCOMPARE(c.modes[0].name, QStringLiteral("Direct"));
        QCOMPARE(c.modes[0].value, 7);
        QCOMPARE(c.modes[0].flags, quint32(HasPerLedColour));
        QCOMPARE(c.zones.size(), 2);
        QCOMPARE(c.zones[0].ledsCount, 2u);
        QCOMPARE(c.zones[1].name, QStringLiteral("D_LED1"));
        QCOMPARE(c.zones[1].ledsMin, 0u);
        QCOMPARE(c.zones[1].ledsMax, 1024u);
        QCOMPARE(c.zones[1].flags, protocol >= 5 ? quint32(ManuallySized) : 0u);
        QCOMPARE(c.leds, 2);
    }

    void resizesOnlyWhatTheServerWill()
    {
        Zone fixed;
        fixed.ledsMin = fixed.ledsMax = 12;
        fixed.flags = ManuallySized;
        Zone ranged;
        ranged.ledsMax = 300;
        Zone flagged = ranged;
        flagged.flags = ManuallySized;
        QVERIFY(!Client::canResize(fixed, 6));
        QVERIFY(!Client::canResize(ranged, 6));
        QVERIFY(Client::canResize(flagged, 6));
        // Before OpenRGB 1.0 a range was all it took.
        QVERIFY(Client::canResize(ranged, 5));
        QVERIFY(Client::canResize(ranged, 4));
    }

    void refusesABlockCutShort()
    {
        const QByteArray whole = board(5);
        Controller c;
        for (qsizetype length = 0; length < whole.size() - 16; length += 7) {
            QVERIFY2(!parseController(whole.left(length), 5, &c), qPrintable(QString::number(length)));
        }
        // Lengths that lie about what follows them.
        QByteArray lying = whole;
        lying[8] = char(0xff);
        lying[9] = char(0xff);
        QVERIFY(!parseController(lying, 5, &c));
    }

    void buildsPackets()
    {
        const QByteArray leds = ledsData({qRgb(1, 2, 3), qRgb(250, 251, 252)});
        QCOMPARE(leds.toHex(), QByteArray("0e000000" "0200" "01020300" "fafbfc00"));
        const QByteArray whole = packet(3, 1050, leds);
        QCOMPARE(whole.left(16).toHex(), QByteArray("4f524742" "03000000" "1a040000" "0e000000"));
        QCOMPARE(resizeData(1, 30).toHex(), QByteArray("01000000" "1e000000"));
    }

    void sendsAModeBackAsItCame()
    {
        Controller c;
        QVERIFY(parseController(board(5), 5, &c));
        Mode mode = c.modes[0];
        mode.colours = {qRgb(9, 8, 7)};
        const QByteArray data = modeData(0, mode, 5);
        QCOMPARE(qFromLittleEndian<quint32>(data.constData()), quint32(data.size()));
        // The mode's own value goes back untouched: versions up to 5 need it.
        const qsizetype value = 4 + 4 + 2 + 7;
        QCOMPARE(qFromLittleEndian<quint32>(data.constData() + value), 7u);
        QCOMPARE(data.right(6).toHex(), QByteArray("0100" "09080700"));
        QCOMPARE(modeData(0, mode, 3).size(), data.size());
    }
};

QTEST_GUILESS_MAIN(SdkTest)

#include "sdktest.moc"
