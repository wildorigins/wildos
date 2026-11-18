#include "config.h"
#include "terminalwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>

namespace
{
void usage()
{
    QTextStream(stdout) << "Usage: wildos-terminal [--workdir DIR] [--settings] [-e COMMAND [ARGS...]]\n"
                           "  -d, --workdir DIR   Start in this folder\n"
                           "  --settings          Open with Terminal's settings in front\n"
                           "  -e COMMAND          Run COMMAND instead of the shell; the rest of the line is its arguments\n"
                           "  --version           Show the version\n";
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-terminal"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("Terminal"));
    app.setApplicationVersion(QStringLiteral(WILDOS_TERMINAL_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.terminal"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    app.setQuitOnLastWindowClosed(true);

    // QCommandLineParser cannot take "everything after -e" as a command,
    // and the desktop launches terminals that way (as with xterm and
    // Konsole), so the arguments are read by hand.
    QString directory;
    QStringList command;
    bool settings = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == QLatin1String("-e") || a == QLatin1String("--command") || a == QLatin1String("-x")) {
            command = args.mid(i + 1);
            break;
        } else if ((a == QLatin1String("--workdir") || a == QLatin1String("-d") || a == QLatin1String("--working-directory")) && i + 1 < args.size()) {
            directory = args[++i];
        } else if (a.startsWith(QLatin1String("--workdir=")) || a.startsWith(QLatin1String("--working-directory="))) {
            directory = a.section(QLatin1Char('='), 1);
        } else if (a == QLatin1String("--settings")) {
            // How Remote, whose terminal follows these settings, opens them.
            settings = true;
        } else if (a == QLatin1String("--version") || a == QLatin1String("-v")) {
            QTextStream(stdout) << "wildos-terminal " << WILDOS_TERMINAL_VERSION << "\n";
            return 0;
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            usage();
            return 0;
        }
    }
    // A single quoted command ("htop -d 5") is split as a shell would.
    if (command.size() == 1 && command.first().contains(QLatin1Char(' '))) {
        command = QProcess::splitCommand(command.first());
    }
    // "Open in Terminal" from the desktop starts the terminal inside the
    // folder rather than naming it, so a starting folder other than home
    // (or the root a service manager uses) is taken as the place to open.
    if (directory.isEmpty()) {
        const QString cwd = QDir::currentPath();
        if (cwd != QDir::homePath() && cwd != QDir::rootPath()) {
            directory = cwd;
        }
    }
    if (!directory.isEmpty()) {
        directory = QFileInfo(directory).absoluteFilePath();
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    auto *window = new TerminalWindow(directory, command);
    if (Config::instance()->launchMaximised()) {
        window->showMaximized();
    } else {
        window->show();
    }
    if (settings) {
        window->openSettings();
    }
    return app.exec();
}
