#include "iteminfo.h"

#include "deskops.h"
#include "format.h"

#include <KDesktopFile>
#include <KFileItem>
#include <KIO/DirectorySizeJob>
#include <KIO/PreviewJob>
#include <KIO/SimpleJob>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>

#include <unistd.h>

// Properties for the desktop, from wildos-files/src/iteminfo.cpp with the
// drive names left out: everything on the desktop is in the Home folder.
namespace
{
QString friendlyFolder(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home || path.startsWith(home + QLatin1Char('/'))) {
        return QCoreApplication::translate("ItemInfo", "Home") + path.mid(home.size());
    }
    return path;
}
}

ItemInfo::ItemInfo(QObject *parent)
    : QObject(parent)
{
    connect(DeskOps::instance(), &DeskOps::viewChanged, this, &ItemInfo::reload);
}

ItemInfo::~ItemInfo()
{
    if (m_job) {
        m_job->kill();
    }
}

void ItemInfo::setUrls(const QList<QUrl> &urls)
{
    if (urls == m_urls) {
        return;
    }
    m_urls = urls;
    Q_EMIT urlsChanged();
    reload();
}

void ItemInfo::reload()
{
    if (m_job) {
        m_job->kill();
        m_job = nullptr;
    }
    m_name.clear();
    m_fileName.clear();
    m_icon.clear();
    m_type.clear();
    m_location.clear();
    m_size.clear();
    m_contains.clear();
    m_created.clear();
    m_modified.clear();
    m_accessed.clear();
    m_opensWith.clear();
    m_isDir = false;
    m_preview = false;
    m_readOnly = false;
    m_canChangeReadOnly = false;

    QList<QUrl> folders;
    qint64 bytes = 0;
    int files = 0;

    if (m_urls.size() == 1) {
        const QUrl url = m_urls.constFirst();
        KFileItem item(url);
        const QMimeType mime = item.determineMimeType();
        m_isDir = item.isDir();
        m_fileName = item.text();
        m_name = Format::visibleName(m_fileName, m_isDir, DeskOps::instance()->showExtensions());
        if (url.isLocalFile() && mime.name() == QLatin1String("application/x-desktop")) {
            const QString shortcut = KDesktopFile(url.toLocalFile()).readName();
            if (!shortcut.isEmpty()) {
                m_name = shortcut;
            }
        }
        m_icon = item.iconName() + QLatin1Char(',') + (m_isDir ? QStringLiteral("folder") : mime.genericIconName());
        m_type = Format::typeName(mime, m_isDir);
        if (!m_isDir) {
            m_type += QStringLiteral(" (.") + (QMimeDatabase().suffixForFileName(m_fileName).isEmpty() ? QFileInfo(m_fileName).suffix() : QMimeDatabase().suffixForFileName(m_fileName)) + QLatin1Char(')');
            if (m_type.endsWith(QStringLiteral(" (.)"))) {
                m_type.chop(4);
            }
            m_opensWith = DeskOps::instance()->defaultAppName(url);
        }
        if (url.isLocalFile()) {
            const QFileInfo info(url.toLocalFile());
            m_location = friendlyFolder(info.absolutePath());
            m_created = Format::longDate(info.birthTime());
            m_modified = Format::longDate(info.lastModified());
            m_accessed = Format::longDate(info.lastRead());
            m_stamp = info.lastModified().toSecsSinceEpoch();
            m_readOnly = !info.isWritable();
            m_canChangeReadOnly = !m_isDir && info.ownerId() == ::getuid();
        } else {
            m_modified = Format::longDate(item.time(KFileItem::ModificationTime));
        }
        static const QStringList previewTypes = KIO::PreviewJob::supportedMimeTypes();
        m_preview = !m_isDir && url.isLocalFile() && std::any_of(previewTypes.cbegin(), previewTypes.cend(), [&mime](const QString &t) {
            return t == mime.name() || (t.endsWith(QStringLiteral("/*")) && mime.name().startsWith(t.left(t.size() - 1)));
        });
        if (m_isDir) {
            folders.append(url);
        } else {
            bytes = qint64(item.size());
            files = 1;
        }
    } else if (!m_urls.isEmpty()) {
        m_name = Format::count(int(m_urls.size()), QCoreApplication::translate("ItemInfo", "1 item"), QCoreApplication::translate("ItemInfo", "%Ln items", nullptr, int(m_urls.size())));
        m_icon = QStringLiteral("folder,folder");
        bool allDirs = true;
        for (const QUrl &url : std::as_const(m_urls)) {
            const KFileItem item(url);
            if (item.isDir()) {
                folders.append(url);
            } else {
                allDirs = false;
                bytes += qint64(item.size());
                ++files;
            }
        }
        m_type = allDirs ? QCoreApplication::translate("ItemInfo", "Folders") : folders.isEmpty() ? QCoreApplication::translate("ItemInfo", "Files") : QCoreApplication::translate("ItemInfo", "Files and folders");
        m_location = m_urls.constFirst().isLocalFile() ? friendlyFolder(QFileInfo(m_urls.constFirst().toLocalFile()).absolutePath()) : QString();
    }
    Q_EMIT changed();
    count(folders, bytes, files);
}

void ItemInfo::count(const QList<QUrl> &folders, qint64 filesBytes, int files)
{
    if (folders.isEmpty()) {
        m_size = m_urls.isEmpty() ? QString() : Format::exactSize(filesBytes);
        Q_EMIT sizeChanged();
        return;
    }
    m_size = QCoreApplication::translate("ItemInfo", "Working it out");
    KFileItemList items;
    for (const QUrl &url : folders) {
        items.append(KFileItem(url));
    }
    auto job = KIO::directorySize(items);
    m_job = job;
    connect(job, &KJob::result, this, [this, job, filesBytes, files, folders] {
        if (m_job != job) {
            return;
        }
        m_job = nullptr;
        if (job->error()) {
            m_size = QCoreApplication::translate("ItemInfo", "Unknown");
        } else {
            const qint64 total = qint64(job->totalSize()) + filesBytes;
            m_size = Format::exactSize(total);
            const qint64 fileCount = qint64(job->totalFiles()) + files;
            // DirectorySizeJob counts what is inside the folders it was given;
            // for a selection the selected folders count too, as on Windows.
            const qint64 folderCount = qint64(job->totalSubdirs()) + (m_urls.size() > 1 ? folders.size() : 0);
            m_contains = Format::count(int(fileCount), QCoreApplication::translate("ItemInfo", "1 file"), QCoreApplication::translate("ItemInfo", "%Ln files", nullptr, int(fileCount)))
                + QStringLiteral(", ")
                + Format::count(int(folderCount), QCoreApplication::translate("ItemInfo", "1 folder"), QCoreApplication::translate("ItemInfo", "%Ln folders", nullptr, int(folderCount)));
        }
        Q_EMIT sizeChanged();
    });
    Q_EMIT sizeChanged();
}

void ItemInfo::setReadOnly(bool on)
{
    if (!m_canChangeReadOnly || m_urls.size() != 1 || on == m_readOnly) {
        return;
    }
    const QUrl url = m_urls.constFirst();
    QFile::Permissions perms = QFileInfo(url.toLocalFile()).permissions();
    if (on) {
        perms &= ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup | QFileDevice::WriteOther);
    } else {
        perms |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
    }
    if (QFile::setPermissions(url.toLocalFile(), perms)) {
        m_readOnly = on;
        Q_EMIT changed();
    }
}
