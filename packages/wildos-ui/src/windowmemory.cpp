#include "windowmemory.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace
{
int windowsMade = 0;
}

WindowMemory::WindowMemory(QObject *parent)
    : QObject(parent)
    , m_first(windowsMade++ == 0)
{
    // Resizing and maximising send a burst of size and state changes in no
    // fixed order; the window is read once they have settled.
    m_settle.setSingleShot(true);
    m_settle.setInterval(400);
    connect(&m_settle, &QTimer::timeout, this, &WindowMemory::save);
    // An app that quits without closing its window (from its tray icon, at
    // sign-out) still keeps a change made in the last moment.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &WindowMemory::save);
}

void WindowMemory::setWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }
    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
        disconnect(m_window, nullptr, &m_settle, nullptr);
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::widthChanged, &m_settle, qOverload<>(&QTimer::start));
        connect(m_window, &QWindow::heightChanged, &m_settle, qOverload<>(&QTimer::start));
        connect(m_window, &QWindow::windowStateChanged, &m_settle, qOverload<>(&QTimer::start));
        connect(m_window, &QWindow::visibleChanged, this, &WindowMemory::shown);
    }
    Q_EMIT windowChanged();
}

void WindowMemory::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        Q_EMIT enabledChanged();
    }
}

void WindowMemory::setKey(const QString &key)
{
    if (m_key != key) {
        m_key = key;
        Q_EMIT keyChanged();
    }
}

// Runs once the app's own width, height and limits are in place and before
// the window is shown, so the app's size is what a first start gets and a
// remembered size never flashes in after it.
void WindowMemory::componentComplete()
{
    m_complete = true;
    if (m_window) {
        m_normal = m_window->size();
    }
    restore();
}

bool WindowMemory::active() const
{
    if (!m_complete || !m_enabled || !m_window || (m_key.isEmpty() && !m_first)) {
        return false;
    }
    return m_window->minimumWidth() != m_window->maximumWidth() && m_window->minimumHeight() != m_window->maximumHeight();
}

KConfigGroup WindowMemory::entry() const
{
    QString app = QGuiApplication::desktopFileName();
    if (app.isEmpty()) {
        app = QCoreApplication::applicationName();
    }
    // Made from the shared pointer, so the group keeps the file open: a
    // group taken from the file itself is left pointing at nothing once
    // the pointer goes.
    return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("wildoswindowsrc"), KConfig::SimpleConfig), app)
        .group(m_key.isEmpty() ? QStringLiteral("main") : m_key);
}

QSize WindowMemory::fitted(const QSize &size) const
{
    int width = std::min(size.width(), m_window->maximumWidth());
    int height = std::min(size.height(), m_window->maximumHeight());
    if (const QScreen *screen = m_window->screen()) {
        const QSize room = screen->availableGeometry().size();
        width = std::min(width, room.width());
        height = std::min(height, room.height());
    }
    return QSize(std::max(width, m_window->minimumWidth()), std::max(height, m_window->minimumHeight()));
}

void WindowMemory::restore()
{
    if (!active()) {
        return;
    }
    const KConfigGroup group = entry();
    const QSize saved(group.readEntry("Width", 0), group.readEntry("Height", 0));
    if (saved.width() > 0 && saved.height() > 0) {
        // On a smaller screen the window is made to fit, and the file keeps
        // the larger size until the window is resized by hand.
        m_normal = fitted(saved);
        m_window->resize(m_normal);
    }
    m_maximised = group.readEntry("Maximised", false);
    m_maximiseOnShow = m_maximised;
    if (m_window->isVisible()) {
        shown(true);
    }
}

// Showing a window from QML puts it back in the normal state first, so the
// maximised state can only be asked for once that has happened. This runs
// before the window reaches the compositor, which then opens it maximised
// with the remembered size as the one to return to.
void WindowMemory::shown(bool visible)
{
    if (!visible || !m_maximiseOnShow) {
        return;
    }
    m_maximiseOnShow = false;
    if (active() && m_window->windowStates() == Qt::WindowNoState) {
        m_window->setWindowStates(Qt::WindowMaximized);
    }
}

bool WindowMemory::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::Close) {
        save();
    }
    return QObject::eventFilter(watched, event);
}

void WindowMemory::save()
{
    m_settle.stop();
    if (!active() || !m_window->isVisible()) {
        return;
    }
    const Qt::WindowStates states = m_window->windowStates();
    if (states & (Qt::WindowMinimized | Qt::WindowFullScreen)) {
        return;
    }
    const bool maximised = states.testFlag(Qt::WindowMaximized);
    // A maximised window's size is the screen's; the size to return to is
    // the last one it had while it was not.
    const QSize normal = maximised ? m_normal : m_window->size();
    if (maximised == m_maximised && normal == m_normal) {
        return;
    }
    m_maximised = maximised;
    m_normal = normal;
    KConfigGroup group = entry();
    if (!normal.isEmpty()) {
        group.writeEntry("Width", normal.width());
        group.writeEntry("Height", normal.height());
    }
    group.writeEntry("Maximised", maximised);
    group.sync();
}
