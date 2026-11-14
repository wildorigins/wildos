/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <input.h>
#include <plugin.h>

#include <QElapsedTimer>
#include <QPointer>
#include <QVariantMap>

#include <memory>

class QQmlEngine;
class QQuickWindow;

namespace KWin
{
class Window;
}

namespace WildOS
{

// The window menu of MenusShell.dc ("Window title bar"): Minimise,
// Maximise, Snap, Move to desktop, Keep on top, Move and Close, drawn with
// the shared Wild OS menu instead of KWin's widget menu. It opens on a
// right click on a title bar KWin draws, on the shortcut KWin calls
// "Window Operations Menu" (Alt+Space in Wild OS), and over D-Bus
// (org.wildos.WindowMenu ShowAtPointer) for the apps that draw their own
// title bar with WildOS.UI.
class WindowMenu : public KWin::Plugin, public KWin::InputEventFilter
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.WindowMenu")

public:
    WindowMenu();
    ~WindowMenu() override;

    bool pointerButton(KWin::PointerButtonEvent *event) override;

    // Called by WindowMenu.qml when a row is chosen.
    Q_INVOKABLE void act(const QString &action, const QVariant &argument = {});

public Q_SLOTS:
    Q_SCRIPTABLE void ShowAtPointer();
    // A double click on a resize edge, as on Windows: top or bottom stretches
    // the window under the pointer to the screen's height, left or right to
    // its width, a corner fills the screen. For apps that draw their own
    // frame with WildOS.UI (ResizeHandles); edges is Qt::Edges.
    Q_SCRIPTABLE void MaximizeEdgeAtPointer(int edges);

private:
    void takeOverShortcut();
    void showForActive();
    void show(KWin::Window *window, const QPointF &position);
    QVariantMap describe(KWin::Window *window) const;
    bool ensureView();
    bool edgeDoubleClick(KWin::PointerButtonEvent *event);
    void maximizeForEdges(KWin::Window *window, Qt::Edges edges);

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_view;
    // The last press in the resize margin of a window that draws its own
    // frame, to tell a double click there.
    QElapsedTimer m_edgeClick;
    QPointer<KWin::Window> m_edgeWindow;
    Qt::Edges m_edgeEdges;
    QPointF m_edgePosition;
    bool m_swallowLeftRelease = false;
    QPointer<KWin::Window> m_window;
    bool m_swallowRelease = false;
};

}
