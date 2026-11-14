#include "app.h"
#include "imageproviders.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

namespace
{
const QString serviceName = QStringLiteral("org.wildos.Files");
const QString objectPath = QStringLiteral("/Files");
}

int main(int argc, char *argv[])
{
    // Widgets are only for the few KIO dialogs Files has not replaced yet
    // (choosing an app for an unknown type, passwords for network places).
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-files"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Files"));
    app.setApplicationVersion(QStringLiteral(WILDOS_FILES_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.files"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("system-file-manager")));
    // The last window closing ends the process, as with Explorer windows.
    app.setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Files"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("locations"), QStringLiteral("Folders to open, each in its own tab. A file opens its folder with it selected."), QStringLiteral("[locations...]"));
    QCommandLineOption selectOption(QStringLiteral("select"), QStringLiteral("Show the locations selected in their folders, folders included."));
    parser.addOption(selectOption);
    QCommandLineOption formatOption(QStringLiteral("format"), QStringLiteral("Open the Format dialog for a removable drive, such as /dev/sdb1."), QStringLiteral("device"));
    parser.addOption(formatOption);
    QCommandLineOption choicesOption(QStringLiteral("remove-details-choices"), QStringLiteral("Open \"Choose what is removed\" for Remove personal details."));
    parser.addOption(choicesOption);
    // Started by D-Bus for a call such as "show this file in its folder";
    // the call itself opens the window.
    QCommandLineOption serviceOption(QStringLiteral("service"), QStringLiteral("Start without a window, waiting for D-Bus requests."));
    parser.addOption(serviceOption);
    parser.process(app);
    const bool service = parser.isSet(serviceOption);

    QStringList locations;
    for (const QString &arg : parser.positionalArguments()) {
        QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (parser.isSet(selectOption)) {
            url.setFragment(QStringLiteral("wildos-select"));
        }
        locations.append(url.toString());
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    const bool preview = qEnvironmentVariableIsSet("WILDOS_FILES_PREVIEW");
    if (service && bus.isConnected() && bus.interface()->isServiceRegistered(serviceName)) {
        return 0;
    }
    const QString formatDevice = parser.value(formatOption);
    const bool choices = parser.isSet(choicesOption);
    if (!preview && bus.isConnected() && bus.interface()->isServiceRegistered(serviceName)) {
        if (choices) {
            bus.call(QDBusMessage::createMethodCall(serviceName, objectPath, serviceName, QStringLiteral("ChooseRemovedDetails")), QDBus::Block, 5000);
            return 0;
        }
        auto message = formatDevice.isEmpty() ? QDBusMessage::createMethodCall(serviceName, objectPath, serviceName, QStringLiteral("Open"))
                                              : QDBusMessage::createMethodCall(serviceName, objectPath, serviceName, QStringLiteral("Format"));
        if (formatDevice.isEmpty()) {
            message << locations;
        } else {
            message << formatDevice;
        }
        bus.call(message, QDBus::Block, 5000);
        return 0;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);

    App *files = App::instance();
    files->setEngine(&engine);

    if (preview) {
        // The design rig: Files drawn over the wallpaper at the design's
        // size and place, for side by side comparison with the artboards.
        engine.setInitialProperties({{QStringLiteral("startUrls"), QVariantList{QUrl::fromUserInput(locations.value(0), QDir::currentPath(), QUrl::AssumeLocalFile)}}});
        engine.loadFromModule("WildOS.Files", "PreviewStage");
        return app.exec();
    }

    if (bus.isConnected()) {
        bus.registerObject(objectPath, files, QDBusConnection::ExportScriptableSlots);
        bus.registerService(serviceName);
        bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), new FileManager1(files), QDBusConnection::ExportScriptableSlots);
        bus.registerService(QStringLiteral("org.freedesktop.FileManager1"));
    }

    if (choices) {
        files->ChooseRemovedDetails();
    } else if (!formatDevice.isEmpty()) {
        files->Format(formatDevice);
    } else if (!service) {
        files->Open(locations);
    } else {
        QTimer::singleShot(60000, &app, [] {
            if (QGuiApplication::topLevelWindows().isEmpty()) {
                QCoreApplication::quit();
            }
        });
    }
    return app.exec();
}
