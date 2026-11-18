#include "hosts.h"
#include "paths.h"
#include "secrets.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace
{
QString file()
{
    return Paths::config() + QStringLiteral("/hosts.json");
}

// What a desktop starts with. How it opens and whether it shares the
// clipboard are Remote's settings; a saved host keeps its own.
QVariantMap desktopDefaults()
{
    return {
        {QStringLiteral("display"), RemoteSettings::instance()->desktopDisplay()},
        {QStringLiteral("resolution"), QStringLiteral("match")},
        {QStringLiteral("scaling"), QStringLiteral("auto")},
        {QStringLiteral("password"), QStringLiteral("ask")},
        {QStringLiteral("clipboard"), RemoteSettings::instance()->desktopClipboard()},
        {QStringLiteral("sound"), true},
        {QStringLiteral("microphone"), false},
        {QStringLiteral("printers"), false},
        {QStringLiteral("folders"), false},
        {QStringLiteral("folderPath"), QDir::homePath() + QStringLiteral("/Downloads")},
        {QStringLiteral("gateway"), QString()},
        {QStringLiteral("speed"), QStringLiteral("auto")},
    };
}

QVariantMap sshDefaults()
{
    return {
        {QStringLiteral("identity"), QString()},
        {QStringLiteral("forwards"), QVariantList()},
        {QStringLiteral("colour"), QStringLiteral("#5fd39a")},
    };
}

// Fills in whatever an older or hand-written entry leaves out.
QVariantMap complete(QVariantMap h)
{
    const bool desktop = h.value(QStringLiteral("kind")).toString() == QLatin1String("desktop");
    h.insert(QStringLiteral("kind"), desktop ? QStringLiteral("desktop") : QStringLiteral("terminal"));
    const QString section = desktop ? QStringLiteral("desktop") : QStringLiteral("ssh");
    QVariantMap options = desktop ? desktopDefaults() : sshDefaults();
    const QVariantMap given = h.value(section).toMap();
    for (auto it = given.cbegin(); it != given.cend(); ++it) {
        options.insert(it.key(), it.value());
    }
    h.insert(section, options);
    if (h.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        h.insert(QStringLiteral("name"), h.value(QStringLiteral("address")).toString());
    }
    return h;
}

// "web-01:2222" or "[fe80::1]:2222".
QPair<QString, int> splitAddress(const QString &address)
{
    const QString a = address.trimmed();
    static const QRegularExpression bracketed(QStringLiteral("^\\[([^\\]]+)\\](?::(\\d+))?$"));
    if (const auto m = bracketed.match(a); m.hasMatch()) {
        return {m.captured(1), m.captured(2).isEmpty() ? 0 : m.captured(2).toInt()};
    }
    if (a.count(QLatin1Char(':')) == 1) {
        const QString port = a.section(QLatin1Char(':'), 1);
        bool ok = false;
        const int p = port.toInt(&ok);
        if (ok) {
            return {a.section(QLatin1Char(':'), 0, 0), p};
        }
    }
    return {a, 0};
}
}

Hosts *Hosts::instance()
{
    static Hosts *hosts = new Hosts;
    return hosts;
}

Hosts *Hosts::create(QQmlEngine *, QJSEngine *)
{
    Hosts *hosts = instance();
    QJSEngine::setObjectOwnership(hosts, QJSEngine::CppOwnership);
    return hosts;
}

Hosts::Hosts(QObject *parent)
    : QObject(parent)
{
    load();
}

void Hosts::load()
{
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly)) {
        m_groups = {QStringLiteral("Work"), QStringLiteral("Servers"), QStringLiteral("Home")};
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &g : root.value(QStringLiteral("groups")).toArray()) {
        m_groups.append(g.toString());
    }
    for (const QJsonValue &h : root.value(QStringLiteral("hosts")).toArray()) {
        m_hosts.append(complete(h.toObject().toVariantMap()));
    }
}

void Hosts::write()
{
    QDir().mkpath(Paths::config());
    QJsonObject root;
    root.insert(QStringLiteral("groups"), QJsonArray::fromStringList(m_groups));
    root.insert(QStringLiteral("hosts"), QJsonArray::fromVariantList(m_hosts));
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
        // Addresses and user names are nobody else's business.
        QFile::setPermissions(file(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    ++m_revision;
    Q_EMIT changed();
}

int Hosts::indexOf(const QString &id) const
{
    for (int i = 0; i < m_hosts.size(); ++i) {
        if (m_hosts[i].toMap().value(QStringLiteral("id")).toString() == id) {
            return i;
        }
    }
    return -1;
}

QVariantMap Hosts::host(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? m_hosts[i].toMap() : QVariantMap();
}

QString Hosts::save(const QVariantMap &input)
{
    QVariantMap h = complete(input);
    QString id = h.value(QStringLiteral("id")).toString();
    const int i = id.isEmpty() ? -1 : indexOf(id);
    if (i < 0) {
        if (id.isEmpty()) {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        }
        h.insert(QStringLiteral("id"), id);
        m_hosts.append(h);
    } else {
        const QString before = passwordKey(m_hosts[i].toMap());
        m_hosts[i] = h;
        forgetUnused(before);
    }
    const QString group = h.value(QStringLiteral("group")).toString();
    if (!group.isEmpty() && !m_groups.contains(group)) {
        m_groups.append(group);
    }
    write();
    return id;
}

void Hosts::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    const QString key = passwordKey(m_hosts[i].toMap());
    m_hosts.removeAt(i);
    QFile::remove(screenPath(id));
    forgetUnused(key);
    write();
}

void Hosts::setFavourite(const QString &id, bool favourite)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    QVariantMap h = m_hosts[i].toMap();
    h.insert(QStringLiteral("favourite"), favourite);
    m_hosts[i] = h;
    write();
}

void Hosts::touch(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    QVariantMap h = m_hosts[i].toMap();
    h.insert(QStringLiteral("lastUsed"), QDateTime::currentDateTime().toString(Qt::ISODate));
    m_hosts[i] = h;
    write();
}

void Hosts::addGroup(const QString &name)
{
    const QString n = name.trimmed();
    if (!n.isEmpty() && !m_groups.contains(n)) {
        m_groups.append(n);
        write();
    }
}

void Hosts::renameGroup(const QString &from, const QString &to)
{
    const QString n = to.trimmed();
    const int g = m_groups.indexOf(from);
    if (g < 0 || n.isEmpty() || m_groups.contains(n)) {
        return;
    }
    m_groups[g] = n;
    for (QVariant &v : m_hosts) {
        QVariantMap h = v.toMap();
        if (h.value(QStringLiteral("group")).toString() == from) {
            h.insert(QStringLiteral("group"), n);
            v = h;
        }
    }
    write();
}

void Hosts::removeGroup(const QString &name)
{
    // The hosts stay, without a group.
    if (!m_groups.removeOne(name)) {
        return;
    }
    for (QVariant &v : m_hosts) {
        QVariantMap h = v.toMap();
        if (h.value(QStringLiteral("group")).toString() == name) {
            h.insert(QStringLiteral("group"), QString());
            v = h;
        }
    }
    write();
}

QVariantMap Hosts::parseAddress(const QString &text) const
{
    QString t = text.trimmed();
    if (t.isEmpty()) {
        return {};
    }
    QVariantMap out;
    if (t.startsWith(QLatin1String("rdp://"), Qt::CaseInsensitive) || t.startsWith(QLatin1String("ssh://"), Qt::CaseInsensitive)) {
        const QUrl url(t);
        if (!url.isValid() || url.host().isEmpty()) {
            return {};
        }
        const bool rdp = url.scheme().toLower() == QLatin1String("rdp");
        out.insert(QStringLiteral("kind"), rdp ? QStringLiteral("desktop") : QStringLiteral("terminal"));
        out.insert(QStringLiteral("user"), url.userName());
        out.insert(QStringLiteral("address"), url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host());
        return out;
    }
    if (t.startsWith(QLatin1String("ssh "))) {
        // As ssh takes it: ssh [-p port] [-l user] [user@]host.
        const QStringList args = QProcess::splitCommand(t).mid(1);
        QString user, hostPart;
        int port = 0;
        for (int i = 0; i < args.size(); ++i) {
            const QString &a = args[i];
            if (a == QLatin1String("-p") && i + 1 < args.size()) {
                port = args[++i].toInt();
            } else if (a == QLatin1String("-l") && i + 1 < args.size()) {
                user = args[++i];
            } else if (a.startsWith(QLatin1Char('-'))) {
                // Anything else ssh takes a value for is skipped with it.
                if (QStringLiteral("bcDEeFIiJLmOoQRSWw").contains(a.mid(1, 1)) && a.size() == 2) {
                    ++i;
                }
            } else if (hostPart.isEmpty()) {
                hostPart = a;
            }
        }
        if (hostPart.contains(QLatin1Char('@'))) {
            user = hostPart.section(QLatin1Char('@'), 0, -2);
            hostPart = hostPart.section(QLatin1Char('@'), -1);
        }
        if (hostPart.isEmpty()) {
            return {};
        }
        out.insert(QStringLiteral("kind"), QStringLiteral("terminal"));
        out.insert(QStringLiteral("user"), user);
        out.insert(QStringLiteral("address"), port > 0 ? QStringLiteral("%1:%2").arg(hostPart).arg(port) : hostPart);
        return out;
    }
    // user@host is an SSH address; host:3389 a desktop. A bare word is a
    // search unless it looks like a name on a network or an IP address.
    static const QRegularExpression userAt(QStringLiteral("^([^@\\s]+)@([^@\\s]+)$"));
    if (const auto m = userAt.match(t); m.hasMatch()) {
        const auto [h, p] = splitAddress(m.captured(2));
        out.insert(QStringLiteral("kind"), p == 3389 ? QStringLiteral("desktop") : QStringLiteral("terminal"));
        out.insert(QStringLiteral("user"), m.captured(1));
        out.insert(QStringLiteral("address"), m.captured(2));
        return out;
    }
    static const QRegularExpression addressLike(QStringLiteral("^(\\[[0-9a-fA-F:]+\\]|[0-9]{1,3}(\\.[0-9]{1,3}){3}|[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+)(:\\d+)?$"));
    if (addressLike.match(t).hasMatch()) {
        const auto [h, p] = splitAddress(t);
        out.insert(QStringLiteral("kind"), p == 22 ? QStringLiteral("terminal") : QStringLiteral("desktop"));
        out.insert(QStringLiteral("user"), QString());
        out.insert(QStringLiteral("address"), t);
        return out;
    }
    return {};
}

QString Hosts::passwordKey(const QVariantMap &h) const
{
    if (h.value(QStringLiteral("kind")).toString() != QLatin1String("terminal")) {
        return {};
    }
    QString user = h.value(QStringLiteral("user")).toString();
    if (user.isEmpty()) {
        // ssh signs in as the person using this PC when no user is given.
        user = qEnvironmentVariable("USER");
    }
    return Secrets::sshKey(user, hostName(h), port(h));
}

void Hosts::forgetUnused(const QString &key)
{
    if (key.isEmpty()) {
        return;
    }
    for (const QVariant &v : std::as_const(m_hosts)) {
        if (passwordKey(v.toMap()) == key) {
            return;
        }
    }
    Secrets::instance()->remove(key);
}

QVariantMap Hosts::addressParts(const QString &address) const
{
    const auto [h, p] = splitAddress(address);
    return {{QStringLiteral("host"), h}, {QStringLiteral("port"), p}};
}

QString Hosts::joinAddress(const QString &host, int port, const QString &kind) const
{
    const QString h = host.trimmed();
    const int standard = kind == QLatin1String("desktop") ? 3389 : 22;
    if (port <= 0 || port > 65535 || port == standard) {
        return h;
    }
    // An IPv6 address takes brackets before a port.
    return QString::fromLatin1(h.contains(QLatin1Char(':')) ? "[%1]:%2" : "%1:%2").arg(h).arg(port);
}

QString Hosts::hostName(const QVariantMap &h) const
{
    return splitAddress(h.value(QStringLiteral("address")).toString()).first;
}

int Hosts::port(const QVariantMap &h) const
{
    const int p = splitAddress(h.value(QStringLiteral("address")).toString()).second;
    if (p > 0) {
        return p;
    }
    return h.value(QStringLiteral("kind")).toString() == QLatin1String("desktop") ? 3389 : 22;
}

void Hosts::probe(const QString &id)
{
    const QVariantMap h = host(id);
    if (h.isEmpty()) {
        return;
    }
    auto *socket = new QTcpSocket(this);
    auto finish = [this, socket, id](bool online) {
        if (socket->property("done").toBool()) {
            return;
        }
        socket->setProperty("done", true);
        m_reach.insert(id, online ? QStringLiteral("online") : QStringLiteral("offline"));
        Q_EMIT reachChanged();
        socket->abort();
        socket->deleteLater();
    };
    connect(socket, &QTcpSocket::connected, this, [finish] { finish(true); });
    connect(socket, &QTcpSocket::errorOccurred, this, [finish] { finish(false); });
    QTimer::singleShot(2500, socket, [finish] { finish(false); });
    socket->connectToHost(hostName(h), quint16(port(h)));
}

QString Hosts::screenPath(const QString &id) const
{
    return Paths::cache() + QStringLiteral("/screens/") + id + QStringLiteral(".png");
}

QUrl Hosts::screenUrl(const QString &id) const
{
    const QString path = screenPath(id);
    if (!QFileInfo::exists(path)) {
        return {};
    }
    QUrl url = QUrl::fromLocalFile(path);
    // A new screen under the same name must not come from QML's cache.
    url.setQuery(QStringLiteral("v=%1").arg(m_screens.value(id, QFileInfo(path).lastModified().toSecsSinceEpoch())));
    return url;
}

void Hosts::screenSaved(const QString &id)
{
    m_screens.insert(id, QDateTime::currentMSecsSinceEpoch());
    ++m_revision;
    Q_EMIT changed();
}

QStringList Hosts::colours() const
{
    return {QStringLiteral("#5fd39a"), QStringLiteral("#6ee0d0"), QStringLiteral("#8fb4ff"), QStringLiteral("#c78bff"),
            QStringLiteral("#ff9ac0"), QStringLiteral("#ff9a8a"), QStringLiteral("#ffc46a")};
}
