#include "app.h"
#include "hosts.h"
#include "keychain.h"
#include "sessionwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QKeyEvent>
#include <QProcess>
#include <QQuickWindow>
#include <QUrl>
#include <QWidget>
#include <QWindow>
#include <QScreen>
#include <qpa/qplatformnativeinterface.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

App *App::instance()
{
    static App *app = new App;
    return app;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    App *a = instance();
    QJSEngine::setObjectOwnership(a, QJSEngine::CppOwnership);
    return a;
}

App::App(QObject *parent)
    : QObject(parent)
{
}

QString App::version() const
{
    return QStringLiteral(WILDOS_REMOTE_VERSION);
}

QString App::homePath() const
{
    return QDir::homePath();
}

bool App::fullScreenOn(QQuickWindow *window, const QString &screenName)
{
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!native || !app || !window || !window->handle()) {
        return false;
    }
    QScreen *screen = nullptr;
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->name() == screenName) {
            screen = s;
        }
    }
    auto *top = static_cast<xdg_toplevel *>(native->nativeResourceForWindow(QByteArrayLiteral("xdg_toplevel"), window));
    auto *output = screen ? static_cast<wl_output *>(native->nativeResourceForScreen(QByteArrayLiteral("output"), screen)) : nullptr;
    if (!top || !output) {
        return false;
    }
    // KWin keeps a window that is already full screen on its screen, so it
    // leaves full screen first.
    xdg_toplevel_unset_fullscreen(top);
    xdg_toplevel_set_fullscreen(top, output);
    wl_display_flush(app->display());
    return true;
}

QUrl App::testScript() const
{
    const QString path = qEnvironmentVariable("WILDOS_REMOTE_TEST");
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

void App::testShot(const QString &name)
{
    // A folder: every window Remote has open, as drawn (headless KWin has no
    // screenshot effect without a GPU).
    const QString dir = qEnvironmentVariable("WILDOS_REMOTE_TEST_SHOT");
    if (dir.isEmpty()) {
        return;
    }
    int n = 0;
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        if (!w->isVisible() || w->width() < 200) {
            continue;
        }
        QImage image;
        if (auto *quick = qobject_cast<QQuickWindow *>(w)) {
            image = quick->grabWindow();
        } else {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (widget->windowHandle() == w) {
                    image = widget->grab().toImage();
                }
            }
        }
        if (!image.isNull()) {
            image.save(QStringLiteral("%1/%2%3.png").arg(dir, name, n > 0 ? QStringLiteral("-%1").arg(n) : QString()));
            ++n;
        }
    }
}

void App::testType(const QString &text)
{
    for (const QChar c : text) {
        QWidget *target = QApplication::focusWidget();
        if (!target) {
            return;
        }
        const int key = c == QLatin1Char('\r') ? Qt::Key_Return : c.toUpper().unicode();
        const QString t = c == QLatin1Char('\r') ? QStringLiteral("\r") : QString(c);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, t);
        QApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, t);
        QApplication::sendEvent(target, &release);
    }
}

void App::testKey(int key)
{
    if (QWidget *target = QApplication::focusWidget()) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
    }
}

void App::testRun(const QString &command)
{
    if (qEnvironmentVariableIsSet("WILDOS_REMOTE_TEST")) {
        QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
    }
}

QObject *App::testSessionWindow() const
{
    SessionWindow *window = SessionWindow::instance(false);
    // It has no parent; QML must not take it for its own and collect it.
    if (window) {
        QJSEngine::setObjectOwnership(window, QJSEngine::CppOwnership);
    }
    return window;
}

void App::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void App::openTerminal(const QString &hostId)
{
    const QVariantMap host = Hosts::instance()->host(hostId);
    if (host.isEmpty()) {
        return;
    }
    Hosts::instance()->touch(hostId);
    const QString identity = host.value(QStringLiteral("ssh")).toMap().value(QStringLiteral("identity")).toString();
    auto open = [this, hostId] {
        SessionWindow *window = SessionWindow::instance();
        connect(window, &SessionWindow::homeRequested, this, &App::fromSession, Qt::UniqueConnection);
        // The host as saved now, which may have changed since the check.
        window->open(Hosts::instance()->host(hostId));
    };
    if (identity.isEmpty()) {
        open();
    } else {
        Keychain::instance()->prepare(identity, open);
    }
}

void App::openLocal()
{
    SessionWindow *window = SessionWindow::instance();
    connect(window, &SessionWindow::homeRequested, this, &App::fromSession, Qt::UniqueConnection);
    window->open({});
}

void App::sendKey(const QString &keyPath, const QString &hostId)
{
    QVariantMap host = Hosts::instance()->host(hostId);
    if (host.isEmpty()) {
        return;
    }
    QStringList exec{QStringLiteral("ssh-copy-id"), QStringLiteral("-i"), keyPath + QStringLiteral(".pub"),
                     QStringLiteral("-p"), QString::number(Hosts::instance()->port(host)),
                     QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=yes")};
    const QString user = host.value(QStringLiteral("user")).toString();
    exec.append((user.isEmpty() ? QString() : user + QLatin1Char('@')) + Hosts::instance()->hostName(host));
    host.insert(QStringLiteral("exec"), exec);
    SessionWindow *window = SessionWindow::instance();
    connect(window, &SessionWindow::homeRequested, this, &App::fromSession, Qt::UniqueConnection);
    window->open(host);
}

void App::fromSession(const QString &what)
{
    if (what == QLatin1String("settings")) {
        Q_EMIT settingsRequested();
    } else if (what.startsWith(QLatin1String("edit:"))) {
        Q_EMIT editRequested(what.mid(5));
    } else if (!what.isEmpty()) {
        Q_EMIT connectRequested(what);
    } else {
        Q_EMIT raiseRequested();
    }
}

void App::openArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        Q_EMIT raiseRequested();
        return;
    }
    const QVariantMap address = Hosts::instance()->parseAddress(arg);
    if (address.isEmpty()) {
        Q_EMIT raiseRequested();
        return;
    }
    Q_EMIT quickConnectRequested(address);
}
