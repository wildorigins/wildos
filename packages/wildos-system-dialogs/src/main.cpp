#include "agent.h"
#include "prompts.h"
#include "security.h"
#include "session.h"
#include "verifier.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include <memory>

// One program for every Wild OS system dialog:
//
//   wildos-dialogs --session        the session service (systemd user unit):
//                                   permission prompts, Ctrl+Alt+Del kept
//                                   loaded so it opens at once, battery and
//                                   restart prompts
//   wildos-dialogs security         Ctrl+Alt+Del (KWin's shortcut)
//   wildos-dialogs restart          the restart prompt (Settings > Updates)
//   wildos-dialogs windows-mode     from the App Bridge
//   kwin_killer_helper ...          KWin's "not responding" helper, which
//                                   this replaces (a diverted symlink)
//   wildos-dialogs --preview board  a board drawn over a picture, for
//                                   design reviews
int main(int argc, char *argv[])
{
    const bool killer = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == QLatin1String("kwin_killer_helper");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-dialogs"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Wild OS"));
    app.setApplicationVersion(QStringLiteral(WILDOS_DIALOGS_VERSION));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS system dialogs"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("mode"), QStringLiteral("security, restart, windows-mode or not-responding"));
    const QCommandLineOption sessionOption(QStringLiteral("session"), QStringLiteral("Run the session service."));
    const QCommandLineOption previewOption(QStringLiteral("preview"), QStringLiteral("Draw a board for a design review."), QStringLiteral("board"));
    const QCommandLineOption backdropOption(QStringLiteral("backdrop"), QStringLiteral("The picture behind a preview."), QStringLiteral("image"));
    const QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Show a prompt with made-up values on the real desktop."), QStringLiteral("kind"));
    // KWin's kill helper arguments.
    const QCommandLineOption pidOption(QStringLiteral("pid"), QStringLiteral("Process of the app."), QStringLiteral("pid"));
    const QCommandLineOption hostOption(QStringLiteral("hostname"), QStringLiteral("Computer the app runs on."), QStringLiteral("host"));
    const QCommandLineOption captionOption(QStringLiteral("windowname"), QStringLiteral("Title of its window."), QStringLiteral("caption"));
    const QCommandLineOption appOption(QStringLiteral("applicationname"), QStringLiteral("Its desktop file name."), QStringLiteral("name"));
    const QCommandLineOption widOption(QStringLiteral("wid"), QStringLiteral("Its window."), QStringLiteral("id"));
    const QCommandLineOption timestampOption(QStringLiteral("timestamp"), QStringLiteral("Time of the click."), QStringLiteral("time"));
    // Windows mode.
    const QCommandLineOption idOption(QStringLiteral("id"), QStringLiteral("Windows app id."), QStringLiteral("id"));
    const QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("Windows app name."), QStringLiteral("name"));
    const QCommandLineOption iconOption(QStringLiteral("icon"), QStringLiteral("Windows app icon."), QStringLiteral("icon"));
    const QCommandLineOption targetOption(QStringLiteral("target"), QStringLiteral("Program to start."), QStringLiteral("path"));
    parser.addOptions({sessionOption, previewOption, backdropOption, demoOption, pidOption, hostOption, captionOption, appOption, widOption,
                       timestampOption, idOption, nameOption, iconOption, targetOption});
    parser.process(app);

    const QString mode = killer ? QStringLiteral("not-responding") : parser.positionalArguments().value(0);
    const bool session = parser.isSet(sessionOption);
    const QString preview = parser.value(previewOption);

    // "wildos-dialogs explain <action id> [--target program] [--pid asker]"
    // prints what the permission prompt would say, for checking a request
    // without raising one.
    if (mode == QLatin1String("explain")) {
        QHash<QString, QString> details;
        if (parser.isSet(targetOption)) {
            details.insert(QStringLiteral("program"), parser.value(targetOption));
        }
        if (parser.isSet(pidOption)) {
            details.insert(QStringLiteral("polkit.subject-pid"), parser.value(pidOption));
        }
        const QString action = parser.positionalArguments().value(1);
        const Verdict verdict = Verifier::check(action, QStringLiteral("Authentication is required to do this"), details);
        QTextStream out(stdout);
        out << "App: " << verdict.appName << " (" << verdict.iconName << ")\n"
            << "Says: " << verdict.purpose << '\n'
            << (verdict.verified ? QStringLiteral("Verified") : QStringLiteral("Not verified: ") + verdict.why) << '\n'
            << "Program: " << verdict.program << "\nAsked by: " << verdict.caller << "\nPackage: " << verdict.package << " from " << verdict.origin
            << "\nPublisher: " << verdict.vendor << Qt::endl;
        return 0;
    }

    if (mode == QLatin1String("security") && !parser.isSet(demoOption) && DialogsService::forward(QStringLiteral("ShowSecurity"))) {
        return 0;
    }
    if (mode == QLatin1String("restart") && !parser.isSet(demoOption) && DialogsService::forward(QStringLiteral("ShowRestart"))) {
        return 0;
    }
    if (!session && preview.isEmpty() && !QStringList{QStringLiteral("security"), QStringLiteral("restart"), QStringLiteral("windows-mode"),
                                                        QStringLiteral("not-responding"), QStringLiteral("permission")}
                                               .contains(mode)) {
        parser.showHelp(1);
    }

    if (killer) {
        // KWin hands over an activation token for this desktop file.
        app.setDesktopFileName(QStringLiteral("org.kde.kwin.killer"));
    } else {
        app.setDesktopFileName(QStringLiteral("org.wildos.security"));
    }
    if (!preview.isEmpty()) {
        qputenv("WILDOS_DIALOGS_PREVIEW", "1");
    }

    // Left to the end of the process on purpose: polkit-qt crashes when a
    // listener is torn down during QCoreApplication destruction.
    auto *agent = new Agent;
    auto *security = new Security(&app);
    auto *prompts = new Prompts(&app);
    qmlRegisterSingletonInstance("WildOS.SystemDialogs.Backend", 1, 0, "Agent", agent);
    qmlRegisterSingletonInstance("WildOS.SystemDialogs.Backend", 1, 0, "Security", security);
    qmlRegisterSingletonInstance("WildOS.SystemDialogs.Backend", 1, 0, "Prompts", prompts);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);

    if (!preview.isEmpty()) {
        const QString backdrop = parser.value(backdropOption);
        engine.setInitialProperties({
            {QStringLiteral("board"), preview},
            {QStringLiteral("backdrop"), backdrop.isEmpty() ? QString() : QUrl::fromLocalFile(QFileInfo(backdrop).absoluteFilePath()).toString()},
        });
        if (preview.startsWith(QLatin1String("permission"))) {
            const QString kind = preview.section(QLatin1Char('-'), 1);
            agent->demo(kind.isEmpty() ? QStringLiteral("verified") : kind);
        }
        engine.loadFromModule("WildOS.SystemDialogs", "Preview");
        app.setQuitOnLastWindowClosed(true);
        return app.exec();
    }

    if (session) {
        auto *service = new DialogsService(&app);
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.registerObject(QString::fromLatin1(DialogsService::objectPath), service, QDBusConnection::ExportScriptableSlots);
        if (!bus.registerService(QString::fromLatin1(DialogsService::serviceName))) {
            qWarning("wildos-dialogs: another session service is already running");
            return 0;
        }
        QObject::connect(service, &DialogsService::securityRequested, security, &Security::show);
        QObject::connect(service, &DialogsService::restartRequested, prompts, &Prompts::showRestart);
        QObject::connect(service, &DialogsService::windowsShutdownRequested, prompts, &Prompts::askWindowsShutdown);
        QObject::connect(service, &DialogsService::windowsDevicesRequested, prompts, &Prompts::askWindowsDevices);
        QObject::connect(service, &DialogsService::windowsStartingRequested, prompts, &Prompts::windowsStarting);
        QObject::connect(service, &DialogsService::windowsStartingDone, prompts, &Prompts::windowsStartingDone);
        // Ctrl+Alt+Del is loaded now, so it opens at once however busy the
        // PC is. The permission prompt and the other prompts are loaded the
        // first time one is needed, then kept: most sessions never show
        // them, and each held a full-screen window for the whole session.
        // Both pick up a request already waiting when they load.
        engine.loadFromModule("WildOS.SystemDialogs", "SecurityScreen");
        auto loadOnce = [&engine](const char *name) {
            auto loaded = std::make_shared<bool>(false);
            return [&engine, name, loaded] {
                if (!*loaded) {
                    *loaded = true;
                    engine.loadFromModule("WildOS.SystemDialogs", name);
                }
            };
        };
        auto permission = loadOnce("PermissionPrompt");
        QObject::connect(agent, &Agent::activeChanged, &app, [agent, permission] {
            if (agent->active()) {
                permission();
            }
        });
        auto promptWindow = loadOnce("PromptWindow");
        QObject::connect(prompts, &Prompts::showingChanged, &app, [prompts, promptWindow] {
            if (!prompts->showing().isEmpty()) {
                promptWindow();
            }
        });
        agent->start();
        prompts->watch();
        return app.exec();
    }

    const QString demo = parser.value(demoOption);
    if (mode == QLatin1String("permission")) {
        engine.loadFromModule("WildOS.SystemDialogs", "PermissionPrompt");
        agent->demo(demo.isEmpty() ? QStringLiteral("verified") : demo);
        QObject::connect(agent, &Agent::activeChanged, &app, [agent] {
            if (!agent->active()) {
                QCoreApplication::quit();
            }
        });
    } else if (mode == QLatin1String("security")) {
        engine.loadFromModule("WildOS.SystemDialogs", "SecurityScreen");
        security->setQuitOnClose(true);
        security->show();
    } else {
        engine.loadFromModule("WildOS.SystemDialogs", "PromptWindow");
        if (!demo.isEmpty()) {
            prompts->demo(demo);
        } else if (mode == QLatin1String("restart")) {
            prompts->showRestart();
        } else if (mode == QLatin1String("windows-mode")) {
            prompts->windowsMode(parser.value(idOption), parser.value(nameOption), parser.value(iconOption), parser.value(targetOption));
        } else {
            bool ok = false;
            const qint64 pid = parser.value(pidOption).toLongLong(&ok);
            if (!ok || pid <= 0) {
                return 1;
            }
            const QString host = parser.value(hostOption);
            prompts->notResponding(pid, parser.value(appOption), parser.value(captionOption), parser.value(widOption),
                                   host.isEmpty() || host == QLatin1String("localhost"));
        }
        QObject::connect(prompts, &Prompts::showingChanged, &app, [prompts] {
            if (prompts->showing().isEmpty()) {
                QTimer::singleShot(200, qApp, &QCoreApplication::quit);
            }
        });
    }
    return app.exec();
}
