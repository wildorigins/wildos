#include "blocked.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QtEndian>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iterator>
#include <fcntl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
const QString logDir = QStringLiteral("/var/lib/wildos/firewall");
const QString logFile = QStringLiteral("/var/lib/wildos/firewall/blocked.log");
const QString stateFile = QStringLiteral("/run/wildos/firewall-state.json");
// settings-firewall's LOG_GROUP.
constexpr quint16 logGroup = 47;
constexpr qint64 keepSeconds = 7 * 24 * 3600;
// Enough for a day of steady knocking from the internet at the firewall's
// rate limit, and a bound on the file whatever happens.
constexpr int maxLines = 20000;

bool configure(int fd, quint16 attrType, const void *data, int size)
{
    static quint32 seq = 0;
    alignas(nlmsghdr) char buf[128] = {};
    auto *nh = reinterpret_cast<nlmsghdr *>(buf);
    nh->nlmsg_type = (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG;
    nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nh->nlmsg_seq = ++seq;
    auto *g = static_cast<nfgenmsg *>(NLMSG_DATA(nh));
    g->nfgen_family = AF_UNSPEC;
    g->version = NFNETLINK_V0;
    g->res_id = htons(logGroup);
    const int head = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(nfgenmsg)));
    auto *a = reinterpret_cast<nlattr *>(buf + head);
    a->nla_type = attrType;
    a->nla_len = NLA_HDRLEN + size;
    std::memcpy(buf + head + NLA_HDRLEN, data, size);
    nh->nlmsg_len = head + NLA_ALIGN(a->nla_len);

    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    if (sendto(fd, buf, nh->nlmsg_len, 0, reinterpret_cast<sockaddr *>(&kernel), sizeof kernel) < 0) {
        return false;
    }
    alignas(nlmsghdr) char reply[1024];
    for (int tries = 0; tries < 8; ++tries) {
        int n = recv(fd, reply, sizeof reply, 0);
        if (n <= 0) {
            return false;
        }
        for (auto *r = reinterpret_cast<nlmsghdr *>(reply); NLMSG_OK(r, n); r = NLMSG_NEXT(r, n)) {
            if (r->nlmsg_type == NLMSG_ERROR && r->nlmsg_seq == seq) {
                const int error = static_cast<nlmsgerr *>(NLMSG_DATA(r))->error;
                if (error != 0) {
                    qWarning() << "nflog config" << attrType << std::strerror(-error);
                }
                return error == 0;
            }
        }
    }
    return false;
}

bool isLocalRange(const QHostAddress &a)
{
    return a.isPrivateUse() || a.isLinkLocal() || a.isUniqueLocalUnicast()
        || a.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("100.64.0.0/10")));
}
}

BlockedLog::BlockedLog(QObject *parent)
    : QObject(parent)
{
    prune();
    m_prune.setInterval(3600 * 1000);
    connect(&m_prune, &QTimer::timeout, this, &BlockedLog::prune);
    m_prune.start();
    m_retry.setSingleShot(true);
    m_retry.setInterval(30000);
    connect(&m_retry, &QTimer::timeout, this, &BlockedLog::open);
    open();
}

BlockedLog::~BlockedLog()
{
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

void BlockedLog::open()
{
    m_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_NETFILTER);
    sockaddr_nl local{};
    local.nl_family = AF_NETLINK;
    nfulnl_msg_config_cmd bind{NFULNL_CFG_CMD_BIND};
    nfulnl_msg_config_mode mode{};
    // The IP header and the ports are all that is kept.
    mode.copy_range = htonl(64);
    mode.copy_mode = NFULNL_COPY_PACKET;
    if (m_fd < 0 || ::bind(m_fd, reinterpret_cast<sockaddr *>(&local), sizeof local) < 0
        || !configure(m_fd, NFULA_CFG_CMD, &bind, sizeof bind) || !configure(m_fd, NFULA_CFG_MODE, &mode, sizeof mode)) {
        qWarning() << "cannot listen to the firewall's log; trying again shortly";
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        m_retry.start();
        return;
    }
    int big = 1 << 20;
    setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &BlockedLog::read);
}

void BlockedLog::read()
{
    alignas(nlmsghdr) char buf[8192];
    for (;;) {
        int n = recv(m_fd, buf, sizeof buf, 0);
        if (n <= 0) {
            // ENOBUFS: some were lost while this was busy; carry on.
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS && errno != EINTR) {
                qWarning() << "firewall log:" << std::strerror(errno);
            }
            return;
        }
        for (auto *nh = reinterpret_cast<nlmsghdr *>(buf); NLMSG_OK(nh, n); nh = NLMSG_NEXT(nh, n)) {
            if (nh->nlmsg_type != ((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_PACKET)) {
                continue;
            }
            const char *start = reinterpret_cast<const char *>(NLMSG_DATA(nh)) + NLMSG_ALIGN(sizeof(nfgenmsg));
            int rest = int(nh->nlmsg_len) - int(start - reinterpret_cast<const char *>(nh));
            QByteArray payload;
            QString device;
            while (rest >= int(NLA_HDRLEN)) {
                const auto *a = reinterpret_cast<const nlattr *>(start);
                if (a->nla_len < NLA_HDRLEN || a->nla_len > rest) {
                    break;
                }
                const char *data = start + NLA_HDRLEN;
                const int size = a->nla_len - NLA_HDRLEN;
                switch (a->nla_type & NLA_TYPE_MASK) {
                case NFULA_PAYLOAD:
                    payload = QByteArray(data, size);
                    break;
                case NFULA_IFINDEX_INDEV:
                    if (size >= 4) {
                        quint32 index;
                        std::memcpy(&index, data, 4);
                        char name[IF_NAMESIZE] = {};
                        if (if_indextoname(ntohl(index), name)) {
                            device = QString::fromLatin1(name);
                        }
                    }
                    break;
                }
                start += NLA_ALIGN(a->nla_len);
                rest -= NLA_ALIGN(a->nla_len);
            }
            if (!payload.isEmpty()) {
                record(payload, device);
            }
        }
    }
}

void BlockedLog::record(const QByteArray &packet, QString device)
{
    const auto *p = reinterpret_cast<const quint8 *>(packet.constData());
    const int size = packet.size();
    QHostAddress source;
    int protocol = 0;
    int l4 = 0;
    if (size >= 20 && (p[0] >> 4) == 4) {
        l4 = (p[0] & 0x0f) * 4;
        protocol = p[9];
        source = QHostAddress(qFromBigEndian<quint32>(p + 12));
    } else if (size >= 40 && (p[0] >> 4) == 6) {
        l4 = 40;
        protocol = p[6];
        source = QHostAddress(p + 8);
    }
    if ((protocol != 6 && protocol != 17) || size < l4 + 4 || source.isNull()) {
        return;
    }
    const int port = (p[l4 + 2] << 8) | p[l4 + 3];
    const QString proto = protocol == 6 ? QStringLiteral("tcp") : QStringLiteral("udp");
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    const QString key = source.toString() + QLatin1Char('/') + proto + QLatin1Char('/') + QString::number(port);
    const qint64 last = m_recent.value(key);
    m_recent.insert(key, now);
    if (now - last < 60) {
        return;
    }
    if (m_recent.size() > 1000) {
        for (auto it = m_recent.begin(); it != m_recent.end();) {
            it = now - it.value() >= 60 ? m_recent.erase(it) : std::next(it);
        }
    }

    const QString kind = kindOf(source, &device);
    QJsonObject line{{QStringLiteral("t"), now},
                     {QStringLiteral("src"), source.toString()},
                     {QStringLiteral("proto"), proto},
                     {QStringLiteral("port"), port},
                     {QStringLiteral("kind"), kind},
                     {QStringLiteral("device"), device}};
    QDir().mkpath(logDir);
    QFile file(logFile);
    if (file.open(QIODevice::Append)) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        file.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
    }
}

// "router", "local" (another device on the network it came in from),
// "internet", or "other" (a private address that is not on that network,
// such as one behind a VPN). Nothing is guessed beyond that.
QString BlockedLog::kindOf(const QHostAddress &source, QString *device)
{
    const QFileInfo info(stateFile);
    if (info.lastModified() != m_stateRead) {
        m_stateRead = info.lastModified();
        QFile file(stateFile);
        if (file.open(QIODevice::ReadOnly)) {
            m_networks = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("networks")).toArray();
        }
    }
    for (const QJsonValue &v : std::as_const(m_networks)) {
        const QJsonObject net = v.toObject();
        const QString dev = net.value(QStringLiteral("device")).toString();
        if (!device->isEmpty() && dev != *device) {
            continue;
        }
        for (const QJsonValue &g : net.value(QStringLiteral("gateways")).toArray()) {
            if (QHostAddress(g.toString()).isEqual(source, QHostAddress::TolerantConversion)) {
                *device = dev;
                return QStringLiteral("router");
            }
        }
        const QJsonArray ranges = net.value(QStringLiteral("v4")).toArray() + net.value(QStringLiteral("v6")).toArray();
        for (const QJsonValue &r : ranges) {
            if (source.isInSubnet(QHostAddress::parseSubnet(r.toString()))) {
                *device = dev;
                return QStringLiteral("local");
            }
        }
    }
    return isLocalRange(source) ? QStringLiteral("other") : QStringLiteral("internet");
}

void BlockedLog::prune()
{
    QFile file(logFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const qint64 oldest = QDateTime::currentSecsSinceEpoch() - keepSeconds;
    QList<QByteArray> kept;
    int total = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        ++total;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.value(QStringLiteral("t")).toInteger() >= oldest) {
            kept.append(line);
        }
    }
    file.close();
    if (kept.size() > maxLines) {
        kept = kept.mid(kept.size() - maxLines);
    }
    if (kept.size() == total) {
        return;
    }
    QSaveFile out(logFile);
    if (out.open(QIODevice::WriteOnly)) {
        for (const QByteArray &line : std::as_const(kept)) {
            out.write(line);
        }
        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        out.commit();
    }
}

void BlockedLog::clear()
{
    QFile file(logFile);
    if (file.exists()) {
        file.resize(0);
    }
    m_recent.clear();
}
