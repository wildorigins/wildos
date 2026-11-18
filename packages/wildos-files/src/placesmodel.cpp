#include "placesmodel.h"

#include "drives.h"
#include "fileops.h"
#include "foldermodel.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace
{
// Network folders Files opens through KIO: a NAS or another PC's share
// (smb), and servers over SSH, WebDAV or FTP.
bool isNetwork(const QUrl &url)
{
    static const QStringList schemes{QStringLiteral("smb"), QStringLiteral("sftp"), QStringLiteral("fish"), QStringLiteral("webdav"),
                                     QStringLiteral("webdavs"), QStringLiteral("ftp"), QStringLiteral("nfs")};
    return url.isValid() && !url.host().isEmpty() && schemes.contains(url.scheme());
}

// How a pinned folder is stored: a clean local path, or a network address
// without a password, which KIO keeps in the wallet instead.
QString pinAddress(const QUrl &url)
{
    if (url.isLocalFile()) {
        return QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile())).toString();
    }
    return url.adjusted(QUrl::RemovePassword | QUrl::StripTrailingSlash | QUrl::NormalizePathSegments).toString();
}
}

PlacesModel *PlacesModel::create(QQmlEngine *, QJSEngine *)
{
    static PlacesModel *model = new PlacesModel;
    QJSEngine::setObjectOwnership(model, QJSEngine::CppOwnership);
    return model;
}

PlacesModel::PlacesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
    connect(Drives::instance(), &Drives::changed, this, &PlacesModel::reload);
    connect(FileOps::instance(), &FileOps::binCountChanged, this, [this] {
        for (int i = 0; i < m_places.size(); ++i) {
            if (m_places.at(i).kind == QStringLiteral("bin")) {
                Q_EMIT dataChanged(index(i), index(i), {IconRole});
            }
        }
    });
}

void PlacesModel::reload()
{
    QList<Place> places;
    auto folder = [&places](const QString &name, const QString &icon, QStandardPaths::StandardLocation location) {
        const QString path = QStandardPaths::writableLocation(location);
        // An XDG folder that points at Home itself (common for Desktop on
        // minimal setups) would be a second Home; leave it out.
        if (path.isEmpty() || (location != QStandardPaths::HomeLocation && QDir::cleanPath(path) == QDir::homePath())) {
            return;
        }
        places.append({name, icon, QUrl::fromLocalFile(path), QStringLiteral("place"), QString(), false});
    };
    folder(QCoreApplication::translate("PlacesModel", "Home"), QStringLiteral("home"), QStandardPaths::HomeLocation);
    folder(QCoreApplication::translate("PlacesModel", "Desktop"), QStringLiteral("desktop"), QStandardPaths::DesktopLocation);
    folder(QCoreApplication::translate("PlacesModel", "Documents"), QStringLiteral("document"), QStandardPaths::DocumentsLocation);
    folder(QCoreApplication::translate("PlacesModel", "Downloads"), QStringLiteral("download"), QStandardPaths::DownloadLocation);
    folder(QCoreApplication::translate("PlacesModel", "Pictures"), QStringLiteral("pictures"), QStandardPaths::PicturesLocation);
    folder(QCoreApplication::translate("PlacesModel", "Music"), QStringLiteral("music"), QStandardPaths::MusicLocation);
    folder(QCoreApplication::translate("PlacesModel", "Videos"), QStringLiteral("videos"), QStandardPaths::MoviesLocation);
    const QStringList pinned = Settings::instance()->pinnedPlaces();
    for (const QString &address : pinned) {
        const QUrl url(address);
        if (isNetwork(url)) {
            // A share is named after its folder, as Windows names a pinned
            // network folder; the NAS itself by its own name.
            const QString folder = url.adjusted(QUrl::StripTrailingSlash).fileName();
            places.append({folder.isEmpty() ? url.host() : folder, QStringLiteral("server"), url, QStringLiteral("pinned"), QString(), false});
            continue;
        }
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = QDir::cleanPath(url.toLocalFile());
        const QString name = QFileInfo(path).fileName();
        places.append({name.isEmpty() ? path : name, QStringLiteral("folder"), QUrl::fromLocalFile(path), QStringLiteral("pinned"), QString(), false});
    }
    places.append({QString(), QString(), QUrl(), QStringLiteral("separator"), QString(), false});
    places.append({QCoreApplication::translate("PlacesModel", "This computer"), QStringLiteral("computer"), FolderModel::computerUrl(), QStringLiteral("computer"), QString(), false});
    const auto drives = Drives::instance()->list();
    for (const Drive &drive : drives) {
        if (drive.system) {
            continue;
        }
        places.append({drive.name, drive.lineIcon, drive.mounted ? QUrl::fromLocalFile(drive.mountPath) : QUrl(), QStringLiteral("drive"), drive.udi, drive.removable});
    }
    places.append({QCoreApplication::translate("PlacesModel", "Recycle Bin"), QStringLiteral("bin"), FolderModel::binUrl(), QStringLiteral("bin"), QString(), false});

    beginResetModel();
    m_places = places;
    endResetModel();
    ++m_revision;
    Q_EMIT revisionChanged();
}

int PlacesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_places.size());
}

QHash<int, QByteArray> PlacesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconRole, "iconName"},
        {UrlRole, "url"},
        {KindRole, "kind"},
        {UdiRole, "udi"},
        {RemovableRole, "removable"},
    };
}

QVariant PlacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_places.size()) {
        return {};
    }
    const Place &p = m_places.at(index.row());
    switch (role) {
    case NameRole:
        return p.name;
    case IconRole:
        if (p.kind == QStringLiteral("bin")) {
            return FileOps::instance()->binCount() > 0 ? QStringLiteral("bin-full") : QStringLiteral("bin");
        }
        return p.icon;
    case UrlRole:
        return p.url;
    case KindRole:
        return p.kind;
    case UdiRole:
        return p.udi;
    case RemovableRole:
        return p.removable;
    }
    return {};
}

int PlacesModel::rowFor(const QUrl &url) const
{
    const QUrl clean = url.adjusted(QUrl::StripTrailingSlash);
    for (int i = 0; i < m_places.size(); ++i) {
        const QUrl place = m_places.at(i).url.adjusted(QUrl::StripTrailingSlash);
        if (!place.isEmpty() && place == clean) {
            return i;
        }
    }
    if (url.scheme() == QStringLiteral("trash")) {
        for (int i = 0; i < m_places.size(); ++i) {
            if (m_places.at(i).kind == QStringLiteral("bin")) {
                return i;
            }
        }
    }
    return -1;
}

QVariantMap PlacesModel::get(int row) const
{
    if (row < 0 || row >= m_places.size()) {
        return {};
    }
    const Place &p = m_places.at(row);
    return {
        {QStringLiteral("name"), p.name},
        {QStringLiteral("url"), p.url},
        {QStringLiteral("kind"), p.kind},
        {QStringLiteral("udi"), p.udi},
        {QStringLiteral("removable"), p.removable},
    };
}

bool PlacesModel::canPin(const QUrl &url) const
{
    if (rowFor(url) >= 0) {
        return false;
    }
    return isNetwork(url) || (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir());
}

bool PlacesModel::isPinned(const QUrl &url) const
{
    const int row = rowFor(url);
    return row >= 0 && m_places.at(row).kind == QStringLiteral("pinned");
}

void PlacesModel::pin(const QUrl &url)
{
    if (!canPin(url)) {
        return;
    }
    QStringList pinned = Settings::instance()->pinnedPlaces();
    pinned.append(pinAddress(url));
    Settings::instance()->setPinnedPlaces(pinned);
    reload();
}

void PlacesModel::unpin(const QUrl &url)
{
    if (!isPinned(url)) {
        return;
    }
    QStringList pinned = Settings::instance()->pinnedPlaces();
    pinned.removeAll(pinAddress(m_places.at(rowFor(url)).url));
    Settings::instance()->setPinnedPlaces(pinned);
    reload();
}

void PlacesModel::mount(const QString &udi)
{
    Drives::instance()->mount(udi);
}
