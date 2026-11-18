#include "videosapp.h"
#include "player.h"

#include <KWindowSystem>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QTemporaryFile>
#include <QTimer>

namespace
{
VideosApp *s_instance = nullptr;

const QString pipPath = QStringLiteral("/org/wildos/videos/PictureInPicture");

QDBusMessage kwinCall(const QString &path, const QString &interface, const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), path, interface, method);
    message.setArguments(arguments);
    return message;
}

// %1 the process, %2 what to do to the window it finds. Only this process's
// normal window matches: menus and tooltips are not normal windows.
constexpr const char findWindow[] = R"(
(function () {
    for (const w of workspace.windowList()) {
        if (w.pid === %1 && w.normalWindow) {
            %2
            return;
        }
    }
})();
)";

// %1 width, %2 height. KWin is told where the window was first, so leaving
// can put it back.
constexpr const char shrink[] = R"(
const g = w.frameGeometry;
callDBus("org.wildos.videos", "/org/wildos/videos/PictureInPicture", "org.wildos.videos.PictureInPicture", "remember",
         [Math.round(g.x), Math.round(g.y), Math.round(g.width), Math.round(g.height)].join(","));
const area = workspace.clientArea(KWin.MaximizeArea, w);
w.keepAbove = true;
w.frameGeometry = { x: area.x + area.width - %1 - 24, y: area.y + area.height - %2 - 24, width: %1, height: %2 };
)";

// %1 the geometry to go back to, or null to centre the window at %2 by %3.
constexpr const char grow[] = R"(
const back = %1;
w.keepAbove = false;
if (back) {
    w.frameGeometry = back;
} else {
    const area = workspace.clientArea(KWin.MaximizeArea, w);
    w.frameGeometry = { x: area.x + (area.width - %2) / 2, y: area.y + (area.height - %3) / 2, width: %2, height: %3 };
}
)";
}

VideosApp::VideosApp(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    QDBusConnection::sessionBus().registerObject(pipPath, this, QDBusConnection::ExportScriptableSlots);
}

VideosApp *VideosApp::instance()
{
    if (!s_instance) {
        new VideosApp(QCoreApplication::instance());
    }
    return s_instance;
}

VideosApp *VideosApp::create(QQmlEngine *, QJSEngine *)
{
    VideosApp *app = instance();
    QQmlEngine::setObjectOwnership(app, QQmlEngine::CppOwnership);
    return app;
}

void VideosApp::setMainWindow(QQuickWindow *window)
{
    m_main = window;
}

QStringList VideosApp::filesFrom(const QStringList &arguments, const QString &workingDirectory)
{
    const QDir dir(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    QStringList files;
    for (const QString &arg : arguments) {
        if (arg.startsWith(QLatin1Char('-'))) {
            continue;
        }
        files.append(arg.contains(QLatin1String("://")) ? arg : dir.absoluteFilePath(arg));
    }
    return files;
}

void VideosApp::handle(const QStringList &arguments, const QString &workingDirectory)
{
    const QStringList files = filesFrom(arguments.mid(1), workingDirectory);
    if (!files.isEmpty()) {
        Player::instance()->openFiles(files);
    }
    showMain();
}

void VideosApp::showMain()
{
    if (m_main) {
        activate(m_main);
    }
}

void VideosApp::activate(QQuickWindow *window)
{
    window->show();
    window->raise();
    // A start from the launcher carries an activation token, which lets the
    // window take the focus from the app in front.
    KWindowSystem::updateStartupId(window);
    KWindowSystem::activateWindow(window);
    window->requestActivate();
}

void VideosApp::remember(const QString &geometry)
{
    const QStringList parts = geometry.split(QLatin1Char(','));
    if (parts.size() == 4) {
        m_before = QRect(parts.at(0).toInt(), parts.at(1).toInt(), parts.at(2).toInt(), parts.at(3).toInt());
    }
}

void VideosApp::enterPictureInPicture(const QSize &size)
{
    m_before = QRect();
    if (!QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        // Another window manager: the window shrinks where it is and asks
        // to stay on top the X11 way.
        if (m_main) {
            m_before = m_main->geometry();
            m_main->setFlag(Qt::WindowStaysOnTopHint, true);
            m_main->show();
            // After the window's new smallest size has taken.
            QTimer::singleShot(120, this, [this, size] {
                if (m_main) {
                    m_main->resize(size);
                }
            });
        }
        return;
    }
    runKWinScript(QString::fromLatin1(findWindow).arg(QCoreApplication::applicationPid()).arg(QString::fromLatin1(shrink).arg(size.width()).arg(size.height())));
}

void VideosApp::leavePictureInPicture(const QSize &fallback)
{
    if (!QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        if (m_main) {
            m_main->setFlag(Qt::WindowStaysOnTopHint, false);
            m_main->setGeometry(m_before.isValid() ? m_before : QRect(m_main->position(), fallback));
            m_main->show();
        }
        return;
    }
    const QString back = m_before.isValid()
        ? QStringLiteral("{ x: %1, y: %2, width: %3, height: %4 }").arg(m_before.x()).arg(m_before.y()).arg(m_before.width()).arg(m_before.height())
        : QStringLiteral("null");
    runKWinScript(QString::fromLatin1(findWindow).arg(QCoreApplication::applicationPid()).arg(QString::fromLatin1(grow).arg(back).arg(fallback.width()).arg(fallback.height())));
}

void VideosApp::runKWinScript(const QString &source)
{
    // A short wait: the window's new smallest size has to reach KWin first,
    // or it keeps the window at the old one.
    QTimer::singleShot(120, this, [source] {
        auto *file = new QTemporaryFile(QDir::temp().filePath(QStringLiteral("wildos-videos-XXXXXX.js")));
        if (!file->open()) {
            delete file;
            return;
        }
        file->write(source.toUtf8());
        file->flush();

        const QString plugin = QStringLiteral("wildos-videos-picture-in-picture");
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.call(kwinCall(QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"), {plugin}), QDBus::Block, 500);
        const QDBusMessage loaded = bus.call(kwinCall(QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"),
                                                      {file->fileName(), plugin}),
                                             QDBus::Block, 500);
        const int id = loaded.arguments().value(0).toInt();
        if (loaded.type() == QDBusMessage::ReplyMessage && id >= 0) {
            bus.call(kwinCall(QStringLiteral("/Scripting/Script%1").arg(id), QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"), {}), QDBus::Block, 500);
        }
        // KWin has read the file once the script runs; it is unloaded when
        // it has had time to.
        QTimer::singleShot(2000, [file, plugin] {
            QDBusConnection::sessionBus().asyncCall(
                kwinCall(QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"), {plugin}));
            delete file;
        });
    });
}
