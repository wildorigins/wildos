#include "catalog.h"
#include "localpackage.h"
#include "network.h"
#include "themeiconprovider.h"
#include "timing.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>

#include <atomic>
#include <memory>

namespace
{
const QString serviceName = QStringLiteral("org.wildos.Store");
const QString objectPath = QStringLiteral("/Store");
const QString interfaceName = QStringLiteral("org.wildos.Store");
}

// A second "wildos-store --app x" (from Start, Welcome or the .exe handler)
// brings the open Store forward on that app instead of opening another one.
class Remote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Store")

public:
    explicit Remote(QQmlApplicationEngine *engine)
        : QObject(engine)
        , m_engine(engine)
    {
    }

public Q_SLOTS:
    Q_SCRIPTABLE void show(const QString &app, const QString &search, const QString &page)
    {
        const auto roots = m_engine->rootObjects();
        if (roots.isEmpty()) {
            return;
        }
        QObject *root = roots.constFirst();
        QMetaObject::invokeMethod(root, "openTarget", Q_ARG(QVariant, app), Q_ARG(QVariant, search), Q_ARG(QVariant, page));
        if (auto window = qobject_cast<QQuickWindow *>(root)) {
            if (window->visibility() == QWindow::Minimized) {
                window->showNormal();
            }
            window->show();
            window->raise();
            window->requestActivate();
        }
    }

private:
    QQmlApplicationEngine *m_engine;
};

int main(int argc, char *argv[])
{
    storeClock();
    QGuiApplication app(argc, argv);
    qCInfo(lcStore) << "timing: application up at" << storeClock() << "ms";
    app.setApplicationName(QStringLiteral("wildos-store"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Store"));
    app.setApplicationVersion(QStringLiteral(WILDOS_STORE_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.store"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.wildos.store"), QIcon::fromTheme(QStringLiteral("plasmadiscover"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Store"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption appOption(QStringLiteral("app"), QStringLiteral("Open the page of this app (an AppStream ID, package or Flathub ID)."),
                                 QStringLiteral("id"));
    QCommandLineOption searchOption(QStringLiteral("search"), QStringLiteral("Search for this text."), QStringLiteral("text"));
    QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Open discover, swap, create, work, play, develop, categories, windowsapps, updates or library."), QStringLiteral("page"));
    parser.addOption(appOption);
    parser.addOption(searchOption);
    parser.addOption(pageOption);
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("An appstream: link, or a .deb, .flatpakref or .flatpak file to install."), QStringLiteral("[url]"));
    parser.process(app);

    QString target = parser.value(appOption);
    QString page = parser.value(pageOption);
    const QStringList positional = parser.positionalArguments();
    if (target.isEmpty() && !positional.isEmpty()) {
        target = positional.constFirst();
        // A package file opened from Files or a download gets its own
        // install page. The path is made absolute here, since a Store
        // already open elsewhere has another working folder.
        const QString file = LocalPackage::fileArgument(target);
        if (!file.isEmpty()) {
            target = file;
            page = QStringLiteral("file");
        }
    }
    const QString search = parser.value(searchOption);

    qCInfo(lcStore) << "timing: options read at" << storeClock() << "ms";
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.interface()->isServiceRegistered(serviceName)) {
        auto message = QDBusMessage::createMethodCall(serviceName, objectPath, interfaceName, QStringLiteral("show"));
        message << target << search << page;
        bus.call(message, QDBus::Block, 5000);
        return 0;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qCInfo(lcStore) << "timing: building the window from" << storeClock() << "ms";

    StoreNetworkFactory networkFactory;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    engine.addImageProvider(QStringLiteral("theme"), new ThemeIconProvider);
    engine.setInitialProperties({
        {QStringLiteral("startApp"), target},
        {QStringLiteral("startSearch"), search},
        {QStringLiteral("startPage"), page.isEmpty() ? QStringLiteral("home") : page},
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WildOS.Store", "Main");
    qCInfo(lcStore) << "timing: window built at" << storeClock() << "ms";

    // The first frame, and the first one drawn once the catalogue is in, are
    // what someone opening the Store waits for. frameSwapped comes from the
    // render thread, hence the atomics.
    if (auto window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        auto firstFrame = std::make_shared<std::atomic_bool>(false);
        auto filledFrame = std::make_shared<std::atomic_bool>(false);
        auto catalogueIn = std::make_shared<std::atomic_bool>(Catalog::instance() && Catalog::instance()->isReady());
        if (Catalog *catalog = Catalog::instance()) {
            QObject::connect(catalog, &Catalog::readyChanged, window, [catalogueIn] {
                catalogueIn->store(true);
            });
        }
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [firstFrame, filledFrame, catalogueIn] {
            if (!firstFrame->exchange(true)) {
                qCInfo(lcStore) << "timing: first frame at" << storeClock() << "ms";
                if (Catalog *catalog = Catalog::instance()) {
                    QMetaObject::invokeMethod(catalog, &Catalog::startBackgroundWork, Qt::QueuedConnection);
                }
            }
            if (catalogueIn->load() && !filledFrame->exchange(true)) {
                qCInfo(lcStore) << "timing: first frame with the catalogue at" << storeClock() << "ms";
            }
        }, Qt::DirectConnection);
    }

    if (bus.isConnected()) {
        auto remote = new Remote(&engine);
        bus.registerObject(objectPath, remote, QDBusConnection::ExportScriptableSlots);
        bus.registerService(serviceName);
    }

    const int code = app.exec();
    // The name is given up before the Store is taken down, which can take a
    // few seconds: a Store opened again in that time then starts as a new
    // one, instead of handing over to this one and showing nothing.
    if (bus.isConnected()) {
        bus.unregisterService(serviceName);
    }
    return code;
}

#include "main.moc"
