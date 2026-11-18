#include "sdk.h"

#include <QHostAddress>
#include <QtEndian>

namespace Sdk
{
namespace
{
enum Packet : quint32 {
    RequestControllerCount = 0,
    RequestControllerData = 1,
    RequestProtocolVersion = 40,
    SetClientName = 50,
    DeviceListUpdated = 100,
    ResizeZone = 1000,
    UpdateLeds = 1050,
    SetCustomMode = 1100,
    UpdateMode = 1101,
};

constexpr int headerSize = 16;
// A controller with a few hundred lights describes itself in tens of
// kilobytes. Anything far beyond that is not a server worth trusting.
constexpr quint32 largestPacket = 8u * 1024 * 1024;

// Reads the little-endian fields of a data block, and remembers when it
// has been asked for more than is there.
class Reader
{
public:
    explicit Reader(const QByteArray &data)
        : m_data(data)
    {
    }

    bool ok() const { return m_ok; }

    quint32 u32()
    {
        if (!need(4)) {
            return 0;
        }
        const quint32 v = qFromLittleEndian<quint32>(m_data.constData() + m_at);
        m_at += 4;
        return v;
    }

    quint16 u16()
    {
        if (!need(2)) {
            return 0;
        }
        const quint16 v = qFromLittleEndian<quint16>(m_data.constData() + m_at);
        m_at += 2;
        return v;
    }

    QString string()
    {
        const int length = u16();
        if (!need(length)) {
            return QString();
        }
        const QByteArray raw = m_data.mid(m_at, length);
        m_at += length;
        return QString::fromUtf8(raw.constData(), qstrnlen(raw.constData(), raw.size()));
    }

    void skip(qsizetype bytes)
    {
        if (need(bytes)) {
            m_at += bytes;
        }
    }

private:
    bool need(qsizetype bytes)
    {
        if (bytes < 0 || m_data.size() - m_at < bytes) {
            m_ok = false;
        }
        return m_ok;
    }

    const QByteArray &m_data;
    qsizetype m_at = 0;
    bool m_ok = true;
};

void put32(QByteArray &out, quint32 v)
{
    char raw[4];
    qToLittleEndian<quint32>(v, raw);
    out.append(raw, 4);
}

void put16(QByteArray &out, quint16 v)
{
    char raw[2];
    qToLittleEndian<quint16>(v, raw);
    out.append(raw, 2);
}

void putString(QByteArray &out, const QString &s)
{
    const QByteArray raw = s.toUtf8();
    put16(out, quint16(raw.size() + 1));
    out.append(raw);
    out.append('\0');
}

// On the wire a colour is red, green, blue and a spare byte.
QRgb colour(quint32 wire)
{
    return qRgb(wire & 0xff, (wire >> 8) & 0xff, (wire >> 16) & 0xff);
}

quint32 wire(QRgb c)
{
    return quint32(qRed(c)) | quint32(qGreen(c)) << 8 | quint32(qBlue(c)) << 16;
}

Mode readMode(Reader &r, quint32 protocol)
{
    Mode m;
    m.name = r.string();
    m.value = qint32(r.u32());
    m.flags = r.u32();
    m.speedMin = r.u32();
    m.speedMax = r.u32();
    if (protocol >= 3) {
        m.brightnessMin = r.u32();
        m.brightnessMax = r.u32();
    }
    m.coloursMin = r.u32();
    m.coloursMax = r.u32();
    m.speed = r.u32();
    if (protocol >= 3) {
        m.brightness = r.u32();
    }
    m.direction = r.u32();
    m.colourMode = r.u32();
    const int colours = r.u16();
    for (int i = 0; i < colours && r.ok(); ++i) {
        m.colours.append(colour(r.u32()));
    }
    return m;
}

Zone readZone(Reader &r, quint32 protocol)
{
    Zone z;
    z.name = r.string();
    z.type = qint32(r.u32());
    z.ledsMin = r.u32();
    z.ledsMax = r.u32();
    z.ledsCount = r.u32();
    r.skip(r.u16());
    if (protocol >= 4) {
        const int segments = r.u16();
        for (int i = 0; i < segments && r.ok(); ++i) {
            r.string();
            r.skip(12);
        }
    }
    if (protocol >= 5) {
        z.flags = r.u32();
    }
    return z;
}
}

bool parseController(const QByteArray &block, quint32 protocol, Controller *out)
{
    Reader r(block);
    r.u32();
    Controller c;
    c.type = qint32(r.u32());
    c.name = r.string();
    if (protocol >= 1) {
        c.vendor = r.string();
    }
    r.string();
    r.string();
    c.serial = r.string();
    c.location = r.string();
    const int modes = r.u16();
    c.activeMode = qint32(r.u32());
    for (int i = 0; i < modes && r.ok(); ++i) {
        c.modes.append(readMode(r, protocol));
    }
    const int zones = r.u16();
    for (int i = 0; i < zones && r.ok(); ++i) {
        c.zones.append(readZone(r, protocol));
    }
    c.leds = r.u16();
    for (int i = 0; i < c.leds && r.ok(); ++i) {
        r.string();
        r.u32();
    }
    // The colours and what follows them are not needed: what the lights
    // show is decided here, not read back.
    if (!r.ok()) {
        return false;
    }
    *out = c;
    return true;
}

QByteArray packet(quint32 device, quint32 id, const QByteArray &data)
{
    QByteArray out("ORGB", 4);
    put32(out, device);
    put32(out, id);
    put32(out, quint32(data.size()));
    out.append(data);
    return out;
}

QByteArray ledsData(const QList<QRgb> &colours)
{
    QByteArray out;
    put32(out, quint32(4 + 2 + 4 * colours.size()));
    put16(out, quint16(colours.size()));
    for (const QRgb c : colours) {
        put32(out, wire(c));
    }
    return out;
}

QByteArray modeData(int index, const Mode &mode, quint32 protocol)
{
    QByteArray body;
    put32(body, quint32(index));
    putString(body, mode.name);
    put32(body, quint32(mode.value));
    put32(body, mode.flags);
    put32(body, mode.speedMin);
    put32(body, mode.speedMax);
    if (protocol >= 3) {
        put32(body, mode.brightnessMin);
        put32(body, mode.brightnessMax);
    }
    put32(body, mode.coloursMin);
    put32(body, mode.coloursMax);
    put32(body, mode.speed);
    if (protocol >= 3) {
        put32(body, mode.brightness);
    }
    put32(body, mode.direction);
    put32(body, mode.colourMode);
    put16(body, quint16(mode.colours.size()));
    for (const QRgb c : mode.colours) {
        put32(body, wire(c));
    }
    QByteArray out;
    put32(out, quint32(4 + body.size()));
    out.append(body);
    return out;
}

QByteArray resizeData(int zone, int size)
{
    QByteArray out;
    put32(out, quint32(zone));
    put32(out, quint32(size));
    return out;
}

Client::Client(QObject *parent)
    : QObject(parent)
{
    m_patience.setSingleShot(true);
    m_patience.setInterval(5000);
    connect(&m_patience, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("the server did not answer"));
    });
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        QByteArray version;
        put32(version, newestProtocol);
        send(0, RequestProtocolVersion, version);
        m_patience.start();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &Client::received);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        fail(QStringLiteral("the connection closed"));
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        fail(m_socket.errorString());
    });
}

void Client::open(quint16 port)
{
    close();
    m_open = true;
    m_socket.connectToHost(QHostAddress::LocalHost, port);
}

void Client::close()
{
    m_patience.stop();
    m_open = false;
    m_ready = false;
    m_reading = false;
    m_readAgain = false;
    m_protocol = 0;
    m_buffer.clear();
    m_controllers.clear();
    m_incoming.clear();
    m_arrived.clear();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        const QSignalBlocker quiet(&m_socket);
        m_socket.abort();
    }
}

void Client::fail(const QString &why)
{
    const bool was = m_open;
    close();
    if (was) {
        Q_EMIT lost(why);
    }
}

void Client::send(quint32 device, quint32 id, const QByteArray &data)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        m_socket.write(packet(device, id, data));
    }
}

void Client::readAgain()
{
    if (m_protocol == 0) {
        return;
    }
    if (m_reading) {
        m_readAgain = true;
        return;
    }
    m_reading = true;
    m_readAgain = false;
    m_incoming.clear();
    m_arrived.clear();
    send(0, RequestControllerCount);
    m_patience.start();
}

void Client::received()
{
    m_buffer.append(m_socket.readAll());
    while (m_buffer.size() >= headerSize) {
        if (!m_buffer.startsWith("ORGB")) {
            fail(QStringLiteral("the server sent something that is not OpenRGB's protocol"));
            return;
        }
        const quint32 device = qFromLittleEndian<quint32>(m_buffer.constData() + 4);
        const quint32 id = qFromLittleEndian<quint32>(m_buffer.constData() + 8);
        const quint32 size = qFromLittleEndian<quint32>(m_buffer.constData() + 12);
        if (size > largestPacket) {
            fail(QStringLiteral("the server sent a packet too large to be real"));
            return;
        }
        if (m_buffer.size() - headerSize < qsizetype(size)) {
            return;
        }
        const QByteArray data = m_buffer.mid(headerSize, size);
        m_buffer.remove(0, headerSize + size);
        handle(device, id, data);
        if (m_socket.state() != QAbstractSocket::ConnectedState) {
            return;
        }
    }
}

void Client::handle(quint32 device, quint32 id, const QByteArray &data)
{
    switch (id) {
    case RequestProtocolVersion: {
        if (data.size() < 4 || m_protocol != 0) {
            return;
        }
        const quint32 server = qFromLittleEndian<quint32>(data.constData());
        if (server < oldestProtocol) {
            fail(QStringLiteral("this OpenRGB is too old"));
            return;
        }
        m_serverProtocol = server;
        m_protocol = qMin(server, newestProtocol);
        send(0, SetClientName, QByteArray("Wild OS lighting", 17));
        readAgain();
        return;
    }
    case RequestControllerCount: {
        if (!m_reading || data.size() < 4) {
            return;
        }
        m_expected = int(qMin<quint32>(qFromLittleEndian<quint32>(data.constData()), 4096));
        m_incoming = QList<Controller>(m_expected);
        m_arrived = QList<bool>(m_expected, false);
        if (m_expected == 0) {
            break;
        }
        QByteArray version;
        put32(version, m_protocol);
        for (int i = 0; i < m_expected; ++i) {
            send(quint32(i), RequestControllerData, version);
        }
        m_patience.start();
        return;
    }
    case RequestControllerData: {
        // The server answers in whatever order its threads finish, not
        // the order it was asked in.
        if (!m_reading || device >= quint32(m_arrived.size()) || m_arrived[device]) {
            return;
        }
        if (!parseController(data, m_protocol, &m_incoming[device])) {
            fail(QStringLiteral("a controller's description could not be read"));
            return;
        }
        for (Zone &zone : m_incoming[device].zones) {
            zone.resizable = canResize(zone, m_serverProtocol);
        }
        m_arrived[device] = true;
        if (m_arrived.contains(false)) {
            m_patience.start();
            return;
        }
        break;
    }
    case DeviceListUpdated:
        readAgain();
        return;
    default:
        return;
    }

    m_patience.stop();
    m_reading = false;
    m_controllers = m_incoming;
    m_incoming.clear();
    m_ready = true;
    if (m_readAgain) {
        readAgain();
        return;
    }
    Q_EMIT listChanged();
}

bool Client::canResize(const Zone &zone, quint32 serverProtocol)
{
    if (zone.ledsMin == zone.ledsMax) {
        return false;
    }
    return serverProtocol < 6 || (zone.flags & (ManuallySized | ResizeForEffectsOnly)) != 0;
}

void Client::setCustomMode(int device)
{
    send(quint32(device), SetCustomMode);
}

void Client::updateLeds(int device, const QList<QRgb> &colours)
{
    send(quint32(device), UpdateLeds, ledsData(colours));
}

void Client::updateMode(int device, int index, const Mode &mode)
{
    send(quint32(device), UpdateMode, modeData(index, mode, m_protocol));
}

void Client::resizeZone(int device, int zone, int size)
{
    send(quint32(device), ResizeZone, resizeData(zone, size));
    readAgain();
}
}
