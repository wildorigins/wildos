#include "firewall.h"

#include <KService>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace
{
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-firewall");
// Switching the firewall off asks for the password every time.
const QString offHelper = QStringLiteral("/usr/libexec/wildos/settings-firewall-off");
const QString configFile = QStringLiteral("/etc/wildos/firewall.json");
const QString sessionFile = QStringLiteral("/run/wildos/firewall-session.json");
const QString markFile = QStringLiteral("/run/wildos/firewall");
const QString logDir = QStringLiteral("/var/lib/wildos/firewall");
const QString logFile = QStringLiteral("/var/lib/wildos/firewall/blocked.log");
const QString krdpUnit = QStringLiteral("app-org.kde.krdpserver.service");

// The servers Settings can switch on, as settings-firewall knows them.
struct Server {
    const char *name;
    const char *binary;
};
constexpr Server servers[] = {{"ssh", "/usr/sbin/sshd"}, {"files", "/usr/sbin/smbd"}, {"remote", "/usr/bin/krdpserver"}};

// Ports the rows on the page stand for, so the list of apps to allow does
// not offer them again.
const QList<QPair<QString, int>> rowPorts{{QStringLiteral("tcp"), 22},   {QStringLiteral("tcp"), 445},  {QStringLiteral("tcp"), 139},
                                         {QStringLiteral("tcp"), 3389}, {QStringLiteral("tcp"), 5357}, {QStringLiteral("udp"), 3702},
                                         {QStringLiteral("udp"), 1900}, {QStringLiteral("udp"), 5353}, {QStringLiteral("udp"), 137},
                                         {QStringLiteral("udp"), 138}};

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QVariantMap ruleMap(const QJsonObject &r, bool session)
{
    const qint64 until = r.value(QStringLiteral("until")).toInteger();
    return {{QStringLiteral("id"), r.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), r.value(QStringLiteral("name")).toString()},
            {QStringLiteral("port"), r.value(QStringLiteral("port")).toInt()},
            {QStringLiteral("proto"), r.value(QStringLiteral("proto")).toString()},
            {QStringLiteral("scope"), r.value(QStringLiteral("scope")).toString()},
            {QStringLiteral("address"), r.value(QStringLiteral("address")).toString()},
            {QStringLiteral("until"), until > 0 ? QDateTime::fromSecsSinceEpoch(until) : QDateTime()},
            {QStringLiteral("on"), r.value(QStringLiteral("on")).toBool(true)},
            {QStringLiteral("session"), session}};
}

QString prettyProcess(const QString &process)
{
    static const QHash<QString, QString> known{{QStringLiteral("jellyfin"), QStringLiteral("Jellyfin")},
                                               {QStringLiteral("syncthing"), QStringLiteral("Syncthing")},
                                               {QStringLiteral("kdeconnectd"), QStringLiteral("KDE Connect")},
                                               {QStringLiteral("plexmediaserver"), QStringLiteral("Plex Media Server")},
                                               {QStringLiteral("java"), QStringLiteral("A Java program")},
                                               {QStringLiteral("python3"), QStringLiteral("A Python program")},
                                               {QStringLiteral("node"), QStringLiteral("A Node.js program")}};
    if (known.contains(process)) {
        return known.value(process);
    }
    if (process.isEmpty()) {
        return QStringLiteral("A system program");
    }
    return process.at(0).toUpper() + process.mid(1);
}
}

Firewall::Firewall(QObject *parent)
    : QObject(parent)
{
    // Writes come in bursts (the config, then the state), so a change is
    // read once they settle.
    m_settle.setSingleShot(true);
    m_settle.setInterval(150);
    connect(&m_settle, &QTimer::timeout, this, &Firewall::refresh);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    refresh();
}

bool Firewall::available() const
{
    return QFileInfo::exists(helper);
}

void Firewall::watch()
{
    const QStringList dirs{QStringLiteral("/etc/wildos"), QStringLiteral("/run/wildos"), logDir};
    for (const QString &dir : dirs) {
        if (QFileInfo::exists(dir) && !m_watcher.directories().contains(dir)) {
            m_watcher.addPath(dir);
        }
    }
    // Appends to the list do not change its folder.
    if (QFileInfo::exists(logFile) && !m_watcher.files().contains(logFile)) {
        m_watcher.addPath(logFile);
    }
    if (!QFileInfo::exists(logDir) && QFileInfo::exists(QStringLiteral("/var/lib/wildos")) && !m_watcher.directories().contains(QStringLiteral("/var/lib/wildos"))) {
        m_watcher.addPath(QStringLiteral("/var/lib/wildos"));
    }
}

void Firewall::refresh()
{
    const QJsonObject cfg = readObject(configFile);
    m_enabled = cfg.value(QStringLiteral("enabled")).toBool(true);
    m_discovery = cfg.value(QStringLiteral("discovery")).toBool(true);
    m_loaded = QFileInfo::exists(markFile);

    m_public.clear();
    m_home.clear();
    const QJsonObject nets = cfg.value(QStringLiteral("networks")).toObject();
    for (auto it = nets.begin(); it != nets.end(); ++it) {
        if (it.value().toString() == QLatin1String("public")) {
            m_public.insert(it.key(), true);
        } else if (it.value().toString() == QLatin1String("home")) {
            m_home.insert(it.key(), true);
        }
    }

    m_services.clear();
    const QJsonObject chosen = cfg.value(QStringLiteral("services")).toObject();
    for (const Server &s : servers) {
        const QString name = QString::fromLatin1(s.name);
        const bool installed = QFileInfo::exists(QString::fromLatin1(s.binary));
        QVariantMap state{{QStringLiteral("on"), installed && chosen.value(name).toBool()}, {QStringLiteral("installed"), installed}};
        if (name == QLatin1String("remote") && state.value(QStringLiteral("on")).toBool()) {
            state.insert(QStringLiteral("running"),
                         QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("is-active"), QStringLiteral("--quiet"), krdpUnit}) == 0);
        }
        m_services.insert(name, state);
    }

    m_rules.clear();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QJsonValue &v : cfg.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject r = v.toObject();
        // An hour that has run out, before the service has tidied it away.
        const qint64 until = r.value(QStringLiteral("until")).toInteger();
        if (until > 0 && until <= now) {
            continue;
        }
        m_rules.append(ruleMap(r, false));
    }
    for (const QJsonValue &v : readObject(sessionFile).value(QStringLiteral("rules")).toArray()) {
        m_rules.append(ruleMap(v.toObject(), true));
    }

    readBlocked();
    watch();
    Q_EMIT changed();
}

void Firewall::readBlocked()
{
    QVariantList list;
    QFile file(logFile);
    if (file.open(QIODevice::ReadOnly)) {
        const qint64 oldest = QDateTime::currentSecsSinceEpoch() - 7 * 24 * 3600;
        while (!file.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(file.readLine()).object();
            const qint64 t = o.value(QStringLiteral("t")).toInteger();
            if (t < oldest) {
                continue;
            }
            list.prepend(QVariantMap{{QStringLiteral("when"), QDateTime::fromSecsSinceEpoch(t)},
                                     {QStringLiteral("source"), o.value(QStringLiteral("src")).toString()},
                                     {QStringLiteral("proto"), o.value(QStringLiteral("proto")).toString()},
                                     {QStringLiteral("port"), o.value(QStringLiteral("port")).toInt()},
                                     {QStringLiteral("kind"), o.value(QStringLiteral("kind")).toString()}});
        }
    }
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("when")).toDateTime() > b.toMap().value(QStringLiteral("when")).toDateTime();
    });
    if (list != m_blocked) {
        m_blocked = list;
        Q_EMIT blockedChanged();
    }
}

void Firewall::refreshListening()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Network"), QStringLiteral("/org/wildos/Network"),
                                                       QStringLiteral("org.wildos.Network"), QStringLiteral("Listening"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 15000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QString> reply = *w;
        QVariantList list;
        if (!reply.isError()) {
            for (const QJsonValue &v : QJsonDocument::fromJson(reply.value().toUtf8()).array()) {
                const QJsonObject o = v.toObject();
                const int port = o.value(QStringLiteral("port")).toInt();
                const QString proto = o.value(QStringLiteral("proto")).toString();
                if (allowed(port, proto) || rowPorts.contains(qMakePair(proto, port))) {
                    continue;
                }
                QString name;
                const QString app = o.value(QStringLiteral("app")).toString();
                if (!app.isEmpty()) {
                    KService::Ptr service = KService::serviceByDesktopName(app);
                    if (!service) {
                        service = KService::serviceByStorageId(app + QStringLiteral(".desktop"));
                    }
                    if (service && !service->name().isEmpty()) {
                        name = service->name();
                    }
                }
                if (name.isEmpty()) {
                    name = prettyProcess(o.value(QStringLiteral("process")).toString());
                }
                list.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("port"), port}, {QStringLiteral("proto"), proto}});
            }
        }
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("port")).toInt() < b.toMap().value(QStringLiteral("port")).toInt();
        });
        m_listening = list;
        Q_EMIT listeningChanged();
    });
}

bool Firewall::allowed(int port, const QString &proto) const
{
    for (const QVariant &v : m_rules) {
        const QVariantMap r = v.toMap();
        const QString p = r.value(QStringLiteral("proto")).toString();
        if (r.value(QStringLiteral("port")).toInt() == port && (p == proto || p == QLatin1String("both")) && r.value(QStringLiteral("on")).toBool()) {
            return true;
        }
    }
    return false;
}

QString Firewall::serviceName(int port, const QString &proto) const
{
    const bool udp = proto == QLatin1String("udp");
    switch (port) {
    case 21:
        return udp ? QString() : QStringLiteral("File transfer (FTP)");
    case 22:
        return udp ? QString() : QStringLiteral("SSH");
    case 23:
        return udp ? QString() : QStringLiteral("Telnet");
    case 25:
        return udp ? QString() : QStringLiteral("Mail server");
    case 53:
        return QStringLiteral("Name lookups (DNS)");
    case 80:
    case 443:
    case 8080:
        return QStringLiteral("Web server");
    case 137:
    case 138:
    case 139:
    case 445:
        return QStringLiteral("File sharing");
    case 548:
        return udp ? QString() : QStringLiteral("File sharing (Mac)");
    case 631:
        return QStringLiteral("Printing");
    case 1194:
        return QStringLiteral("OpenVPN");
    case 1900:
    case 3702:
    case 5353:
    case 5357:
        return QStringLiteral("Device discovery");
    case 3389:
        return QStringLiteral("Remote Desktop");
    case 5900:
        return udp ? QString() : QStringLiteral("Screen sharing (VNC)");
    case 8096:
        return QStringLiteral("Media server");
    case 22000:
        return QStringLiteral("File sync");
    case 25565:
        return QStringLiteral("Game server");
    case 51820:
        return udp ? QStringLiteral("WireGuard") : QString();
    default:
        return port >= 1714 && port <= 1764 ? QStringLiteral("KDE Connect") : QString();
    }
}

QString Firewall::checkAddress(const QString &address) const
{
    const QString text = address.trimmed();
    if (text.isEmpty()) {
        return QStringLiteral("Give the device's address.");
    }
    const QHostAddress a(text);
    if (a.isNull()) {
        return QStringLiteral("That isn't an address. It looks like 192.168.1.24.");
    }
    if (a.isLoopback() || a.isMulticast() || a == QHostAddress(QHostAddress::AnyIPv4) || a == QHostAddress(QHostAddress::AnyIPv6)) {
        return QStringLiteral("That address can't be another device.");
    }
    return {};
}

void Firewall::readOutput()
{
    while (m_process && m_process->canReadLine()) {
        const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (line.startsWith(QLatin1String("progress "))) {
            m_progress = line.mid(9).toInt();
            Q_EMIT busyChanged();
        } else if (line.startsWith(QLatin1String("step "))) {
            m_step = line.mid(5);
            Q_EMIT busyChanged();
        } else if (!line.isEmpty()) {
            m_lastLine = line;
        }
    }
}

void Firewall::run(const QStringList &args, const QString &what, const QString &program)
{
    if (m_process) {
        return;
    }
    m_busyWhat = what;
    m_progress = -1;
    m_step.clear();
    m_lastLine.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, &Firewall::readOutput);
    connect(m_process, &QProcess::finished, this, [this, args, what](int code, QProcess::ExitStatus status) {
        readOutput();
        const QString last = m_lastLine;
        m_process->deleteLater();
        m_process = nullptr;
        const bool ok = status == QProcess::NormalExit && code == 0;
        // Remote Desktop runs in the person's own session, so its service is
        // theirs to start.
        if (ok && what == QLatin1String("remote")) {
            const bool on = args.value(2) == QLatin1String("on");
            QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), on ? QStringLiteral("enable") : QStringLiteral("disable"),
                                                            QStringLiteral("--now"), krdpUnit});
            // It stops again within a moment when no account can sign in yet.
            QTimer::singleShot(3000, this, &Firewall::refresh);
        }
        m_busyWhat.clear();
        Q_EMIT busyChanged();
        refresh();
        if (ok) {
            Q_EMIT done(what);
        } else if (status != QProcess::NormalExit || (code != 126 && code != 127)) {
            // 126 and 127: the password prompt was dismissed, which needs no
            // message.
            Q_EMIT failed(last.isEmpty() ? QStringLiteral("Something went wrong. Nothing was changed.") : last);
        }
    });
    Q_EMIT busyChanged();
    m_process->start(QStringLiteral("pkexec"), QStringList{program.isEmpty() ? helper : program} + args);
}

void Firewall::setEnabled(bool on)
{
    if (on) {
        run({QStringLiteral("enabled"), QStringLiteral("on")}, QStringLiteral("enabled"));
    } else {
        run({}, QStringLiteral("enabled"), offHelper);
    }
}

void Firewall::setPublic(const QString &uuid, bool isPublic)
{
    run({QStringLiteral("trust"), uuid, isPublic ? QStringLiteral("public") : QStringLiteral("home")}, QStringLiteral("trust"));
}

void Firewall::setDiscovery(bool on)
{
    run({QStringLiteral("discovery"), on ? QStringLiteral("on") : QStringLiteral("off")}, QStringLiteral("discovery"));
}

void Firewall::setService(const QString &name, bool on)
{
    if (name != QLatin1String("ssh") && name != QLatin1String("files") && name != QLatin1String("remote")) {
        return;
    }
    run({QStringLiteral("service"), name, on ? QStringLiteral("on") : QStringLiteral("off")}, name);
}

void Firewall::allow(const QString &name, int port, const QString &proto, const QString &scope, const QString &address, const QString &duration)
{
    run({QStringLiteral("allow"), name, QString::number(port), proto, scope, address.trimmed().isEmpty() ? QStringLiteral("-") : address.trimmed(),
         duration},
        QStringLiteral("allow"));
}

void Firewall::setRuleOn(const QString &id, bool on)
{
    run({QStringLiteral("rule"), id, on ? QStringLiteral("on") : QStringLiteral("off")}, id);
}

void Firewall::removeRule(const QString &id)
{
    run({QStringLiteral("remove"), id}, id);
}

void Firewall::clearBlocked()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Network"), QStringLiteral("/org/wildos/Network"),
                                                       QStringLiteral("org.wildos.Network"), QStringLiteral("ClearBlocked"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            Q_EMIT failed(QStringLiteral("The list could not be cleared. ") + w->error().message());
        }
        readBlocked();
    });
}
