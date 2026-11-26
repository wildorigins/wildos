// wildos-archive: Archive, the Wild OS archive manager. Opens ZIP, 7z,
// RAR and TAR archives and GZ, XZ, BZ2 and ZST files, and makes ZIP and 7z
// archives, on libarchive. Nothing goes online.
//
//   wildos-archive [ARCHIVE...]
//   wildos-archive --extract-here ARCHIVE...    Files' Extract here
//   wildos-archive --compress FILE...           Files' Compress...
//
// One process: a second start hands its command line to the running one.

#include "archiveapp.h"
#include "thumbnails.h"

#include <KDBusService>

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQmlIncubator>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <clocale>
#include <cstring>
#include <langinfo.h>

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

// The launch splash (SplashDaily.dc), in the icon's amber. Opening an
// archive it waits a third of a second first, so a quick open never
// flashes; started on its own it shows at once and stays long enough to
// read.
QObject *makeSplash(QQmlEngine *engine, const QString &fileName)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const bool opening = !fileName.isEmpty();
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("Archive")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-archive.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#e3ac6c"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#93592a"))},
        {QStringLiteral("status"),
         opening ? QCoreApplication::translate("main", "Opening %1").arg(fileName) : QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "ZIP, 7z, RAR, TAR and GZ")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Nothing leaves this PC")},
        {QStringLiteral("showDelay"), opening ? 333 : 0},
        {QStringLiteral("minimumTime"), opening ? 0 : 900},
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
    // platform theme hand open and save dialogs to the Wild OS file dialog.
    QApplication app(argc, argv);
    // libarchive converts names with the C library's character set; names
    // in archives are UTF-8 nearly always, and new ZIPs mark them so.
    if (!std::setlocale(LC_CTYPE, "") || std::strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
        std::setlocale(LC_CTYPE, "C.UTF-8");
    }
    // KDBusService takes the D-Bus name org.wildos.archive from this domain
    // and name, the name in the desktop file.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setOrganizationName(QStringLiteral("wildos"));
    app.setApplicationName(QStringLiteral("archive"));
    app.setApplicationDisplayName(QStringLiteral("Archive"));
    app.setApplicationVersion(QStringLiteral(WILDOS_ARCHIVE_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.archive"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-archive")));
    // Extract here runs with no window until it takes long enough to show
    // one, so the app ends when its jobs do, not when a window closes.
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS Archive"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("extract-here"), QStringLiteral("Extract each archive beside it.")});
    parser.addOption({QStringLiteral("compress"), QStringLiteral("Make a new archive of these files and folders.")});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Archives to open."), QStringLiteral("[files...]"));
    parser.process(app);

    KDBusService service(KDBusService::Unique);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    ArchiveApp *archiveApp = ArchiveApp::instance();

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    engine.addImageProvider(QStringLiteral("archivethumb"), new ThumbnailProvider);
    archiveApp->setEngine(&engine);
    QObject::connect(&service, &KDBusService::activateRequested, archiveApp, [archiveApp](const QStringList &arguments, const QString &workingDirectory) {
        archiveApp->handle(arguments, workingDirectory, false);
    });

    const QStringList arguments = QCoreApplication::arguments();
    if (parser.isSet(QStringLiteral("extract-here"))) {
        archiveApp->handle(arguments, QDir::currentPath(), true);
        return app.exec();
    }

    const QVariantMap first = ArchiveApp::firstWindow(arguments, QDir::currentPath());
    const QString startPath = first.value(QStringLiteral("startPath")).toString();
    QObject *splash = makeSplash(&engine, startPath.isEmpty() ? QString() : QFileInfo(startPath).fileName());
    QQmlComponent home(&engine);
    QQmlIncubator incubator(QQmlIncubator::Asynchronous);
    QVariantMap initial = first;
    if (splash) {
        // Built hidden behind the splash and shown as it fades.
        initial.insert(QStringLiteral("visible"), false);
    }
    incubator.setInitialProperties(initial);

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
        archiveApp->adopt(window);
        if (splash) {
            if (window) {
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(show()));
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(requestActivate()));
            }
            QMetaObject::invokeMethod(splash, "finish");
        }
        archiveApp->handle(arguments, QDir::currentPath(), true);
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
    home.loadFromModule("WildOS.Archive", "Main", QQmlComponent::Asynchronous);
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
