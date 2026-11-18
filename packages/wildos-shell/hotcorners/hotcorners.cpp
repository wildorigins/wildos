/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "hotcorners.h"

#include <core/output.h>
#include <cursor.h>
#include <globalshortcuts.h>
#include <input.h>
#include <main.h>
#include <pointer_input.h>
#include <virtualdesktops.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>

namespace WildOS
{

namespace
{
// The names Settings and wildosrc use, in Corner order, with KWin's own
// number for each corner.
struct CornerName {
    const char *name;
    int kwin;
};
constexpr CornerName cornerNames[] = {
    {"TopLeft", 7},
    {"TopRight", 1},
    {"BottomLeft", 5},
    {"BottomRight", 3},
};
// KWin's "no corner", which keeps a corner list empty: an empty list reads
// back as KWin's default, which gives Overview the top left corner.
constexpr int kwinNoCorner = 9;

// The pointer stops on the last pixel of the screen; a little give either
// side keeps fractional scaling from missing it.
constexpr qreal cornerReach = 2;
// Moving this far from a corner that fired counts as leaving it. Any less,
// and the small wobble of a hand resting on the mouse fires it again.
constexpr qreal leaveDistance = 8;
// Turning the screen off waits for the pointer to settle: any movement wakes
// the screen, and the hand is usually still on the mouse.
constexpr int screenOffSettle = 500;

const QString defaultActions[] = {
    QStringLiteral("none"),
    QStringLiteral("none"),
    QStringLiteral("none"),
    QStringLiteral("none"),
};

Qt::KeyboardModifiers parseKey(const QString &key)
{
    Qt::KeyboardModifiers result;
    const QStringList parts = key.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String("ctrl")) {
            result |= Qt::ControlModifier;
        } else if (part == QLatin1String("alt")) {
            result |= Qt::AltModifier;
        } else if (part == QLatin1String("shift")) {
            result |= Qt::ShiftModifier;
        } else if (part == QLatin1String("super")) {
            result |= Qt::MetaModifier;
        }
    }
    return result;
}

constexpr Qt::KeyboardModifiers keyMask = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;

QDBusMessage call(const QString &service, const QString &path, const QString &interface, const QString &method)
{
    return QDBusMessage::createMethodCall(service, path, interface, method);
}

// Letting go of the Windows key on its own opens Start: the shortcut
// service watches for it through KWin's input hook, and forgets it once a
// pointer button is pressed, which a corner counts as. KWin keeps that hook
// private with no getter, so it is read through the one access C++ allows to
// a private member, an explicit template instantiation. The plugin is built
// against the exact KWin it is loaded by, so the member is the one KWin has.
struct AccelHook {
    using type = KGlobalAccelInterface *KWin::GlobalShortcutsManager::*;
    friend type hookMember(AccelHook);
};
template<typename Tag, typename Tag::type Member>
struct ReadPrivate {
    friend typename Tag::type hookMember(Tag)
    {
        return Member;
    }
};
template struct ReadPrivate<AccelHook, &KWin::GlobalShortcutsManager::m_kglobalAccelInterface>;

void forgetModifierOnlyPress()
{
    KWin::GlobalShortcutsManager *shortcuts = KWin::input()->shortcuts();
    if (!shortcuts) {
        return;
    }
    // KGlobalAccelInterface is a QObject subclass that KWin only declares.
    auto hook = reinterpret_cast<QObject *>(shortcuts->*hookMember(AccelHook()));
    if (hook) {
        bool handled = false;
        QMetaObject::invokeMethod(hook, "checkPointerPressed", Qt::DirectConnection, Q_RETURN_ARG(bool, handled),
                                  Q_ARG(Qt::MouseButtons, Qt::LeftButton));
    }
}

const QString policyService = QStringLiteral("org.kde.Solid.PowerManagement.PolicyAgent");
const QString policyPath = QStringLiteral("/org/kde/Solid/PowerManagement/PolicyAgent");
const QString screenSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString screenSaverPath = QStringLiteral("/ScreenSaver");
}

HotCorners::HotCorners()
    : m_watcher(KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("wildosrc"))))
{
    migrate();
    keepKWinCornersOff();
    readConfig();

    // Keep awake lasts until it is turned off or the session ends, so one
    // left on when the last session ended is not on now.
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    if (wildos->group(QStringLiteral("KeepAwake")).readEntry("Active", false)) {
        wildos->group(QStringLiteral("KeepAwake")).writeEntry("Active", false, KConfig::Notify);
        wildos->sync();
    }

    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("HotCorners")) {
            readConfig();
        } else if (group.name() == QLatin1String("KeepAwake")) {
            KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
            config->reparseConfiguration();
            applyKeepAwake(config->group(QStringLiteral("KeepAwake")).readEntry("Active", false));
        }
    });

    m_dwell.setSingleShot(true);
    connect(&m_dwell, &QTimer::timeout, this, [this] {
        if (m_current >= 0 && !m_fired && !blocked(m_spots[m_current]) && keyHeld(m_spots[m_current].corner)) {
            fire();
        }
    });
    m_screenOff.setSingleShot(true);
    m_screenOff.setInterval(screenOffSettle);
    connect(&m_screenOff, &QTimer::timeout, this, &HotCorners::turnScreenOff);

    connect(KWin::workspace(), &KWin::Workspace::outputsChanged, this, &HotCorners::updateSpots);
    connect(KWin::Cursors::self()->mouse(), &KWin::Cursor::posChanged, this, &HotCorners::pointerMoved);
    connect(KWin::input(), &KWin::InputRedirection::keyboardModifiersChanged, this, &HotCorners::evaluate);
    updateSpots();
}

HotCorners::~HotCorners()
{
    applyKeepAwake(false);
}

void HotCorners::readConfig()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    config->reparseConfiguration();
    const KConfigGroup group = config->group(QStringLiteral("HotCorners"));
    for (int i = 0; i < CornerCount; ++i) {
        const QString name = QString::fromLatin1(cornerNames[i].name);
        m_settings[i].action = group.readEntry(name, defaultActions[i]);
        m_settings[i].key = parseKey(group.readEntry(name + QLatin1String("Key"), QString()));
    }
    m_delay = std::clamp(group.readEntry("Delay", 150), 0, 2000);
    m_skipFullScreen = group.readEntry("SkipFullScreen", true);
    m_dwell.stop();
    evaluate();
}

// Corners chosen before Wild OS had its own were KWin's, kept in kwinrc:
// Overview's list of corners for Task view, [ElectricBorders] for showing
// the desktop and locking. They come across once, so nobody's corners
// change. New accounts get theirs from /etc/skel instead.
void HotCorners::migrate()
{
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    KConfigGroup group = wildos->group(QStringLiteral("HotCorners"));
    if (group.hasKey("TopLeft")) {
        return;
    }
    // Only the account's own file: a default from /etc/xdg is not something
    // the person chose, and KWin's built-in default is what they had.
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/kwinrc");
    KConfig kwin(path, KConfig::SimpleConfig);
    const QList<int> overview = kwin.group(QStringLiteral("Effect-overview")).readEntry("BorderActivate", QList<int>{7});
    const QList<int> appWindows = kwin.group(QStringLiteral("Effect-windowview")).readEntry("BorderActivateClass", QList<int>());
    const KConfigGroup borders = kwin.group(QStringLiteral("ElectricBorders"));
    for (const CornerName &corner : cornerNames) {
        const QString action = borders.readEntry(corner.name, QStringLiteral("None")).toLower();
        QString value = QStringLiteral("none");
        if (overview.contains(corner.kwin)) {
            value = QStringLiteral("taskview");
        } else if (appWindows.contains(corner.kwin)) {
            value = QStringLiteral("appwindows");
        } else if (action == QLatin1String("showdesktop")) {
            value = QStringLiteral("desktop");
        } else if (action == QLatin1String("lockscreen")) {
            value = QStringLiteral("lock");
        }
        group.writeEntry(corner.name, value);
    }
    wildos->sync();
}

void HotCorners::keepKWinCornersOff()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    bool changed = false;

    KConfigGroup borders = config->group(QStringLiteral("ElectricBorders"));
    for (const CornerName &corner : cornerNames) {
        if (borders.readEntry(corner.name, QStringLiteral("None")) != QLatin1String("None")) {
            borders.writeEntry(corner.name, QStringLiteral("None"));
            changed = true;
        }
    }

    KConfigGroup overview = config->group(QStringLiteral("Effect-overview"));
    QList<int> overviewCorners = overview.readEntry("BorderActivate", QList<int>{7});
    QList<int> kept;
    for (int edge : std::as_const(overviewCorners)) {
        if (edge != 1 && edge != 3 && edge != 5 && edge != 7) {
            kept.append(edge);
        }
    }
    if (kept.isEmpty()) {
        kept = {kwinNoCorner};
    }
    if (kept != overviewCorners) {
        overview.writeEntry("BorderActivate", kept);
        changed = true;
    }

    KConfigGroup windowView = config->group(QStringLiteral("Effect-windowview"));
    for (const char *key : {"BorderActivate", "BorderActivateAll", "BorderActivateClass"}) {
        if (!windowView.hasKey(key)) {
            continue;
        }
        QList<int> list = windowView.readEntry(key, QList<int>());
        const qsizetype before = list.size();
        list.removeIf([](int edge) {
            return edge == 1 || edge == 3 || edge == 5 || edge == 7;
        });
        if (list.size() != before) {
            windowView.writeEntry(key, list.isEmpty() ? QList<int>{kwinNoCorner} : list);
            changed = true;
        }
    }

    if (!changed) {
        return;
    }
    config->sync();
    // Overview and window view read their corners only when told to by name.
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.asyncCall(call(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
    for (const QString &effect : {QStringLiteral("overview"), QStringLiteral("windowview")}) {
        QDBusMessage message = call(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
        message << effect;
        bus.asyncCall(message);
    }
}

// A corner of a screen counts when no screen lies beyond it to the side,
// above or below, or diagonally: the outer corners of the desktop as a whole.
void HotCorners::updateSpots()
{
    const QList<KWin::Output *> outputs = KWin::workspace()->outputs();
    // QRect::contains would count the pixel just past the right and bottom
    // edges as inside.
    auto covered = [&outputs](const QPointF &point) {
        for (KWin::Output *output : outputs) {
            const QRect g = output->geometry();
            if (point.x() >= g.x() && point.x() < g.x() + g.width() && point.y() >= g.y() && point.y() < g.y() + g.height()) {
                return true;
            }
        }
        return false;
    };

    m_spots.clear();
    for (KWin::Output *output : outputs) {
        const QRectF g = output->geometry();
        const qreal left = g.x();
        const qreal top = g.y();
        const qreal right = g.x() + g.width() - 1;
        const qreal bottom = g.y() + g.height() - 1;
        const struct {
            Corner corner;
            QPointF point;
            qreal dx;
            qreal dy;
        } candidates[] = {
            {TopLeft, {left, top}, -1, -1},
            {TopRight, {right, top}, 1, -1},
            {BottomLeft, {left, bottom}, -1, 1},
            {BottomRight, {right, bottom}, 1, 1},
        };
        for (const auto &c : candidates) {
            const QPointF p = c.point;
            if (!covered(p + QPointF(c.dx, 0)) && !covered(p + QPointF(0, c.dy)) && !covered(p + QPointF(c.dx, c.dy))) {
                m_spots.append(Spot{c.corner, p, output});
            }
        }
    }
    m_current = -1;
    m_fired = false;
    m_dwell.stop();
}

void HotCorners::pointerMoved(const QPointF &pos)
{
    m_pointer = pos;
    if (m_screenOff.isActive()) {
        m_screenOff.start();
    }
    if (m_fired && (qAbs(pos.x() - m_firedAt.x()) > leaveDistance || qAbs(pos.y() - m_firedAt.y()) > leaveDistance)) {
        m_fired = false;
    }

    int found = -1;
    for (int i = 0; i < m_spots.size(); ++i) {
        const QPointF p = m_spots[i].point;
        if (qAbs(pos.x() - p.x()) <= cornerReach && qAbs(pos.y() - p.y()) <= cornerReach) {
            found = i;
            break;
        }
    }
    if (found != m_current) {
        m_current = found;
        m_dwell.stop();
    }
    evaluate();
}

// Starts the wait once the pointer is in a corner with its key held, and
// stops it if either stops being true.
void HotCorners::evaluate()
{
    if (m_current < 0 || m_current >= m_spots.size() || m_fired) {
        m_dwell.stop();
        return;
    }
    const Spot &spot = m_spots[m_current];
    if (blocked(spot) || !keyHeld(spot.corner)) {
        m_dwell.stop();
        return;
    }
    if (m_dwell.isActive()) {
        return;
    }
    if (m_delay <= 0) {
        fire();
    } else {
        m_dwell.start(m_delay);
    }
}

bool HotCorners::blocked(const Spot &spot) const
{
    const QString &action = m_settings[spot.corner].action;
    if (action.isEmpty() || action == QLatin1String("none")) {
        return true;
    }
    if (KWin::waylandServer() && KWin::waylandServer()->isScreenLocked()) {
        return true;
    }
    KWin::PointerInputRedirection *pointer = KWin::input()->pointer();
    // A game holding the pointer, a button held for a drag or a window being
    // moved: the pointer reached the corner on its way somewhere else.
    if (pointer->isConstrained() || pointer->areButtonsPressed() || KWin::workspace()->moveResizeWindow()) {
        return true;
    }
    if (m_skipFullScreen) {
        const KWin::Window *active = KWin::workspace()->activeWindow();
        if (active && active->isFullScreen() && active->frameGeometry().contains(spot.point)) {
            return true;
        }
    }
    return false;
}

// A corner without a key fires whatever is held. One with a key fires only
// while exactly that key, or pair of keys, is down.
bool HotCorners::keyHeld(Corner corner) const
{
    const Qt::KeyboardModifiers wanted = m_settings[corner].key;
    if (wanted == Qt::NoModifier) {
        return true;
    }
    return (KWin::input()->keyboardModifiers() & keyMask) == wanted;
}

void HotCorners::fire()
{
    const Spot spot = m_spots[m_current];
    m_dwell.stop();
    m_fired = true;
    m_firedAt = spot.point;
    const Qt::KeyboardModifiers held = KWin::input()->keyboardModifiers() & keyMask;
    if (held & Qt::MetaModifier) {
        forgetModifierOnlyPress();
    }
    run(m_settings[spot.corner].action, spot);
}

void HotCorners::run(const QString &action, const Spot &spot)
{
    if (action == QLatin1String("taskview")) {
        invokeShortcut(QStringLiteral("kwin"), QStringLiteral("Overview"));
    } else if (action == QLatin1String("appwindows")) {
        invokeShortcut(QStringLiteral("kwin"), QStringLiteral("ExposeClassCurrentDesktop"));
    } else if (action == QLatin1String("desktop")) {
        invokeShortcut(QStringLiteral("kwin"), QStringLiteral("Wild OS show desktop"));
    } else if (action == QLatin1String("appgrid")) {
        askShell(QStringLiteral("ToggleAllApps"), spot.point);
    } else if (action == QLatin1String("notifications")) {
        askShell(QStringLiteral("ToggleNotifications"), spot.point);
    } else if (action == QLatin1String("quicknote")) {
        invokeShortcut(QStringLiteral("org_wildos_notes_desktop"), QStringLiteral("QuickNote"));
    } else if (action == QLatin1String("lock")) {
        lock();
    } else if (action == QLatin1String("screenoff")) {
        m_screenOff.start();
    } else if (action == QLatin1String("keepawake")) {
        toggleKeepAwake(spot);
    } else if (action == QLatin1String("dnd")) {
        toggleDoNotDisturb(spot);
    }
}

// The same path as the keyboard shortcut, so a corner and the keys always
// do the same thing.
void HotCorners::invokeShortcut(const QString &component, const QString &name)
{
    QDBusMessage message = call(QStringLiteral("org.kde.kglobalaccel"), QLatin1String("/component/") + component,
                                QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("invokeShortcut"));
    message << name;
    QDBusConnection::sessionBus().asyncCall(message);
}

// Start and the clock live in plasmashell, which takes these through the
// org.wildos.shell plugin (ShellRequests).
void HotCorners::askShell(const QString &method, const QPointF &point)
{
    QDBusMessage message = call(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/wildos/Shell"), QStringLiteral("org.wildos.Shell"), method);
    message << int(point.x()) << int(point.y());
    QDBusConnection::sessionBus().asyncCall(message);
}

void HotCorners::lock()
{
    QDBusConnection::sessionBus().asyncCall(call(screenSaverService, screenSaverPath, screenSaverService, QStringLiteral("Lock")));
}

// Only the displays go off: nothing locks, and the next key press, click or
// movement brings them back (KWin's own wake-up on input).
void HotCorners::turnScreenOff()
{
    for (KWin::Output *output : KWin::workspace()->outputs()) {
        if (output->capabilities() & KWin::Output::Capability::Dpms) {
            output->setDpmsMode(KWin::Output::DpmsMode::Off);
        }
    }
}

void HotCorners::toggleKeepAwake(const Spot &spot)
{
    const bool on = !m_keepAwake;
    applyKeepAwake(on);
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    config->group(QStringLiteral("KeepAwake")).writeEntry("Active", on, KConfig::Notify);
    config->sync();
    showToast(on ? QT_TRANSLATE_NOOP("HotCorners", "Keep awake on") : QT_TRANSLATE_NOOP("HotCorners", "Keep awake off"), spot);
}

// Two blocks, as one does not cover everything: powerdevil's, for dimming,
// turning the screen off and sleep, and the screen locker's, for locking
// after the idle time. Both end on their own if KWin goes away.
void HotCorners::applyKeepAwake(bool on)
{
    if (on == m_keepAwake) {
        return;
    }
    m_keepAwake = on;
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString reason = QStringLiteral("Keep awake is on");

    if (on) {
        // Types 1 and 4: sleep, and dimming or turning off the screen.
        QDBusMessage policy = call(policyService, policyPath, policyService, QStringLiteral("AddInhibition"));
        policy << uint(1 | 4) << QStringLiteral("Wild OS") << reason;
        m_policyPending = true;
        auto policyWatcher = new QDBusPendingCallWatcher(bus.asyncCall(policy), this);
        connect(policyWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            m_policyPending = false;
            const QDBusPendingReply<uint> reply = *watcher;
            if (reply.isError()) {
                return;
            }
            m_policyCookie = reply.value();
            // Turned off again before powerdevil answered.
            if (!m_keepAwake) {
                QDBusMessage release = call(policyService, policyPath, policyService, QStringLiteral("ReleaseInhibition"));
                release << m_policyCookie;
                QDBusConnection::sessionBus().asyncCall(release);
                m_policyCookie = 0;
            }
        });

        QDBusMessage screenSaver = call(screenSaverService, screenSaverPath, screenSaverService, QStringLiteral("Inhibit"));
        screenSaver << QStringLiteral("Wild OS") << reason;
        m_screenSaverPending = true;
        auto screenSaverWatcher = new QDBusPendingCallWatcher(bus.asyncCall(screenSaver), this);
        connect(screenSaverWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            m_screenSaverPending = false;
            const QDBusPendingReply<uint> reply = *watcher;
            if (reply.isError()) {
                return;
            }
            m_screenSaverCookie = reply.value();
            if (!m_keepAwake) {
                QDBusMessage release = call(screenSaverService, screenSaverPath, screenSaverService, QStringLiteral("UnInhibit"));
                release << m_screenSaverCookie;
                QDBusConnection::sessionBus().asyncCall(release);
                m_screenSaverCookie = 0;
            }
        });
        return;
    }

    if (m_policyCookie != 0) {
        QDBusMessage release = call(policyService, policyPath, policyService, QStringLiteral("ReleaseInhibition"));
        release << m_policyCookie;
        bus.asyncCall(release);
        m_policyCookie = 0;
    }
    if (m_screenSaverCookie != 0) {
        QDBusMessage release = call(screenSaverService, screenSaverPath, screenSaverService, QStringLiteral("UnInhibit"));
        release << m_screenSaverCookie;
        bus.asyncCall(release);
        m_screenSaverCookie = 0;
    }
}

// Do not disturb is the notification server's own setting in plasmanotifyrc,
// written as the switch in the notifications panel and quick settings write
// it: on means until a year from now, off clears it. Both read the file live,
// so they follow at once.
void HotCorners::toggleDoNotDisturb(const Spot &spot)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    config->reparseConfiguration();
    KConfigGroup group = config->group(QStringLiteral("DoNotDisturb"));
    const QDateTime until = group.readEntry("Until", QDateTime());
    const bool on = !(until.isValid() && until > QDateTime::currentDateTime());
    if (on) {
        group.writeEntry("Until", QDateTime::currentDateTime().addYears(1), KConfig::Notify);
    } else {
        group.deleteEntry("Until", KConfig::Notify);
    }
    config->sync();
    showToast(on ? QT_TRANSLATE_NOOP("HotCorners", "Do not disturb on") : QT_TRANSLATE_NOOP("HotCorners", "Do not disturb off"), spot);
}

void HotCorners::showToast(const char *text, const Spot &spot)
{
    if (!spot.output) {
        return;
    }
    if (!m_toast) {
        m_engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(m_engine.get(), QUrl(QStringLiteral("qrc:/wildos/hotcorners/Toast.qml")));
        std::unique_ptr<QObject> object(component.create());
        auto view = qobject_cast<QQuickWindow *>(object.get());
        if (!view) {
            qWarning() << "Wild OS hot corners:" << component.errors();
            m_engine.reset();
            return;
        }
        object.release();
        m_toast.reset(view);
    }
    // Inside the taskbar, so the bottom corners' message sits above it.
    const QRectF area = KWin::workspace()->clientArea(KWin::MaximizeArea, spot.output, KWin::VirtualDesktopManager::self()->currentDesktop());
    QMetaObject::invokeMethod(m_toast.get(), "showAt",
                              Q_ARG(QVariant, QCoreApplication::translate("HotCorners", text)),
                              Q_ARG(QVariant, area),
                              Q_ARG(QVariant, int(spot.corner)));
}

}
