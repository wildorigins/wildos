#pragma once

#include "client.h"

#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QScreen;
class RailWindow;

// The windows of the one RemoteApp session: one RailWindow per Windows
// window, the loading windows waiting for their app, and which Windows
// window each loading window turns into.
class Rail : public QObject
{
    Q_OBJECT

public:
    Rail(Client *client, double scale, QObject *parent = nullptr);
    ~Rail() override;

    // The scale Windows draws at on its primary monitor: Windows' pixels
    // per desktop unit, for anything not yet on a monitor.
    double scale() const { return m_scale; }
    void setScale(double scale) { m_scale = scale; }

    // Windows' monitors as Windows has them, the primary first.
    const QList<Monitor> &layout() const { return m_layout; }
    void setLayout(const QList<Monitor> &layout) { m_layout = layout; }
    // The screens changed: Windows is given their new layout, and every
    // window goes to the monitor of the screen it is on now.
    void relayout(const QList<Monitor> &layout);
    // The monitor for a screen here; -1 when Windows has none for it (a
    // screen plugged in while Windows could not be told).
    int monitorFor(const QScreen *screen) const;
    // Whether Windows has a monitor for exactly the screens there are now.
    // Not while a screen just went or came and the new layout has yet to
    // reach Windows: a window moved then lands in a layout Windows is about
    // to replace, and is not rescaled for its new monitor.
    bool layoutCurrent() const;
    // The part of each screen here that windows maximise into, by screen
    // name in logical units; Windows is given each monitor's as soon as it
    // can take it, and again whenever it or the layout changes.
    void setWorkAreas(const QHash<QString, QRectF> &areas);
    // A monitor's work area in Windows' pixels, as last given to Windows.
    // Invalid when KWin has not said, or the layout is about to change.
    QRect workAreaFor(int monitor) const;
    // The scale Windows draws a window at, by the monitor it is on there.
    double scaleFor(const QRect &frame) const;
    double scaleForScreen(const QScreen *screen) const;
    // The pointer is over a window Windows draws at this scale; its
    // pictures of the pointer are at that scale too.
    void pointerOver(double scale);

    // Shows a loading window for an app, at once or after `delayMs` if the
    // app has not opened by then. `program` is the app's program (a Windows
    // path or file name): the first app window from it takes the loading
    // window over, and every window from it gets the app's id. Returns the
    // loading window's token.
    quint32 prepare(const QString &appId, const QString &title, const QString &icon, const QString &program = {}, int delayMs = 0);
    // The program of a loading window that was opened without one.
    void setProgram(quint32 token, const QString &program);
    // The loading window of the app the connection itself asks for.
    void setCold(quint32 token);
    // News for a loading window from the agent: stage "almost" once the
    // app's process is up, and why it is taking longer when it knows.
    bool update(quint32 token, const QString &stage, const QString &reason, int percent, const QString &progress);
    // Closes a loading window that is still waiting.
    bool cancel(quint32 token, const QString &reason);
    // Which app a Windows window belongs to, from the agent.
    void setWindowApp(quint32 id, const QString &appId);
    // Every window, loading or not, closes.
    void closeAll();

    RailWindow *window(quint32 id) const { return m_windows.value(id); }
    RailWindow *loadingWindow(quint32 token) const
    {
        RailWindow *w = m_placeholders.value(token).window;
        return w ? w : m_loadingByToken.value(token).data();
    }
    QList<quint32> windowIds() const { return m_windows.keys(); }
    const QCursor &cursor() const { return m_cursor; }

    // Each app's last place on screen, kept between sessions.
    static QJsonObject placements();
    static void savePlacement(const QString &appId, const QPoint &at);

    // From the windows.
    void windowActivated(RailWindow *window, bool active);
    // Our close (taskbar, Alt+F4): off the screen now, Windows told after.
    void closeRequested(RailWindow *window);
    void loadingShown(quint32 token);
    void detailsRequested(quint32 token) { Q_EMIT loadingDetails(token); }
    void retryRequested(quint32 token) { Q_EMIT loadingRetry(token); }
    // A window's first picture is on screen.
    void windowDrawn(RailWindow *window);

Q_SIGNALS:
    // A Windows window appeared on screen, or went.
    void windowShown(quint32 id, const QString &title);
    void windowGone(quint32 id);
    // A loading window finished: "window" when its app's window took it
    // over, "timeout", "cancelled" or "ended" otherwise.
    void loadingFinished(quint32 token, const QString &reason, quint32 id);
    void loadingVisible(quint32 token, qint64 milliseconds);
    // "Show details" on a loading window that is taking longer.
    void loadingDetails(quint32 token);
    void loadingRetry(quint32 token);
    // A window closed here that Windows has not closed, with nothing unsaved
    // in it, and its program has no other window: it can be ended.
    void closeStalled(quint32 id, quint32 pid, const QString &program);

public Q_SLOTS:
    void notificationAction(uint notification, const QString &action);

private:
    void order(const RailOrder &order);
    void deleted(quint32 id);
    void icon(quint32 id, const QImage &image, bool big);
    void desktop(quint32 active, bool hasActive, const QList<quint32> &z, bool hasZ);
    void surface(quint64 id, quint16 surfaceId, const QRegion &damage, const QSize &size);
    void primary(const QRegion &damage);
    void moveSize(quint32 id, bool start, quint16 type, int x, int y);
    void minMax(quint32 id, const QSize &minTrack, const QSize &maxTrack);
    void stepAsideFor(quint32 id);
    void desktopSynced();
    void hideClosing(quint32 id);
    void setCursorAll(const QCursor &cursor);
    void sendWorkAreas();

    void realize(quint32 id);
    void process(quint32 id, const QString &image, const QString &appUserModel, quint32 pid);
    void cloak(quint32 id, bool cloaked);
    void giveBack(RailWindow *window);
    void noticeActivation(quint32 active);
    void closeUnanswered(quint32 id, bool end);
    void closeGrace(quint32 id, bool last);
    void notifyUnsaved(quint32 id);
    void reopen(quint32 id);
    void connectionLost();
    void connectionBack();
    bool ownedByShownWindow(const RailOrder &state) const;
    bool overlapsAppWindow(const QRect &rect) const;
    // The monitor a window covers as a full screen window (no frame, the
    // whole of a monitor), or -1.
    int fullScreenMonitor(const RailOrder &state) const;
    QScreen *screenOf(int monitor) const;
    // A window that went full screen in Windows, or left it.
    void updateFullScreen(RailWindow *window, const RailOrder &state);
    void log(quint32 id, const QString &what) const;
    RailWindow *parentFor(quint32 id) const;
    RailWindow *takePlaceholder(quint32 id, const QString &title, bool early);
    QString appIdFor(quint32 id) const;
    void loadingTimedOut(quint32 token);

    struct Pending {
        RailOrder state;
        QImage icon;
        bool iconBig = false;
    };

    Client *m_client;
    double m_scale;
    QList<Monitor> m_layout;
    // The layout for the screens now, when Windows has not taken it yet.
    QList<Monitor> m_wanted;
    QHash<QString, QRectF> m_workAreas;
    // Windows has FreeRDP's work area (the whole desktop) and takes ours
    // in its place from now until the connection drops.
    bool m_railStarted = false;
    double m_pointerScale = 0;
    QImage m_pointerImage;
    QPoint m_pointerHotspot;
    void setPointer();
    QHash<quint32, RailWindow *> m_windows;
    // Windows' windows that are known but not on screen (hidden, or a
    // popup still without a window to belong to).
    QHash<quint32, Pending> m_pending;
    QHash<quint32, QString> m_apps;
    QHash<quint32, QPair<quint16, QSize>> m_surfaces;
    QList<quint32> m_zOrder;
    quint32 m_active = 0;
    QCursor m_cursor;

    struct Placeholder {
        QPointer<RailWindow> window;
        QString title;
        QString program;
        QString appId;
        QIcon icon;
        QElapsedTimer age;
        QTimer *timeout = nullptr;
        // The connection's own app, asked for when it connected: its window
        // comes after Windows has sent the windows already open.
        bool cold = false;
    };
    QHash<quint32, Placeholder> m_placeholders;
    quint32 m_nextToken = 1;
    // Loading windows already handed to their app, still finishing, and
    // what they were waiting for, in case the window they took goes.
    QHash<quint32, QPointer<RailWindow>> m_loadingByToken;
    QHash<quint32, Placeholder> m_adopted;
    // Each window's program (file name, lower case), the ones asked about,
    // the ones waiting for the answer before they are shown, and Windows'
    // cloaked (hidden while it prepares them) windows.
    QHash<quint32, QString> m_processes;
    QSet<quint32> m_askedProcess;
    QSet<quint32> m_deciding;
    QSet<quint32> m_cloaked;
    // The apps of the programs being opened, and windows whose app the agent
    // named, which nothing here overrides.
    QHash<QString, QString> m_programApps;
    QHash<QString, QIcon> m_programIcons;
    QSet<quint32> m_agentApps;
    QHash<quint32, quint32> m_pids;

    // Windows closed here that Windows has not closed yet.
    struct Closing {
        QElapsedTimer since;
        QString title;
        quint32 pid = 0;
        QSet<quint32> dialogs;
        bool notified = false;
        bool hidden = false;
        qint64 firstDamage = -1;
        // Never shown: the window of a launch the person cancelled. Its
        // first picture is not a question.
        bool unwanted = false;
        // Its program never asks before closing: it is off the screen at
        // once, and its redrawing while Windows ends it is not a question.
        bool quiet = false;
        // Windows disabled it after the close: a modal question is up.
        bool disabled = false;
    };
    QHash<quint32, Closing> m_closing;
    // Programs whose launch the person cancelled on the loading window, with
    // when each cancelled launch stops counting (ms since the epoch).
    QHash<QString, QList<qint64>> m_cancelled;
    // On connecting, Windows first sends every window already open in the
    // session (an app the last connection left, another copy of the same
    // program) and only then starts the connection's app. Those windows are
    // never that app's and say nothing about its launch; an app opened in
    // the session meanwhile can be among them, so other loading windows
    // still take them.
    bool m_synced = false;
    bool m_syncWaiting = false;
    QSet<quint32> m_preexisting;
    bool takeCancelled(const QString &program);
    QHash<quint32, quint32> m_notifications;
    QElapsedTimer m_lostAt;
    QSet<quint32> m_hiddenByLoss;
};
