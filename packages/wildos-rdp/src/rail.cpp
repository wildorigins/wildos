#include "rail.h"
#include "railwindow.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <freerdp/rail.h>
#include <freerdp/window.h>

#include <cmath>

namespace
{
constexpr quint32 stateHidden = 0x00;
constexpr quint32 stateMinimized = 0x02;
constexpr quint32 stateMaximized = 0x03;
// How long a loading window waits for its app before giving up. Board 9.13
// takes over after 20 s with a way out; this only catches a launch the agent
// forgot.
constexpr int loadingTimeoutMs = 10 * 60 * 1000;
// How long a new window waits for the agent to say which app it is,
// so it is shown once, in the right taskbar group.
constexpr int appIdWaitMs = 400;
// How long a new window waits for Windows to say which program it belongs
// to before a loading window is matched to it by name alone.
constexpr int processWaitMs = 1000;
// How long another window of the program a loading window waits for may
// be on screen before the loading window gives way to it.
constexpr int stepAsideMs = 1500;
// How long after a launch is cancelled its app's window is still closed
// as it comes: Windows may have been waking when it was.
constexpr int cancelledForMs = 120 * 1000;
// How long after Windows first sends windows the client waits to hear that
// its desktop sync is over.
constexpr int syncWaitMs = 4000;
// After our close: when to say a window with unsaved changes has not
// closed, and when a stuck one with nothing unsaved is ended; how long a
// dropped connection may try to come back before its windows leave the
// screen.
constexpr int closeQuietMs = 5000;
// The close's grace period: a window Windows has neither closed nor drawn
// in again after this leaves the screen (and the taskbar) while Windows is
// still dealing with it. One Windows draws in again stays up a little
// longer, to see whether it closes or is asking something (Windows 11's
// Paint asks inside its own window after about 0.6 s). Either way a normal
// close is over before the first deadline, and a question never flickers.
// From the timings measured with WILDOS_RDP_CLOSE_TIMING (see the log).
constexpr int closeGraceMs = 500;
// Programs with nothing to lose: they never ask before closing, so their
// windows leave the screen the moment they are closed. Explorer redraws a
// folder window as it closes and can take seconds to end it, which would
// otherwise read as a question and keep the window up meanwhile.
const QSet<QString> neverAsk = {
    QStringLiteral("explorer.exe"), QStringLiteral("charmap.exe"), QStringLiteral("calculatorapp.exe"),
    QStringLiteral("calc.exe"), QStringLiteral("systemsettings.exe"), QStringLiteral("taskmgr.exe"),
    QStringLiteral("control.exe"), QStringLiteral("regedit.exe"), QStringLiteral("magnify.exe"),
    QStringLiteral("osk.exe"), QStringLiteral("snippingtool.exe"),
};
constexpr int closeAskingMs = 1200;
constexpr int closeEndMs = 10000;
constexpr int lostHideMs = 2000;
// A Windows window nothing is known about. It must match no desktop entry:
// "wildos-winmode" would give it Windows mode's own icon, the W, where the
// icon Windows sends for the window belongs.
const QString genericAppId = QStringLiteral("windows-app");

// A program's file name in lower case, from a full Windows path or not.
QString exeOf(const QString &path)
{
    return path.section(u'\\', -1).section(u'/', -1).toLower();
}

// Menus, tooltips and other windows that belong to another and never take
// the keyboard, told apart from apps and dialogs by their styles.
bool isPopup(quint32 style, quint32 exStyle)
{
    const bool captioned = (style & WS_CAPTION) == WS_CAPTION;
    const bool appLike = captioned || (style & (WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) || (exStyle & WS_EX_APPWINDOW);
    const bool toolOrPopup = (exStyle & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) || (style & WS_POPUP);
    const bool noActivate = (exStyle & WS_EX_NOACTIVATE) && !(exStyle & WS_EX_APPWINDOW);
    return noActivate || (toolOrPopup && !appLike);
}

// A window that could be a full screen one: no title bar, no edge to size
// it by and not maximised (a video, a game, a remote desktop viewer), and
// one that takes clicks and the keyboard. Covering a whole monitor is what
// makes it full screen.
bool fullScreenStyle(quint32 style, quint32 exStyle)
{
    const bool captioned = (style & WS_CAPTION) == WS_CAPTION;
    const bool clickThrough = (exStyle & WS_EX_TRANSPARENT) && (exStyle & WS_EX_LAYERED);
    return !captioned && !(style & (WS_THICKFRAME | WS_MAXIMIZE | WS_CHILD)) && !(exStyle & WS_EX_NOACTIVATE) && !clickThrough;
}

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/wildos/windows-mode-windows.ini");
}

QSize rememberedSize(const QString &appId)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    const QSize size = s.value(appId + QStringLiteral("/size")).toSize();
    if (size.isValid() && size.width() >= 200 && size.height() >= 150) {
        return size;
    }
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QSize avail = screen ? screen->availableGeometry().size() : QSize(1280, 800);
    return QSize(std::min(1100, avail.width() * 7 / 10), std::min(720, avail.height() * 7 / 10));
}

void rememberSize(const QString &appId, const QSize &size)
{
    if (appId.isEmpty() || appId == genericAppId || !size.isValid()) {
        return;
    }
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(appId + QStringLiteral("/size"), size);
}

// Shows a window with the app id it belongs to; Qt reads the id from the
// application when the window is shown.
void showAs(RailWindow *window, quint32 state)
{
    const QString saved = QGuiApplication::desktopFileName();
    QGuiApplication::setDesktopFileName(window->appId());
    if (window->remoteFullScreen() && state != stateMinimized) {
        window->showFullScreen();
    } else if (state == stateMaximized) {
        window->showMaximized();
    } else if (state == stateMinimized) {
        window->showMinimized();
    } else {
        window->show();
    }
    QGuiApplication::setDesktopFileName(saved);
}
}

Rail::Rail(Client *client, double scale, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_scale(scale)
    , m_cursor(Qt::ArrowCursor)
{
    connect(client, &Client::windowChanged, this, &Rail::order);
    connect(client, &Client::desktopSynced, this, &Rail::desktopSynced);
    connect(client, &Client::windowDeleted, this, &Rail::deleted);
    connect(client, &Client::windowIcon, this, &Rail::icon);
    connect(client, &Client::desktopChanged, this, &Rail::desktop);
    connect(client, &Client::surfaceDamaged, this, &Rail::surface);
    connect(client, &Client::primaryDamaged, this, &Rail::primary);
    connect(client, &Client::localMoveSize, this, &Rail::moveSize);
    connect(client, &Client::minMaxInfo, this, &Rail::minMax);
    connect(client, &Client::windowCloaked, this, &Rail::cloak);
    connect(client, &Client::windowProcess, this, &Rail::process);
    connect(client, &Client::connectionLost, this, &Rail::connectionLost);
    connect(client, &Client::connectionBack, this, &Rail::connectionBack);
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                          QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("ActionInvoked"), this,
                                          SLOT(notificationAction(uint, QString)));
    connect(client, &Client::pointerImage, this, [this](const QImage &image, const QPoint &hotspot) {
        m_pointerImage = image;
        m_pointerHotspot = hotspot;
        setPointer();
    });
    connect(client, &Client::pointerHidden, this, [this] {
        m_pointerImage = QImage();
        setCursorAll(QCursor(Qt::BlankCursor));
    });
    connect(client, &Client::pointerDefault, this, [this] {
        m_pointerImage = QImage();
        setCursorAll(QCursor(Qt::ArrowCursor));
    });
    connect(client, &Client::railStarted, this, [this] {
        m_railStarted = true;
        sendWorkAreas();
    });
    connect(client, &Client::displayControlReady, this, [this] {
        // A screen changed while Windows could not be told, or the
        // connection came back with the layout it started with.
        if (!m_wanted.isEmpty() && !monitors::same(m_wanted, m_layout)) {
            relayout(m_wanted);
        }
    });
}

void Rail::setPointer()
{
    if (m_pointerImage.isNull()) {
        return;
    }
    // Windows draws the pointer for the monitor it is on there, which is
    // the monitor of the window it is over.
    const double scale = m_pointerScale > 0 ? m_pointerScale : m_scale;
    QPixmap pixmap = QPixmap::fromImage(m_pointerImage);
    pixmap.setDevicePixelRatio(scale);
    setCursorAll(QCursor(pixmap, int(std::lround(m_pointerHotspot.x() / scale)), int(std::lround(m_pointerHotspot.y() / scale))));
}

void Rail::pointerOver(double scale)
{
    if (std::abs(scale - m_pointerScale) < 0.01) {
        return;
    }
    m_pointerScale = scale;
    setPointer();
}

bool Rail::layoutCurrent() const
{
    if (!m_wanted.isEmpty() && !monitors::same(m_wanted, m_layout)) {
        return false;
    }
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.size() != m_layout.size()) {
        return false;
    }
    for (const QScreen *s : screens) {
        if (monitors::named(m_layout, s->name()) < 0) {
            return false;
        }
    }
    return true;
}

int Rail::monitorFor(const QScreen *screen) const
{
    if (!screen) {
        return m_layout.isEmpty() ? -1 : 0;
    }
    return monitors::named(m_layout, screen->name());
}

double Rail::scaleFor(const QRect &frame) const
{
    const int i = monitors::at(m_layout, frame);
    return i >= 0 ? m_layout[i].scale : m_scale;
}

double Rail::scaleForScreen(const QScreen *screen) const
{
    const int i = monitorFor(screen);
    return i >= 0 ? m_layout[i].scale : m_scale;
}

void Rail::setWorkAreas(const QHash<QString, QRectF> &areas)
{
    m_workAreas = areas;
    sendWorkAreas();
}

QRect Rail::workAreaFor(int monitor) const
{
    if (monitor < 0 || monitor >= m_layout.size() || !layoutCurrent()) {
        return {};
    }
    const auto it = m_workAreas.constFind(m_layout[monitor].name);
    return it == m_workAreas.cend() ? QRect() : monitors::workArea(m_layout[monitor], *it);
}

void Rail::sendWorkAreas()
{
    if (!m_railStarted) {
        return;
    }
    // One per monitor: Windows takes each for the monitor it is on. A
    // monitor KWin has not described keeps the whole of itself.
    QStringList sent;
    for (int i = 0; i < m_layout.size(); ++i) {
        const QRect area = workAreaFor(i);
        if (area.isValid()) {
            m_client->setWorkArea(area);
            sent.append(QStringLiteral("%1 %2x%3+%4+%5").arg(m_layout[i].name).arg(area.width()).arg(area.height()).arg(area.x()).arg(area.y()));
        }
    }
    if (!sent.isEmpty()) {
        qInfo("wildos-rdp: Windows' work area now %s", qPrintable(sent.join(QStringLiteral(", "))));
    }
}

void Rail::relayout(const QList<Monitor> &layout)
{
    m_wanted = layout;
    if (layout.isEmpty()) {
        return;
    }
    if (monitors::same(layout, m_layout)) {
        // Nothing Windows would notice; a screen swapped for one just like
        // it still has its own name here.
        m_layout = layout;
        return;
    }
    if (!m_client->sendLayout(layout)) {
        qInfo("wildos-rdp: the screens changed (%s); Windows is told when it can take a new layout", qPrintable(monitors::describe(layout)));
        return;
    }
    qInfo("wildos-rdp: Windows' monitors now %s", qPrintable(monitors::describe(layout)));
    m_layout = layout;
    // Windows moves the windows of a monitor that went onto one that is
    // left; each then goes to the monitor of the screen it is on here.
    QTimer::singleShot(1500, this, [this] {
        // Windows may reset each monitor's work area for a new layout.
        sendWorkAreas();
        for (RailWindow *w : std::as_const(m_windows)) {
            w->followScreen();
        }
    });
}

Rail::~Rail()
{
    closeAll();
}

void Rail::setCursorAll(const QCursor &cursor)
{
    m_cursor = cursor;
    for (RailWindow *w : std::as_const(m_windows)) {
        w->setCursor(cursor);
    }
}

quint32 Rail::prepare(const QString &appId, const QString &title, const QString &icon, const QString &program, int delayMs)
{
    const quint32 token = m_nextToken++;
    auto *window = new RailWindow(this, m_client, appId.isEmpty() ? genericAppId : appId);
    window->setProperty("token", token);
    const QIcon themed = icon.startsWith(u'/') ? QIcon(icon) : QIcon::fromTheme(icon);
    if (!themed.isNull()) {
        window->QWindow::setIcon(themed);
    }
    window->startLoading(title, themed, rememberedSize(appId));
    window->setLoadingReason(QStringLiteral("Windows mode is taking longer than usual to start. %1 will open as soon as it's ready.").arg(title), -1, {});
    Placeholder p;
    p.window = window;
    p.title = title;
    p.program = exeOf(program);
    p.appId = window->appId();
    p.icon = themed;
    if (!p.program.isEmpty() && p.appId != genericAppId) {
        // Every window this program opens belongs to the app, whether the
        // loading window takes it or not.
        m_programApps.insert(p.program, p.appId);
        m_programIcons.insert(p.program, themed);
    }
    p.age.start();
    p.timeout = new QTimer(this);
    p.timeout->setSingleShot(true);
    connect(p.timeout, &QTimer::timeout, this, [this, token] { loadingTimedOut(token); });
    p.timeout->start(loadingTimeoutMs);
    m_placeholders.insert(token, p);
    if (delayMs <= 0) {
        showAs(window, 0x05);
    } else {
        // Board 9.12: an app that opens this quickly opens without the
        // loader at all.
        QPointer<RailWindow> guard(window);
        QTimer::singleShot(delayMs, this, [guard] {
            if (guard && guard->loading() && !guard->isVisible()) {
                showAs(guard, 0x05);
            }
        });
    }
    return token;
}

void Rail::setCold(quint32 token)
{
    if (auto it = m_placeholders.find(token); it != m_placeholders.end()) {
        it->cold = true;
    }
}

void Rail::setProgram(quint32 token, const QString &program)
{
    auto it = m_placeholders.find(token);
    if (it == m_placeholders.end() || program.isEmpty()) {
        return;
    }
    it->program = exeOf(program);
    if (it->appId != genericAppId) {
        m_programApps.insert(it->program, it->appId);
        m_programIcons.insert(it->program, it->icon);
    }
}

bool Rail::update(quint32 token, const QString &stage, const QString &reason, int percent, const QString &progress)
{
    if (token == 0) {
        // The connection's own loading window, which its caller has no
        // token for (it was made from the command line).
        for (auto p = m_placeholders.cbegin(); p != m_placeholders.cend(); ++p) {
            if (p->cold) {
                token = p.key();
            }
        }
    }
    const auto it = m_placeholders.constFind(token);
    RailWindow *w = it != m_placeholders.cend() ? it->window.data() : m_loadingByToken.value(token).data();
    if (!w || !w->loading()) {
        return false;
    }
    if (stage == QLatin1String("almost")) {
        w->setLoadingStage(LoadingView::Stage::AlmostReady);
    } else if (stage == QLatin1String("ended")) {
        qInfo("wildos-rdp: loading window %u: the app ended before it opened", token);
        w->setLoadingEnded();
        return true;
    }
    if (!reason.isEmpty()) {
        w->setLoadingReason(reason, percent, progress);
    }
    return true;
}

void Rail::loadingShown(quint32 token)
{
    auto it = m_placeholders.find(token);
    if (it != m_placeholders.end() && it->age.isValid()) {
        Q_EMIT loadingVisible(token, it->age.elapsed());
        it->age.invalidate();
    }
}

bool Rail::cancel(quint32 token, const QString &reason)
{
    auto it = m_placeholders.find(token);
    if (it == m_placeholders.end()) {
        return false;
    }
    Placeholder p = *it;
    m_placeholders.erase(it);
    delete p.timeout;
    if (p.window) {
        p.window->hide();
        p.window->deleteLater();
    }
    if (reason == QLatin1String("cancelled") && !p.program.isEmpty()) {
        // Windows may well start the app anyway; its window is closed as it
        // comes rather than appear after the person said no.
        m_cancelled[p.program].append(QDateTime::currentMSecsSinceEpoch() + cancelledForMs);
    }
    Q_EMIT loadingFinished(token, reason, 0);
    return true;
}

bool Rail::takeCancelled(const QString &program)
{
    auto it = m_cancelled.find(program);
    if (program.isEmpty() || it == m_cancelled.end()) {
        return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    it->removeIf([now](qint64 until) { return until < now; });
    const bool taken = !it->isEmpty();
    if (taken) {
        it->removeFirst();
    }
    if (it->isEmpty()) {
        m_cancelled.erase(it);
    }
    return taken;
}

void Rail::loadingTimedOut(quint32 token)
{
    cancel(token, QStringLiteral("timeout"));
}

void Rail::closeAll()
{
    const QList<quint32> tokens = m_placeholders.keys();
    for (quint32 token : tokens) {
        cancel(token, QStringLiteral("ended"));
    }
    const QList<quint32> ids = m_windows.keys();
    for (quint32 id : ids) {
        deleted(id);
    }
    m_pending.clear();
}

RailWindow *Rail::takePlaceholder(quint32 id, const QString &title, bool early)
{
    if (m_placeholders.isEmpty()) {
        return nullptr;
    }
    // The loading window for the program the new window belongs to, when
    // Windows has said which that is; a window of another program is never
    // taken for it. Otherwise the one whose app the title names, or the one
    // waiting longest.
    QList<quint32> tokens = m_placeholders.keys();
    std::sort(tokens.begin(), tokens.end());
    if (early) {
        tokens.removeIf([this](quint32 token) { return m_placeholders.value(token).cold; });
    }
    const QString exe = m_processes.value(id);
    quint32 best = 0;
    if (!exe.isEmpty()) {
        for (quint32 token : tokens) {
            if (m_placeholders.value(token).program == exe) {
                best = token;
                break;
            }
        }
    }
    if (!best) {
        // Another program can own the app's window (a packaged app's frame
        // host, a launcher that starts the real program); a title naming
        // the app is enough then.
        for (quint32 token : tokens) {
            const QString t = m_placeholders.value(token).title;
            if (!t.isEmpty() && title.contains(t, Qt::CaseInsensitive)) {
                best = token;
                break;
            }
        }
    }
    if (!best) {
        // Otherwise only loading windows that do not know their program may
        // still be this window's. One that does is never given a window
        // whose program Windows has not named: on reconnecting, a window
        // another app left open would take it.
        tokens.removeIf([this](quint32 token) { return !m_placeholders.value(token).program.isEmpty(); });
    }
    if (!best && !tokens.isEmpty()) {
        best = tokens.first();
    }
    if (!best) {
        return nullptr;
    }
    Placeholder p = m_placeholders.take(best);
    delete p.timeout;
    p.timeout = nullptr;
    if (!p.window) {
        return nullptr;
    }
    m_adopted.insert(best, p);
    return p.window;
}

QString Rail::appIdFor(quint32 id) const
{
    if (const QString app = m_apps.value(id); !app.isEmpty()) {
        return app;
    }
    const auto it = m_pending.constFind(id);
    if (it != m_pending.cend() && it->state.owner) {
        if (RailWindow *owner = m_windows.value(it->state.owner)) {
            return owner->appId();
        }
    }
    const QString exe = m_processes.value(id);
    if (const QString app = m_programApps.value(exe); !app.isEmpty()) {
        return app;
    }
    // A program nothing is known about still groups its own windows.
    if (!exe.isEmpty()) {
        return QStringLiteral("windows-") + exe.section(u'.', 0, 0);
    }
    return genericAppId;
}

bool Rail::ownedByShownWindow(const RailOrder &s) const
{
    RailWindow *owner = m_windows.value(s.owner);
    return owner && owner->isVisible();
}

RailWindow *Rail::parentFor(quint32 id) const
{
    const RailOrder &s = m_pending.value(id).state;
    if (RailWindow *owner = m_windows.value(s.owner); owner && !owner->isPopup()) {
        return owner;
    }
    // A menu often names no owner: it belongs to the window it opens over,
    // the topmost one first.
    const QPoint origin(s.offset);
    for (quint32 z : m_zOrder) {
        RailWindow *w = m_windows.value(z);
        if (w && !w->isPopup() && w->isVisible() && w->frame().contains(origin)) {
            return w;
        }
    }
    for (RailWindow *w : m_windows) {
        if (!w->isPopup() && w->isVisible() && w->frame().contains(origin)) {
            return w;
        }
    }
    if (RailWindow *active = m_windows.value(m_active); active && !active->isPopup() && active->isVisible()) {
        return active;
    }
    for (RailWindow *w : m_windows) {
        if (!w->isPopup() && w->isVisible()) {
            return w;
        }
    }
    return nullptr;
}

void Rail::desktopSynced()
{
    // Only the first sync counts: after a reconnection the windows Windows
    // sends again are known already, and a new one can be the window of an
    // app opened while the connection was down.
    m_synced = true;
}

void Rail::order(const RailOrder &o)
{
    if (!m_synced && !m_syncWaiting) {
        // A Windows that never says its sync is over is not waited for long.
        m_syncWaiting = true;
        QTimer::singleShot(syncWaitMs, this, &Rail::desktopSynced);
    }
    Pending &p = m_pending[o.id];
    RailOrder &s = p.state;
    if (o.created && !m_synced && !m_windows.contains(o.id)) {
        m_preexisting.insert(o.id);
    }
    if (o.created) {
        s = RailOrder();
        s.id = o.id;
        s.showState = 0x05;
    }
    const quint32 f = o.fields;
    if (f & WINDOW_ORDER_FIELD_OWNER) {
        s.owner = o.owner;
    }
    if (f & WINDOW_ORDER_FIELD_STYLE) {
        s.style = o.style;
        s.exStyle = o.exStyle;
    }
    if (f & WINDOW_ORDER_FIELD_SHOW) {
        s.showState = o.showState;
    }
    if (f & WINDOW_ORDER_FIELD_TITLE) {
        s.title = o.title;
    }
    if (f & WINDOW_ORDER_FIELD_WND_OFFSET) {
        s.offset = o.offset;
    }
    if (f & WINDOW_ORDER_FIELD_WND_SIZE) {
        s.size = o.size;
    }
    if (f & (WINDOW_ORDER_FIELD_RESIZE_MARGIN_X | WINDOW_ORDER_FIELD_RESIZE_MARGIN_Y)) {
        s.resizeMargins = o.resizeMargins;
    }
    if (f & WINDOW_ORDER_FIELD_VISIBILITY) {
        s.visibility = o.visibility;
    }
    s.fields |= f;

    if (qEnvironmentVariableIsSet("WILDOS_RDP_DEBUG")) {
        qInfo("window %08x%s fields=%08x style=%08x ex=%08x owner=%08x show=%u rect=%d,%d %dx%d title=%s", o.id, o.created ? " new" : "", f,
              s.style, s.exStyle, s.owner, s.showState, s.offset.x(), s.offset.y(), s.size.width(), s.size.height(), qPrintable(s.title));
    }

    RailWindow *w = m_windows.value(o.id);
    const bool visible = s.showState != stateHidden && s.size.width() > 0 && s.size.height() > 0;
    if (o.created) {
        log(o.id, QStringLiteral("created \"%1\" show %2").arg(s.title).arg(s.showState));
    }
    if (!w) {
        if (visible) {
            realize(o.id);
        }
        return;
    }
    if (f & WINDOW_ORDER_FIELD_TITLE) {
        w->setRemoteTitle(s.title);
    }
    if (f & (WINDOW_ORDER_FIELD_WND_OFFSET | WINDOW_ORDER_FIELD_WND_SIZE)) {
        w->setFrame(QRect(s.offset, s.size));
        if (w->isPopup()) {
            if (RailWindow *parent = qobject_cast<RailWindow *>(w->transientParent())) {
                // Measured on the parent's monitor, where the parent is.
                const double scale = parent->scale();
                const QPoint delta = s.offset - parent->frame().topLeft();
                w->setPosition(parent->position() + QPoint(int(std::lround(delta.x() / scale)), int(std::lround(delta.y() / scale))));
            }
        }
    }
    if ((f & (WINDOW_ORDER_FIELD_STYLE | WINDOW_ORDER_FIELD_WND_OFFSET | WINDOW_ORDER_FIELD_WND_SIZE)) && !w->loading() && !m_closing.contains(o.id)) {
        updateFullScreen(w, s);
    }
    if ((f & WINDOW_ORDER_FIELD_STYLE) && (s.style & WS_DISABLED)) {
        if (auto it = m_closing.find(o.id); it != m_closing.end() && !it->disabled) {
            // Disabled by Windows after the close: a modal question is up.
            it->disabled = true;
            log(o.id, QStringLiteral("disabled %1 ms after its close: asking").arg(it->since.elapsed()));
            if (it->hidden && !it->unwanted) {
                reopen(o.id);
            }
        }
    }
    if ((f & WINDOW_ORDER_FIELD_SHOW) && m_closing.contains(o.id)) {
        // Closed here already: it stays off screen whatever Windows says
        // about it, until it goes or the close turns out not to happen.
        w->setShowState(s.showState);
        return;
    }
    if ((f & WINDOW_ORDER_FIELD_SHOW) && w->loading()) {
        // The window a loading window took is being hidden or minimised
        // before the app ever showed in it: not the app's window after all.
        if (s.showState == stateHidden || s.showState == stateMinimized) {
            giveBack(w);
            return;
        }
        w->setShowState(s.showState);
        return;
    }
    if (f & WINDOW_ORDER_FIELD_SHOW) {
        const quint32 before = w->remoteMinimized() ? stateMinimized : 0;
        w->setShowState(s.showState);
        if (s.showState == stateHidden) {
            w->hide();
        } else if (s.showState == stateMinimized) {
            if (!w->isPopup()) {
                w->showMinimized();
            }
        } else if (s.showState == stateMaximized) {
            if (!w->isPopup() && !(w->windowStates() & Qt::WindowMaximized)) {
                w->showMaximized();
            }
        } else {
            if (!w->isVisible()) {
                showAs(w, s.showState);
            } else if (!w->isPopup() && (w->windowStates() & Qt::WindowMaximized) && !w->localMaximisePending()) {
                // A window maximised here that Windows has not maximised
                // yet is not restored by the state Windows still has.
                w->showNormal();
                w->resize(w->logicalSize(s.size));
            } else if (before == stateMinimized) {
                w->requestActivate();
            }
        }
    }
}

void Rail::realize(quint32 id)
{
    Pending &p = m_pending[id];
    const RailOrder &s = p.state;
    if (m_cloaked.contains(id) || m_deciding.contains(id)) {
        return;
    }
    // A full screen window is never a popup, whatever its styles: a popup
    // here stays above every other window, Wild OS's own included, and
    // cannot fill the screen (issue 92: LogMeIn's full screen view).
    const int fullMonitor = fullScreenMonitor(s);
    const bool popup = fullMonitor < 0 && isPopup(s.style, s.exStyle);
    const QRect rect(s.offset, s.size);
    // Windows 11 draws a window's drop shadow as see-through windows round
    // it; the compositor draws shadows here. A see-through window over an
    // app window is content (a XAML island, an overlay) and is shown.
    if ((s.exStyle & WS_EX_LAYERED) && popup && !overlapsAppWindow(rect)) {
        log(id, QStringLiteral("not shown: see-through, beside an app window rather than over one"));
        return;
    }
    RailWindow *parent = nullptr;
    if (popup) {
        parent = parentFor(id);
        if (!parent) {
            log(id, QStringLiteral("waiting: a popup with no window to belong to yet"));
            return;
        }
    }
    // An app's main window is often owned by a hidden window of its own;
    // it is still the app's window, not a dialog of another.
    const bool topLevel = !popup && !ownedByShownWindow(s);
    const bool appSized = s.size.width() >= 160 && s.size.height() >= 100;
    const bool shown = s.showState == 0x05 || s.showState == stateMaximized;
    // A dialog a closing window opens (Save changes?) is its question: the
    // window stays for it. Popups of a window already hidden belong to
    // nothing on screen.
    quint32 closingOwner = 0;
    if (m_closing.contains(s.owner)) {
        if (popup && m_closing.value(s.owner).hidden) {
            return;
        }
        if (!popup) {
            closingOwner = s.owner;
            m_closing[s.owner].dialogs.insert(id);
            log(id, QStringLiteral("opened by a window that is closing; shown for an answer"));
            if (m_closing.value(s.owner).hidden) {
                reopen(s.owner);
            }
        }
    }
    RailWindow *w = nullptr;
    bool adopted = false;
    const bool early = m_preexisting.contains(id);
    if (topLevel && appSized && shown && !closingOwner && fullMonitor < 0 && (!m_placeholders.isEmpty() || !m_cancelled.isEmpty())) {
        if (!m_processes.contains(id) && !m_askedProcess.contains(id)) {
            // Which program this is decides which loading window it takes;
            // Windows answers within a few milliseconds.
            m_askedProcess.insert(id);
            m_deciding.insert(id);
            m_client->requestProcess(id);
            QTimer::singleShot(processWaitMs, this, [this, id] {
                if (m_deciding.remove(id)) {
                    log(id, QStringLiteral("no answer about its program; matched by name"));
                    if (m_pending.contains(id) && !m_windows.contains(id)) {
                        realize(id);
                    }
                }
            });
            return;
        }
        w = takePlaceholder(id, s.title, early);
        if (w && !w->isVisible()) {
            // The app opened before its loading window was due (board 9.12):
            // it opens on its own, with the loading window's app id.
            const quint32 token = w->property("token").toUInt();
            QString app = w->appId();
            if (app == genericAppId && m_processes.contains(id)) {
                // A loading window that did not know its app: the program's.
                app = QStringLiteral("windows-") + m_processes.value(id).section(u'.', 0, 0);
            }
            m_adopted.remove(token);
            w->deleteLater();
            w = nullptr;
            m_apps.insert(id, app);
            Q_EMIT loadingFinished(token, QStringLiteral("window"), id);
        }
        if (w) {
            adopted = true;
            w->adopt(id);
            if (w->appId() == genericAppId && m_processes.contains(id)) {
                // The loading window did not know its app (a program Windows
                // mode has no entry for); Windows has just named the program,
                // which groups its windows and is never the generic W.
                w->setAppId(QStringLiteral("windows-") + m_processes.value(id).section(u'.', 0, 0));
            }
            if (!m_apps.contains(id)) {
                m_apps.insert(id, w->appId());
            }
            log(id, QStringLiteral("takes over loading window %1 (%2)").arg(w->property("token").toUInt()).arg(w->appId()));
            Q_EMIT loadingFinished(w->property("token").toUInt(), QStringLiteral("window"), id);
        }
    }
    if (!w) {
        w = new RailWindow(this, m_client, appIdFor(id));
        if (!m_processes.contains(id) && !m_askedProcess.contains(id) && !popup) {
            m_askedProcess.insert(id);
            m_client->requestProcess(id);
        }
        const QIcon known = m_programIcons.value(m_processes.value(id));
        if (!known.isNull()) {
            w->QWindow::setIcon(known);
        }
    }
    w->setRemoteState(id, s.style, s.exStyle, popup);
    w->setRemoteFullScreen(fullMonitor >= 0, screenOf(fullMonitor));
    w->setOwner(s.owner);
    w->setShowState(s.showState);
    w->setFrame(rect);
    w->setRemoteTitle(s.title);
    if (!p.icon.isNull()) {
        w->setIcon(p.icon, p.iconBig);
    }
    m_windows.insert(id, w);
    if (const auto known = m_surfaces.constFind(id); known != m_surfaces.cend()) {
        w->surfaceDamaged(known->first, QRect(QPoint(0, 0), known->second), known->second);
    }
    Q_EMIT windowShown(id, s.title);
    if (adopted) {
        m_loadingByToken.insert(w->property("token").toUInt(), w);
        if (!(s.style & WS_THICKFRAME) && !(s.style & WS_MAXIMIZE)) {
            // Its own size, next time, from the start.
            rememberSize(w->appId(), w->logicalSize(s.size));
        }
        w->beginHandoff();
        return;
    }
    if (!popup) {
        stepAsideFor(id);
    }
    if (topLevel && appSized && shown && !closingOwner && takeCancelled(m_processes.value(id))) {
        log(id, QStringLiteral("belongs to a launch cancelled on its loading window; closed without being shown"));
        Closing c;
        c.since.start();
        c.title = s.title;
        c.pid = m_pids.value(id);
        c.hidden = true;
        c.unwanted = true;
        m_closing.insert(id, c);
        m_client->sysCommand(id, SC_CLOSE);
        QTimer::singleShot(closeEndMs, this, [this, id] { closeUnanswered(id, true); });
        return;
    }
    log(id, QStringLiteral("shown as %1 (%2)")
                .arg(popup ? QStringLiteral("a popup") : fullMonitor >= 0 ? QStringLiteral("a full screen window") : QStringLiteral("a window"), w->appId()));
    if (popup) {
        w->setTransientParent(parent);
        const double scale = parent->scale();
        const QPoint delta = s.offset - parent->frame().topLeft();
        w->setGeometry(QRect(parent->position() + QPoint(int(std::lround(delta.x() / scale)), int(std::lround(delta.y() / scale))),
                             w->logicalSize(s.size)));
        showAs(w, s.showState);
        return;
    }
    if (RailWindow *owner = m_windows.value(s.owner); owner && owner->isVisible()) {
        w->setTransientParent(owner);
    }
    w->resize(w->logicalSize(s.size));
    if (m_apps.contains(id) || ownedByShownWindow(s) || m_programApps.contains(m_processes.value(id))) {
        showAs(w, s.showState);
        return;
    }
    // The agent says which app this is in a moment; showing it once with
    // that app's id puts it straight into the right taskbar group.
    QPointer<RailWindow> guard(w);
    QTimer::singleShot(appIdWaitMs, this, [this, guard, id] {
        if (guard && !guard->isVisible() && m_windows.value(id) == guard) {
            const RailOrder &state = m_pending.value(id).state;
            if (state.showState != stateHidden && !m_cloaked.contains(id)) {
                showAs(guard, state.showState);
            }
        }
    });
}

void Rail::stepAsideFor(quint32 id)
{
    // A window of the program a loading window waits for is on screen but
    // was not the one it took (a first-run question, a small dialog before
    // the main window). The app is up: if the loading window has not taken
    // one by the time this has been showing a moment, it goes, so it never
    // sits there saying the app is still opening.
    const QString exe = m_processes.value(id);
    if (exe.isEmpty()) {
        return;
    }
    QTimer::singleShot(stepAsideMs, this, [this, id, exe] {
        RailWindow *w = m_windows.value(id);
        if (!w || !w->isVisible() || m_closing.contains(id)) {
            return;
        }
        const QList<quint32> tokens = m_placeholders.keys();
        for (quint32 token : tokens) {
            if (m_placeholders.value(token).program == exe && !(m_placeholders.value(token).cold && m_preexisting.contains(id))) {
                log(id, QStringLiteral("is on screen and loading window %1 took nothing; it goes").arg(token));
                cancel(token, QStringLiteral("window"));
            }
        }
    });
}

int Rail::fullScreenMonitor(const RailOrder &s) const
{
    if (!fullScreenStyle(s.style, s.exStyle) || s.size.width() <= 0 || s.size.height() <= 0) {
        return -1;
    }
    return monitors::covered(m_layout, QRect(s.offset, s.size));
}

QScreen *Rail::screenOf(int monitor) const
{
    if (monitor < 0 || monitor >= m_layout.size()) {
        return nullptr;
    }
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (s->name() == m_layout[monitor].name) {
            return s;
        }
    }
    return nullptr;
}

void Rail::updateFullScreen(RailWindow *w, const RailOrder &s)
{
    const int monitor = fullScreenMonitor(s);
    if ((monitor >= 0) == w->remoteFullScreen()) {
        return;
    }
    if (monitor < 0 && w->isPopup()) {
        return;
    }
    log(w->remoteId(), monitor >= 0 ? QStringLiteral("went full screen") : QStringLiteral("left full screen"));
    if (w->setRemoteFullScreen(monitor >= 0, screenOf(monitor))) {
        showAs(w, s.showState);
    }
}

bool Rail::overlapsAppWindow(const QRect &rect) const
{
    for (RailWindow *w : m_windows) {
        if (!w->isPopup() && w->isVisible() && w->frame().intersects(rect)) {
            return true;
        }
    }
    return false;
}

void Rail::log(quint32 id, const QString &what) const
{
    // One line for each decision about a window, always: what a test on real
    // hardware needs to tell why a window looks the way it does.
    const RailOrder s = m_pending.value(id).state;
    qInfo("wildos-rdp: window %08x [%s, style %08x ex %08x owner %08x %dx%d] %s", id, qPrintable(m_processes.value(id, QStringLiteral("?"))),
          s.style, s.exStyle, s.owner, s.size.width(), s.size.height(), qPrintable(what));
}

void Rail::process(quint32 id, const QString &image, const QString &appUserModel, quint32 pid)
{
    const QString exe = exeOf(image);
    m_processes.insert(id, exe);
    m_pids.insert(id, pid);
    if (auto it = m_closing.find(id); it != m_closing.end()) {
        it->pid = pid;
    }
    qInfo("wildos-rdp: window %08x belongs to %s (process %u%s%s)", id, qPrintable(image), pid, appUserModel.isEmpty() ? "" : ", ",
          qPrintable(appUserModel));
    if (m_deciding.remove(id)) {
        if (m_pending.contains(id) && !m_windows.contains(id)) {
            realize(id);
        }
        return;
    }
    // A window shown before its program was known joins that program's app,
    // unless the agent has already said which app it is.
    RailWindow *w = m_windows.value(id);
    if (!w || w->loading() || m_agentApps.contains(id)) {
        return;
    }
    const QString app = m_programApps.value(exe);
    if (!app.isEmpty() && w->appId() != app) {
        m_apps.insert(id, app);
        w->setAppId(app);
        const QIcon icon = m_programIcons.value(exe);
        if (!icon.isNull() && w->icon().isNull()) {
            w->QWindow::setIcon(icon);
        }
    } else if (app.isEmpty() && w->appId() == genericAppId && !exe.isEmpty()) {
        // Its program's own id, even if the generic one was recorded for it
        // while the program was unknown.
        const QString own = QStringLiteral("windows-") + exe.section(u'.', 0, 0);
        m_apps.insert(id, own);
        w->setAppId(own);
    }
    if (!w->isPopup() && w->isVisible()) {
        // Named too late to take the loading window: it gives way instead.
        stepAsideFor(id);
    }
}

void Rail::cloak(quint32 id, bool cloaked)
{
    log(id, cloaked ? QStringLiteral("cloaked") : QStringLiteral("uncloaked"));
    if (cloaked) {
        m_cloaked.insert(id);
        if (RailWindow *w = m_windows.value(id)) {
            if (w->loading()) {
                giveBack(w);
            } else {
                w->hide();
            }
        }
        return;
    }
    m_cloaked.remove(id);
    const RailOrder s = m_pending.value(id).state;
    const bool visible = s.showState != stateHidden && s.size.width() > 0 && s.size.height() > 0;
    if (!visible || m_closing.contains(id)) {
        return;
    }
    if (RailWindow *w = m_windows.value(id)) {
        if (!w->isVisible()) {
            showAs(w, s.showState);
        }
    } else if (m_pending.contains(id)) {
        realize(id);
    }
}

void Rail::giveBack(RailWindow *w)
{
    // The window a loading window took went away or was hidden before the
    // app showed in it: the loading window waits again, so it is never left
    // on the taskbar with nothing of the app in it.
    const quint32 token = w->property("token").toUInt();
    const quint32 id = w->remoteId();
    log(id, QStringLiteral("hidden before its first picture; loading window %1 waits again").arg(token));
    m_windows.remove(id);
    m_loadingByToken.remove(token);
    Placeholder p = m_adopted.take(token);
    p.window = w;
    p.timeout = new QTimer(this);
    p.timeout->setSingleShot(true);
    connect(p.timeout, &QTimer::timeout, this, [this, token] { loadingTimedOut(token); });
    p.timeout->start(loadingTimeoutMs);
    m_placeholders.insert(token, p);
    w->unadopt();
    if (w->windowStates() & Qt::WindowMinimized) {
        w->showNormal();
    }
}

void Rail::noticeActivation(quint32 active)
{
    // An app that is already open often only brings its window forward
    // when it is opened again (a second Explorer, a document app showing
    // its one window): no new window comes, and the loading window would
    // wait for ever. When Windows makes an existing window of the program
    // active and nothing new has come a moment later, the loading window
    // closes and that window comes forward.
    const QString exe = m_processes.value(active);
    RailWindow *existing = m_windows.value(active);
    if (exe.isEmpty() || !existing || existing->loading()) {
        return;
    }
    for (auto it = m_placeholders.cbegin(); it != m_placeholders.cend(); ++it) {
        if (it->program != exe) {
            continue;
        }
        const quint32 token = it.key();
        QTimer::singleShot(1500, this, [this, token, active] {
            if (!m_placeholders.contains(token) || m_active != active) {
                return;
            }
            log(active, QStringLiteral("already open and brought forward; loading window %1 closes").arg(token));
            cancel(token, QStringLiteral("window"));
            if (RailWindow *w = m_windows.value(active)) {
                w->requestActivate();
            }
        });
    }
}

void Rail::windowDrawn(RailWindow *w)
{
    // An app window of a program is on screen while a loading window for the
    // same program still waits: the app went another way (a second process,
    // a window handed over), so the loading window goes rather than stay on
    // the taskbar with nothing in it, and the app window takes its app id.
    const QString exe = m_processes.value(w->remoteId());
    if (exe.isEmpty() || w->loading()) {
        return;
    }
    const RailOrder s = m_pending.value(w->remoteId()).state;
    if (w->isPopup() || ownedByShownWindow(s) || s.size.width() < 160 || s.size.height() < 100) {
        return;
    }
    const QList<quint32> tokens = m_placeholders.keys();
    for (quint32 token : tokens) {
        const Placeholder p = m_placeholders.value(token);
        if (p.program != exe || (p.cold && m_preexisting.contains(w->remoteId()))) {
            continue;
        }
        log(w->remoteId(), QStringLiteral("its app is on screen; loading window %1 closes").arg(token));
        if (!m_agentApps.contains(w->remoteId())) {
            m_apps.insert(w->remoteId(), p.appId);
            w->setAppId(p.appId);
            if (!p.icon.isNull() && w->icon().isNull()) {
                w->QWindow::setIcon(p.icon);
            }
        }
        cancel(token, QStringLiteral("window"));
    }
}

void Rail::setWindowApp(quint32 id, const QString &appId)
{
    if (appId.isEmpty()) {
        return;
    }
    m_apps.insert(id, appId);
    m_agentApps.insert(id);
    RailWindow *w = m_windows.value(id);
    if (!w) {
        return;
    }
    if (!w->isVisible()) {
        w->setProperty("appIdPending", false);
        const QString saved = w->appId();
        if (saved != appId) {
            // Not on screen yet: the id is simply taken before showing.
            w->setAppId(appId);
        }
        const RailOrder &state = m_pending.value(id).state;
        if (state.showState != stateHidden) {
            showAs(w, state.showState);
        }
        return;
    }
    w->setAppId(appId);
}

void Rail::deleted(quint32 id)
{
    if (RailWindow *w = m_windows.value(id); w && w->loading()) {
        if (m_closing.contains(id)) {
            // Closed by the person while it was still handing over: that
            // launch is over, its loading window does not wait again.
            const quint32 token = w->property("token").toUInt();
            m_loadingByToken.remove(token);
            m_adopted.remove(token);
        } else {
            giveBack(w);
        }
    }
    if (const auto c = m_closing.constFind(id); c != m_closing.cend()) {
        log(id, QStringLiteral("deleted %1 ms after its close").arg(c->since.elapsed()));
    } else {
        log(id, QStringLiteral("deleted"));
    }
    m_closing.remove(id);
    m_pids.remove(id);
    m_hiddenByLoss.remove(id);
    for (auto it = m_closing.begin(); it != m_closing.end(); ++it) {
        if (it->dialogs.remove(id)) {
            // The dialog was answered. If its window is still there a moment
            // later, the answer was not to close (Cancel): it comes back.
            const quint32 parent = it.key();
            QTimer::singleShot(1500, this, [this, parent] {
                if (m_closing.contains(parent) && m_windows.contains(parent) && m_closing.value(parent).dialogs.isEmpty()) {
                    reopen(parent);
                }
            });
        }
    }
    m_pending.remove(id);
    m_surfaces.remove(id);
    m_apps.remove(id);
    m_agentApps.remove(id);
    m_processes.remove(id);
    m_askedProcess.remove(id);
    m_cloaked.remove(id);
    m_deciding.remove(id);
    m_preexisting.remove(id);
    RailWindow *w = m_windows.take(id);
    if (!w) {
        return;
    }
    if (!w->isPopup() && w->isVisible()) {
        rememberSize(w->appId(), w->size());
    }
    // Popups of this window go with it.
    for (RailWindow *other : std::as_const(m_windows)) {
        if (other->transientParent() == w) {
            other->hide();
            other->setTransientParent(nullptr);
        }
    }
    if (m_active == id) {
        m_active = 0;
    }
    w->hide();
    w->deleteLater();
    Q_EMIT windowGone(id);
}

void Rail::icon(quint32 id, const QImage &image, bool big)
{
    if (RailWindow *w = m_windows.value(id)) {
        w->setIcon(image, big);
        return;
    }
    if (m_pending.contains(id)) {
        Pending &p = m_pending[id];
        if (big || p.icon.isNull()) {
            p.icon = image;
            p.iconBig = big;
        }
    }
}

void Rail::desktop(quint32 active, bool hasActive, const QList<quint32> &z, bool hasZ)
{
    if (hasZ) {
        m_zOrder = z;
    }
    if (!hasActive) {
        return;
    }
    m_active = active;
    noticeActivation(active);
    RailWindow *w = m_windows.value(active);
    if (w && !w->isPopup() && w->isVisible() && QGuiApplication::focusWindow() != w) {
        w->requestActivate();
    }
}

void Rail::surface(quint64 id, quint16 surfaceId, const QRegion &damage, const QSize &size)
{
    if (const auto seen = m_surfaces.constFind(quint32(id)); seen == m_surfaces.cend() || seen->first != surfaceId) {
        log(quint32(id), QStringLiteral("drawn from surface %1, %2x%3%4")
                             .arg(surfaceId)
                             .arg(size.width())
                             .arg(size.height())
                             .arg(seen == m_surfaces.cend() ? QString() : QStringLiteral(" (was surface %1)").arg(seen->first)));
    }
    m_surfaces.insert(quint32(id), {surfaceId, size});
    if (auto it = m_closing.find(quint32(id)); it != m_closing.end()) {
        const qint64 at = it->since.elapsed();
        if (it->firstDamage < 0) {
            it->firstDamage = at;
            if (qEnvironmentVariableIsSet("WILDOS_RDP_CLOSE_TIMING")) {
                log(quint32(id), QStringLiteral("drew again %1 ms after its close").arg(at));
            }
        }
        if (it->hidden && !it->unwanted && !it->quiet) {
            // A question that came after the grace period: it comes back.
            log(quint32(id), QStringLiteral("drew again %1 ms after its close, while hidden; asking, so it comes back").arg(at));
            reopen(quint32(id));
        }
    }
    RailWindow *w = m_windows.value(quint32(id));
    if (!w) {
        // Windows draws windows it has not shown yet (and ones it never
        // will); the whole surface is read when the window appears.
        m_surfaces.insert(quint32(id), {surfaceId, size});
        return;
    }
    w->surfaceDamaged(surfaceId, damage, size);
}

void Rail::primary(const QRegion &damage)
{
    for (RailWindow *w : std::as_const(m_windows)) {
        w->primaryDamaged(damage);
    }
}

void Rail::moveSize(quint32 id, bool start, quint16 type, int, int)
{
    RailWindow *w = m_windows.value(id);
    if (!w || w->isPopup()) {
        return;
    }
    if (start) {
        w->beginLocalMoveSize(type);
    } else {
        w->endLocalMoveSize();
    }
}

void Rail::minMax(quint32 id, const QSize &minTrack, const QSize &maxTrack)
{
    if (RailWindow *w = m_windows.value(id)) {
        w->setMinMax(minTrack, maxTrack);
    }
}

void Rail::windowActivated(RailWindow *window, bool active)
{
    const quint32 id = window->remoteId();
    if (active) {
        if (window->remoteMinimized()) {
            m_client->sysCommand(id, SC_RESTORE);
        }
        if (m_active != id) {
            m_active = id;
            m_client->activate(id, true);
        }
        return;
    }
    // Deactivated: if the keyboard went to another app on the desktop, not
    // to another Windows window, Windows is told, which closes its menus.
    QTimer::singleShot(0, this, [this, id] {
        QWindow *focus = QGuiApplication::focusWindow();
        if (qobject_cast<RailWindow *>(focus)) {
            return;
        }
        if (m_active == id) {
            m_client->activate(id, false);
            m_active = 0;
        }
    });
}

QJsonObject Rail::placements()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    QJsonObject out;
    for (const QString &app : s.childGroups()) {
        const QPoint at = s.value(app + QStringLiteral("/place")).toPoint();
        if (s.contains(app + QStringLiteral("/place"))) {
            out.insert(app, QJsonObject{{QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y()}});
        }
    }
    return out;
}

void Rail::savePlacement(const QString &appId, const QPoint &at)
{
    if (!appId.startsWith(QLatin1String("wildos-winmode-")) && !appId.startsWith(QLatin1String("windows-"))) {
        return;
    }
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(appId + QStringLiteral("/place"), at);
}

void Rail::closeRequested(RailWindow *w)
{
    // Windows is told at once and the window stays exactly as it is for the
    // grace period: most windows are gone well within it; a window asking
    // about unsaved work is never hidden and shown again.
    const quint32 id = w->remoteId();
    if (!id || m_closing.contains(id)) {
        return;
    }
    Closing c;
    c.since.start();
    c.title = w->title();
    c.pid = m_pids.value(id);
    m_closing.insert(id, c);
    log(id, QStringLiteral("closed here; Windows asked to close it"));
    m_client->sysCommand(id, SC_CLOSE);
    if (neverAsk.contains(m_processes.value(id))) {
        m_closing[id].quiet = true;
        log(id, QStringLiteral("its program never asks; hidden at once"));
        hideClosing(id);
        QTimer::singleShot(closeQuietMs, this, [this, id] { closeUnanswered(id, false); });
        QTimer::singleShot(closeEndMs, this, [this, id] { closeUnanswered(id, true); });
        return;
    }
    QTimer::singleShot(closeGraceMs, this, [this, id] { closeGrace(id, false); });
    QTimer::singleShot(closeAskingMs, this, [this, id] { closeGrace(id, true); });
}

void Rail::closeGrace(quint32 id, bool last)
{
    auto it = m_closing.find(id);
    if (it == m_closing.end() || it->hidden || !m_windows.contains(id)) {
        return;
    }
    const bool asked = !it->dialogs.isEmpty() || it->firstDamage >= 0 || it->disabled;
    if (asked && last) {
        // Still there and it drew in itself or opened a dialog: it is
        // asking. It stays, an ordinary open window again.
        log(id, QStringLiteral("asking before it closes (drew again at %1 ms); stays on screen").arg(it->firstDamage));
        m_closing.erase(it);
        return;
    }
    if (asked) {
        return;
    }
    // No word from Windows at all: the window leaves the screen and the
    // taskbar now, and the rest is handled in the background.
    log(id, QStringLiteral("no answer to its close in %1 ms; hidden").arg(closeGraceMs));
    hideClosing(id);
    QTimer::singleShot(closeQuietMs, this, [this, id] { closeUnanswered(id, false); });
    QTimer::singleShot(closeEndMs, this, [this, id] { closeUnanswered(id, true); });
}

void Rail::hideClosing(quint32 id)
{
    auto it = m_closing.find(id);
    RailWindow *w = m_windows.value(id);
    if (it == m_closing.end() || !w) {
        return;
    }
    it->hidden = true;
    for (RailWindow *other : std::as_const(m_windows)) {
        if (other->transientParent() == w) {
            other->hide();
        }
    }
    w->hide();
}

void Rail::closeUnanswered(quint32 id, bool end)
{
    auto it = m_closing.find(id);
    if (it == m_closing.end() || !m_windows.contains(id)) {
        return;
    }
    // A dialog it showed (Save changes?) is the person's to answer.
    for (quint32 dialog : std::as_const(it->dialogs)) {
        if (m_windows.contains(dialog)) {
            return;
        }
    }
    // Windows apps mark a document with changes that are not saved with an
    // asterisk in the title (Notepad, Paint, most editors).
    const bool unsaved = it->title.contains(u'*');
    if (!end) {
        if (unsaved && !it->notified) {
            it->notified = true;
            notifyUnsaved(id);
        }
        return;
    }
    if (unsaved) {
        // Kept running in the background rather than lose the work.
        return;
    }
    // Nothing unsaved and no answer: the app is stuck. It is ended quietly
    // in Windows, unless its process has other windows still open here.
    for (auto w = m_windows.cbegin(); w != m_windows.cend(); ++w) {
        if (w.key() != id && it->pid && m_pids.value(w.key()) == it->pid && !m_closing.contains(w.key()) && w.value()->isVisible()) {
            log(id, QStringLiteral("did not close; its program has other windows open, so it stays in the background"));
            return;
        }
    }
    log(id, QStringLiteral("did not close in %1 s; asking for its program to be ended").arg(closeEndMs / 1000));
    Q_EMIT closeStalled(id, it->pid, m_processes.value(id));
}

void Rail::notifyUnsaved(quint32 id)
{
    const Closing c = m_closing.value(id);
    RailWindow *w = m_windows.value(id);
    QString app = c.title;
    app.remove(u'*');
    app = app.section(QStringLiteral(" - "), -1).trimmed();
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                       QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    QVariantMap hints;
    if (w) {
        hints.insert(QStringLiteral("desktop-entry"), w->appId());
    }
    call << QStringLiteral("Windows mode") << quint32(0) << QString() << QStringLiteral("%1 hasn't closed").arg(app.isEmpty() ? QStringLiteral("The app") : app)
         << QStringLiteral("It has changes that aren't saved, so it's still open in the background.")
         << QStringList{QStringLiteral("show"), QStringLiteral("Show it")} << hints << qint32(-1);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, id] {
        QDBusPendingReply<quint32> reply = *watcher;
        if (reply.isValid()) {
            m_notifications.insert(reply.value(), id);
        }
        watcher->deleteLater();
    });
}

void Rail::notificationAction(quint32 notification, const QString &action)
{
    const quint32 id = m_notifications.take(notification);
    if (!id || action != QLatin1String("show")) {
        return;
    }
    reopen(id);
}

void Rail::reopen(quint32 id)
{
    // The close did not happen (the person cancelled it, or asked to see
    // the window again): it comes back as it was.
    m_closing.remove(id);
    RailWindow *w = m_windows.value(id);
    const RailOrder s = m_pending.value(id).state;
    if (w && s.showState != stateHidden && !m_cloaked.contains(id)) {
        log(id, QStringLiteral("still open after its close; shown again"));
        showAs(w, s.showState);
        w->requestActivate();
    }
}

void Rail::connectionLost()
{
    // Reconnecting: windows that would only sit frozen go from the screen
    // if it takes more than a moment, and come back when it is back.
    m_lostAt.start();
    m_railStarted = false;
    QTimer::singleShot(lostHideMs, this, [this] {
        if (!m_lostAt.isValid()) {
            return;
        }
        for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it) {
            if (it.value()->isVisible()) {
                m_hiddenByLoss.insert(it.key());
                it.value()->hide();
            }
        }
    });
}

void Rail::connectionBack()
{
    m_lostAt.invalidate();
    // A reconnection starts from the layout the first connection had;
    // the one for the screens now follows once Windows can take it.
    const QList<Monitor> first = m_client->connectLayout();
    if (!first.isEmpty()) {
        m_layout = first;
        m_client->layoutReset();
    }
    const QSet<quint32> hidden = std::exchange(m_hiddenByLoss, {});
    for (quint32 id : hidden) {
        if (RailWindow *w = m_windows.value(id); w && !m_closing.contains(id)) {
            showAs(w, m_pending.value(id).state.showState);
        }
    }
}
