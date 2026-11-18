#include "surface.h"

#include <KWindowEffects>
#include <KWindowSystem>
#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QPainterPath>
#include <QRegion>
#include <QScreen>

Surface::Surface(QObject *parent)
    : QObject(parent)
{
}

bool Surface::layerShell()
{
    return KWindowSystem::isPlatformWayland() && qEnvironmentVariableIsEmpty("WILDOS_DIALOGS_PREVIEW");
}

void Surface::setWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = window;
    m_prepared = false;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::visibleChanged, this, &Surface::shownChanged);
        connect(m_window, &QWindow::widthChanged, this, &Surface::applyBlur);
        connect(m_window, &QWindow::heightChanged, this, &Surface::applyBlur);
    }
    Q_EMIT changed();
}

void Surface::setRole(const QString &role)
{
    if (m_role != role) {
        m_role = role;
        m_prepared = false;
        Q_EMIT changed();
    }
}

void Surface::setScope(const QString &scope)
{
    if (m_scope != scope) {
        m_scope = scope;
        Q_EMIT changed();
    }
}

void Surface::setBlur(bool on)
{
    if (m_blur != on) {
        m_blur = on;
        applyBlur();
        Q_EMIT changed();
    }
}

void Surface::setBlurRect(const QRectF &rect)
{
    if (m_blurRect != rect) {
        m_blurRect = rect;
        applyBlur();
        Q_EMIT changed();
    }
}

void Surface::setBlurRadius(qreal radius)
{
    if (!qFuzzyCompare(m_blurRadius, radius)) {
        m_blurRadius = radius;
        applyBlur();
        Q_EMIT changed();
    }
}

void Surface::setParentHandle(const QString &handle)
{
    if (m_parentHandle != handle) {
        m_parentHandle = handle;
        Q_EMIT changed();
    }
}

// Layer-shell has to be chosen before the window has a platform surface,
// so this runs once, right before the first show.
void Surface::prepare()
{
    if (!m_window || m_prepared) {
        return;
    }
    m_prepared = true;
    m_window->setColor(Qt::transparent);

    if (m_role == QLatin1String("dialog")) {
        m_window->setFlags(m_window->flags() | Qt::FramelessWindowHint | Qt::Dialog);
        return;
    }

    if (!layerShell()) {
        m_window->setFlags(m_window->flags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                           | (fullScreen() ? Qt::BypassWindowManagerHint : Qt::Dialog));
        return;
    }

    auto *layer = LayerShellQt::Window::get(m_window);
    layer->setScope(m_scope);
    layer->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
    layer->setCloseOnDismissed(false);
    if (fullScreen()) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                        | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        // -1: ignore the taskbar's reserved strip and cover the whole screen.
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(m_role == QLatin1String("overlay") ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                                                        : LayerShellQt::Window::KeyboardInteractivityNone);
    } else {
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setAnchors(LayerShellQt::Window::AnchorNone);
        layer->setExclusiveZone(0);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    }
}

void Surface::show()
{
    if (!m_window) {
        return;
    }
    // A full-screen surface takes its screen's place and size before the
    // layer surface is set up: that makes the native window, and Qt puts a
    // new window on the screen that holds its position. Left at 0,0, every
    // screen's cover went to the first screen, over the dialog.
    if (fullScreen() && layerShell()) {
        if (QScreen *screen = m_window->screen()) {
            m_window->setGeometry(screen->geometry());
        }
    }
    prepare();
    if (fullScreen()) {
        if (layerShell()) {
            m_window->show();
        } else {
            m_window->showFullScreen();
        }
    } else {
        m_window->show();
    }
    if (m_role == QLatin1String("dialog") && !m_parentHandle.isEmpty()) {
        KWindowSystem::setMainWindow(m_window, m_parentHandle);
    }
    m_window->raise();
    m_window->requestActivate();
    applyBlur();
}

void Surface::hide()
{
    if (m_window) {
        m_window->hide();
    }
}

void Surface::applyBlur()
{
    if (!m_window || !m_window->isVisible()) {
        return;
    }
    const bool available = KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
    if (available != m_blurAvailable) {
        m_blurAvailable = available;
        Q_EMIT blurAvailableChanged();
    }
    if (!m_blur) {
        KWindowEffects::enableBlurBehind(m_window, false);
        return;
    }
    QRegion region;
    if (!m_blurRect.isEmpty()) {
        QPainterPath path;
        path.addRoundedRect(m_blurRect, m_blurRadius, m_blurRadius);
        region = QRegion(path.toFillPolygon().toPolygon());
        // Clicks on the clear margin kept for the shadow go to the window
        // underneath, not to this one.
        // On X11 a mask would also cut the shadow away.
        if (!fullScreen() && KWindowSystem::isPlatformWayland()) {
            m_window->setMask(region);
        }
    }
    KWindowEffects::enableBlurBehind(m_window, true, region);
}

bool Surface::eventFilter(QObject *watched, QEvent *event)
{
    // The compositor forgets the blur when the platform surface is
    // recreated, which happens on every hide and show of a layer surface.
    if (watched == m_window && event->type() == QEvent::Expose) {
        applyBlur();
    }
    return QObject::eventFilter(watched, event);
}
