#include "overlayplacement.h"

#include <KWindowEffects>
#include <LayerShellQt/Window>

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>

OverlayPlacement::OverlayPlacement(QObject *parent)
    : QObject(parent)
{
}

bool OverlayPlacement::wayland() const
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

void OverlayPlacement::componentComplete()
{
    m_complete = true;
    apply();
}

void OverlayPlacement::setWindow(QWindow *window)
{
    if (m_window == window) {
        return;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::widthChanged, this, &OverlayPlacement::placeOnX11);
        connect(m_window, &QWindow::heightChanged, this, &OverlayPlacement::placeOnX11);
        connect(m_window, &QWindow::screenChanged, this, &OverlayPlacement::placeOnX11);
    }
    Q_EMIT changed();
    apply();
}

#define SETTER(name, member)            \
    if (member == value) {              \
        return;                         \
    }                                   \
    member = value;                     \
    Q_EMIT changed();                   \
    apply();

void OverlayPlacement::setEdges(Edges value)
{
    SETTER(edges, m_edges)
}

void OverlayPlacement::setMarginTop(int value)
{
    if (m_margins.top() == value) {
        return;
    }
    m_margins.setTop(value);
    Q_EMIT changed();
    apply();
}

void OverlayPlacement::setMarginRight(int value)
{
    if (m_margins.right() == value) {
        return;
    }
    m_margins.setRight(value);
    Q_EMIT changed();
    apply();
}

void OverlayPlacement::setMarginBottom(int value)
{
    if (m_margins.bottom() == value) {
        return;
    }
    m_margins.setBottom(value);
    Q_EMIT changed();
    apply();
}

void OverlayPlacement::setMarginLeft(int value)
{
    if (m_margins.left() == value) {
        return;
    }
    m_margins.setLeft(value);
    Q_EMIT changed();
    apply();
}

void OverlayPlacement::setLayer(Layer value)
{
    SETTER(layer, m_layer)
}

void OverlayPlacement::setKeyboard(Keyboard value)
{
    SETTER(keyboard, m_keyboard)
}

void OverlayPlacement::setScope(const QString &value)
{
    SETTER(scope, m_scope)
}

void OverlayPlacement::setSlideFrom(Edge value)
{
    SETTER(slideFrom, m_slide)
}

void OverlayPlacement::setFromScreenEdge(bool value)
{
    SETTER(fromScreenEdge, m_fromScreenEdge)
}

#undef SETTER

bool OverlayPlacement::eventFilter(QObject *watched, QEvent *event)
{
    // The slide is a property of the surface, which is made afresh every
    // time the window is shown.
    if (watched == m_window && event->type() == QEvent::Show) {
        if (m_slide != NoEdge) {
            KWindowEffects::SlideFromLocation from = KWindowEffects::NoEdge;
            switch (m_slide) {
            case Top:
                from = KWindowEffects::TopEdge;
                break;
            case Bottom:
                from = KWindowEffects::BottomEdge;
                break;
            case Left:
                from = KWindowEffects::LeftEdge;
                break;
            case Right:
                from = KWindowEffects::RightEdge;
                break;
            default:
                break;
            }
            KWindowEffects::slideWindow(m_window, from);
        }
        placeOnX11();
    }
    return QObject::eventFilter(watched, event);
}

void OverlayPlacement::apply()
{
    if (!m_complete || !m_window) {
        return;
    }
    if (!wayland()) {
        placeOnX11();
        return;
    }
    auto *surface = LayerShellQt::Window::get(m_window);
    if (!surface) {
        return;
    }
    surface->setScope(m_scope);
    surface->setLayer(LayerShellQt::Window::Layer(m_layer));
    surface->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivity(m_keyboard));
    surface->setAnchors(LayerShellQt::Window::Anchors(int(m_edges)));
    surface->setMargins(m_margins);
    // Never push other windows aside; these float over them. 0 places the
    // surface inside the space the taskbar leaves free, -1 on the whole
    // screen.
    surface->setExclusiveZone(m_fromScreenEdge || m_edges == (Top | Bottom | Left | Right) ? -1 : 0);
    surface->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
}

void OverlayPlacement::placeOnX11()
{
    if (!m_complete || !m_window || wayland()) {
        return;
    }
    QScreen *screen = m_window->screen() ? m_window->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }
    const QRect area = screen->geometry();
    const QSize size = m_window->size();
    int x = area.x() + (area.width() - size.width()) / 2;
    int y = area.y() + (area.height() - size.height()) / 2;
    const bool left = m_edges & Left;
    const bool right = m_edges & Right;
    const bool top = m_edges & Top;
    const bool bottom = m_edges & Bottom;
    if (left && right) {
        x = area.x() + m_margins.left();
        m_window->setWidth(area.width() - m_margins.left() - m_margins.right());
    } else if (left) {
        x = area.x() + m_margins.left();
    } else if (right) {
        x = area.x() + area.width() - size.width() - m_margins.right();
    }
    if (top && bottom) {
        y = area.y() + m_margins.top();
        m_window->setHeight(area.height() - m_margins.top() - m_margins.bottom());
    } else if (top) {
        y = area.y() + m_margins.top();
    } else if (bottom) {
        y = area.y() + area.height() - size.height() - m_margins.bottom();
    }
    m_window->setPosition(x, y);
}
