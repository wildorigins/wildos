// wildos-videos: Videos, the Wild OS video player. Plays the videos on
// this PC with libmpv; remembers where you stopped in each. Nothing goes
// online.
//
//   wildos-videos              the library of the Videos folder
//   wildos-videos FILE         plays a video, as Files opens it
//
// One instance: a second start hands its arguments to the running one.

#include "inhibitor.h"
#include "library.h"
#include "mpris.h"
#include "player.h"
#include "positions.h"
#include "thumbnails.h"
#include "videosapp.h"

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
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

#include <clocale>

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

// The launch splash (SplashDaily.dc), in the icon's teal.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Videos")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-videos.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#45dbe8"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#0a8193"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Your videos, on this PC")},
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
    // mpv draws through OpenGL into the scene, so Qt Quick has to use
    // OpenGL too; it does on Linux already, but a QSG_RHI_BACKEND set for
    // other apps must not take Videos to Vulkan.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand dialogs to the Wild OS ones.
    QApplication app(argc, argv);
    // libmpv reads numbers with the C locale; Qt has just set the user's.
    std::setlocale(LC_NUMERIC, "C");
    // KDBusService takes the D-Bus name org.wildos.videos from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("videos"));
    app.setApplicationDisplayName(QStringLiteral("Videos"));
    app.setApplicationVersion(QStringLiteral(WILDOS_VIDEOS_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.videos"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-videos")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Videos"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The video to play."), QStringLiteral("[file]"));
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    Positions::instance();
    Thumbnails::instance();
    Library::instance();
    Player *player = Player::instance();
    VideosApp *videos = VideosApp::instance();
    Mpris mpris(&app);
    QObject::connect(&mpris, &Mpris::raiseRequested, videos, &VideosApp::showMain);

    // The screen stays on while a video plays, not while one is paused or
    // only sound is playing.
    Inhibitor inhibitor(&app);
    auto inhibit = [player, &inhibitor] {
        inhibitor.setActive(player->playing() && player->hasVideo());
    };
    QObject::connect(player, &Player::playingChanged, &inhibitor, inhibit);
    QObject::connect(player, &Player::tracksChanged, &inhibitor, inhibit);

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    QObject::connect(&service, &KDBusService::activateRequested, videos, [videos](const QStringList &arguments, const QString &workingDirectory) {
        videos->handle(arguments, workingDirectory);
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
        videos->setMainWindow(window);
        if (splash) {
            if (window) {
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(show()));
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(requestActivate()));
            }
            QMetaObject::invokeMethod(splash, "finish");
        }
        const QStringList files = VideosApp::filesFrom(parser.positionalArguments(), QDir::currentPath());
        if (!files.isEmpty()) {
            player->openFiles(files);
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
    home.loadFromModule("WildOS.Videos", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    return app.exec();
}
