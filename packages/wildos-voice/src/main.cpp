// wildos-voice: Voice, the Wild OS voice recorder. Records to Opus files in
// the Recordings folder, with markers, and writes transcripts on this PC
// with whisper.cpp once they are turned on. Nothing else goes online.
//
//   wildos-voice           the Voice window
//   wildos-voice FILE      opens a recording, as Files does
//
// One instance: a second start hands its arguments to the running one.

#include "mpris.h"
#include "player.h"
#include "recorder.h"
#include "recordings.h"
#include "transcripts.h"
#include "voiceapp.h"

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

// The launch splash (SplashDaily.dc), in the icon's green.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Voice")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-voice.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#9edc4a"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#3f8f12"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Recordings and transcripts")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Stays on this PC")},
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
    // KDBusService takes the D-Bus name org.wildos.voice from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("voice"));
    app.setApplicationDisplayName(QStringLiteral("Voice"));
    app.setApplicationVersion(QStringLiteral(WILDOS_VOICE_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.voice"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-voice")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Voice"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A recording to open."), QStringLiteral("[file]"));
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    Recordings *recordings = Recordings::instance();
    Player::instance();
    Recorder *recorder = Recorder::instance();
    Transcripts *transcripts = Transcripts::instance();
    VoiceApp *voice = VoiceApp::instance();
    Q_UNUSED(recordings)

    Mpris mpris(&app);
    QObject::connect(&mpris, &Mpris::raiseRequested, voice, &VoiceApp::showMain);

    QObject::connect(recorder, &Recorder::started, transcripts, &Transcripts::recordingStarted);
    QObject::connect(recorder, &Recorder::speech, transcripts, &Transcripts::speech);
    QObject::connect(recorder, &Recorder::finished, transcripts, &Transcripts::recordingFinished);
    // Quitting mid-recording keeps the recording: it is finished properly,
    // and the transcript keeps the lines made so far.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [recorder, transcripts]() {
        recorder->stopNow();
        transcripts->shutDown();
    });

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    // Qt.quit(), once a window closed mid-recording has finished its work.
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    QObject::connect(&service, &KDBusService::activateRequested, voice, [voice](const QStringList &arguments, const QString &workingDirectory) {
        voice->handle(arguments, workingDirectory);
    });
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) {
        voice->handle(QStringList{QCoreApplication::applicationFilePath()} + files, QDir::currentPath());
    }

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
        voice->setMainWindow(window);
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
    home.loadFromModule("WildOS.Voice", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    return app.exec();
}
