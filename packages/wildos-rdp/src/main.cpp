// wildos-rdp: Windows mode's RemoteApp client. Each Windows app window is a
// window of its own on the desktop, drawn at the screen's own scale.
//
//   wildos-rdp [--app-id ID --title TITLE --icon ICON --reason TEXT --program EXE] /args-from:stdin
//   wildos-rdp --desktop /args-from:stdin
//
// The FreeRDP arguments are the ones xfreerdp3 takes, one per line on
// standard input with the password last, so nothing secret is ever on a
// command line. With --app-id the loading window for that app is on screen
// before they arrive: the agent starts this the moment an app is opened
// and writes them once Windows is awake.
//
// --desktop opens the whole Windows desktop in one window instead, for
// Settings' "Open Windows desktop"; the arguments arrive the same way.

#include "client.h"
#include "desktopwindow.h"
#include "rail.h"
#include "railwindow.h"
#include "screens.h"
#include "selftest.h"
#include "service.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace
{
int signalPipe[2] = {-1, -1};

void onSignal(int)
{
    const char c = 1;
    (void)!write(signalPipe[1], &c, 1);
}

struct Options {
    QString appId;
    QString title;
    QString icon;
    QString reason;
    QString program;
    bool fromStdin = false;
    // Signs in and leaves the session ready, disconnected, for the next app.
    bool prepareSession = false;
    // The whole desktop in one window (desktopwindow.h).
    bool desktop = false;
    QList<QByteArray> freerdp;
};

Options parse(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if ((a == "--app-id" || a == "--title" || a == "--icon" || a == "--reason" || a == "--program") && i + 1 < argc) {
            const QString value = QString::fromUtf8(argv[++i]);
            (a == "--app-id" ? o.appId : a == "--title" ? o.title : a == "--icon" ? o.icon : a == "--reason" ? o.reason : o.program) = value;
        } else if (a == "/args-from:stdin") {
            o.fromStdin = true;
        } else if (a == "--prepare-session") {
            o.prepareSession = true;
        } else if (a == "--desktop") {
            o.desktop = true;
        } else {
            o.freerdp.append(a);
        }
    }
    return o;
}

// The scale Windows was asked to draw at by the caller, as a fallback for
// when no window has been on screen yet to say what the screen's is.
double askedScale(const QList<QByteArray> &args)
{
    for (const QByteArray &a : args) {
        if (a.startsWith("/scale-desktop:")) {
            bool ok = false;
            const double percent = a.mid(15).toDouble(&ok);
            if (ok && percent >= 100 && percent <= 500) {
                return percent / 100.0;
            }
        }
    }
    return 0;
}

// Reads the arguments (password last) from standard input, keeps the
// window's title out of them, and runs until the window closes or the
// session ends.
int runDesktop(QGuiApplication &app, const Options &options)
{
    QList<QByteArray> args = options.freerdp;
    QString title = QStringLiteral("Windows desktop");
    QFile in;
    if (options.fromStdin && in.open(stdin, QIODevice::ReadOnly)) {
        while (!in.atEnd()) {
            QByteArray line = in.readLine();
            while (line.endsWith('\n') || line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.startsWith("/title:")) {
                title = QString::fromUtf8(line.mid(7));
            } else if (!line.isEmpty()) {
                args.append(line);
            }
        }
    }
    if (args.isEmpty()) {
        return 1;
    }
    QGuiApplication::setQuitOnLastWindowClosed(true);
    int status = 0;
    QString error;
    {
        DesktopWindow window(title);
        QObject::connect(&window, &DesktopWindow::ended, &app, [&status](const QString &code) {
            if (!code.isEmpty()) {
                qWarning("wildos-rdp: the Windows desktop ended: %s", qPrintable(code));
                status = 1;
            }
            QCoreApplication::quit();
        });
        const bool started = window.start(args, &error);
        for (QByteArray &a : args) {
            a.fill('\0');
        }
        if (!started) {
            qWarning("wildos-rdp: the Windows desktop couldn't open: %s", qPrintable(error));
            return 1;
        }
        const int code = app.exec();
        if (code != 0) {
            status = code;
        }
    }
    return status;
}
}

int main(int argc, char **argv)
{
    QElapsedTimer clock;
    clock.start();
    // Each window carries its own app's id; this is only the fallback.
    QGuiApplication::setDesktopFileName(QStringLiteral("wildos-winmode"));
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);

    Options options = parse(argc, argv);

    if (options.desktop) {
        return runDesktop(app, options);
    }

    if (pipe2(signalPipe, O_CLOEXEC) == 0) {
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
    }

    Client client;
    QScreen *screen = QGuiApplication::primaryScreen();
    Rail rail(&client, screen ? screen->devicePixelRatio() : 1.0);
    Service service(&rail, &client);
    if (!service.registerOnBus()) {
        qInfo("wildos-rdp: org.wildos.RemoteApp is not available on the session bus");
    }

    quint32 coldToken = 0;
    // A program Windows mode has no entry for still has a name to show; its
    // window gets the program's own app id when Windows names it.
    if (!options.appId.isEmpty() || !options.title.isEmpty()) {
        coldToken = rail.prepare(options.appId, options.title, options.icon, options.program);
        rail.setCold(coldToken);
        if (!options.reason.isEmpty()) {
            rail.update(coldToken, QString(), options.reason, -1, QString());
        }
    }
    QObject::connect(&rail, &Rail::loadingVisible, &app, [&clock](quint32 token, qint64 ms) {
        qInfo("wildos-rdp: loading window %u on screen %lld ms after it was asked for (%lld ms after start)", token, ms, qint64(clock.elapsed()));
    });

    QElapsedTimer connecting;
    bool firstFrame = false;
    QObject::connect(&rail, &Rail::windowShown, &app, [&](quint32 id, const QString &title) {
        if (!firstFrame && connecting.isValid()) {
            firstFrame = true;
            qInfo("wildos-rdp: first window (%08x \"%s\") %lld ms after connecting began", id, qPrintable(title), qint64(connecting.elapsed()));
        }
    });

    startSelfTest(&rail, &client);

    Screens screens;
    QObject::connect(&screens, &Screens::changed, &app, [&] {
        if (client.running()) {
            rail.relayout(screens.layout(rail.scale()));
        }
    });

    bool quitting = false;
    if (options.prepareSession) {
        client.setPrepareOnly(true);
        // Windows has started the session's shell and sent its windows: the
        // session is ready. Leaving it disconnected (not signed out) keeps it
        // so; the next connection takes it over in a moment instead of
        // signing in afresh.
        QObject::connect(&client, &Client::desktopSynced, &app, [&] {
            qInfo("wildos-rdp: session ready; disconnecting");
            QTimer::singleShot(500, &app, [&] {
                client.stop();
                QTimer::singleShot(2000, &app, [] { QCoreApplication::exit(0); });
            });
        });
    }
    QObject::connect(&client, &Client::finished, &app, [&](const QString &error, bool authFailed) {
        rail.closeAll();
        if (authFailed) {
            qWarning("wildos-rdp: Windows refused the account (%s)", qPrintable(error));
        }
        quitting = true;
        QCoreApplication::exit(error.isEmpty() ? 0 : authFailed ? 2 : 1);
    });

    auto begin = [&](const QList<QByteArray> &args) {
        QString error;
        if (!client.configure(args, &error)) {
            qWarning("wildos-rdp: %s", qPrintable(error));
            rail.closeAll();
            QCoreApplication::exit(1);
            return;
        }
        // Without --program the app is the one /app names, unless that is
        // wild-guest, which only starts the real app and ends.
        if (coldToken && options.program.isEmpty()) {
            const QString program = QString::fromUtf8(freerdp_settings_get_string(client.context()->settings, FreeRDP_RemoteApplicationProgram));
            if (!program.endsWith(QLatin1String("wild-guest.exe"), Qt::CaseInsensitive)) {
                rail.setProgram(coldToken, program);
            }
        }
        // The screen's scale as a window on it reports it, which on a
        // fractional scale is only known once one has been shown.
        double scale = 0;
        for (QWindow *w : QGuiApplication::topLevelWindows()) {
            if (w->isExposed()) {
                scale = w->devicePixelRatio();
                break;
            }
        }
        if (scale <= 0) {
            scale = askedScale(args);
        }
        if (scale <= 0) {
            scale = screen ? screen->devicePixelRatio() : 1.0;
        }
        // A monitor in Windows for every screen, each at its own scale when
        // KWin says exactly what that is; Windows' primary is this desktop's.
        QList<Monitor> layout = screens.layout(scale);
        if (layout.isEmpty()) {
            const QSize logical = screen ? screen->geometry().size() : QSize(1920, 1080);
            Monitor only;
            only.logical = QRect(QPoint(0, 0), logical);
            only.scale = scale;
            layout = monitors::arrange({only});
        }
        const Monitor &primary = layout.first();
        rail.setScale(primary.scale);
        rail.setLayout(layout);
        client.setLayout(layout);
        client.setDisplay(layout.size() > 1 ? monitors::bounds(layout).size() : primary.rect.size(), primary.scale);
        qInfo("wildos-rdp: connecting at scale %.2f, monitors %s", primary.scale, qPrintable(monitors::describe(layout)));
        connecting.start();
        client.start();
    };

    QByteArray input;
    QSocketNotifier *reader = nullptr;
    if (options.fromStdin) {
        const int flags = fcntl(STDIN_FILENO, F_GETFL);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        reader = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
        QObject::connect(reader, &QSocketNotifier::activated, &app, [&] {
            char buffer[4096];
            for (;;) {
                const ssize_t n = read(STDIN_FILENO, buffer, sizeof buffer);
                if (n > 0) {
                    input.append(buffer, n);
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                    return;
                }
                break;
            }
            reader->setEnabled(false);
            QList<QByteArray> args = options.freerdp;
            for (const QByteArray &line : input.split('\n')) {
                const QByteArray arg = line.trimmed();
                if (!arg.isEmpty()) {
                    args.append(arg);
                }
            }
            input.fill('\0');
            input.clear();
            if (args.isEmpty()) {
                // The agent gave up before Windows woke: nothing to connect to.
                rail.closeAll();
                QCoreApplication::exit(1);
                return;
            }
            begin(args);
        });
    } else {
        QTimer::singleShot(0, &app, [&] { begin(options.freerdp); });
    }

    if (signalPipe[0] >= 0) {
        auto *stopper = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(stopper, &QSocketNotifier::activated, &app, [&] {
            char c;
            (void)!read(signalPipe[0], &c, 1);
            if (quitting) {
                return;
            }
            if (client.running()) {
                // A clean goodbye to Windows, but never longer than it takes
                // to say it.
                client.stop();
                QTimer::singleShot(2000, &app, [] { QCoreApplication::exit(0); });
            } else {
                rail.closeAll();
                QCoreApplication::exit(0);
            }
        });
    }

    const int code = app.exec();
    rail.closeAll();
    if (client.running()) {
        // FreeRDP is stuck in a call that does not return (a connection to a
        // Windows that stopped answering): waiting for its thread would leave
        // this process, and its windows, behind. Nothing is left to save.
        qWarning("wildos-rdp: the connection did not end in time; leaving without it");
        fflush(stderr);
        std::_Exit(code);
    }
    return code;
}
