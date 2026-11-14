#include "kcmhost.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KConfigWatcher>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KWindowSystem>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

namespace
{
const QString serviceName = QStringLiteral("org.wildos.Settings");
const QString objectPath = QStringLiteral("/Settings");
const QString interfaceName = QStringLiteral("org.wildos.Settings");
}

// A second "wildos-settings --page x" brings the open window forward on the
// requested page instead of opening another Settings, as Windows does. On
// Wayland only the compositor can bring a window back from the taskbar, and
// only with the activation token the launcher (the desktop menu, Start, a
// flyout) handed the second Settings, so that token is passed along.
class Remote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Settings")

public:
    explicit Remote(QQmlApplicationEngine *engine)
        : QObject(engine)
        , m_engine(engine)
    {
    }

public Q_SLOTS:
    Q_SCRIPTABLE void showPage(const QString &page)
    {
        showPageActivated(page, QString());
    }

    Q_SCRIPTABLE void showPageActivated(const QString &page, const QString &token)
    {
        const auto roots = m_engine->rootObjects();
        if (roots.isEmpty()) {
            return;
        }
        QObject *root = roots.constFirst();
        if (!page.isEmpty()) {
            QMetaObject::invokeMethod(root, "showPage", Q_ARG(QVariant, page));
        }
        if (auto window = qobject_cast<QQuickWindow *>(root)) {
            window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
            window->show();
            window->raise();
            if (KWindowSystem::isPlatformWayland()) {
                if (!token.isEmpty()) {
                    KWindowSystem::setCurrentXdgActivationToken(token);
                }
                KWindowSystem::activateWindow(window);
            } else {
                window->requestActivate();
            }
        }
    }

private:
    QQmlApplicationEngine *m_engine;
};

// The KDE modules shown inside Settings draw with the colour scheme.
// Settings always uses a Wild OS one, light or dark to match the account's
// colours, whatever scheme the account has, so a module never shows a page
// of another look inside the window. This is what KColorSchemeManager does
// for an app that picks its own scheme. Light or dark is judged by the
// window colour, as every other part of Wild OS judges it.
static void applyScheme(QApplication &app)
{
    const KColorScheme window(QPalette::Active, KColorScheme::Window, KSharedConfig::openConfig(QStringLiteral("kdeglobals")));
    const bool light = window.background().color().lightness() > 127;
    QString scheme;
    if (light) {
        scheme = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/WildOSLight.colors"));
    }
    if (scheme.isEmpty()) {
        scheme = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/WildOS.colors"));
    }
    if (scheme.isEmpty() || app.property("KDE_COLOR_SCHEME_PATH").toString() == scheme) {
        return;
    }
    app.setProperty("KDE_COLOR_SCHEME_PATH", scheme);
    const QPalette palette = KColorScheme::createApplicationPalette(KSharedConfig::openConfig(scheme, KConfig::SimpleConfig));
    app.setPalette(palette);
    // QML modules miss QApplication::setPalette alone; plasma-integration
    // sends this too when it changes the palette.
    Q_EMIT app.paletteChanged(palette);
}

int main(int argc, char *argv[])
{
    // Read before QApplication: Qt's Wayland plugin takes the token out of
    // the environment as it starts.
    const QString activationToken = QString::fromUtf8(qgetenv("XDG_ACTIVATION_TOKEN"));

    // Widgets are needed for the handful of KDE modules that are still
    // QWidget based and open in their own window.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-settings"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Settings"));
    app.setApplicationVersion(QStringLiteral(WILDOS_SETTINGS_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.settings"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.wildos.settings"), QIcon::fromTheme(QStringLiteral("preferences-system"))));

    // KDE modules translate strings while their QML plugins load, which
    // happens on the QML loader thread. The first translation creates KI18n's
    // language watcher as a child of the application, so it has to happen
    // here on the main thread or Qt refuses to parent it there.
    KLocalizedString::setApplicationDomain("wildos-settings");
    (void)ki18n("Settings").toString();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Settings"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Open the page with this id, for example display or update."), QStringLiteral("id"));
    parser.addOption(pageOption);
    parser.process(app);
    const QString page = parser.value(pageOption);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.interface()->isServiceRegistered(serviceName)) {
        auto message = QDBusMessage::createMethodCall(serviceName, objectPath, interfaceName, QStringLiteral("showPageActivated"));
        message << page << activationToken;
        const QDBusMessage reply = bus.call(message, QDBus::Block, 5000);
        // A Settings started before an update has only the older call.
        if (reply.type() == QDBusMessage::ErrorMessage && reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
            auto older = QDBusMessage::createMethodCall(serviceName, objectPath, interfaceName, QStringLiteral("showPage"));
            older << page;
            bus.call(older, QDBus::Block, 5000);
        }
        return 0;
    }

    applyScheme(app);
    // Kept up to date while Settings is open: plasma-integration leaves an
    // app with its own scheme alone, so a switch to light or dark (by hand
    // or at sunset) left KDE's modules in the old colours until a restart.
    auto watcher = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kdeglobals")));
    auto schemeTimer = new QTimer(&app);
    schemeTimer->setSingleShot(true);
    schemeTimer->setInterval(150);
    QObject::connect(schemeTimer, &QTimer::timeout, &app, [&app] {
        applyScheme(app);
    });
    QObject::connect(watcher.data(), &KConfigWatcher::configChanged, &app, [schemeTimer](const KConfigGroup &group) {
        if (group.name() == QLatin1String("General") || group.name().startsWith(QLatin1String("Colors:"))) {
            schemeTimer->start();
        }
    });

    // KDE's modules are written against the desktop style, which in turn
    // follows the Wild OS Kvantum theme.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    // The window draws its own title bar and rounded glass sheet, so it
    // needs an alpha channel outside the corners.
    QQuickWindow::setDefaultAlphaBuffer(true);

    auto engine = std::make_shared<QQmlApplicationEngine>();
    KcmHost::setEngine(engine);
    engine->setInitialProperties({{QStringLiteral("startPage"), page.isEmpty() ? QStringLiteral("home") : page}});
    QObject::connect(engine.get(), &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    engine->loadFromModule("WildOS.Settings", "Main");

    if (bus.isConnected()) {
        auto remote = new Remote(engine.get());
        bus.registerObject(objectPath, remote, QDBusConnection::ExportScriptableSlots);
        bus.registerService(serviceName);
    }

    const int code = app.exec();
    // Modules hold items from the engine; drop them before the engine goes.
    // KcmHost keeps its own reference for modules opened later; while it
    // held one, the engine outlived the application and tore down its
    // images after Qt had gone, which aborted Settings as it closed.
    KcmHost::setEngine(nullptr);
    engine.reset();
    return code;
}

#include "main.moc"
