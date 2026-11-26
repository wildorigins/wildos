#pragma once

#include "loading.h"

#include <QBackingStore>
#include <QElapsedTimer>
#include <QImage>
#include <QMargins>
#include <QTimer>
#include <QRegion>
#include <QWindow>

#include <memory>

class Client;
class QPainter;
class Rail;

// One window on the desktop. It starts either as a Windows app's window
// (RemoteApp) or as the loading window shown the moment an app is asked
// for, which becomes that app's window when Windows shows it.
class RailWindow : public QWindow
{
    Q_OBJECT

public:
    RailWindow(Rail *rail, Client *client, const QString &appId);
    ~RailWindow() override;

    // The Windows window this shows; 0 while it is still loading.
    quint32 remoteId() const { return m_id; }
    QString appId() const { return m_appId; }
    bool loading() const { return m_loading; }
    bool isPopup() const { return m_popup; }
    // Covers a whole monitor in Windows with no frame: full screen here too.
    bool remoteFullScreen() const { return m_fullScreen; }
    // Returns true when the window had to be made again (a popup until now)
    // and is to be shown afresh.
    bool setRemoteFullScreen(bool full, QScreen *screen);
    // Where Windows has the window, in its desktop's pixels.
    QRect frame() const { return m_frame; }
    quint32 owner() const { return m_owner; }
    quint32 style() const { return m_style; }
    quint32 exStyle() const { return m_exStyle; }
    bool remoteVisible() const;
    bool remoteMinimized() const { return m_showState == 0x02; }

    // A loading window (boards 9.11 to 9.13).
    void startLoading(const QString &title, const QIcon &icon, const QSize &size);
    void stopLoading();
    void setLoadingStage(LoadingView::Stage stage);
    void setLoadingEnded();
    void setLoadingReason(const QString &reason, int percent, const QString &progress);
    // Hands the window over to Windows window `id`. It keeps showing the
    // loading mark until the first picture of the app arrives.
    void adopt(quint32 id);
    // The window it took turned out not to be the app's: back to waiting.
    void unadopt();
    // Once adopted and placed: asks Windows for the app at this window's
    // size, and starts the closing sequence when it arrives.
    void beginHandoff();

    void setRemoteState(quint32 id, quint32 style, quint32 exStyle, bool popup);
    void setOwner(quint32 owner) { m_owner = owner; }
    void setFrame(const QRect &frame);
    void setShowState(quint32 state);
    void setRemoteTitle(const QString &title);
    void setIcon(const QImage &image, bool big);
    void setAppId(const QString &appId);
    void setMinMax(const QSize &minTrack, const QSize &maxTrack);

    void surfaceDamaged(quint16 surfaceId, const QRegion &damage, const QSize &size);
    void primaryDamaged(const QRegion &desktopDamage);
    // Windows started moving or sizing the window from its own title bar or
    // edge; the compositor takes over until the button is let go.
    void beginLocalMoveSize(quint16 type);
    void endLocalMoveSize();
    // Maximised here and Windows not yet told, or told and not yet
    // answered: Windows' own show state is not this window's meanwhile.
    bool localMaximisePending() const { return m_maximiseAfterMove || m_maximiseSent; }

    // The window's size in device pixels for a size in Windows' pixels.
    QSize logicalSize(const QSize &remote) const;
    // Windows' pixels per logical unit for this window: the scale of the
    // monitor it is on in Windows.
    double scale() const;
    // Moves the window in Windows to the monitor of the screen it is on
    // here, where Windows draws it at that screen's scale. Windows cannot
    // see where the compositor puts a window, so this is how it learns.
    void followScreen();
    // The window's pixels as Windows last sent them.
    QImage snapshot() const;
    // The loading window as it looks now.
    QImage loadingSnapshot(qreal dpr) const;

protected:
    bool event(QEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void render();
    void renderLoading();
    void paintLoading(QPainter &p) const;
    void loadingPress(QMouseEvent *event);
    void appReady();
    // The app's window has drawn (at this size, in Windows' pixels).
    void handoffContent(const QSize &drawn);
    // The loading window's size changes about its centre, through KWin.
    void resizeAroundCentre(const QSize &to);
    // The size the window is on its way to, or has.
    QSize intendedSize() const;
    void renderRemote();
    QPoint toRemote(const QPointF &local) const;
    void sendButton(QMouseEvent *event, bool down);
    void releaseKeys();
    void sendIcon();
    void finishMoveSize(const QPointF &local);
    // The compositor maximised the window: Windows maximises the app.
    void sendMaximise();
    void maximiseTimeout();
    // Windows' work area for the monitor of this screen: KWin's, or until
    // KWin has said, as big as a window maximised here at this logical size.
    void sendWorkArea(const QSize &logical);
    // Where a window this size (in the target's pixels) goes on monitor
    // `to`, at about the same place it had on `from`.
    QRect placeOn(int from, int to, const QSize &size) const;
    // Asks Windows to move the window to monitor `to`, at this logical size.
    void moveToMonitor(int to, const QSize &logical);
    // A frame from Windows while a move to another monitor is under way;
    // true when the window here keeps its size meanwhile.
    bool followFrame();
    // Asks for a render on the compositor's next frame, once per frame.
    void scheduleRender();

    Rail *m_rail;
    Client *m_client;
    QString m_appId;
    std::unique_ptr<QBackingStore> m_store;

    quint32 m_id = 0;
    quint32 m_style = 0;
    quint32 m_exStyle = 0;
    quint32 m_owner = 0;
    quint32 m_showState = 0x05;
    bool m_popup = false;
    bool m_fullScreen = false;
    bool m_layered = false;
    // A popup's own transparency, looked at once in its first picture.
    bool m_alpha = false;
    bool m_alphaChecked = false;
    QRect m_frame;

    // Pixels: the window's surface in the graphics pipeline, or its part
    // of the whole desktop picture when the server draws without one.
    quint16 m_surfaceId = 0;
    bool m_hasSurface = false;
    QSize m_surfaceSize;
    QRegion m_damage;
    bool m_primary = false;
    bool m_hasContent = false;
    bool m_updateQueued = false;

    // Loading.
    bool m_loading = false;
    bool m_loadingPainted = false;
    bool m_fullRepaint = true;
    bool m_wasSlow = false;
    LoadingView m_view;
    QTimer m_handoffTimer;
    QSize m_handoffSize;
    // Windows was asked to resize the app to this window.
    bool m_handoffResizing = false;
    QSize m_targetSize;
    QImage m_loadingBackground;
    QImage m_remoteIcon;
    bool m_iconSent = false;

    // Input.
    QList<quint32> m_keysDown;
    int m_wheelX = 0;
    int m_wheelY = 0;
    int m_pinch = 0;
    bool m_moveSize = false;
    quint16 m_moveSizeType = 0;
    QPoint m_pressRemote;
    QRect m_moveSizeStart;
    // Maximised by the compositor at the end of a move Windows started:
    // Windows is asked to maximise once its move loop is over.
    bool m_maximiseAfterMove = false;
    bool m_maximiseSent = false;
    QTimer m_maximiseTimer;

    // A move to another of Windows' monitors: which one, the size the window
    // keeps here, and whether Windows has been asked once more for that
    // size after the move (an app that rescaled itself twice, or not at
    // all, when its scale changed).
    struct Follow {
        int monitor = -1;
        QSize logical;
        bool corrected = false;
        QElapsedTimer since;
    };
    Follow m_follow;
    // Quiet for a moment after the last frame: the app has rescaled.
    QTimer m_followSettle;
    QTimer m_screenSettle;
    void followSettled();
};
