#include "network.h"

#include <NetworkManagerQt/AccessPoint>
#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/Device>
#include <NetworkManagerQt/IpConfig>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/WiredDevice>
#include <NetworkManagerQt/WirelessDevice>

#include "netshared.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace
{
const QString killSwitchFile = QStringLiteral("/etc/wildos/vpn-kill-switch");
const QString killSwitchStateFile = QStringLiteral("/run/wildos/kill-switch");
const QString networkHelper = QStringLiteral("/usr/libexec/wildos/settings-network");

const QString wifiType = QStringLiteral("802-11-wireless");
const QString wiredType = QStringLiteral("802-3-ethernet");

// NM_SETTINGS_CONNECTION_FLAG_NM_GENERATED and _EXTERNAL: profiles
// NetworkManager made up for interfaces something else set up, such as the
// Mullvad app's tunnel. They are not the person's to switch or forget.
constexpr uint generatedFlags = 0x2 | 0x8;

// NMActiveConnectionState
constexpr int activating = 1;
constexpr int activated = 2;

QString str(const QVariantMap &map, const char *key)
{
    return map.value(QString::fromLatin1(key)).toString();
}

uint connectionFlags(const QString &path)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NetShared::nmService, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("Get"));
    call << QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection") << QStringLiteral("Flags");
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return 0;
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant().toUInt();
}

// "wg-home-office" from "Home office": WireGuard names the interface after
// the file, and Linux allows 15 characters.
QString interfaceFor(const QString &name, const QStringList &taken)
{
    QString base = name.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    base.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (base.isEmpty()) {
        base = QStringLiteral("vpn");
    }
    QString candidate = (QStringLiteral("wg-") + base).left(15);
    while (candidate.endsWith(QLatin1Char('-'))) {
        candidate.chop(1);
    }
    const QString stem = candidate.left(12);
    for (int n = 2; taken.contains(candidate) && n < 100; ++n) {
        candidate = stem + QString::number(n);
    }
    return candidate;
}

bool isWireGuardKey(const QString &key)
{
    const QByteArray raw = QByteArray::fromBase64(key.trimmed().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    return raw.size() == 32;
}

// "255.255.255.0", "24" or "/24" to 24; -1 if it is none of those.
int prefixFromMask(QString mask)
{
    mask = mask.trimmed();
    if (mask.startsWith(QLatin1Char('/'))) {
        mask.remove(0, 1);
    }
    bool number = false;
    const int n = mask.toInt(&number);
    if (number) {
        return n >= 1 && n <= 32 ? n : -1;
    }
    const QHostAddress address(mask);
    if (address.protocol() != QAbstractSocket::IPv4Protocol) {
        return -1;
    }
    const quint32 bits = address.toIPv4Address();
    const quint32 inverted = ~bits;
    // A mask is ones then zeros, so its inverse plus one is a power of two.
    if (bits == 0 || (inverted & (inverted + 1)) != 0) {
        return -1;
    }
    int prefix = 0;
    for (quint32 b = bits; b & 0x80000000u; b <<= 1) {
        ++prefix;
    }
    return prefix;
}

QStringList splitServers(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
}
}

NetworkSettings::NetworkSettings(QObject *parent)
    : QObject(parent)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(150);
    connect(m_refreshTimer, &QTimer::timeout, this, &NetworkSettings::refresh);

    connect(NetworkManager::notifier(), &NetworkManager::Notifier::activeConnectionsChanged, this, &NetworkSettings::scheduleRefresh);
    connect(NetworkManager::notifier(), &NetworkManager::Notifier::primaryConnectionChanged, this, &NetworkSettings::scheduleRefresh);
    connect(NetworkManager::notifier(), &NetworkManager::Notifier::serviceAppeared, this, &NetworkSettings::scheduleRefresh);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionAdded, this, &NetworkSettings::scheduleRefresh);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionRemoved, this, &NetworkSettings::scheduleRefresh);

    readProxy();
    refresh();
}

void NetworkSettings::scheduleRefresh()
{
    m_refreshTimer->start();
}

void NetworkSettings::profileChanged()
{
    if (auto connection = qobject_cast<NetworkManager::Connection *>(sender())) {
        m_profiles.remove(connection->path());
    }
    scheduleRefresh();
}

void NetworkSettings::setBusy(const QString &busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void NetworkSettings::readKillSwitch()
{
    m_killSwitch = QFile::exists(killSwitchFile);
    m_killSwitchState.clear();
    m_blocked.clear();
    QFile file(killSwitchStateFile);
    if (!m_killSwitch || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString key = line.section(QLatin1Char('='), 0, 0);
        const QString value = line.section(QLatin1Char('='), 1);
        if (key == QLatin1String("state") && (value == QLatin1String("protecting") || value == QLatin1String("blocking") || value == QLatin1String("mullvad"))) {
            m_killSwitchState = value;
        } else if (key == QLatin1String("blocked")) {
            m_blocked = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
    }
}

void NetworkSettings::refresh()
{
    const bool killSwitch = m_killSwitch;
    const QString killSwitchState = m_killSwitchState;
    readKillSwitch();

    const auto active = NetworkManager::activeConnections();
    QHash<QString, NetworkManager::ActiveConnection::Ptr> activeByUuid;
    for (const auto &ac : active) {
        activeByUuid.insert(ac->uuid(), ac);
        connect(ac.data(), &NetworkManager::ActiveConnection::stateChanged, this, &NetworkSettings::scheduleRefresh, Qt::UniqueConnection);
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> paths;
    QVariantList vpns;
    QVariantList wifi;
    for (const auto &connection : NetworkManager::listConnections()) {
        const QString path = connection->path();
        paths.insert(path);
        connect(connection.data(), &NetworkManager::Connection::updated, this, &NetworkSettings::profileChanged, Qt::UniqueConnection);
        if (!m_profiles.contains(path)) {
            m_profiles.insert(path, NetShared::profile(path));
            m_flags.insert(path, connectionFlags(path));
        }
        if (m_flags.value(path) & generatedFlags) {
            continue;
        }
        const NetShared::Profile &p = m_profiles[path];
        const QVariantMap c = p.value(QStringLiteral("connection"));
        const QString type = str(c, "type");
        const QString uuid = str(c, "uuid");
        const auto ac = activeByUuid.value(uuid);
        const int state = ac ? int(ac->state()) : 0;
        // Seconds since 1970 of the last time it was fully connected.
        const qint64 used = c.value(QStringLiteral("timestamp")).toLongLong() * 1000;

        if (type == QLatin1String("wireguard") || type == QLatin1String("vpn")) {
            QString kind = QStringLiteral("VPN");
            const QString service = str(p.value(QStringLiteral("vpn")), "service-type");
            if (type == QLatin1String("wireguard") || service.contains(QLatin1String("wireguard"))) {
                kind = QStringLiteral("WireGuard");
            } else if (service.contains(QLatin1String("openvpn"))) {
                kind = QStringLiteral("OpenVPN");
            }
            // A server's name says something ("se-sto-wg-001.relays..."),
            // a bare address does not.
            QString server;
            for (const QString &host : NetShared::vpnServers(p)) {
                if (QHostAddress(host).isNull()) {
                    server = host.section(QLatin1Char('.'), 0, 0);
                    break;
                }
            }
            QString vpnState = QStringLiteral("off");
            if (state == activated) {
                vpnState = QStringLiteral("on");
                if (!m_vpnSince.contains(uuid)) {
                    // Seen already up when Settings opened: NetworkManager's
                    // own record of when it connected is the best there is.
                    m_vpnSince.insert(uuid, m_firstRefresh && used > 0 ? used : now);
                }
            } else {
                m_vpnSince.remove(uuid);
                if (state == activating) {
                    vpnState = QStringLiteral("connecting");
                } else if (m_blocked.contains(uuid)) {
                    vpnState = QStringLiteral("dropped");
                }
            }
            vpns.append(QVariantMap{
                {QStringLiteral("uuid"), uuid},
                {QStringLiteral("name"), str(c, "id")},
                {QStringLiteral("kind"), kind},
                {QStringLiteral("server"), server},
                {QStringLiteral("state"), vpnState},
                {QStringLiteral("since"), m_vpnSince.value(uuid, 0)},
            });
        } else if (type == wifiType) {
            // A hotspot this computer shares is not a network it joins.
            if (str(p.value(wifiType), "mode") == QLatin1String("ap")) {
                continue;
            }
            wifi.append(QVariantMap{
                {QStringLiteral("uuid"), uuid},
                {QStringLiteral("name"), str(c, "id")},
                {QStringLiteral("active"), state == activated},
                {QStringLiteral("connecting"), state == activating},
                {QStringLiteral("autoconnect"), c.value(QStringLiteral("autoconnect"), true).toBool()},
                {QStringLiteral("metered"), c.value(QStringLiteral("metered")).toInt() == 1},
                {QStringLiteral("lastUsed"), used},
            });
        }
    }

    for (auto it = m_profiles.begin(); it != m_profiles.end();) {
        if (paths.contains(it.key())) {
            ++it;
        } else {
            m_flags.remove(it.key());
            it = m_profiles.erase(it);
        }
    }

    std::sort(vpns.begin(), vpns.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    std::sort(wifi.begin(), wifi.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        if (x.value(QStringLiteral("active")).toBool() != y.value(QStringLiteral("active")).toBool()) {
            return x.value(QStringLiteral("active")).toBool();
        }
        return x.value(QStringLiteral("lastUsed")).toLongLong() > y.value(QStringLiteral("lastUsed")).toLongLong();
    });

    m_firstRefresh = false;
    if (vpns != m_vpns) {
        m_vpns = vpns;
        Q_EMIT vpnsChanged();
    }
    if (wifi != m_savedWifi) {
        m_savedWifi = wifi;
        Q_EMIT savedWifiChanged();
    }
    // While a change is on its way the control keeps showing it, rather
    // than flicking back to the profile it is about to replace.
    if (m_busy.isEmpty()) {
        const QVariantMap current = readCurrent();
        if (current != m_current) {
            m_current = current;
            Q_EMIT currentChanged();
        }
    }
    if (killSwitch != m_killSwitch || killSwitchState != m_killSwitchState) {
        Q_EMIT killSwitchChanged();
    }
    const QVariantMap proxy = m_proxy;
    readProxy();
    if (proxy != m_proxy) {
        Q_EMIT proxyChanged();
    }
}

QVariantMap NetworkSettings::readCurrent()
{
    // The Wi-Fi or Ethernet connection carrying the default route; with a
    // VPN up that is the one underneath it.
    NetworkManager::ActiveConnection::Ptr chosen;
    NetShared::Profile p;
    for (const auto &ac : NetworkManager::activeConnections()) {
        if (int(ac->state()) != activated || !ac->connection()) {
            continue;
        }
        const QString path = ac->connection()->path();
        if (!m_profiles.contains(path)) {
            m_profiles.insert(path, NetShared::profile(path));
        }
        const QString type = str(m_profiles.value(path).value(QStringLiteral("connection")), "type");
        if (type != wifiType && type != wiredType) {
            continue;
        }
        if (!chosen || ac->default4()) {
            chosen = ac;
            p = m_profiles.value(path);
        }
        if (ac->default4()) {
            break;
        }
    }
    if (!chosen) {
        return {};
    }

    const QVariantMap c = p.value(QStringLiteral("connection"));
    const QVariantMap ipv4 = p.value(QStringLiteral("ipv4"));
    const bool wifi = str(c, "type") == wifiType;

    QVariantMap current{
        {QStringLiteral("uuid"), str(c, "uuid")},
        {QStringLiteral("name"), str(c, "id")},
        {QStringLiteral("wifi"), wifi},
        {QStringLiteral("manual"), str(ipv4, "method") == QLatin1String("manual")},
        {QStringLiteral("metered"), c.value(QStringLiteral("metered")).toInt() == 1},
    };

    // The address a manual setup was given, to start the editor from.
    const auto addressData = NetShared::unpack<QList<QVariantMap>>(ipv4.value(QStringLiteral("address-data")));
    if (!addressData.isEmpty()) {
        current.insert(QStringLiteral("manualAddress"), addressData.constFirst().value(QStringLiteral("address")).toString());
        current.insert(QStringLiteral("manualPrefix"), addressData.constFirst().value(QStringLiteral("prefix")).toInt());
    }
    current.insert(QStringLiteral("manualGateway"), str(ipv4, "gateway"));

    const QStringList dnsEntries = NetShared::profileDns(p, QStringLiteral("ipv4")) + NetShared::profileDns(p, QStringLiteral("ipv6"));
    const QString dns = NetShared::dnsChoice(NetShared::profileDnsAddresses(p), ipv4.value(QStringLiteral("ignore-auto-dns")).toBool());
    // connection.dns-over-tls: 2 is "yes"; 1, opportunistic (Wild OS's
    // default), encrypts wherever the network allows it.
    const bool tls = c.value(QStringLiteral("dns-over-tls"), -1).toInt() >= 1;
    current.insert(QStringLiteral("dns"), dns);
    current.insert(QStringLiteral("dnsCustom"), dns == QLatin1String("custom") ? dnsEntries.join(QStringLiteral(", ")) : QString());
    current.insert(QStringLiteral("dnsEncrypted"), dns != QLatin1String("automatic") && tls);

    // An empty choice means the default (NetworkManager.conf.d): Wi-Fi gets
    // a new random address every time it connects, as GrapheneOS does, and
    // Ethernet an address of its own for each network.
    const QString assigned = str(p.value(wifi ? wifiType : wiredType), "assigned-mac-address");
    current.insert(QStringLiteral("privateAddress"),
                   assigned == QLatin1String("stable") || assigned == QLatin1String("random") || assigned.isEmpty());

    NetworkManager::Device::Ptr device;
    if (!chosen->devices().isEmpty()) {
        device = NetworkManager::findNetworkInterface(chosen->devices().constFirst());
    }
    if (!device) {
        return current;
    }
    current.insert(QStringLiteral("interface"), device->interfaceName());

    const NetworkManager::IpConfig v4 = device->ipV4Config();
    if (!v4.addresses().isEmpty()) {
        current.insert(QStringLiteral("address"), v4.addresses().constFirst().ip().toString());
        current.insert(QStringLiteral("prefix"), v4.addresses().constFirst().prefixLength());
    }
    current.insert(QStringLiteral("gateway"), v4.gateway());
    QStringList servers;
    for (const QHostAddress &server : v4.nameservers()) {
        servers.append(server.toString());
    }
    QStringList ipv6;
    QStringList linkLocal;
    const NetworkManager::IpConfig v6 = device->ipV6Config();
    for (const auto &address : v6.addresses()) {
        (address.ip().isLinkLocal() ? linkLocal : ipv6).append(address.ip().toString());
    }
    for (const QHostAddress &server : v6.nameservers()) {
        servers.append(server.toString());
    }
    // The profile's servers are what lookups use; the device's list can
    // also hold the network's own when they are being ignored.
    current.insert(QStringLiteral("dnsServers"), dns == QLatin1String("automatic") ? servers : NetShared::profileDnsAddresses(p));
    current.insert(QStringLiteral("ipv6"), ipv6.isEmpty() ? linkLocal : ipv6);

    if (auto wireless = device.objectCast<NetworkManager::WirelessDevice>()) {
        current.insert(QStringLiteral("speed"), wireless->bitRate());
        if (auto ap = wireless->activeAccessPoint()) {
            current.insert(QStringLiteral("frequency"), ap->frequency());
        }
        current.insert(QStringLiteral("mac"), wireless->hardwareAddress());
        current.insert(QStringLiteral("permanentMac"), wireless->permanentHardwareAddress());
    } else if (auto wired = device.objectCast<NetworkManager::WiredDevice>()) {
        current.insert(QStringLiteral("speed"), wired->bitRate());
        current.insert(QStringLiteral("mac"), wired->hardwareAddress());
        current.insert(QStringLiteral("permanentMac"), wired->permanentHardwareAddress());
    }
    return current;
}

void NetworkSettings::nmcli(const QStringList &args, const Finished &finished)
{
    auto process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process->setProcessEnvironment(env);
    connect(process, &QProcess::finished, this, [process, finished](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok) {
            qWarning() << "nmcli" << process->arguments() << QString::fromUtf8(process->readAllStandardError()).trimmed();
        }
        finished(ok, QString::fromUtf8(process->readAllStandardOutput()));
    });
    connect(process, &QProcess::errorOccurred, this, [process, finished](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            finished(false, QString());
        }
    });
    process->start(QStringLiteral("nmcli"), args);
}

void NetworkSettings::modifyCurrent(const QString &busyKey, const QStringList &settings, bool reconnect, const QString &failTitle)
{
    const QString uuid = m_current.value(QStringLiteral("uuid")).toString();
    const QString iface = m_current.value(QStringLiteral("interface")).toString();
    if (uuid.isEmpty() || !m_busy.isEmpty()) {
        // Puts back what the control showed before it was touched.
        m_current = readCurrent();
        Q_EMIT currentChanged();
        return;
    }
    setBusy(busyKey);
    Q_EMIT currentChanged();
    nmcli(QStringList{QStringLiteral("connection"), QStringLiteral("modify"), uuid} + settings, [=, this](bool ok, const QString &) {
        if (!ok) {
            setBusy(QString());
            m_profiles.clear();
            refresh();
            Q_EMIT failed(failTitle, tr("NetworkManager did not accept the change, so this network is set up as it was."));
            return;
        }
        const auto finish = [this, failTitle](bool up, const QString &) {
            setBusy(QString());
            m_profiles.clear();
            refresh();
            if (!up) {
                Q_EMIT failed(failTitle, tr("The change is saved, but the network did not reconnect with it. Try turning Wi-Fi off and on."));
            }
        };
        const QStringList up{QStringLiteral("connection"), QStringLiteral("up"), uuid};
        if (reconnect || iface.isEmpty()) {
            nmcli(up, finish);
            return;
        }
        nmcli({QStringLiteral("device"), QStringLiteral("reapply"), iface}, [=, this](bool applied, const QString &output) {
            if (applied) {
                finish(true, output);
            } else {
                nmcli(up, finish);
            }
        });
    });
}

void NetworkSettings::setDns(const QString &choice, const QString &custom)
{
    if (choice != QLatin1String("automatic") && choice != QLatin1String("custom") && !NetShared::provider(choice)) {
        return;
    }
    if (choice == QLatin1String("custom") && !checkCustomDns(custom).isEmpty()) {
        return;
    }
    const auto connection = NetworkManager::findConnectionByUuid(m_current.value(QStringLiteral("uuid")).toString());
    const NetShared::Profile p = connection ? m_profiles.value(connection->path()) : NetShared::Profile();
    const bool ipv4 = NetShared::ipv4TakesDns(str(p.value(QStringLiteral("ipv4")), "method"));
    const bool ipv6 = NetShared::ipv6TakesDns(str(p.value(QStringLiteral("ipv6")), "method"));
    m_current.insert(QStringLiteral("dns"), choice);
    modifyCurrent(QStringLiteral("dns"), NetShared::dnsModifyArgs(choice, splitServers(custom), ipv4, ipv6), false, tr("DNS was not changed"));
}

QString NetworkSettings::checkCustomDns(const QString &custom) const
{
    const QStringList entries = splitServers(custom);
    if (entries.isEmpty()) {
        return tr("Type the address of at least one DNS server.");
    }
    static const QRegularExpression hostName(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*$"));
    for (const QString &entry : entries) {
        if (QHostAddress(NetShared::serverAddress(entry)).isNull()) {
            return tr("%1 is not an IP address.").arg(entry.section(QLatin1Char('#'), 0, 0));
        }
        const QString name = entry.section(QLatin1Char('#'), 1);
        if (entry.contains(QLatin1Char('#')) && !hostName.match(name).hasMatch()) {
            return tr("%1 is not a server name.").arg(name);
        }
    }
    return QString();
}

QString NetworkSettings::checkAddress(const QString &address, const QString &mask, const QString &gateway) const
{
    if (QHostAddress(address.trimmed()).protocol() != QAbstractSocket::IPv4Protocol) {
        return tr("Type an IP address such as 192.168.1.42.");
    }
    if (prefixFromMask(mask) < 0) {
        return tr("Type a subnet mask such as 255.255.255.0.");
    }
    if (!gateway.trimmed().isEmpty() && QHostAddress(gateway.trimmed()).protocol() != QAbstractSocket::IPv4Protocol) {
        return tr("Type a gateway address such as 192.168.1.1.");
    }
    return QString();
}

void NetworkSettings::setAddress(bool automatic, const QString &address, const QString &mask, const QString &gateway)
{
    QStringList settings;
    if (automatic) {
        settings << QStringLiteral("ipv4.method") << QStringLiteral("auto") << QStringLiteral("ipv4.addresses") << QString()
                 << QStringLiteral("ipv4.gateway") << QString();
    } else {
        if (!checkAddress(address, mask, gateway).isEmpty()) {
            return;
        }
        settings << QStringLiteral("ipv4.method") << QStringLiteral("manual") << QStringLiteral("ipv4.addresses")
                 << address.trimmed() + QLatin1Char('/') + QString::number(prefixFromMask(mask)) << QStringLiteral("ipv4.gateway") << gateway.trimmed();
        // A manual address gets no DNS servers from the network, so a
        // network that was taking them uses Wild OS's default instead.
        if (m_current.value(QStringLiteral("dns")).toString() == QLatin1String("automatic")) {
            settings << NetShared::dnsModifyArgs(QStringLiteral("quad9"), {}, true, false, true);
        }
    }
    modifyCurrent(QStringLiteral("address"), settings, false, tr("The IP address was not changed"));
}

// The new address only takes effect on joining again, so the network
// reconnects for a moment.
void NetworkSettings::setPrivateAddress(bool on)
{
    const bool wifi = m_current.value(QStringLiteral("wifi")).toBool();
    const QString key = wifi ? QStringLiteral("802-11-wireless.cloned-mac-address") : QStringLiteral("802-3-ethernet.cloned-mac-address");
    const QString off = wifi ? QStringLiteral("permanent") : QStringLiteral("preserve");
    m_current.insert(QStringLiteral("privateAddress"), on);
    const QString private_ = wifi ? QStringLiteral("random") : QStringLiteral("stable");
    modifyCurrent(QStringLiteral("private"), {key, on ? private_ : off}, true, tr("The hardware address was not changed"));
}

void NetworkSettings::setMetered(bool on)
{
    m_current.insert(QStringLiteral("metered"), on);
    modifyCurrent(QStringLiteral("metered"), {QStringLiteral("connection.metered"), on ? QStringLiteral("yes") : QStringLiteral("no")}, false,
                  tr("Metered connection was not changed"));
}

void NetworkSettings::forget(const QString &uuid)
{
    if (!m_busy.isEmpty()) {
        return;
    }
    setBusy(QStringLiteral("forget:") + uuid);
    nmcli({QStringLiteral("connection"), QStringLiteral("delete"), QStringLiteral("uuid"), uuid}, [this](bool ok, const QString &) {
        setBusy(QString());
        scheduleRefresh();
        if (!ok) {
            Q_EMIT failed(tr("The network was not forgotten"), tr("NetworkManager did not remove it. Try again."));
        }
    });
}

void NetworkSettings::setVpnActive(const QString &uuid, bool on)
{
    const auto connection = NetworkManager::findConnectionByUuid(uuid);
    if (!connection) {
        return;
    }
    const QString name = connection->name();
    if (on) {
        auto watcher = new QDBusPendingCallWatcher(NetworkManager::activateConnection(connection->path(), QStringLiteral("/"), QStringLiteral("/")), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, name] {
            watcher->deleteLater();
            if (watcher->isError()) {
                qWarning() << "VPN" << name << watcher->error().message();
                Q_EMIT failed(tr("%1 did not connect").arg(name), tr("Check that you are online, then try again."));
                Q_EMIT vpnsChanged();
            }
            scheduleRefresh();
        });
        return;
    }
    // Switched off on purpose, so the kill switch stops holding the
    // internet back for it, whether it was up or had dropped.
    QDBusMessage release = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Network"), QStringLiteral("/org/wildos/Network"),
                                                          QStringLiteral("org.wildos.Network"), QStringLiteral("Release"));
    release << uuid;
    QDBusConnection::systemBus().asyncCall(release);
    for (const auto &ac : NetworkManager::activeConnections()) {
        if (ac->uuid() == uuid) {
            NetworkManager::deactivateConnection(ac->path());
        }
    }
    scheduleRefresh();
}

void NetworkSettings::setKillSwitch(bool on)
{
    if (!m_busy.isEmpty() || on == m_killSwitch) {
        Q_EMIT killSwitchChanged();
        return;
    }
    setBusy(QStringLiteral("killswitch"));
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        setBusy(QString());
        if (code == 126 || code == 127) {
            Q_EMIT failed(tr("The kill switch was not changed"), tr("The password was not given."));
        } else if (code != 0) {
            Q_EMIT failed(tr("The kill switch was not changed"), tr("Wild OS could not change it. Try again."));
        }
        // Whatever happened, the switch shows the file again.
        Q_EMIT killSwitchChanged();
        // The service reports what it is guarding a moment later.
        refresh();
        QTimer::singleShot(1500, this, &NetworkSettings::refresh);
    });
    process->start(QStringLiteral("pkexec"), {networkHelper, QStringLiteral("kill-switch"), on ? QStringLiteral("on") : QStringLiteral("off")});
}

QString NetworkSettings::checkWireGuard(const QVariantMap &details) const
{
    if (details.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        return tr("Give the VPN a name.");
    }
    const QString endpoint = details.value(QStringLiteral("endpoint")).toString().trimmed();
    static const QRegularExpression endpointShape(QStringLiteral("^(\\[[0-9A-Fa-f:.]+\\]|[A-Za-z0-9.-]+):[0-9]{1,5}$"));
    if (!endpointShape.match(endpoint).hasMatch()) {
        return tr("Type the server with its port, such as vpn.example.com:51820.");
    }
    if (!isWireGuardKey(details.value(QStringLiteral("publicKey")).toString())) {
        return tr("The server's public key is 44 characters ending in =.");
    }
    if (!isWireGuardKey(details.value(QStringLiteral("privateKey")).toString())) {
        return tr("Your private key is 44 characters ending in =.");
    }
    const QStringList addresses = splitServers(details.value(QStringLiteral("address")).toString());
    if (addresses.isEmpty()) {
        return tr("Type the address your provider gave this computer, such as 10.64.0.2/32.");
    }
    for (const QString &address : addresses) {
        if (QHostAddress::parseSubnet(address).first.isNull() && QHostAddress(address).isNull()) {
            return tr("%1 is not an IP address.").arg(address);
        }
    }
    for (const QString &server : splitServers(details.value(QStringLiteral("dns")).toString())) {
        if (QHostAddress(server).isNull()) {
            return tr("%1 is not an IP address.").arg(server);
        }
    }
    return QString();
}

void NetworkSettings::addWireGuard(const QVariantMap &details)
{
    if (!checkWireGuard(details).isEmpty() || !m_busy.isEmpty()) {
        return;
    }
    QStringList addresses = splitServers(details.value(QStringLiteral("address")).toString());
    for (QString &address : addresses) {
        if (!address.contains(QLatin1Char('/'))) {
            address += QHostAddress(address).protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("/128") : QStringLiteral("/32");
        }
    }
    QString conf = QStringLiteral("[Interface]\nPrivateKey = %1\nAddress = %2\n")
                       .arg(details.value(QStringLiteral("privateKey")).toString().trimmed(), addresses.join(QStringLiteral(", ")));
    const QStringList dns = splitServers(details.value(QStringLiteral("dns")).toString());
    if (!dns.isEmpty()) {
        conf += QStringLiteral("DNS = %1\n").arg(dns.join(QStringLiteral(", ")));
    }
    conf += QStringLiteral("\n[Peer]\nPublicKey = %1\nAllowedIPs = 0.0.0.0/0, ::/0\nEndpoint = %2\nPersistentKeepalive = 25\n")
                .arg(details.value(QStringLiteral("publicKey")).toString().trimmed(), details.value(QStringLiteral("endpoint")).toString().trimmed());

    QTemporaryDir dir;
    QFile file(dir.filePath(QStringLiteral("typed.conf")));
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly)) {
        Q_EMIT failed(tr("The VPN was not added"), tr("Wild OS could not write its details down. Try again."));
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(conf.toUtf8());
    file.close();
    importFile(file.fileName(), QStringLiteral("wireguard"), details.value(QStringLiteral("name")).toString().trimmed());
}

void NetworkSettings::importVpn(const QUrl &url)
{
    if (!m_busy.isEmpty()) {
        return;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT failed(tr("The VPN was not added"), tr("That file could not be opened."));
        return;
    }
    const QString text = QString::fromUtf8(file.read(256 * 1024));
    QString type;
    if (text.contains(QRegularExpression(QStringLiteral("^\\s*\\[Interface\\]"), QRegularExpression::MultilineOption))) {
        type = QStringLiteral("wireguard");
    } else if (text.contains(QRegularExpression(QStringLiteral("^\\s*(remote|client|dev\\s+tun|<ca>)"), QRegularExpression::MultilineOption))) {
        type = QStringLiteral("openvpn");
    } else {
        Q_EMIT failed(tr("The VPN was not added"), tr("That file is not a WireGuard or OpenVPN connection."));
        return;
    }
    importFile(path, type, QFileInfo(path).completeBaseName());
}

// nmcli's import is NetworkManager's own: WireGuard files are read by
// libnm, OpenVPN files by the network-manager-openvpn plugin, which also
// finds certificates the file names beside it. WireGuard takes the
// interface name from the file name, so that file is copied under a name
// Linux accepts first.
void NetworkSettings::importFile(const QString &path, const QString &type, const QString &name)
{
    setBusy(QStringLiteral("import"));
    auto dir = std::make_shared<QTemporaryDir>();
    QString source = path;
    if (type == QLatin1String("wireguard")) {
        QStringList taken;
        for (const auto &p : std::as_const(m_profiles)) {
            taken.append(str(p.value(QStringLiteral("connection")), "interface-name"));
        }
        source = dir->filePath(interfaceFor(name, taken) + QStringLiteral(".conf"));
        if (!dir->isValid() || !QFile::copy(path, source)) {
            setBusy(QString());
            Q_EMIT failed(tr("The VPN was not added"), tr("That file could not be read."));
            return;
        }
        QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    nmcli({QStringLiteral("connection"), QStringLiteral("import"), QStringLiteral("type"), type, QStringLiteral("file"), source},
          [this, dir, name, type](bool ok, const QString &output) {
              const auto match = QRegularExpression(QStringLiteral("\\(([0-9a-fA-F-]{36})\\)")).match(output);
              if (!ok || !match.hasMatch()) {
                  setBusy(QString());
                  Q_EMIT failed(tr("The VPN was not added"),
                                type == QLatin1String("openvpn") ? tr("NetworkManager could not read that OpenVPN file.")
                                                                 : tr("NetworkManager could not read that WireGuard file."));
                  return;
              }
              // A VPN starts only when the person turns it on.
              nmcli({QStringLiteral("connection"), QStringLiteral("modify"), match.captured(1), QStringLiteral("connection.id"), name,
                     QStringLiteral("connection.autoconnect"), QStringLiteral("no")},
                    [this, name](bool, const QString &) {
                        setBusy(QString());
                        scheduleRefresh();
                        Q_EMIT done(tr("%1 added").arg(name), tr("Turn it on with its switch whenever you want to use it."));
                    });
          });
}

void NetworkSettings::readProxy()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kioslaverc"), KConfig::NoGlobals);
    config->reparseConfiguration();
    const KConfigGroup group = config->group(QStringLiteral("Proxy Settings"));
    static const QStringList modes{QStringLiteral("off"), QStringLiteral("manual"), QStringLiteral("script"), QStringLiteral("automatic"),
                                   QStringLiteral("manual")};
    const int type = group.readEntry("ProxyType", 0);
    // KIO writes "http://host port"; older files have "host:port".
    QString http = group.readEntry("httpProxy", QString()).trimmed();
    http.remove(QRegularExpression(QStringLiteral("^[a-z]+://")));
    QString host = http.section(QLatin1Char(' '), 0, 0);
    int port = http.section(QLatin1Char(' '), 1).toInt();
    if (port == 0 && host.count(QLatin1Char(':')) == 1) {
        port = host.section(QLatin1Char(':'), 1).toInt();
        host = host.section(QLatin1Char(':'), 0, 0);
    }
    m_proxy = {
        {QStringLiteral("mode"), modes.value(type, QStringLiteral("off"))},
        {QStringLiteral("host"), host},
        {QStringLiteral("port"), port},
        {QStringLiteral("script"), group.readEntry("Proxy Config Script", QString())},
    };
}

// The same settings KDE's proxy module writes. KDE apps and Chromium-based
// browsers read them; KIO's running workers are told to read them again.
void NetworkSettings::setProxy(const QString &mode, const QString &host, int port, const QString &script)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kioslaverc"), KConfig::NoGlobals);
    KConfigGroup group = config->group(QStringLiteral("Proxy Settings"));
    int type = 0;
    if (mode == QLatin1String("manual")) {
        type = 1;
        QString address = host.trimmed();
        if (!address.contains(QLatin1String("://"))) {
            address.prepend(QStringLiteral("http://"));
        }
        const QString value = address + QLatin1Char(' ') + QString::number(port > 0 ? port : 8080);
        group.writeEntry("httpProxy", value);
        group.writeEntry("httpsProxy", value);
        group.writeEntry("ftpProxy", value);
    } else if (mode == QLatin1String("script")) {
        type = 2;
        group.writeEntry("Proxy Config Script", script.trimmed());
    } else if (mode == QLatin1String("automatic")) {
        type = 3;
    }
    group.writeEntry("ProxyType", type);
    config->sync();

    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KIO/Scheduler"), QStringLiteral("org.kde.KIO.Scheduler"),
                                                      QStringLiteral("reparseSlaveConfiguration"));
    message << QString();
    QDBusConnection::sessionBus().send(message);

    readProxy();
    Q_EMIT proxyChanged();
}
