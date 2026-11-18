#include "clash.h"
#include "dbustypes.h"
#include "filechooser.h"
#include "windows.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QGuiApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    // The dialogs are QML on WildOS.UI; nothing here needs widgets.
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-portal"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setDesktopFileName(QStringLiteral("org.wildos.portal"));
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    Portal::registerTypes();

    QDBusConnection bus = QDBusConnection::sessionBus();
    auto portal = new FileChooserPortal(&app);
    auto clashes = new ClashService(&app);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), portal, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties)
        || !bus.registerObject(QStringLiteral("/org/wildos/Dialogs"), clashes, QDBusConnection::ExportAllSlots)) {
        qWarning("wildos-portal: could not export its objects");
        return 1;
    }
    // The clash name first: by the time the portal name is taken and
    // systemd counts the service as started, both work.
    const bool clashName = bus.registerService(QStringLiteral("org.wildos.Dialogs"));
    const bool portalName = bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.wildos"));
    if (!clashName && !portalName) {
        qWarning("wildos-portal: already running");
        return 1;
    }
    // Windows quits the service a while after its last dialog closes. When
    // something starts it without asking for a dialog, which happens at
    // sign-in, nothing would start that wait, and it stayed for the whole
    // session (38 MB). The same wait runs from start-up; D-Bus starts it
    // again for the next dialog.
    QTimer::singleShot(Windows::idleQuitMs, &app, [] {
        if (!Windows::started()) {
            QCoreApplication::quit();
        }
    });
    return app.exec();
}
