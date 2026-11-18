#include "remotedesktop.h"
#include "hosts.h"
#include "secrets.h"

#include "client.h"
#include "desktop.h"
#include "input.h"
#include "monitors.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

namespace
{
QByteArray arg(const QString &text)
{
    return text.toUtf8();
}

// What FreeRDP's names for an ending mean to the person.
QPair<QString, QString> explain(const QString &code, const QString &name, const QString &address, bool wasOpen)
{
    if (code.contains(QLatin1String("DISCONNECTED_BY_OTHERCONNECTION"))) {
        return {QCoreApplication::translate("RemoteDesktop", "%1 disconnected").arg(name),
                QCoreApplication::translate("RemoteDesktop", "Someone signed in at the PC itself or from another device, which ends a remote session. Your apps are still open there.")};
    }
    if (code.contains(QLatin1String("RPC_INITIATED_DISCONNECT"))) {
        return {QCoreApplication::translate("RemoteDesktop", "%1 disconnected").arg(name), QCoreApplication::translate("RemoteDesktop", "Someone on %1 ended the session.").arg(name)};
    }
    if (code.contains(QLatin1String("IDLE_TIMEOUT")) || code.contains(QLatin1String("LOGON_TIMEOUT"))) {
        return {QCoreApplication::translate("RemoteDesktop", "%1 disconnected").arg(name), QCoreApplication::translate("RemoteDesktop", "%1 ends sessions left idle for a while. Your apps are still open there.").arg(name)};
    }
    if (code.contains(QLatin1String("INSUFFICIENT_PRIVILEGES")) || code.contains(QLatin1String("ACCOUNT_RESTRICTION"))) {
        return {QCoreApplication::translate("RemoteDesktop", "Not allowed on %1").arg(name), QCoreApplication::translate("RemoteDesktop", "That account isn't allowed to use Remote Desktop on %1. Its administrator can add it to Remote Desktop Users.").arg(name)};
    }
    if (code.contains(QLatin1String("LOCKED_OUT"))) {
        return {QCoreApplication::translate("RemoteDesktop", "Account locked"), QCoreApplication::translate("RemoteDesktop", "%1 has locked that account after too many tries. Try again later, or ask its administrator.").arg(name)};
    }
    if (code.contains(QLatin1String("PASSWORD_EXPIRED")) || code.contains(QLatin1String("PASSWORD_MUST_CHANGE"))) {
        return {QCoreApplication::translate("RemoteDesktop", "Password needs changing"), QCoreApplication::translate("RemoteDesktop", "The account's password has expired. Sign in at the PC itself to set a new one.")};
    }
    if (code.contains(QLatin1String("DNS_NAME_NOT_FOUND"))) {
        return {QCoreApplication::translate("RemoteDesktop", "Couldn't find %1").arg(name), QCoreApplication::translate("RemoteDesktop", "There's no computer called %1 on this network. Check the address.").arg(address)};
    }
    if (code.contains(QLatin1String("CONNECT_CANCELLED"))) {
        return {};
    }
    if (code.contains(QLatin1String("TRANSPORT_FAILED")) || code.contains(QLatin1String("CONNECT_FAILED")) || code.contains(QLatin1String("TLS_CONNECT"))) {
        if (wasOpen) {
            return {QCoreApplication::translate("RemoteDesktop", "%1 disconnected").arg(name), QCoreApplication::translate("RemoteDesktop", "The connection was lost and didn't come back. Your session is still running on the PC, so reconnecting picks it up.")};
        }
        return {QCoreApplication::translate("RemoteDesktop", "Couldn't reach %1").arg(name),
                QCoreApplication::translate("RemoteDesktop", "Check %1 is switched on, that Remote Desktop is on in its settings, and that its firewall lets it through.").arg(name)};
    }
    if (code.isEmpty()) {
        return {};
    }
    return {QCoreApplication::translate("RemoteDesktop", "%1 disconnected").arg(name), QCoreApplication::translate("RemoteDesktop", "The connection ended (%1).").arg(code)};
}
}

RemoteDesktop::RemoteDesktop(const QVariantMap &host, const QString &display, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_display(display)
    , m_user(host.value(QStringLiteral("user")).toString())
{
    const QVariantMap d = host.value(QStringLiteral("desktop")).toMap();
    if (m_display.isEmpty()) {
        m_display = d.value(QStringLiteral("display"), QStringLiteral("full")).toString();
    }
    m_clipboard = d.value(QStringLiteral("clipboard"), true).toBool();
    for (const char *k : {"sound", "microphone", "printers", "folders"}) {
        m_sharing.insert(QString::fromLatin1(k), d.value(QString::fromLatin1(k)).toBool());
    }
    m_sharing.insert(QStringLiteral("clipboard"), m_clipboard);
    m_latencyTimer.setInterval(2000);
    connect(&m_latencyTimer, &QTimer::timeout, this, [this] {
        const int l = m_session ? m_session->latency() : 0;
        if (l != m_latency) {
            m_latency = l;
            Q_EMIT latencyChanged();
        }
    });
}

RemoteDesktop::~RemoteDesktop()
{
    if (m_session) {
        m_session->stop();
    }
}

QObject *RemoteDesktop::sessionObject() const
{
    return m_session;
}

QSize RemoteDesktop::desktopSize() const
{
    return m_session ? m_session->desktopSize() : QSize();
}

void RemoteDesktop::setState(const QString &state)
{
    if (state != m_state) {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void RemoteDesktop::setDisplay(const QString &display)
{
    if (display != m_display) {
        m_display = display;
        Q_EMIT displayChanged();
    }
}

QList<QByteArray> RemoteDesktop::arguments(const QString &password) const
{
    const QList<QByteArray> raw = m_host.value(QStringLiteral("arguments")).value<QList<QByteArray>>();
    if (!raw.isEmpty()) {
        return raw;
    }
    Hosts *hosts = Hosts::instance();
    const QVariantMap d = m_host.value(QStringLiteral("desktop")).toMap();
    QList<QByteArray> out;
    out << arg(QStringLiteral("/v:%1:%2").arg(hosts->hostName(m_host)).arg(hosts->port(m_host)));
    // DOMAIN\user or user@domain, as Windows' own client takes them.
    QString user = m_user;
    if (user.contains(QLatin1Char('\\'))) {
        out << arg(QStringLiteral("/d:") + user.section(QLatin1Char('\\'), 0, 0));
        user = user.section(QLatin1Char('\\'), 1);
    }
    if (!user.isEmpty()) {
        out << arg(QStringLiteral("/u:") + user);
    }
    if (!password.isEmpty()) {
        out << arg(QStringLiteral("/p:") + password);
    }
    out << (m_sharing.value(QStringLiteral("clipboard")).toBool() ? QByteArrayLiteral("+clipboard") : QByteArrayLiteral("-clipboard"));
    // Without sound here, Windows doesn't play it there either.
    out << (m_sharing.value(QStringLiteral("sound")).toBool() ? QByteArrayLiteral("/sound:sys:pulse") : QByteArrayLiteral("/audio-mode:2"));
    if (m_sharing.value(QStringLiteral("microphone")).toBool()) {
        out << QByteArrayLiteral("/microphone:sys:pulse");
    }
    if (m_sharing.value(QStringLiteral("printers")).toBool()) {
        out << QByteArrayLiteral("/printer");
    }
    if (m_sharing.value(QStringLiteral("folders")).toBool()) {
        QString path = d.value(QStringLiteral("folderPath")).toString();
        if (path.isEmpty() || !QFileInfo(path).isDir()) {
            path = QDir::homePath() + QStringLiteral("/Downloads");
        }
        // The folder's own name is what Windows shows it as.
        QString shown = QFileInfo(path).fileName().remove(QLatin1Char(','));
        out << arg(QStringLiteral("/drive:%1,%2").arg(shown.isEmpty() ? QStringLiteral("Wild OS") : shown, path));
    }
    const QString gateway = d.value(QStringLiteral("gateway")).toString();
    if (!gateway.isEmpty()) {
        out << arg(QStringLiteral("/gateway:g:") + gateway);
    }
    const QString speed = d.value(QStringLiteral("speed"), QStringLiteral("auto")).toString();
    out << arg(QStringLiteral("/network:") + (speed == QLatin1String("lan") ? QStringLiteral("lan")
                                              : speed == QLatin1String("broadband") ? QStringLiteral("broadband-high")
                                              : speed == QLatin1String("modem") ? QStringLiteral("modem")
                                                                                 : QStringLiteral("auto")));
    const QString resolution = d.value(QStringLiteral("resolution"), QStringLiteral("match")).toString();
    if (resolution != QLatin1String("match")) {
        out << arg(QStringLiteral("/size:") + resolution);
    } else {
        // A window's size follows the window.
        out << QByteArrayLiteral("/dynamic-resolution");
    }
    out << QByteArrayLiteral("+auto-reconnect") << QByteArrayLiteral("/log-level:WARN");
    return out;
}

void RemoteDesktop::start(const QString &password)
{
    if (m_session) {
        m_session->disconnect(this);
        m_session->stop();
        m_session->deleteLater();
    }
    m_session = new DesktopSession(this);
    m_attempt = 0;
    m_endTitle.clear();
    m_endText.clear();
    setState(QStringLiteral("connecting"));

    QString error;
    QList<QByteArray> args = arguments(password);
    if (!m_session->configure(args, &error)) {
        m_endTitle = tr("Couldn't connect to %1").arg(name());
        m_endText = error;
        setState(QStringLiteral("ended"));
        return;
    }
    // Laid out for the screen the window will be on, at that screen's
    // scale; a chosen resolution or scaling wins.
    QScreen *screen = QGuiApplication::primaryScreen();
    const QVariantMap d = m_host.value(QStringLiteral("desktop")).toMap();
    const QString scaling = d.value(QStringLiteral("scaling"), QStringLiteral("auto")).toString();
    const double dpr = screen ? screen->devicePixelRatio() : 1.0;
    const double scale = scaling == QLatin1String("auto") ? dpr : scaling.toDouble() / 100.0;
    if (m_display == QLatin1String("all") && QGuiApplication::screens().size() > 1) {
        // The primary screen first; each in its own pixels, beside the same
        // neighbour as here. Scaling each screen's place by its own scale
        // put screens of different scales on top of each other.
        QList<QScreen *> screens = QGuiApplication::screens();
        screens.removeOne(screen);
        screens.prepend(screen);
        QList<Monitor> layout;
        for (QScreen *s : std::as_const(screens)) {
            Monitor m;
            m.name = s->name();
            m.logical = s->geometry();
            m.scale = s->devicePixelRatio();
            layout.append(m);
        }
        layout = monitors::arrange(layout);
        // Each screen's part of the desktop picture, whose corner is the
        // top left of all of them.
        const QPoint corner = monitors::bounds(layout).topLeft();
        QList<QRect> rects;
        m_monitors.clear();
        for (const Monitor &m : std::as_const(layout)) {
            rects.append(m.rect);
            m_monitors.append(QVariantMap{{QStringLiteral("screen"), m.name}, {QStringLiteral("rect"), m.rect.translated(-corner)}});
        }
        m_session->setMonitors(rects, scale);
    } else if (screen && d.value(QStringLiteral("resolution"), QStringLiteral("match")).toString() == QLatin1String("match")) {
        QSize size = screen->size() * dpr;
        if (m_display == QLatin1String("window")) {
            // A window a little smaller than the screen, its title bar
            // above the picture.
            size = (screen->availableSize() * 0.8 - QSize(0, 40)) * dpr;
        }
        m_session->setDisplay(size, scale);
    }

    connect(m_session, &DesktopSession::certificateNeeded, this, [this](const QVariantMap &info, bool changed) {
        setState(QStringLiteral("certificate"));
        Q_EMIT certificateNeeded(info, changed);
    });
    connect(m_session, &DesktopSession::credentialsNeeded, this, [this](const QString &user, const QString &, const QString &reason) {
        setState(QStringLiteral("signin"));
        Q_EMIT credentialsNeeded(user.isEmpty() ? m_user : user, reason);
    });
    connect(m_session, &DesktopSession::connected, this, [this] {
        m_everConnected = true;
        m_attempt = 0;
        setState(QStringLiteral("connected"));
        m_latencyTimer.start();
        // Only now, signed in, does a typed password go to the keyring.
        if (m_savePassword && !m_pendingPassword.isEmpty()) {
            Secrets::instance()->write(Secrets::hostKey(hostId()), m_pendingPassword);
        }
        m_pendingPassword.fill(QLatin1Char('\0'));
        m_pendingPassword.clear();
        Hosts::instance()->touch(hostId());
        Q_EMIT opened();
    });
    connect(m_session, &DesktopSession::reconnecting, this, [this](int attempt, int) {
        m_attempt = attempt;
        setState(QStringLiteral("reconnecting"));
    });
    connect(m_session, &DesktopSession::reconnected, this, [this] {
        m_attempt = 0;
        setState(QStringLiteral("connected"));
    });
    connect(m_session, &DesktopSession::frameChanged, this, [this] {
        if (desktopSize() != m_lastFrame.size()) {
            Q_EMIT desktopSizeChanged();
        }
    });
    connect(m_session, &DesktopSession::finished, this, &RemoteDesktop::finished);
    m_session->start();
}

void RemoteDesktop::finished(const QString &code, bool authFailed)
{
    m_latencyTimer.stop();
    if (m_everConnected) {
        saveScreen();
    }
    if (authFailed && !m_everConnected) {
        // Asked again on a new connection; a saved password that no longer
        // works is forgotten.
        Secrets::instance()->remove(Secrets::hostKey(hostId()));
        setState(QStringLiteral("signin"));
        Q_EMIT retryWanted(m_user);
        return;
    }
    const auto [title, text] = explain(code, name(), address(), m_everConnected);
    m_endTitle = title;
    m_endText = text;
    const bool unexpected = !title.isEmpty();
    setState(QStringLiteral("ended"));
    if (unexpected && m_everConnected) {
        Q_EMIT lost();
    }
}

void RemoteDesktop::answerCertificate(bool accept, bool remember)
{
    if (m_session) {
        setState(QStringLiteral("connecting"));
        m_session->answerCertificate(accept, remember);
    }
}

void RemoteDesktop::signIn(const QString &user, const QString &password, bool save)
{
    m_user = user;
    m_savePassword = save;
    m_pendingPassword = password;
    if (save) {
        QVariantMap h = Hosts::instance()->host(hostId());
        if (!h.isEmpty()) {
            QVariantMap d = h.value(QStringLiteral("desktop")).toMap();
            d.insert(QStringLiteral("password"), QStringLiteral("keyring"));
            h.insert(QStringLiteral("desktop"), d);
            h.insert(QStringLiteral("user"), user);
            Hosts::instance()->save(h);
        }
    }
    setState(QStringLiteral("connecting"));
    if (m_session && m_session->client()->running()) {
        QString domain;
        QString name = user;
        if (user.contains(QLatin1Char('\\'))) {
            domain = user.section(QLatin1Char('\\'), 0, 0);
            name = user.section(QLatin1Char('\\'), 1);
        }
        m_session->answerCredentials(true, name, domain, password);
    } else {
        start(password);
    }
}

void RemoteDesktop::cancelSignIn()
{
    if (m_session) {
        m_session->answerCredentials(false, {}, {}, {});
        m_session->stop();
    }
    m_endTitle.clear();
    setState(QStringLiteral("ended"));
}

void RemoteDesktop::tryNow()
{
    if (m_session) {
        m_session->tryNow();
    }
}

void RemoteDesktop::stopTrying()
{
    if (m_session) {
        m_session->stopTrying();
    }
}

void RemoteDesktop::disconnectNow()
{
    if (m_everConnected) {
        saveScreen();
    }
    if (m_session) {
        m_session->stop();
    }
}

void RemoteDesktop::sendKeys(const QString &which)
{
    if (!m_session) {
        return;
    }
    Client *c = m_session->client();
    using namespace rdpinput;
    if (which == QLatin1String("ctrl-alt-del")) {
        chord(c, {ctrl, alt, del});
    } else if (which == QLatin1String("windows")) {
        chord(c, {windows});
    } else if (which == QLatin1String("print")) {
        chord(c, {printScreen});
    } else if (which == QLatin1String("alt-tab")) {
        chord(c, {alt, tab});
    }
}

void RemoteDesktop::setClipboard(bool on)
{
    setSharing(QStringLiteral("clipboard"), on);
}

void RemoteDesktop::setSharing(const QString &key, bool on)
{
    if (m_sharing.value(key).toBool() == on) {
        return;
    }
    m_sharing.insert(key, on);
    if (key == QLatin1String("clipboard")) {
        m_clipboard = on;
    }
    // RDP agrees what's shared as it connects: the change waits for the
    // next connection, and the host keeps it for then.
    m_sharingPending = true;
    QVariantMap h = Hosts::instance()->host(hostId());
    if (!h.isEmpty()) {
        QVariantMap d = h.value(QStringLiteral("desktop")).toMap();
        d.insert(key, on);
        h.insert(QStringLiteral("desktop"), d);
        Hosts::instance()->save(h);
    }
    Q_EMIT sharingChanged();
}

void RemoteDesktop::setSendShortcuts(bool on)
{
    if (on != m_sendShortcuts) {
        m_sendShortcuts = on;
        Q_EMIT sendShortcutsChanged();
    }
}

void RemoteDesktop::saveScreen()
{
    if (!m_session || hostId().isEmpty()) {
        return;
    }
    const QImage frame = m_session->frame();
    if (frame.isNull() || frame.width() < 64) {
        return;
    }
    m_lastFrame = frame;
    const QString path = Hosts::instance()->screenPath(hostId());
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (frame.scaledToWidth(720, Qt::SmoothTransformation).save(path)) {
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        Hosts::instance()->screenSaved(hostId());
    }
}

Desktops *Desktops::instance()
{
    static Desktops *d = new Desktops;
    return d;
}

Desktops *Desktops::create(QQmlEngine *, QJSEngine *)
{
    Desktops *d = instance();
    QJSEngine::setObjectOwnership(d, QJSEngine::CppOwnership);
    return d;
}

Desktops::Desktops(QObject *parent)
    : QObject(parent)
{
}

RemoteDesktop *Desktops::start(const QVariantMap &host, const QString &display)
{
    auto *desktop = new RemoteDesktop(host, display, this);
    QJSEngine::setObjectOwnership(desktop, QJSEngine::CppOwnership);
    m_open.append(desktop);
    Q_EMIT started(desktop);
    const bool keyring = host.value(QStringLiteral("desktop")).toMap().value(QStringLiteral("password")).toString() == QLatin1String("keyring");
    if (keyring && !host.value(QStringLiteral("id")).toString().isEmpty()) {
        Secrets::instance()->read(Secrets::hostKey(host.value(QStringLiteral("id")).toString()), [desktop = QPointer(desktop)](const QString &secret, bool) {
            if (desktop) {
                desktop->start(secret);
            }
        });
    } else {
        desktop->start(QString());
    }
    return desktop;
}

RemoteDesktop *Desktops::startWithArguments(const QString &title, const QList<QByteArray> &args)
{
    QVariantMap host;
    host.insert(QStringLiteral("name"), title);
    for (const QByteArray &a : args) {
        if (a.startsWith("/v:")) {
            host.insert(QStringLiteral("address"), QString::fromUtf8(a.mid(3)));
        } else if (a.startsWith("/u:")) {
            host.insert(QStringLiteral("user"), QString::fromUtf8(a.mid(3)));
        }
    }
    host.insert(QStringLiteral("arguments"), QVariant::fromValue(args));
    auto *desktop = new RemoteDesktop(host, QStringLiteral("window"), this);
    QJSEngine::setObjectOwnership(desktop, QJSEngine::CppOwnership);
    m_open.append(desktop);
    Q_EMIT started(desktop);
    desktop->start(QString());
    return desktop;
}

bool Desktops::resizeTo(RemoteDesktop *desktop, const QSize &physical, double scale)
{
    return desktop && desktop->session() && desktop->session()->resize(physical, scale);
}

void Desktops::closeAll()
{
    // Each one's connection thread is joined as it goes, before anything it
    // uses is torn down with the process.
    const auto open = std::exchange(m_open, {});
    for (const QPointer<RemoteDesktop> &d : open) {
        delete d.data();
    }
}
