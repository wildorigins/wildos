#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QIcon>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>

#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "office.h"

// Opening Write, Tables or Slides again while its start screen is up brings that
// window forward instead of a second one.
class Remote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Office.Start")

public:
    explicit Remote(QQmlApplicationEngine *engine)
        : QObject(engine)
        , m_engine(engine)
    {
    }

public Q_SLOTS:
    Q_SCRIPTABLE void raise()
    {
        const auto roots = m_engine->rootObjects();
        if (auto window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.constFirst())) {
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

// Shows the start screen, at once or after the launch splash, and makes it
// the window that "Write", "Tables" or "Slides" from Start brings forward
// next time.
class Launcher : public QObject
{
    Q_OBJECT

public:
    Launcher(QQmlApplicationEngine *engine, const QString &service, const QString &path)
        : QObject(engine)
        , m_engine(engine)
        , m_service(service)
        , m_path(path)
    {
    }

public Q_SLOTS:
    void showStart()
    {
        if (m_shown) {
            return;
        }
        m_shown = true;
        m_engine->loadFromModule("WildOS.Office", "Main");
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (bus.isConnected()) {
            bus.registerObject(m_path, new Remote(m_engine), QDBusConnection::ExportScriptableSlots);
            bus.registerService(m_service);
        }
    }

private:
    QQmlApplicationEngine *m_engine;
    QString m_service;
    QString m_path;
    bool m_shown = false;
};

int main(int argc, char *argv[])
{
    // One program under four names: wildos-write, wildos-tables and
    // wildos-slides are links to it, and --write, --tables or --slides pick
    // the same from any name.
    const std::string self = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName().toStdString();
    Office::App which = self == "wildos-tables" ? Office::Tables : self == "wildos-slides" ? Office::Slides : Office::Write;
    bool blank = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tables") == 0) {
            which = Office::Tables;
        } else if (std::strcmp(argv[i], "--slides") == 0) {
            which = Office::Slides;
        } else if (std::strcmp(argv[i], "--write") == 0) {
            which = Office::Write;
        } else if (std::strcmp(argv[i], "--new") == 0) {
            blank = true;
        } else if (std::strcmp(argv[i], "--") != 0 && argv[i][0] != '-') {
            files.emplace_back(argv[i]);
        }
    }
    Office::setApp(which);
    const bool tables = which == Office::Tables;
    const bool slides = which == Office::Slides;

    // Wild OS draws LibreOffice with its KDE look; a session can still
    // choose another.
    setenv("SAL_USE_VCLPLUGIN", "kf6", 0);

    // A file from Files or "Open with", or the launcher's New document.
    // LibreOffice starts without its own splash; on a cold start Wild OS
    // shows its splash while it loads, and with LibreOffice already running
    // the window opens at once, so nothing else needs to start.
    const bool opening = !files.empty() || blank;
    std::vector<std::string> sofficeArgs;
    if (opening) {
        sofficeArgs.emplace_back(tables ? "--calc" : slides ? "--impress" : "--writer");
        sofficeArgs.emplace_back("--nologo");
        for (const auto &file : files) {
            sofficeArgs.push_back(file);
        }
    }
    if (opening && Office::running()) {
        const std::string soffice = Office::soffice().toStdString();
        std::vector<char *> args;
        args.push_back(const_cast<char *>(soffice.c_str()));
        for (auto &arg : sofficeArgs) {
            args.push_back(arg.data());
        }
        args.push_back(nullptr);
        execv(soffice.c_str(), args.data());
        return 127;
    }

    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand open and save dialogs to the Wild OS file dialog.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-office"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationVersion(QStringLiteral(WILDOS_OFFICE_VERSION));
    // LibreOffice's own ids, which the Write, Tables and Slides launchers are
    // installed under, so the start screen and the documents it opens share
    // one taskbar button.
    app.setDesktopFileName(tables ? QStringLiteral("libreoffice-calc") : slides ? QStringLiteral("libreoffice-impress") : QStringLiteral("libreoffice-writer"));
    app.setWindowIcon(QIcon::fromTheme(tables ? QStringLiteral("org.wildos.tables") : slides ? QStringLiteral("org.wildos.slides") : QStringLiteral("org.wildos.write")));
    Office::followSpellingLanguage();

    if (opening) {
        QStringList arguments;
        for (const auto &arg : sofficeArgs) {
            arguments << QString::fromLocal8Bit(arg.c_str());
        }
        if (!QProcess::startDetached(Office::soffice(), arguments)) {
            return 1;
        }
        QQmlApplicationEngine engine;
        engine.setInitialProperties({
            {QStringLiteral("fileName"), files.empty() ? QString() : QUrl::fromUserInput(QString::fromLocal8Bit(files.front().c_str()), QDir::currentPath()).fileName()},
        });
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
            QCoreApplication::exit(0);
        }, Qt::QueuedConnection);
        engine.loadFromModule("WildOS.Office", "Splash");
        return app.exec();
    }

    const QString service = tables ? QStringLiteral("org.wildos.Tables") : slides ? QStringLiteral("org.wildos.Slides") : QStringLiteral("org.wildos.Write");
    const QString path = QStringLiteral("/Start");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.interface()->isServiceRegistered(service)) {
        bus.call(QDBusMessage::createMethodCall(service, path, QStringLiteral("org.wildos.Office.Start"), QStringLiteral("raise")));
        return 0;
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    auto *launcher = new Launcher(&engine, service, path);

    if (Office::running()) {
        launcher->showStart();
    } else {
        // Opened from Start with LibreOffice not yet running: the splash shows
        // while LibreOffice starts once without a window and exits, which
        // sets up its profile and loads it from disk, so whatever is picked on
        // the start screen that follows opens quickly. A LibreOffice kept
        // running hidden instead would keep the documents it opens hidden.
        engine.setInitialProperties({{QStringLiteral("launching"), true}});
        engine.loadFromModule("WildOS.Office", "Splash");
        engine.setInitialProperties({});
        QObject *splash = engine.rootObjects().value(0);
        if (!splash) {
            launcher->showStart();
        } else {
            QObject::connect(splash, SIGNAL(finished()), launcher, SLOT(showStart()));
            auto *warm = new QProcess(launcher);
            QObject::connect(warm, &QProcess::finished, splash, [splash]() {
                QMetaObject::invokeMethod(splash, "finish");
            });
            QObject::connect(warm, &QProcess::errorOccurred, splash, [splash](QProcess::ProcessError) {
                QMetaObject::invokeMethod(splash, "finish");
            });
            warm->start(Office::soffice(), {QStringLiteral("--headless"), QStringLiteral("--nologo"), QStringLiteral("--norestore"), QStringLiteral("--terminate_after_init")});
        }
    }
    return app.exec();
}

#include "main.moc"
