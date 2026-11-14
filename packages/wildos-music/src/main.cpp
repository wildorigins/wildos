// wildos-music: Music, the Wild OS music player. Plays the songs in the
// Music folder; playlists are .m3u8 files beside them. Nothing goes online.
//
//   wildos-music              the Music window
//   wildos-music FILE...      plays songs or playlists, as Files opens them
//
// One instance: a second start hands its arguments to the running one.

#include "library.h"
#include "mpris.h"
#include "musicapp.h"
#include "player.h"
#include "playlists.h"

#include <KDBusService>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
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

// The launch splash (SplashDaily.dc), in the icon's pink.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Music")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-music.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#ff7fb8"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#c8246f"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Your songs, on this PC")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Nothing streamed")},
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
    // platform theme hand dialogs to the Wild OS ones.
    QApplication app(argc, argv);
    // KDBusService takes the D-Bus name org.wildos.music from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("music"));
    app.setApplicationDisplayName(QStringLiteral("Music"));
    app.setApplicationVersion(QStringLiteral(WILDOS_MUSIC_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.music"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-music")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Music"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Songs or playlists to play."), QStringLiteral("[files...]"));
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    Library::instance();
    Playlists::instance();
    Player::instance();
    MusicApp *music = MusicApp::instance();
    Mpris mpris(&app);
    QObject::connect(&mpris, &Mpris::raiseRequested, music, &MusicApp::showMain);

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    // The mini player's close button calls Qt.quit(), which
    // QQmlApplicationEngine only wires up when it loads a window itself.
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    music->setEngine(&engine);
    QObject::connect(&service, &KDBusService::activateRequested, music, [music](const QStringList &arguments, const QString &workingDirectory) {
        music->handle(arguments, workingDirectory);
    });

    QObject *splash = makeSplash(&engine);
    QQmlComponent home(&engine);
    QQmlIncubator incubator(QQmlIncubator::Asynchronous);
    if (splash) {
        // Built hidden behind the splash and shown as it fades.
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
        music->setMainWindow(window);
        if (splash) {
            if (window) {
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(show()));
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(requestActivate()));
            }
            QMetaObject::invokeMethod(splash, "finish");
        }
        QStringList files;
        for (const QString &arg : parser.positionalArguments()) {
            files.append(arg.contains(QLatin1String("://")) ? arg : QDir::current().absoluteFilePath(arg));
        }
        if (!files.isEmpty()) {
            Player::instance()->openFiles(files);
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
    home.loadFromModule("WildOS.Music", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    return app.exec();
}
