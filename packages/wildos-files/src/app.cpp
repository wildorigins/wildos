#include "app.h"
#include "driveformat.h"
#include "foldermodel.h"

#include <algorithm>

#include <KWindowSystem>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>

namespace
{
QList<QUrl> toUrls(const QStringList &list)
{
    QList<QUrl> urls;
    for (const QString &text : list) {
        if (text.isEmpty()) {
            continue;
        }
        const QUrl url = QUrl::fromUserInput(text, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (url.isValid()) {
            urls.append(url);
        }
    }
    return urls;
}

bool hasControlCharacter(const QString &text)
{
    return std::any_of(text.cbegin(), text.cend(), [](QChar c) {
        return c.category() == QChar::Other_Control;
    });
}

// Any app can call FileManager1, so its requests are held to what "Show in
// folder" needs: existing local paths. Another scheme would let a caller make
// Files contact a host (smb://, sftp://, webdav://), and control characters,
// raw or percent-encoded, only serve to disguise a path. Unlike Open, which
// the user's own launches go through, nothing is guessed from plain text, and
// the caller's fragment is dropped because ShowItems sets its own.
QStringList localPaths(const QStringList &uriList)
{
    QStringList paths;
    for (const QString &text : uriList) {
        if (hasControlCharacter(text)) {
            continue;
        }
        // Tolerant, because KDE apps (Pictures, Dolphin's callers) send
        // addresses with spaces left as they are, which strict parsing
        // refused, so "Open containing folder" did nothing for a photo
        // with a space in its name.
        const QUrl url(text, QUrl::TolerantMode);
        if (!url.isValid() || !url.isLocalFile() || url.hasQuery()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (!QDir::isAbsolutePath(path) || hasControlCharacter(path)) {
            continue;
        }
        if (QFileInfo::exists(path)) {
            paths.append(QDir::cleanPath(path));
        }
    }
    return paths;
}

QStringList toUris(const QStringList &paths, bool select)
{
    QStringList uris;
    for (const QString &path : paths) {
        QUrl url = QUrl::fromLocalFile(path);
        if (select) {
            url.setFragment(QStringLiteral("wildos-select"));
        }
        uris.append(url.toString());
    }
    return uris;
}

// Requests left with nothing valid are dropped: an empty list would open a
// window on Home. The caller's activation token is what lets the window
// come to the front on Wayland; without it KWin keeps Files behind the app
// that asked, as it would any window that opens uninvited.
void showPaths(const QStringList &uriList, bool select, const QString &startUpId)
{
    const QStringList paths = localPaths(uriList);
    if (paths.isEmpty()) {
        return;
    }
    if (KWindowSystem::isPlatformWayland() && !startUpId.isEmpty()) {
        KWindowSystem::setCurrentXdgActivationToken(startUpId);
    }
    App::instance()->Open(toUris(paths, select));
}
}

App *App::instance()
{
    static App *app = new App;
    return app;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    auto app = instance();
    QJSEngine::setObjectOwnership(app, QJSEngine::CppOwnership);
    return app;
}

App::App(QObject *parent)
    : QObject(parent)
{
}

void App::Open(const QStringList &urls)
{
    openWindow(toUrls(urls));
}

void App::Format(const QString &device)
{
    const QString udi = DriveFormat::objectFor(device);
    if (!udi.isEmpty()) {
        openWindow({FolderModel::computerUrl()}, udi);
    }
}

void App::openWindow(const QList<QUrl> &urls, const QString &formatUdi)
{
    if (!m_engine) {
        return;
    }
    if (!m_component) {
        m_component = new QQmlComponent(m_engine, QStringLiteral("WildOS.Files"), QStringLiteral("FilesWindow"), this);
    }
    QVariantList list;
    for (const QUrl &url : urls) {
        list.append(url);
    }
    QObject *object = m_component->createWithInitialProperties({{QStringLiteral("startUrls"), list}, {QStringLiteral("formatUdi"), formatUdi}});
    if (!object) {
        qWarning() << m_component->errors();
        return;
    }
    object->setParent(this);
    if (auto window = qobject_cast<QQuickWindow *>(object)) {
        // The window frees itself when closed; the process stays only while
        // a window is open.
        connect(window, &QQuickWindow::visibleChanged, window, [window] {
            if (!window->isVisible()) {
                window->deleteLater();
            }
        });
        KWindowSystem::updateStartupId(window);
        window->requestActivate();
        KWindowSystem::activateWindow(window);
    }
}

void App::ChooseRemovedDetails()
{
    if (!m_engine) {
        return;
    }
    // One window at a time: a second request brings the open one forward.
    if (m_choices) {
        m_choices->requestActivate();
        KWindowSystem::activateWindow(m_choices);
        return;
    }
    QQmlComponent component(m_engine, QStringLiteral("WildOS.Files"), QStringLiteral("RemoveDetailsWindow"));
    QObject *object = component.create();
    auto window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning() << component.errors();
        delete object;
        return;
    }
    object->setParent(this);
    m_choices = window;
    connect(window, &QQuickWindow::visibleChanged, window, [window] {
        if (!window->isVisible()) {
            window->deleteLater();
        }
    });
    KWindowSystem::updateStartupId(window);
    window->show();
    window->requestActivate();
    KWindowSystem::activateWindow(window);
}

FileManager1::FileManager1(QObject *parent)
    : QObject(parent)
{
}

// None of these opens a file: a file is shown selected in its folder
// (FolderModel::openUrl), so a caller cannot use them to run or open
// anything. That is the guarantee security-misc's fm-shim gives, and the
// reason Files can stand in for it.
void FileManager1::ShowFolders(const QStringList &uriList, const QString &startUpId)
{
    showPaths(uriList, false, startUpId);
}

void FileManager1::ShowItems(const QStringList &uriList, const QString &startUpId)
{
    // Each item is shown selected in its folder, folders included.
    showPaths(uriList, true, startUpId);
}

void FileManager1::ShowItemProperties(const QStringList &uriList, const QString &startUpId)
{
    showPaths(uriList, true, startUpId);
}
