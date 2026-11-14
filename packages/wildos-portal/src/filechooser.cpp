#include "filechooser.h"

#include "chooser.h"
#include "dbustypes.h"
#include "windows.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QFile>
#include <QFileInfo>
#include <QQuickWindow>

namespace
{
template<typename T>
T fromOption(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>()) {
        return qdbus_cast<T>(value.value<QDBusArgument>());
    }
    return value.value<T>();
}

// Paths come as NUL-terminated byte strings in the file name encoding.
QString pathOption(const QVariant &value)
{
    QByteArray bytes = value.toByteArray();
    while (bytes.endsWith('\0')) {
        bytes.chop(1);
    }
    return QFile::decodeName(bytes);
}

bool sameFilter(const Portal::Filter &a, const Portal::Filter &b)
{
    if (a.name != b.name || a.rules.size() != b.rules.size()) {
        return false;
    }
    for (int i = 0; i < a.rules.size(); ++i) {
        if (a.rules.at(i).type != b.rules.at(i).type || a.rules.at(i).pattern != b.rules.at(i).pattern) {
            return false;
        }
    }
    return true;
}
}

FileChooserPortal::FileChooserPortal(QObject *parent)
    : QObject(parent)
{
}

uint FileChooserPortal::OpenFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title,
                                 const QVariantMap &options, QVariantMap &results)
{
    Q_UNUSED(results);
    const bool directory = options.value(QStringLiteral("directory")).toBool();
    start(directory ? Chooser::Folder : Chooser::Open, handle, app_id, parent_window, title, options);
    return 2;
}

uint FileChooserPortal::SaveFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title,
                                 const QVariantMap &options, QVariantMap &results)
{
    Q_UNUSED(results);
    start(Chooser::Save, handle, app_id, parent_window, title, options);
    return 2;
}

uint FileChooserPortal::SaveFiles(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title,
                                  const QVariantMap &options, QVariantMap &results)
{
    Q_UNUSED(results);
    start(Chooser::SaveFiles, handle, app_id, parent_window, title, options);
    return 2;
}

void FileChooserPortal::start(int mode, const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow, const QString &title,
                              const QVariantMap &options)
{
    setDelayedReply(true);
    const QDBusMessage call = message();

    Chooser::Request request;
    request.mode = Chooser::Mode(mode);
    request.title = title;
    request.appId = appId;
    request.acceptLabel = options.value(QStringLiteral("accept_label")).toString();
    request.multiple = options.value(QStringLiteral("multiple")).toBool();
    request.filters = fromOption<Portal::Filters>(options.value(QStringLiteral("filters")));
    request.choices = fromOption<Portal::Choices>(options.value(QStringLiteral("choices")));
    if (options.contains(QStringLiteral("current_filter"))) {
        const auto current = fromOption<Portal::Filter>(options.value(QStringLiteral("current_filter")));
        for (int i = 0; i < request.filters.size(); ++i) {
            if (sameFilter(request.filters.at(i), current)) {
                request.currentFilter = i;
            }
        }
        // A current filter that is not in the list is the only one.
        if (request.currentFilter < 0 && !current.rules.isEmpty()) {
            request.filters.prepend(current);
            request.currentFilter = 0;
        }
    }
    if (request.currentFilter < 0) {
        request.currentFilter = 0;
    }
    if (options.contains(QStringLiteral("current_folder"))) {
        const QString folder = pathOption(options.value(QStringLiteral("current_folder")));
        if (!folder.isEmpty()) {
            request.folder = QUrl::fromLocalFile(folder);
        }
    }
    if (options.contains(QStringLiteral("current_file"))) {
        const QFileInfo file(pathOption(options.value(QStringLiteral("current_file"))));
        if (!file.filePath().isEmpty()) {
            request.folder = QUrl::fromLocalFile(file.absolutePath());
            request.name = file.fileName();
        }
    }
    if (options.contains(QStringLiteral("current_name"))) {
        request.name = options.value(QStringLiteral("current_name")).toString();
    }
    if (mode == Chooser::SaveFiles) {
        const auto files = fromOption<QByteArrayList>(options.value(QStringLiteral("files")));
        for (QByteArray bytes : files) {
            while (bytes.endsWith('\0')) {
                bytes.chop(1);
            }
            request.files.append(QFileInfo(QFile::decodeName(bytes)).fileName());
        }
    }

    auto chooser = new Chooser(request, this);
    auto portalRequest = new PortalRequest(handle, chooser, chooser);
    Q_UNUSED(portalRequest);

    QQuickWindow *window = Windows::instance()->open(QStringLiteral("FileChooser"),
                                                     {{QStringLiteral("chooser"), QVariant::fromValue(chooser)}},
                                                     parentWindow,
                                                     options.value(QStringLiteral("modal"), true).toBool());
    if (!window) {
        QDBusConnection::sessionBus().send(call.createErrorReply(QDBusError::Failed, QStringLiteral("The Wild OS file dialog could not open")));
        chooser->deleteLater();
        return;
    }
    // The window reads the chooser to the end, so the chooser goes with it.
    connect(window, &QObject::destroyed, chooser, &QObject::deleteLater);
    QPointer<QQuickWindow> guard(window);
    connect(chooser, &Chooser::finished, this, [call, guard](uint response, const QVariantMap &results) {
        QDBusConnection::sessionBus().send(call.createReply({response, results}));
        if (guard) {
            guard->hide();
            guard->deleteLater();
        }
    });
}

PortalRequest::PortalRequest(const QDBusObjectPath &handle, Chooser *chooser, QObject *parent)
    : QObject(parent)
    , m_path(handle.path())
    , m_chooser(chooser)
{
    QDBusConnection::sessionBus().registerObject(m_path, this, QDBusConnection::ExportAllSlots);
}

PortalRequest::~PortalRequest()
{
    QDBusConnection::sessionBus().unregisterObject(m_path);
}

void PortalRequest::Close()
{
    if (m_chooser) {
        m_chooser->cancel();
    }
}
