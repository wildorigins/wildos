// wildos-access: the accessibility shortcuts Plasma has no owner for, and
// the switches the lock and sign-in screens flip.
//
//   wildos-access                       in the session (autostart): Super
//                                       Alt H for high contrast, Super Ctrl
//                                       O for the on-screen keyboard and
//                                       Shift pressed five times for sticky
//                                       keys, through KDE's global shortcuts
//   wildos-access toggle WHAT           high-contrast, screen-reader or
//                                       sticky-keys, for the lock and
//                                       sign-in screens, where global
//                                       shortcuts do not reach
//   wildos-access zoom in|out|reset     the magnifier, for the sign-in
//                                       screen
//   wildos-access restore               starts the screen reader if it was
//                                       left on (sign-in screen start-up)
//
// Super Alt S in the session belongs to kaccess and Super plus and minus
// to KWin; both work as Plasma ships them.

#include "accesscore.h"

#include <KGlobalAccel>

#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeySequence>
#include <QTimer>
#include <QVariantMap>

#include <memory>

namespace
{
void notify(const QString &summary, const QString &body)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                          QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    message << QStringLiteral("Accessibility") << uint(0) << QStringLiteral("preferences-desktop-accessibility") << summary << body << QStringList()
            << QVariantMap() << int(5000);
    // A short-lived command: wait for the reply, or it can exit before the
    // message has left.
    QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
}

void toggleStickyKeys(bool tell)
{
    const bool on = !AccessCore::stickyKeys();
    AccessCore::setStickyKeys(on);
    if (tell) {
        notify(on ? QStringLiteral("Sticky keys are on") : QStringLiteral("Sticky keys are off"),
               on ? QStringLiteral("Press the keys of a shortcut one at a time. Press Shift five times to turn this off.")
                  : QStringLiteral("Press Shift five times to turn them on again."));
    }
}

QAction *shortcut(QObject *parent, const QString &name, const QString &text, const QKeySequence &keys)
{
    auto action = new QAction(parent);
    action->setObjectName(name);
    action->setText(text);
    action->setProperty("componentName", QStringLiteral("wildos-access"));
    action->setProperty("componentDisplayName", QStringLiteral("Accessibility"));
    KGlobalAccel::self()->setGlobalShortcut(action, keys);
    return action;
}

int runService(int argc, char **argv)
{
    // No window ever opens here: the shortcuts arrive over D-Bus. Connected
    // to the display, this Qt sets up OpenGL at start-up all the same, which
    // held about 35 MB for as long as the session lasted; without a
    // connection the service takes 2. kaccess, Orca and the on-screen
    // keyboard, started from here, get the session's setting back.
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (platform.isEmpty()) {
        qunsetenv("QT_QPA_PLATFORM");
    } else {
        qputenv("QT_QPA_PLATFORM", platform);
    }
    app.setApplicationName(QStringLiteral("wildos-access"));
    app.setQuitOnLastWindowClosed(false);

    // One per session, however often it is started.
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.wildos.Access"))) {
        return 0;
    }

    QAction *contrast = shortcut(&app, QStringLiteral("high-contrast"), QStringLiteral("Turn high contrast on or off"), QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
    QObject::connect(contrast, &QAction::triggered, &app, [&app] {
        AccessCore::setHighContrast(!AccessCore::highContrast(), &app);
    });

    QAction *keyboard = shortcut(&app, QStringLiteral("on-screen-keyboard"), QStringLiteral("Show or hide the on-screen keyboard"),
                                 QKeySequence(Qt::META | Qt::CTRL | Qt::Key_O));
    QObject::connect(keyboard, &QAction::triggered, &app, [&app] {
        AccessCore::setOnScreenKeyboard(!AccessCore::onScreenKeyboard(), &app);
    });

    // A shortcut of Shift alone fires each time Shift is pressed and let go
    // with no other key, the way Super alone opens Start. KWin passes the
    // key on to the app as well, so nothing is lost in typing. Five in a
    // row, each within a second of the last, toggle sticky keys, as on
    // Windows.
    QAction *shift = shortcut(&app, QStringLiteral("sticky-keys"), QStringLiteral("Sticky keys (press Shift five times)"), QKeySequence(Qt::Key_Shift));
    auto last = std::make_shared<QElapsedTimer>();
    auto count = std::make_shared<int>(0);
    QObject::connect(shift, &QAction::triggered, &app, [last, count] {
        if (!last->isValid() || last->elapsed() > 1000) {
            *count = 0;
        }
        last->restart();
        if (++*count == 5) {
            *count = 0;
            toggleStickyKeys(true);
        }
    });

    return app.exec();
}
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        return runService(argc, argv);
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-access"));
    const QString command = QString::fromLocal8Bit(argv[1]);
    const QString what = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    // Settings written with KConfig::Notify are announced on the session
    // bus, and those announcements are sent from the event loop; quitting
    // straight away could lose them, so the tool lingers for a moment.
    const auto finish = [&app] {
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    };

    if (command == QLatin1String("toggle") && what == QLatin1String("high-contrast")) {
        AccessCore::setHighContrast(!AccessCore::highContrast(), &app, finish);
    } else if (command == QLatin1String("toggle") && what == QLatin1String("screen-reader")) {
        AccessCore::setScreenReader(!AccessCore::screenReader());
        finish();
    } else if (command == QLatin1String("toggle") && what == QLatin1String("sticky-keys")) {
        // The lock and sign-in screens have nowhere to show a notification.
        toggleStickyKeys(false);
        finish();
    } else if (command == QLatin1String("zoom") && (what == QLatin1String("in") || what == QLatin1String("out") || what == QLatin1String("reset"))) {
        AccessCore::zoom(what);
        finish();
    } else if (command == QLatin1String("restore")) {
        AccessCore::restoreScreenReader();
        finish();
    } else {
        qWarning("Usage: wildos-access [toggle high-contrast|screen-reader|sticky-keys] [zoom in|out|reset] [restore]");
        return 2;
    }
    return app.exec();
}
