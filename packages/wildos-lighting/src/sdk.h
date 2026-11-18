#pragma once

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

// A client for OpenRGB's SDK network protocol, written from its published
// description (Documentation/OpenRGBSDK.md in OpenRGB's repository). It
// only ever connects to wildos-openrgb.service on the loopback.
//
// It speaks protocol 5. OpenRGB 1.0 and 1.1, whose own newest is 6,
// answer in the version asked for (checked against a real 1.1 server);
// an older server is spoken to in its own version, as far back as 3.
// Version 6 is left alone until OpenRGB's document stops marking it as
// unfinished.
namespace Sdk
{
constexpr quint32 newestProtocol = 5;
constexpr quint32 oldestProtocol = 3;

enum ModeFlag : quint32 {
    HasSpeed = 1u << 0,
    HasBrightness = 1u << 4,
    HasPerLedColour = 1u << 5,
    HasModeColour = 1u << 6,
    HasRandomColour = 1u << 7,
    AutomaticSave = 1u << 9,
};

enum ColourMode : quint32 {
    ColoursNone = 0,
    ColoursPerLed = 1,
    ColoursModeSpecific = 2,
};

enum ZoneType : qint32 {
    ZoneSingle = 0,
    ZoneLinear = 1,
    ZoneMatrix = 2,
    ZoneLinearLoop = 3,
};

struct Mode {
    QString name;
    qint32 value = 0;
    quint32 flags = 0;
    quint32 speedMin = 0;
    quint32 speedMax = 0;
    quint32 brightnessMin = 0;
    quint32 brightnessMax = 0;
    quint32 coloursMin = 0;
    quint32 coloursMax = 0;
    quint32 speed = 0;
    quint32 brightness = 0;
    quint32 direction = 0;
    quint32 colourMode = 0;
    QList<QRgb> colours;
};

enum ZoneFlag : quint32 {
    ResizeForEffectsOnly = 1u << 0,
    ManuallySized = 1u << 1,
};

struct Zone {
    QString name;
    qint32 type = 0;
    quint32 ledsMin = 0;
    quint32 ledsMax = 0;
    quint32 ledsCount = 0;
    quint32 flags = 0;
    // The server will take a new count for it: a strip connector that
    // cannot count its own lights. Worked out by Client, which knows
    // which server it is talking to.
    bool resizable = false;
};

struct Controller {
    qint32 type = 0;
    QString name;
    QString vendor;
    QString serial;
    QString location;
    qint32 activeMode = 0;
    QList<Mode> modes;
    QList<Zone> zones;
    int leds = 0;
};

// One controller from the answer to a controller data request. False when
// the block is cut short, in which case `out` is not to be used.
bool parseController(const QByteArray &block, quint32 protocol, Controller *out);

// A whole packet: the 16 byte header and its data.
QByteArray packet(quint32 device, quint32 id, const QByteArray &data = QByteArray());
QByteArray ledsData(const QList<QRgb> &colours);
QByteArray modeData(int index, const Mode &mode, quint32 protocol);
QByteArray resizeData(int zone, int size);

class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);

    void open(quint16 port);
    void close();
    // True once the server's controllers have all been read.
    bool ready() const { return m_ready; }
    quint32 protocol() const { return m_protocol; }
    // Whether a zone of this server's can be resized. OpenRGB 1.0 and
    // later (protocol 6) resize only zones flagged for it and ignore the
    // request for any other; before that, any zone with a range could be.
    static bool canResize(const Zone &zone, quint32 serverProtocol);
    const QList<Controller> &controllers() const { return m_controllers; }
    // Bytes not yet taken by the server: a frame is skipped rather than
    // queued behind a server that has fallen behind.
    qint64 backlog() const { return m_socket.bytesToWrite(); }

    void readAgain();
    void setCustomMode(int device);
    void updateLeds(int device, const QList<QRgb> &colours);
    void updateMode(int device, int index, const Mode &mode);
    void resizeZone(int device, int zone, int size);

Q_SIGNALS:
    // The controllers were read, for the first time or again.
    void listChanged();
    // The connection went, or the server is one this cannot talk to.
    void lost(const QString &why);

private:
    void send(quint32 device, quint32 id, const QByteArray &data = QByteArray());
    void received();
    void handle(quint32 device, quint32 id, const QByteArray &data);
    void fail(const QString &why);

    QTcpSocket m_socket;
    QTimer m_patience;
    QByteArray m_buffer;
    bool m_open = false;
    quint32 m_protocol = 0;
    quint32 m_serverProtocol = 0;
    bool m_ready = false;
    bool m_reading = false;
    bool m_readAgain = false;
    int m_expected = 0;
    QList<Controller> m_incoming;
    QList<bool> m_arrived;
    QList<Controller> m_controllers;
};
}
