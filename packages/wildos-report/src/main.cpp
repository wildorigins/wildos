// wildos-report: Report a problem. Writes up what went wrong in Wild OS,
// with the details that could identify someone removed, for the person to
// send themselves. Wild OS has no server and nothing is sent for them.
//
//   wildos-report                      the window
//   wildos-report wildos-report:files  the window, with Files chosen (the
//                                      apps' Report a problem menu items)
//   wildos-report --part files         the same
//   wildos-report --crash USEC --app NAME
//                                      after "<App> closed unexpectedly"
//   wildos-report --event FILE         with an event from Settings >
//                                      System > History attached (a JSON
//                                      note in the runtime folder, deleted
//                                      once read)
//   wildos-report --save DIR [--part X] [--since SECS] [--until SECS]
//                 [--boot -1] [--description TEXT]
//                                      no window: the whole report, saved
//                                      to DIR as a zip (start-up recovery)
//   wildos-report --crash-notify       the notice after a crash, from
//                                      wildos-report-crash.service
//
// One window: a second launch hands its arguments to the running one.

#include "crashnotify.h"
#include "report.h"

#include <KDBusService>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
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

#include <cstdio>
#include <cstring>

namespace
{
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

// The launch splash, in the icon's amber.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Report a problem")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-report.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#ffb36b"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#e5604f"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Tell us what went wrong")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Nothing is sent for you")},
        {QStringLiteral("showDelay"), 0},
        {QStringLiteral("minimumTime"), 700},
    };
    QObject *splash = component.createWithInitialProperties(properties);
    if (!qobject_cast<QQuickWindow *>(splash)) {
        delete splash;
        return nullptr;
    }
    return splash;
}

bool has(int argc, char *argv[], const char *option)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], option) == 0) {
            return true;
        }
    }
    return false;
}

void setNames(QCoreApplication &app)
{
    // KDBusService takes org.wildos.report from these, the name in the
    // desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("report"));
    app.setApplicationVersion(QStringLiteral(WILDOS_REPORT_VERSION));
}

int saveHeadless(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    setNames(app);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("save"), QStringLiteral("Save the report to this folder."), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("part"), QStringLiteral("The part of Wild OS."), QStringLiteral("part"), QStringLiteral("system")});
    parser.addOption({QStringLiteral("since"), QStringLiteral("Logs from, in seconds since 1970."), QStringLiteral("secs")});
    parser.addOption({QStringLiteral("until"), QStringLiteral("Logs until, in seconds since 1970."), QStringLiteral("secs")});
    parser.addOption({QStringLiteral("boot"), QStringLiteral("0 for this start-up, -1 for the one before."), QStringLiteral("n"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("description"), QStringLiteral("What went wrong."), QStringLiteral("text")});
    parser.process(app);

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime until = parser.isSet(QStringLiteral("until")) ? QDateTime::fromSecsSinceEpoch(parser.value(QStringLiteral("until")).toLongLong()) : now;
    const QDateTime since = parser.isSet(QStringLiteral("since")) ? QDateTime::fromSecsSinceEpoch(parser.value(QStringLiteral("since")).toLongLong())
                                                                  : until.addSecs(-3600);
    const QString path = Report::instance()->saveHeadless(parser.value(QStringLiteral("save")), parser.value(QStringLiteral("part")), since, until,
                                                          parser.value(QStringLiteral("boot")).toInt(), parser.value(QStringLiteral("description")));
    if (path.isEmpty()) {
        std::fprintf(stderr, "wildos-report: the report could not be saved\n");
        return 1;
    }
    std::printf("%s\n", qPrintable(path));
    return 0;
}

int crashNotify(int argc, char *argv[])
{
    // Nothing is drawn: notifications go over D-Bus. KNotification still
    // wants a GUI application for the icon, and offscreen needs no display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    setNames(app);
    app.setQuitLockEnabled(false);
    CrashNotify notify;
    notify.start();
    return app.exec();
}
}

int main(int argc, char *argv[])
{
    if (has(argc, argv, "--save")) {
        return saveHeadless(argc, argv);
    }
    if (has(argc, argv, "--crash-notify")) {
        return crashNotify(argc, argv);
    }

    // A QApplication, so the KDE platform theme hands the save dialog to
    // the Wild OS file dialog.
    QApplication app(argc, argv);
    setNames(app);
    app.setApplicationDisplayName(QStringLiteral("Report a problem"));
    app.setDesktopFileName(QStringLiteral("org.wildos.report"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-report")));
    // The window hides while Capture takes a screenshot; closing it is what
    // ends the app (Main.qml).
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Report a problem"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("part"), QStringLiteral("The part of Wild OS to report a problem with."), QStringLiteral("part")});
    parser.addOption({QStringLiteral("crash"), QStringLiteral("When it crashed, in microseconds since 1970."), QStringLiteral("usec")});
    parser.addOption({QStringLiteral("app"), QStringLiteral("The name of the app that crashed."), QStringLiteral("name")});
    parser.addOption({QStringLiteral("event"), QStringLiteral("An event from History to attach."), QStringLiteral("file")});
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("wildos-report:<part>"));
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    Report *report = Report::instance();
    report->handle(app.arguments());
    QObject::connect(&service, &KDBusService::activateRequested, report, [report](const QStringList &arguments, const QString &) {
        report->handle(arguments);
    });

    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    // QQmlApplicationEngine only wires Qt.quit() up when it loads the window
    // itself; this window comes through a component and an incubator. Left
    // unwired, closing the window hid it but the app kept running, holding
    // the D-Bus name, so every later launch went to a window nobody saw.
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit, Qt::QueuedConnection);

    QObject *splash = makeSplash(&engine);
    QQmlComponent home(&engine);
    QQmlIncubator incubator(QQmlIncubator::Asynchronous);
    if (splash) {
        incubator.setInitialProperties({{QStringLiteral("visible"), false}});
    }

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
    home.loadFromModule("WildOS.Report", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    return app.exec();
}
