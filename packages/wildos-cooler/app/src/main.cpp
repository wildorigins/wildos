// wildos-cooler: Cooler, the Wild OS app for liquid coolers with a screen.
// It chooses the face on the cooler's screen, sets the pump and fan speeds
// and shows the readings. Nothing goes online.
//
// One instance: a second start brings the open window forward.
//
// WILDOS_COOLER_FAKE=kraken, krakenz or none stands in for the cooler, for
// rendering the pages and for tests (see fakebackend.h).

#include "coolerapp.h"
#include "fakebackend.h"
#include "servicebackend.h"

#include <KDBusService>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDebug>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQmlIncubator>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

namespace
{
// Builds the window a little at a time between turns of the event loop,
// so the splash can come up and animate while it is made.
class Incubation : public QQmlIncubationController
{
public:
    Incubation()
    {
        m_timer.setInterval(16);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this]() {
            if (incubatingObjectCount() > 0) {
                incubateFor(12);
            }
        });
        m_timer.start();
    }

private:
    QTimer m_timer;
};

// The launch splash, in the icon's blues.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QCoreApplication::translate("main", "Cooler")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-cooler.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#7fd4ff"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#2a7fd6"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Screen, pump and fans")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Nothing leaves this PC")},
        {QStringLiteral("showDelay"), 0},
        {QStringLiteral("minimumTime"), 900},
    };
    QObject *splash = component.createWithInitialProperties(properties);
    if (!qobject_cast<QQuickWindow *>(splash)) {
        delete splash;
        return nullptr;
    }
    return splash;
}
}

int main(int argc, char *argv[])
{
    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand "Save a report" to the Wild OS file dialog.
    QApplication app(argc, argv);
    // KDBusService takes the D-Bus name org.wildos.cooler from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("cooler"));
    app.setApplicationDisplayName(QStringLiteral("Cooler"));
    app.setApplicationVersion(QStringLiteral(WILDOS_COOLER_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.cooler"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-cooler")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Cooler"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Open on this page: screen, cooling, lighting or about."),
                                  QStringLiteral("page"));
    parser.addOption(pageOption);
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    const QString fake = qEnvironmentVariable("WILDOS_COOLER_FAKE");
    Cooler::Backend *backend = nullptr;
    if (fake.isEmpty()) {
        backend = new Cooler::ServiceBackend(&app);
    } else {
        backend = new Cooler::FakeBackend(fake, &app);
    }

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    auto *cooler = new CoolerApp(backend, &app);
    cooler->setEngine(&engine);
    QObject::connect(&service, &KDBusService::activateRequested, cooler, &CoolerApp::activate);

    QObject *splash = makeSplash(&engine);
    QQmlComponent home(&engine);
    QQmlIncubator incubator(QQmlIncubator::Asynchronous);
    // Built hidden behind the splash and shown as it fades.
    incubator.setInitialProperties({{QStringLiteral("visible"), splash == nullptr}, {QStringLiteral("requestedPage"), parser.value(pageOption)}});

    QTimer poll;
    poll.setInterval(16);
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (incubator.isLoading()) {
            return;
        }
        poll.stop();
        if (incubator.isError()) {
            for (const QQmlError &e : incubator.errors()) {
                qWarning().noquote() << e.toString();
            }
            QCoreApplication::exit(1);
            return;
        }
        QObject *object = incubator.object();
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
        object->setParent(&engine);
        auto *window = qobject_cast<QQuickWindow *>(object);
        cooler->setMainWindow(window);
        if (splash) {
            if (window) {
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(show()));
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(requestActivate()));
            }
            QMetaObject::invokeMethod(splash, "finish");
        }
    });
    bool started = false;
    auto create = [&]() {
        if (started) {
            return;
        }
        started = true;
        if (home.isError()) {
            qWarning().noquote() << home.errorString();
            QCoreApplication::exit(1);
            return;
        }
        home.create(incubator);
        poll.start();
    };
    QObject::connect(&home, &QQmlComponent::statusChanged, &app, [&](QQmlComponent::Status status) {
        if (status == QQmlComponent::Ready || status == QQmlComponent::Error) {
            create();
        }
    });
    home.loadFromModule("WildOS.Cooler", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    const int code = app.exec();
    // The window goes while the engine is still whole: as the engine's child
    // it went after the QML state, and a dialog left open crashed the app on
    // its way out.
    delete incubator.object();
    return code;
}
