// wildos-cooler-faces: draws the face chosen in Cooler on the cooler's
// screen. It runs in the person's session (wildos-cooler-faces.service),
// reads the choices Cooler writes to wildoscoolerrc, and hands frames to
// wildos-coolerd, which owns the cooler. Nothing goes online.
//
// It needs OpenGL without a window, which it gets from the session's own
// display: an offscreen surface on Wayland, or on X11 where a test runs it
// under Xvfb.

#include "streamer.h"

#include <KSignalHandler>

#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTimer>

#include <csignal>
#include <cstdio>

Q_IMPORT_QML_PLUGIN(WildOS_Cooler_FacesPlugin)

int main(int argc, char **argv)
{
    // On a PC with no cooler there is nothing to draw and nobody to ask.
    // The one thing left to do is to take Cooler out of Start if a cooler
    // was there the last time anyone looked. A test's pretend cooler has no
    // device, so a test skips this.
    const bool pretend = qEnvironmentVariable("WILDOS_COOLER_BUS") == QLatin1String("session");
    if (!pretend && !checkLauncher() && !QFileInfo::exists(QStringLiteral("/dev/wildos-cooler"))) {
        return 0;
    }

    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "wayland");
        } else if (qEnvironmentVariableIsSet("DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        } else {
            // Started before the desktop is up, or outside one. The
            // session starts it again once it has a display.
            std::fputs("wildos-cooler-faces: no display to draw the faces with yet; leaving the cooler on its own screen\n", stderr);
            return 0;
        }
    }

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationDomain(QStringLiteral("wildos.org"));
    QCoreApplication::setApplicationName(QStringLiteral("wildos-cooler-faces"));
    QGuiApplication::setQuitOnLastWindowClosed(false);

    Streamer streamer;

    // The end of the session, or a stop from systemd: the socket is closed
    // so the cooler goes back to its own screen, and the brightness is put
    // back if it was dimmed. A moment is left for those to reach the bus.
    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [&streamer]() {
        streamer.stop();
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
    });

    return app.exec();
}
