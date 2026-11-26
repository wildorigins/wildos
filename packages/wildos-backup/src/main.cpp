// wildos-backup: Backup, the Wild OS backup app. Encrypted backups of the
// home folder to the person's own external drive, with restic, on a
// schedule. Nothing leaves the PC: no cloud, no network.
//
//   wildos-backup                  the window
//   wildos-backup --run [--KIND]   one backup with no window (KIND is
//                                  scheduled, manual or catch-up); the
//                                  systemd user units start this
//   wildos-backup --watch          catches up when a backup drive is
//                                  plugged in after a missed backup
//
// One window: a second start brings the running one forward.

#include "drives.h"
#include "runner.h"
#include "schedule.h"
#include "settings.h"
#include "watcher.h"

#include <KDBusService>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQmlIncubator>
#include <QQuickStyle>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

#include <KWindowSystem>

#include <cstring>

namespace
{
void identify(QCoreApplication &app)
{
    // KDBusService takes the D-Bus name org.wildos.backup from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("backup"));
    app.setApplicationVersion(QStringLiteral(WILDOS_BACKUP_VERSION));
}

bool hasArgument(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

int runHeadless(int argc, char *argv[])
{
    // Notifications want a GUI application; there is no window to show,
    // and a timer may start this with no display at all.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    identify(app);
    app.setDesktopFileName(QStringLiteral("org.wildos.backup"));
    QString kind = QStringLiteral("scheduled");
    for (const char *k : {"manual", "catch-up", "scheduled"}) {
        if (hasArgument(argc, argv, (QByteArray("--") + k).constData())) {
            kind = QString::fromLatin1(k);
        }
    }
    Runner runner(kind);
    return runner.exec();
}

int runWatcher(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    identify(app);
    Watcher watcher;
    return app.exec();
}

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

// The launch splash (SplashDaily.dc), in the icon's slate.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Backup")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-backup.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#8d9ab6"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#3b4459"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Encrypted, on your own drive")},
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

void activate(QQuickWindow *window)
{
    window->show();
    window->raise();
    // A start from the launcher carries an activation token, which lets the
    // window take the focus from the app in front.
    KWindowSystem::updateStartupId(window);
    KWindowSystem::activateWindow(window);
    window->requestActivate();
}
}

int main(int argc, char *argv[])
{
    if (hasArgument(argc, argv, "--run")) {
        return runHeadless(argc, argv);
    }
    if (hasArgument(argc, argv, "--watch")) {
        return runWatcher(argc, argv);
    }

    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand the folder dialog to the Wild OS one.
    QApplication app(argc, argv);
    identify(app);
    app.setApplicationDisplayName(QStringLiteral("Backup"));
    app.setDesktopFileName(QStringLiteral("org.wildos.backup"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-backup")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Backup"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("run"), QStringLiteral("Back up now, with no window.")});
    parser.addOption({QStringLiteral("watch"), QStringLiteral("Catch up when a backup drive is plugged in.")});
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    Settings *settings = Settings::instance();
    // The timer follows the Schedule page and the drive list as they change.
    QObject::connect(settings, &Settings::scheduleChanged, &app, [settings] {
        Schedule::apply(settings);
    });
    // Only the first drive added and the last removed switch the units.
    QObject::connect(settings, &Settings::drivesChanged, &app, [settings, configured = settings->configured()]() mutable {
        if (settings->configured() != configured) {
            configured = settings->configured();
            Schedule::apply(settings);
        }
    });
    Drives::instance()->watch();

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);

    QObject *splash = makeSplash(&engine);
    QQmlComponent home(&engine);
    QQmlIncubator incubator(QQmlIncubator::Asynchronous);
    if (splash) {
        // Built hidden behind the splash and shown as it fades.
        incubator.setInitialProperties({{QStringLiteral("visible"), false}});
    }

    QPointer<QQuickWindow> mainWindow;
    QObject::connect(&service, &KDBusService::activateRequested, &app, [&mainWindow](const QStringList &, const QString &) {
        if (mainWindow) {
            activate(mainWindow);
        }
    });

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
        mainWindow = qobject_cast<QQuickWindow *>(object);
        if (splash) {
            if (mainWindow) {
                QObject::connect(splash, SIGNAL(finished()), mainWindow, SLOT(show()));
                QObject::connect(splash, SIGNAL(finished()), mainWindow, SLOT(requestActivate()));
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
    home.loadFromModule("WildOS.Backup", "Main", QQmlComponent::Asynchronous);
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
