#include <QColor>
#include <QQmlComponent>
#include <QElapsedTimer>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include "accounts.h"
#include "googlesignin.h"
#include "mailengine.h"
#include "notifier.h"
#include "reminders.h"

#include <QCoreApplication>
#include <QDate>
#include <sys/types.h>
#include <cstdlib>
#include <csignal>
#include <QTimer>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QThread>
#include <QFileInfo>
#include <QProcess>

namespace
{
const QString serviceName = QStringLiteral("org.wildos.Mail");
const QString objectPath = QStringLiteral("/Mail");
const QString calendarService = QStringLiteral("org.wildos.Calendar");
const QString calendarPath = QStringLiteral("/Calendar");
const QString remindersService = QStringLiteral("org.wildos.CalendarReminders");
const QString backgroundService = QStringLiteral("org.wildos.MailBackground");
const QString backgroundPath = QStringLiteral("/MailBackground");

bool registered(const QString &name)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface()->isServiceRegistered(name);
}

void callOpen(const QString &service, const QString &path, const QString &arg)
{
    auto message = QDBusMessage::createMethodCall(service, path, service, QStringLiteral("open"));
    message << arg;
    QDBusConnection::sessionBus().call(message);
}

void raise(QQmlApplicationEngine *engine)
{
    const auto roots = engine->rootObjects();
    if (auto window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.constFirst())) {
        if (window->visibility() == QWindow::Minimized) {
            window->showNormal();
        }
        window->show();
        window->raise();
        window->requestActivate();
    }
}

// mailto:someone@example.com?subject=Hi&cc=...&body=... as composer fields.
QVariantMap fromMailto(const QString &arg)
{
    QVariantMap data;
    const QUrl url(arg);
    if (url.scheme() != QLatin1String("mailto")) {
        return data;
    }
    QStringList to = url.path().split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QUrlQuery query(url);
    for (const auto &[key, value] : query.queryItems(QUrl::FullyDecoded)) {
        const QString k = key.toLower();
        if (k == QLatin1String("to")) {
            to << value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (k == QLatin1String("cc")) {
            data.insert(QStringLiteral("cc"), value.split(QLatin1Char(','), Qt::SkipEmptyParts));
        } else if (k == QLatin1String("subject")) {
            data.insert(QStringLiteral("subject"), value);
        } else if (k == QLatin1String("body")) {
            data.insert(QStringLiteral("body"), value);
        }
    }
    data.insert(QStringLiteral("to"), to);
    return data;
}
}

// Opening Mail again (or a mailto: link) brings the open window forward
// instead of starting a second one.
class Remote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Mail")

public:
    explicit Remote(QQmlApplicationEngine *engine)
        : QObject(engine)
        , m_engine(engine)
    {
    }

public Q_SLOTS:
    Q_SCRIPTABLE void open(const QString &arg)
    {
        const auto roots = m_engine->rootObjects();
        if (roots.isEmpty()) {
            return;
        }
        QObject *root = roots.constFirst();
        // Google sends the browser back here after "Sign in with Google";
        // Mail comes forward only if the sign-in was its own.
        if (GoogleSignIn::isRedirect(arg)) {
            const bool mine = GoogleSignIn::instance()->waiting();
            GoogleSignIn::instance()->handleRedirect(arg);
            if (mine) {
                raise(m_engine);
            }
            return;
        }
        const QVariantMap mailto = fromMailto(arg);
        if (!mailto.isEmpty()) {
            QMetaObject::invokeMethod(root, "compose", Q_ARG(QVariant, mailto));
        }
        raise(m_engine);
    }
    // A new-mail notification's Open, from the copy of Mail that watches
    // the Inbox while this window is closed.
    Q_SCRIPTABLE void showMessage(const QString &key)
    {
        const auto roots = m_engine->rootObjects();
        if (!roots.isEmpty() && !key.isEmpty()) {
            QMetaObject::invokeMethod(roots.constFirst(), "showMessage", Q_ARG(QVariant, key));
        }
        raise(m_engine);
    }
    // wildos-mail --people or --tasks, or a task's reminder: that tab.
    Q_SCRIPTABLE void showSection(const QString &section)
    {
        const auto roots = m_engine->rootObjects();
        if (!roots.isEmpty()) {
            QMetaObject::invokeMethod(roots.constFirst(), "showSection", Q_ARG(QVariant, section));
        }
        raise(m_engine);
    }
    // Calendar's launcher, or a reminder's Open: the Calendar tab, at the
    // event if one is named.
    Q_SCRIPTABLE void showCalendar(const QString &key, const QString &day)
    {
        const auto roots = m_engine->rootObjects();
        if (!roots.isEmpty()) {
            QMetaObject::invokeMethod(roots.constFirst(), "showCalendar", Q_ARG(QVariant, key),
                                      Q_ARG(QVariant, QVariant::fromValue(QDate::fromString(day, Qt::ISODate))));
        }
        raise(m_engine);
    }

private:
    QQmlApplicationEngine *m_engine;
};

// The same for Calendar: opening it again, or a reminder's Open, which
// names an event and its day.
class CalendarRemote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Calendar")

public:
    explicit CalendarRemote(QQmlApplicationEngine *engine)
        : QObject(engine)
        , m_engine(engine)
    {
    }

public Q_SLOTS:
    Q_SCRIPTABLE void open(const QString &arg)
    {
        if (GoogleSignIn::isRedirect(arg)) {
            if (!GoogleSignIn::instance()->waiting()) {
                return;
            }
            GoogleSignIn::instance()->handleRedirect(arg);
        }
        raise(m_engine);
    }
    Q_SCRIPTABLE void showEvent(const QString &key, const QString &day)
    {
        const auto roots = m_engine->rootObjects();
        if (!roots.isEmpty() && !key.isEmpty()) {
            QMetaObject::invokeMethod(roots.constFirst(), "showEvent", Q_ARG(QVariant, key),
                                      Q_ARG(QVariant, QVariant::fromValue(QDate::fromString(day, Qt::ISODate))));
        }
        raise(m_engine);
    }

private:
    QQmlApplicationEngine *m_engine;
};

// The copy of Mail that runs while its window is closed: it answers only
// "quit", which Mail sends when its window opens.
class BackgroundRemote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.MailBackground")

public:
    using QObject::QObject;

public Q_SLOTS:
    // Mail's window is opening and needs the accounts: let go of the name
    // at once and leave without waiting for server calls under way (an
    // IMAP command can take a minute to time out). Camel's caches are
    // SQLite, which survives a process ending mid-write.
    Q_SCRIPTABLE void quit()
    {
        QDBusConnection::sessionBus().unregisterService(backgroundService);
        QTimer::singleShot(0, []() {
            std::_Exit(0);
        });
    }
};

namespace
{
// New mail notifications while Mail is closed: on in Settings (both the
// notification and its "while Mail is closed" switch) and at least one
// account. A PC with no mail account never runs it.
bool backgroundWanted()
{
    QSettings settings;
    return settings.value(QStringLiteral("notifications/mail"), true).toBool()
        && settings.value(QStringLiteral("notifications/background"), true).toBool()
        && !AccountStore().accounts().isEmpty();
}

// Only one copy of Mail may have the accounts open at a time: both would
// write the same mail cache. Mail's window asks the background copy to quit
// and waits for it to be gone before starting its own.
void stopBackground()
{
    if (!registered(backgroundService)) {
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QDBusReply<uint> pid = bus.interface()->servicePid(backgroundService);
    bus.call(QDBusMessage::createMethodCall(backgroundService, backgroundPath, backgroundService, QStringLiteral("quit")));
    // Gone means the process has ended, not only that it gave up its name:
    // two copies must never have the mail cache open at once.
    if (!pid.isValid() || pid.value() == 0) {
        return;
    }
    const QString proc = QStringLiteral("/proc/%1").arg(pid.value());
    QElapsedTimer waited;
    waited.start();
    while (QFileInfo::exists(proc) && waited.elapsed() < 3000) {
        QThread::msleep(20);
    }
    if (QFileInfo::exists(proc)) {
        ::kill(pid_t(pid.value()), SIGKILL);
        while (QFileInfo::exists(proc) && waited.elapsed() < 5000) {
            QThread::msleep(20);
        }
    }
}

// wildos-mail --background: no window; watches every account's Inbox and
// shows the same new mail notifications as the open window, from sign-in
// and whenever Mail's window closes, until the window opens again.
int runBackground(int argc, char *argv[])
{
    // Nothing is drawn; the notification server shows the notifications.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-mail"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setDesktopFileName(QStringLiteral("org.wildos.mail"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || registered(serviceName) || registered(backgroundService) || !backgroundWanted()) {
        return 0;
    }
    bus.registerObject(backgroundPath, new BackgroundRemote(&app), QDBusConnection::ExportScriptableSlots);
    if (!bus.registerService(backgroundService)) {
        return 0;
    }
    // Mail's window started some other way: it has the accounts now.
    QDBusServiceWatcher watcher(serviceName, bus, QDBusServiceWatcher::WatchForRegistration);
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceRegistered, &app, &QCoreApplication::quit);
    Notifier notifier;
    QObject::connect(&notifier, &Notifier::openRequested, &app, [](const QString &key) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--message"), key});
    });
    // New mail is noticed in the Inbox view, as in the window.
    MailEngine::instance()->openFolder({}, QStringLiteral("inbox"));
    // At sign-in the network is often still coming up and the first tries
    // fail; look again soon rather than at the next minute's check.
    QTimer::singleShot(10000, &app, []() {
        MailEngine::instance()->refresh();
    });
    return app.exec();
}

// wildos-calendar --reminders: no window, just the reminders, from sign-in.
int runReminders(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-mail"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    if (registered(remindersService)) {
        return 0;
    }
    Reminders reminders;
    if (!reminders.start()) {
        return 1;
    }
    QDBusConnection::sessionBus().registerService(remindersService);
    return app.exec();
}

int runCalendar(int argc, char *argv[])
{
    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand open and save dialogs to the Wild OS file dialog.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-mail"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Calendar"));
    app.setApplicationVersion(QStringLiteral(WILDOS_MAIL_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.calendar"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.wildos.calendar")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Calendar"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption event(QStringLiteral("event"), QStringLiteral("An event to open."), QStringLiteral("key"));
    const QCommandLineOption day(QStringLiteral("day"), QStringLiteral("The day it is on, as YYYY-MM-DD."), QStringLiteral("date"));
    parser.addOption(event);
    parser.addOption(day);
    parser.addOption(QCommandLineOption(QStringLiteral("calendar")));
    parser.addOption(QCommandLineOption(QStringLiteral("new-window")));
    parser.process(app);

    if (registered(calendarService)) {
        auto message = QDBusMessage::createMethodCall(calendarService, calendarPath, calendarService, QStringLiteral("showEvent"));
        message << parser.value(event) << parser.value(day);
        QDBusConnection::sessionBus().call(message);
        return 0;
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WildOS.Mail", "CalendarMain");

    auto *remote = new CalendarRemote(&engine);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.registerObject(calendarPath, remote, QDBusConnection::ExportScriptableSlots);
        bus.registerService(calendarService);
    }
    if (parser.isSet(event)) {
        remote->showEvent(parser.value(event), parser.value(day));
    }
    // Reminders start at sign-in; started here too in case that was missed.
    if (!registered(remindersService)) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--reminders")});
    }
    return app.exec();
}
}

// The launch splash, drawn before the window is built so it is on screen
// while Mail starts. Returns null if it could not be made.
QObject *showSplash(QQmlApplicationEngine *engine, bool calendar)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QString icons = QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/");
    QVariantMap properties = calendar
        ? QVariantMap{
              {QStringLiteral("appName"), QCoreApplication::translate("Splash", "Calendar")},
              {QStringLiteral("iconSource"), QUrl(icons + QStringLiteral("org.wildos.calendar.svg"))},
              {QStringLiteral("accent"), QColor(QStringLiteral("#ff8a6e"))},
              {QStringLiteral("accentDeep"), QColor(QStringLiteral("#d9362b"))},
              {QStringLiteral("status"), QCoreApplication::translate("Splash", "Opening your calendar")},
              {QStringLiteral("noteStart"), QCoreApplication::translate("Splash", "Google, iCloud and CalDAV calendars")},
              {QStringLiteral("noteEnd"), QCoreApplication::translate("Splash", "Reminders from sign-in")},
          }
        : QVariantMap{
              {QStringLiteral("appName"), QCoreApplication::translate("Splash", "Mail")},
              {QStringLiteral("iconSource"), QUrl(icons + QStringLiteral("org.wildos.mail.svg"))},
              {QStringLiteral("accent"), QColor(QStringLiteral("#3fb6ff"))},
              {QStringLiteral("accentDeep"), QColor(QStringLiteral("#0a63d1"))},
              {QStringLiteral("status"), QCoreApplication::translate("Splash", "Opening your inbox")},
              {QStringLiteral("noteStart"), QCoreApplication::translate("Splash", "Gmail, iCloud and other IMAP accounts")},
              {QStringLiteral("noteEnd"), QCoreApplication::translate("Splash", "Trackers blocked")},
          };
    properties.insert(QStringLiteral("showDelay"), 0);
    properties.insert(QStringLiteral("minimumTime"), 900);
    QObject *splash = component.createWithInitialProperties(properties);
    auto *window = qobject_cast<QQuickWindow *>(splash);
    if (!window) {
        delete splash;
        return nullptr;
    }
    // Let it reach the screen before building the window blocks the loop.
    QElapsedTimer waited;
    waited.start();
    bool drawn = false;
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&drawn]() {
        drawn = true;
    }, Qt::DirectConnection);
    while (!drawn && waited.elapsed() < 500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QObject::disconnect(window, &QQuickWindow::frameSwapped, window, nullptr);
    return splash;
}

int main(int argc, char *argv[])
{
    // One program: wildos-calendar is a link to it, and opens Mail's window
    // on its Calendar tab; --new-window gives Calendar a window of its own
    // (the rail's Open in new window), --reminders runs without a window.
    bool calendar = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == QLatin1String("wildos-calendar");
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--reminders") == 0) {
            return runReminders(argc, argv);
        }
        if (qstrcmp(argv[i], "--background") == 0) {
            return runBackground(argc, argv);
        }
        if (qstrcmp(argv[i], "--new-window") == 0) {
            return runCalendar(argc, argv);
        }
        calendar = calendar || qstrcmp(argv[i], "--calendar") == 0;
    }

    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand open and save dialogs to the Wild OS file dialog.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-mail"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Mail"));
    app.setApplicationVersion(QStringLiteral(WILDOS_MAIL_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.mail"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.wildos.mail")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Mail"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("mailto"), QStringLiteral("A mailto: link to start a message from."));
    const QCommandLineOption calendarOption(QStringLiteral("calendar"), QStringLiteral("Open on the Calendar tab."));
    const QCommandLineOption event(QStringLiteral("event"), QStringLiteral("An event to open in Calendar."), QStringLiteral("key"));
    const QCommandLineOption day(QStringLiteral("day"), QStringLiteral("The day it is on, as YYYY-MM-DD."), QStringLiteral("date"));
    const QCommandLineOption peopleOption(QStringLiteral("people"), QStringLiteral("Open on the People tab."));
    const QCommandLineOption tasksOption(QStringLiteral("tasks"), QStringLiteral("Open on the Tasks tab."));
    parser.addOption(calendarOption);
    parser.addOption(event);
    parser.addOption(day);
    parser.addOption(peopleOption);
    parser.addOption(tasksOption);
    const QCommandLineOption messageOption(QStringLiteral("message"), QStringLiteral("A message to open, from a notification."), QStringLiteral("key"));
    parser.addOption(messageOption);
    parser.process(app);
    const QString arg = parser.positionalArguments().value(0);
    calendar = calendar || parser.isSet(calendarOption) || parser.isSet(event);
    const QString section = parser.isSet(peopleOption) ? QStringLiteral("people") : parser.isSet(tasksOption) ? QStringLiteral("tasks") : QString();

    QDBusConnection bus = QDBusConnection::sessionBus();
    // Google's answer to a sign-in goes to whichever of Mail and Calendar
    // started it (each ignores one it didn't), and never opens a window.
    if (GoogleSignIn::isRedirect(arg)) {
        if (registered(serviceName)) {
            callOpen(serviceName, objectPath, arg);
        }
        if (registered(calendarService)) {
            callOpen(calendarService, calendarPath, arg);
        }
        return 0;
    }
    if (registered(serviceName)) {
        if (parser.isSet(messageOption)) {
            auto message = QDBusMessage::createMethodCall(serviceName, objectPath, serviceName, QStringLiteral("showMessage"));
            message << parser.value(messageOption);
            bus.call(message);
        } else if (!section.isEmpty()) {
            auto message = QDBusMessage::createMethodCall(serviceName, objectPath, serviceName, QStringLiteral("showSection"));
            message << section;
            bus.call(message);
        } else if (calendar) {
            auto message = QDBusMessage::createMethodCall(serviceName, objectPath, serviceName, QStringLiteral("showCalendar"));
            message << parser.value(event) << parser.value(day);
            bus.call(message);
        } else {
            callOpen(serviceName, objectPath, arg);
        }
        return 0;
    }

    stopBackground();

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    // As Office apps do, Mail opens on its splash (board 12.5) and the window
    // follows: it is built hidden behind the splash and shown as it fades.
    // Mail opens on the tab it was closed on (Main.qml), so when nothing
    // asks for another the splash is that tab's too.
    const bool lastWasCalendar = section.isEmpty() && arg.isEmpty()
        && QSettings().value(QStringLiteral("Window/section")).toString() == QLatin1String("calendar");
    QObject *splash = showSplash(&engine, calendar || lastWasCalendar);
    if (splash) {
        engine.setInitialProperties({{QStringLiteral("visible"), false}});
    }
    engine.loadFromModule("WildOS.Mail", "Main");
    engine.setInitialProperties({});
    if (splash) {
        auto *window = qobject_cast<QWindow *>(engine.rootObjects().value(engine.rootObjects().size() - 1));
        if (window && window != splash) {
            QObject::connect(splash, SIGNAL(finished()), window, SLOT(show()));
            QObject::connect(splash, SIGNAL(finished()), window, SLOT(requestActivate()));
            QMetaObject::invokeMethod(splash, "finish");
        } else if (window) {
            QMetaObject::invokeMethod(splash, "finish");
        }
    }

    auto *remote = new Remote(&engine);
    if (bus.isConnected()) {
        bus.registerObject(objectPath, remote, QDBusConnection::ExportScriptableSlots);
        bus.registerService(serviceName);
    }
    if (!arg.isEmpty()) {
        remote->open(arg);
    }
    if (calendar) {
        remote->showCalendar(parser.value(event), parser.value(day));
    } else if (!section.isEmpty()) {
        remote->showSection(section);
    }
    if (parser.isSet(messageOption)) {
        remote->showMessage(parser.value(messageOption));
    }
    // Reminders start at sign-in; started here too in case that was missed.
    if (!registered(remindersService)) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--reminders")});
    }
    const int result = app.exec();
    // The window is closed: new mail notifications carry on without it.
    bus.unregisterService(serviceName);
    if (backgroundWanted()) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--background")});
    }
    return result;
}

#include "main.moc"
