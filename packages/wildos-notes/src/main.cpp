// wildos-notes: Notes, the Wild OS notes app. Every note is a Markdown file
// in ~/Notes, one folder per notebook, so they read the same in any editor.
// Nothing goes online.
//
//   wildos-notes            the Notes window
//   wildos-notes --quick    a quick note, the window Super+N opens
//
// One instance: a second start hands its arguments to the running one.

#include "notesapp.h"
#include "notestore.h"

#include <KDBusService>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDebug>
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

// The launch splash (SplashDaily.dc), in the icon's yellow.
QObject *makeSplash(QQmlEngine *engine)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Notes")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-notes.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#ffd040"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#e08c00"))},
        {QStringLiteral("status"), QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Notes, lists and ideas")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Nothing leaves this PC")},
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
    // platform theme hand the picture dialog to the Wild OS file dialog.
    QApplication app(argc, argv);
    // KDBusService takes the D-Bus name org.wildos.notes from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("notes"));
    app.setApplicationDisplayName(QStringLiteral("Notes"));
    app.setApplicationVersion(QStringLiteral(WILDOS_NOTES_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.notes"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-notes")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Notes"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("quick"), QStringLiteral("Open a quick note.")});
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    NotesApp *notes = NotesApp::instance();
    notes->setEngine(&engine);
    NoteStore::instance();
    QObject::connect(&service, &KDBusService::activateRequested, notes, [notes](const QStringList &arguments, const QString &) {
        notes->handle(arguments);
    });

    if (parser.isSet(QStringLiteral("quick"))) {
        notes->quickNote();
        return app.exec();
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
        notes->setMainWindow(window);
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
    home.loadFromModule("WildOS.Notes", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    const int code = app.exec();
    // The window goes while the engine is still whole: as the engine's child
    // it went after the QML state, and a dialog left open crashed the app on
    // its way out.
    delete incubator.object();
    return code;
}
