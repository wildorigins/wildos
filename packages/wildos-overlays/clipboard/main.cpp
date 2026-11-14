#include "panelapp.h"
#include "secretwiper.h"

#include <KDBusService>

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

int main(int argc, char *argv[])
{
    // The history model comes from Plasma's clipboard module, a widgets-era
    // library that wants a QApplication.
    QApplication app(argc, argv);
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setApplicationName(QStringLiteral("clipboard"));
    app.setApplicationDisplayName(QStringLiteral("Clipboard and emoji"));
    app.setDesktopFileName(QStringLiteral("org.wildos.clipboard"));
    app.setQuitOnLastWindowClosed(false);
    // It decides itself when to end; finished background jobs (previews,
    // share plugins) must not end it while it waits hidden.
    app.setQuitLockEnabled(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    KDBusService service(KDBusService::Unique);

    PanelApp panel;
    SecretWiper wiper;
    QObject::connect(&service, &KDBusService::activateRequested, &panel, [&panel](const QStringList &arguments, const QString &) {
        panel.handle(arguments);
    });

    // The one instance, which D-Bus activation also reaches.
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("PanelApp"), &panel);
    engine.loadFromModule("WildOS.Clipboard", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    panel.handle(app.arguments());
    return app.exec();
}
