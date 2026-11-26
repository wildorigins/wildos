#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMargins>
#include <QObject>
#include <QRect>
#include <QRegion>

#include "monitors.h"

#include <freerdp/client.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/rail.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/freerdp.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class Client;
class Clipboard;

// FreeRDP allocates the context; ContextSize makes room for this.
struct WrdpContext {
    rdpClientContext common;
    Client *client;
};

// One RemoteApp window order, copied off FreeRDP's thread. Only the fields
// named in `fields` (WINDOW_ORDER_FIELD_*) carry news.
struct RailOrder {
    quint32 id = 0;
    quint32 fields = 0;
    bool created = false;
    quint32 owner = 0;
    quint32 style = 0;
    quint32 exStyle = 0;
    quint32 showState = 0;
    QString title;
    QPoint offset;
    QSize size;
    QPoint clientOffset;
    QSize clientSize;
    QPoint visibleOffset;
    QMargins resizeMargins;
    QList<QRect> visibility;
};
Q_DECLARE_METATYPE(RailOrder)

// The FreeRDP session: settings, the connection thread and its callbacks.
// Everything FreeRDP reports arrives here on its own threads and leaves as
// queued signals for the GUI thread; the GUI thread sends input and
// RemoteApp requests straight through, which FreeRDP allows from any
// thread.
class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    // FreeRDP's own arguments, as xfreerdp3 takes them; the password among
    // them. Returns false with the reason in `error` when they do not parse.
    bool configure(const QList<QByteArray> &args, QString *error);
    // The output the session is laid out for, in physical pixels, and the
    // scale Windows should draw at.
    void setDisplay(const QSize &physical, double scale);
    // Every screen, for a RemoteApp session on more than one: Windows then
    // has a monitor for each, at its own scale, and maximises an app to
    // the monitor it is on. Set before start().
    void setLayout(const QList<Monitor> &layout);
    // A new layout for a session already open (a screen plugged in or
    // taken away), through the display control channel. False when
    // Windows cannot take one now.
    bool sendLayout(const QList<Monitor> &layout);
    // The layout every connection and reconnection starts with: the one
    // given to setLayout(), whatever was sent since.
    QList<Monitor> connectLayout() const { return m_layout; }
    // After a reconnection Windows has the first layout again.
    void layoutReset() { m_origin = m_layout.size() > 1 ? monitors::bounds(m_layout).topLeft() : QPoint(); }
    void start();
    void stop();
    void setPrepareOnly(bool on) { m_prepareOnly = on; }
    bool running() const { return m_running; }

    rdpContext *context() const;
    QSize desktopSize() const;
    // Where the desktop picture's top left corner is on Windows' desktop:
    // left of or above 0,0 when a monitor is left of or above the primary.
    QPoint desktopOrigin() const { return m_origin; }

    // Input, in Windows' desktop coordinates.
    void sendMouse(quint16 flags, int x, int y);
    void sendExtendedMouse(quint16 flags, int x, int y);
    void sendWheel(quint16 flags, int x, int y);
    void sendKey(bool down, bool repeat, quint32 scancode);
    void sendUnicode(bool down, quint16 code);
    void sendPause();
    void sendFocusIn(quint16 toggles);

    // RemoteApp requests.
    void activate(quint32 id, bool active);
    void sysCommand(quint32 id, quint16 command);
    void moveWindow(quint32 id, const QRect &frame);
    void systemMenu(quint32 id, int x, int y);
    void setWorkArea(const QRect &area);
    // Asks Windows to send this part of its desktop again.
    void refresh(const QRect &area);
    // Asks Windows which process a window belongs to (windowProcess).
    void requestProcess(quint32 id);

    // Calls `paint` with a window's pixels from the graphics pipeline,
    // under the lock FreeRDP decodes into them with, so the copy is never
    // torn. False when the surface is gone.
    bool withSurface(quint16 surfaceId, const std::function<void(const uchar *data, int stride, QSize size)> &paint);
    // The same for the whole desktop picture when the server draws without
    // the graphics pipeline.
    bool withPrimary(const std::function<void(const uchar *data, int stride, QSize size)> &paint);

    Clipboard *clipboard() const { return m_clipboard; }

Q_SIGNALS:
    void connected();
    // The session ended. `error` is FreeRDP's name for why, empty when it
    // ended normally; `authFailed` when Windows refused the account.
    void finished(const QString &error, bool authFailed);
    void windowChanged(const RailOrder &order);
    void windowDeleted(quint32 id);
    void windowIcon(quint32 id, const QImage &icon, bool big);
    void desktopChanged(quint32 activeId, bool hasActive, const QList<quint32> &zOrder, bool hasZOrder);
    void surfaceDamaged(quint64 windowId, quint16 surfaceId, const QRegion &damage, const QSize &size);
    void primaryDamaged(const QRegion &damage);
    void localMoveSize(quint32 id, bool start, quint16 type, int x, int y);
    void minMaxInfo(quint32 id, const QSize &minTrack, const QSize &maxTrack);
    void pointerImage(const QImage &image, const QPoint &hotspot);
    void pointerHidden();
    void pointerDefault();
    void execFailed(quint16 result);
    // Windows has sent every window already open in the session (the end of
    // its desktop sync); the connection's app is asked for now.
    void desktopSynced();
    void windowCloaked(quint32 id, bool cloaked);
    // The connection dropped and is being made again, and is back.
    void connectionLost();
    void connectionBack();
    // Windows has this client's settings, FreeRDP's own: among them a work
    // area of the whole desktop, which a setWorkArea() from now on replaces.
    void railStarted();
    // Windows can take a new monitor layout (sendLayout) from now on.
    void displayControlReady();
    // The process behind a window: its image (full path), App User Model
    // ID and process id.
    void windowProcess(quint32 id, const QString &image, const QString &appUserModel, quint32 pid);

private:
    friend struct ClientCallbacks;

    void run();
    void queueSurfaceDamage(quint64 windowId, quint16 surfaceId, const QRegion &damage, const QSize &size);
    void queuePrimaryDamage(const QRegion &damage);
    void deliverDamage();

    rdpContext *m_context = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running = false;
    std::atomic<bool> m_stopping = false;
    QSize m_physical;
    double m_scale = 1.0;
    QList<Monitor> m_layout;
    QPoint m_origin;

    std::mutex m_channels;
    // The connection's app has been asked for; a reconnection must not ask
    // again.
    std::atomic_bool m_appRequested = false;
    // Only signing in: the session is made ready and no app is asked for.
    bool m_prepareOnly = false;
    RailClientContext *m_rail = nullptr;
    RdpgfxClientContext *m_gfx = nullptr;
    DispClientContext *m_disp = nullptr;
    std::atomic_bool m_dispReady = false;
    Clipboard *m_clipboard = nullptr;

    // Damage collects here between deliveries, so a slow GUI thread gets
    // one update per window instead of a queue of them.
    struct Damage {
        quint16 surfaceId = 0;
        QRegion region;
        QSize size;
    };
    std::mutex m_damageLock;
    QHash<quint64, Damage> m_damage;
    QRegion m_primaryDamage;
    bool m_deliveryQueued = false;

    // The RemoteApp icon cache, used only on FreeRDP's thread.
    QHash<quint32, QImage> m_icons;
};
