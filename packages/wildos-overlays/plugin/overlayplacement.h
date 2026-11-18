#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlParserStatus>
#include <QWindow>

// Puts a window on an edge or in the middle of a screen, above other
// windows, the way the shell's own surfaces sit: toasts in the top right
// corner, the volume pill above the taskbar, the Caps Lock square in the
// centre, the screenshot overlay over everything.
//
// On Wayland an ordinary window cannot choose where it goes, so this makes
// it a layer-shell surface anchored to the chosen edges. It must be set up
// before the window is first shown, so declare the window with
// `visible: false` and show it later. Under X11 (the dev rig) the window is
// moved there instead.
class OverlayPlacement : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY changed)
    Q_PROPERTY(Edges edges READ edges WRITE setEdges NOTIFY changed)
    Q_PROPERTY(int marginTop READ marginTop WRITE setMarginTop NOTIFY changed)
    Q_PROPERTY(int marginRight READ marginRight WRITE setMarginRight NOTIFY changed)
    Q_PROPERTY(int marginBottom READ marginBottom WRITE setMarginBottom NOTIFY changed)
    Q_PROPERTY(int marginLeft READ marginLeft WRITE setMarginLeft NOTIFY changed)
    Q_PROPERTY(Layer layer READ layer WRITE setLayer NOTIFY changed)
    Q_PROPERTY(Keyboard keyboard READ keyboard WRITE setKeyboard NOTIFY changed)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY changed)
    // KWin's sliding pop-ups effect: the window slides in from this edge
    // when it appears and back out when it goes, as notifications do on
    // Windows. It honours the system animation speed, so reduced motion
    // turns it off with everything else.
    Q_PROPERTY(Edge slideFrom READ slideFrom WRITE setSlideFrom NOTIFY changed)
    // Margins count from the edge of the screen itself rather than from
    // the edge of the space the taskbar leaves free, so a surface can sit a
    // set distance above the taskbar whether the taskbar keeps its strip
    // of the screen or hides.
    Q_PROPERTY(bool fromScreenEdge READ fromScreenEdge WRITE setFromScreenEdge NOTIFY changed)
    Q_PROPERTY(bool wayland READ wayland CONSTANT)

public:
    enum Edge {
        NoEdge = 0,
        Top = 1,
        Bottom = 2,
        Left = 4,
        Right = 8,
    };
    Q_DECLARE_FLAGS(Edges, Edge)
    Q_FLAG(Edges)
    Q_ENUM(Edge)

    enum Layer {
        Background,
        BelowWindows,
        AboveWindows,
        Overlay,
    };
    Q_ENUM(Layer)

    enum Keyboard {
        NoKeyboard,
        Exclusive,
        OnDemand,
    };
    Q_ENUM(Keyboard)

    explicit OverlayPlacement(QObject *parent = nullptr);

    void classBegin() override {}
    void componentComplete() override;

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    Edges edges() const { return m_edges; }
    void setEdges(Edges edges);
    int marginTop() const { return m_margins.top(); }
    void setMarginTop(int value);
    int marginRight() const { return m_margins.right(); }
    void setMarginRight(int value);
    int marginBottom() const { return m_margins.bottom(); }
    void setMarginBottom(int value);
    int marginLeft() const { return m_margins.left(); }
    void setMarginLeft(int value);
    Layer layer() const { return m_layer; }
    void setLayer(Layer layer);
    Keyboard keyboard() const { return m_keyboard; }
    void setKeyboard(Keyboard keyboard);
    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    Edge slideFrom() const { return m_slide; }
    void setSlideFrom(Edge edge);
    bool fromScreenEdge() const { return m_fromScreenEdge; }
    void setFromScreenEdge(bool value);
    bool wayland() const;

Q_SIGNALS:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply();
    void placeOnX11();

    QPointer<QWindow> m_window;
    Edges m_edges = NoEdge;
    QMargins m_margins;
    Layer m_layer = Overlay;
    Keyboard m_keyboard = NoKeyboard;
    QString m_scope = QStringLiteral("wildos-overlay");
    Edge m_slide = NoEdge;
    bool m_fromScreenEdge = false;
    bool m_complete = false;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(OverlayPlacement::Edges)
