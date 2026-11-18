#include "launcher.h"
#include "preload.h"

#include <KWindowSystem>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QGuiApplication>
#include <QTextStream>

#include <cstring>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--preload") == 0) {
            QCoreApplication core(argc, argv);
            core.setApplicationName(QStringLiteral("wildos-launchd"));
            return Preload::run();
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            QTextStream(stdout) << "wildos-launchd " << WILDOS_LAUNCH_VERSION << Qt::endl;
            return 0;
        }
    }

    // Never draws, so it needs no OpenGL. This Qt sets OpenGL up at start-up
    // for every Wayland client, which held about 30 MB all session; plain
    // shared-memory buffers take that to a few. Apps started from here get
    // the session's setting back.
    const QByteArray buffers = qgetenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION");
    qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
    QGuiApplication app(argc, argv);
    if (buffers.isEmpty()) {
        qunsetenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION");
    } else {
        qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", buffers);
    }
    app.setApplicationName(QStringLiteral("wildos-launchd"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationVersion(QStringLiteral(WILDOS_LAUNCH_VERSION));
    // Never shows a window, but KWin's launch feedback is a Wayland
    // interface, so it is a Wayland client all the same.
    if (!KWindowSystem::isPlatformWayland()) {
        QTextStream(stderr) << "wildos-launchd: needs a Wayland session" << Qt::endl;
        return 0;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.wildos.Launch"))) {
        QTextStream(stderr) << "wildos-launchd: already running" << Qt::endl;
        return 0;
    }

    Launcher launcher;
    bus.registerObject(QStringLiteral("/org/wildos/Launch"), &launcher, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
    return app.exec();
}
