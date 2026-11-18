#include "foldermodel.h"

#include "drives.h"
#include "fileops.h"
#include "format.h"
#include "settings.h"

#include <KCoreDirLister>
#include <KDesktopFile>
#include <KIO/ListJob>
#include <KIO/PreviewJob>
#include <KIO/UDSEntry>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
enum Kind { FolderKind, BinKind, ComputerKind };

constexpr int searchLimit = 5000;

QString homePath()
{
    return QDir::homePath();
}

bool isBin(const QUrl &url)
{
    return url.scheme() == QStringLiteral("trash");
}

bool isComputer(const QUrl &url)
{
    return url == FolderModel::computerUrl();
}

bool isUnder(const QString &path, const QString &root)
{
    return path == root || path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
}

// Shows a folder path the way a person thinks of it: inside Home it
// starts at Home, anywhere else at the drive it is on. Never a bare "/".
QString friendlyPath(const QString &path)
{
    const QString home = homePath();
    if (isUnder(path, home)) {
        const QString rest = path.mid(home.size());
        return rest.isEmpty() ? QCoreApplication::translate("FolderModel", "Home") : QCoreApplication::translate("FolderModel", "Home") + rest;
    }
    const Drive drive = Drives::instance()->driveFor(path);
    const QString root = drive.mountPath == QStringLiteral("/") ? QString() : drive.mountPath;
    return drive.name + path.mid(root.size());
}

bool previewable(const QMimeType &mime)
{
    static const QStringList supported = KIO::PreviewJob::supportedMimeTypes();
    static QSet<QString> exact;
    static QStringList wildcards;
    if (exact.isEmpty()) {
        for (const QString &type : supported) {
            if (type.endsWith(QStringLiteral("/*"))) {
                wildcards.append(type.left(type.size() - 1));
            } else {
                exact.insert(type);
            }
        }
    }
    if (exact.contains(mime.name())) {
        return true;
    }
    for (const QString &prefix : std::as_const(wildcards)) {
        if (mime.name().startsWith(prefix)) {
            return true;
        }
    }
    const auto parents = mime.allAncestors();
    for (const QString &parent : parents) {
        if (exact.contains(parent)) {
            return true;
        }
    }
    return false;
}
}

QUrl FolderModel::computerUrl()
{
    return QUrl(QStringLiteral("wildos:computer"));
}

QUrl FolderModel::binUrl()
{
    return QUrl(QStringLiteral("trash:/"));
}

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_lister(new KCoreDirLister(this))
{
    m_collator.setLocale(QLocale(QLocale::English, QLocale::UnitedKingdom));
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);

    m_lister->setAutoErrorHandlingEnabled(false);
    m_lister->setShowHiddenFiles(Settings::instance()->showHidden());
    connect(m_lister, &KCoreDirLister::started, this, [this] {
        setLoading(true);
    });
    connect(m_lister, &KCoreDirLister::completed, this, &FolderModel::onCompleted);
    connect(m_lister, &KCoreDirLister::canceled, this, [this] {
        setLoading(false);
    });
    connect(m_lister, &KCoreDirLister::clear, this, [this] {
        m_base.clear();
        rebuild();
    });
    connect(m_lister, &KCoreDirLister::newItems, this, &FolderModel::onNewItems);
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, &FolderModel::onItemsDeleted);
    connect(m_lister, &KCoreDirLister::refreshItems, this, &FolderModel::onRefreshItems);
    connect(m_lister, &KCoreDirLister::jobError, this, &FolderModel::onJobError);
    connect(m_lister, &KCoreDirLister::redirection, this, [this](const QUrl &, const QUrl &to) {
        m_url = to;
        Q_EMIT urlChanged();
        Q_EMIT viewModeChanged();
    });

    auto settings = Settings::instance();
    connect(settings, &Settings::showHiddenChanged, this, [this] {
        m_lister->setShowHiddenFiles(Settings::instance()->showHidden());
        m_lister->emitChanges();
    });
    connect(settings, &Settings::showExtensionsChanged, this, [this] {
        for (Entry &entry : m_base) {
            applyName(entry);
        }
        for (Entry &entry : m_deep) {
            applyName(entry);
        }
        rebuild();
    });
    connect(settings, &Settings::viewsChanged, this, &FolderModel::viewModeChanged);
    connect(FileOps::instance(), &FileOps::clipboardChanged, this, [this] {
        if (!m_rows.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {FadedRole});
        }
    });
    connect(Drives::instance(), &Drives::changed, this, [this] {
        if (isComputer(m_url)) {
            rebuild();
        }
    });
    connect(Drives::instance(), &Drives::mounted, this, [this](const QString &udi, const QString &path) {
        if (udi == m_pendingUdi) {
            m_pendingUdi.clear();
            openUrl(QUrl::fromLocalFile(path));
        }
    });

    // QML reads the selection through functions; this counter is what
    // tells its bindings to read again.
    connect(this, &FolderModel::selectionChanged, this, [this] {
        ++m_selectionRevision;
    });

    m_searchDelay.setSingleShot(true);
    m_searchDelay.setInterval(250);
    connect(&m_searchDelay, &QTimer::timeout, this, &FolderModel::startSearch);
}

FolderModel::~FolderModel()
{
    stopSearch();
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {FileNameRole, "fileName"},
        {IconRole, "iconName"},
        {UrlRole, "url"},
        {IsDirRole, "isDir"},
        {IsDriveRole, "isDrive"},
        {Col2Role, "col2"},
        {Col3Role, "col3"},
        {SizeRole, "sizeText"},
        {SelectedRole, "selected"},
        {PreviewRole, "preview"},
        {ModifiedRole, "modified"},
        {FadedRole, "faded"},
        {SubtitleRole, "subtitle"},
        {UsageRole, "usage"},
        {MimeRole, "mimeType"},
        {ChipRole, "chip"},
        {RemovableRole, "removable"},
    };
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Entry &e = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case FileNameRole:
        return e.fileName;
    case IconRole:
        return e.icon;
    case UrlRole:
        return e.url;
    case IsDirRole:
        return e.isDir;
    case IsDriveRole:
        return e.isDrive;
    case Col2Role:
        return e.col2;
    case Col3Role:
        return e.col3;
    case SizeRole:
        return e.sizeText;
    case SelectedRole:
        return e.selected;
    case PreviewRole:
        return e.preview;
    case ModifiedRole:
        return e.modified;
    case FadedRole:
        return e.hidden || FileOps::instance()->isCut(e.url);
    case SubtitleRole:
        return e.subtitle;
    case UsageRole:
        return e.usage;
    case MimeRole:
        return e.mime;
    case ChipRole:
        return e.chip;
    case RemovableRole:
        return e.removable;
    }
    return {};
}

QString FolderModel::kind() const
{
    if (isComputer(m_url)) {
        return QStringLiteral("computer");
    }
    if (isBin(m_url)) {
        return QStringLiteral("bin");
    }
    return QStringLiteral("folder");
}

QString FolderModel::title() const
{
    if (isComputer(m_url)) {
        return QCoreApplication::translate("FolderModel", "This computer");
    }
    if (isBin(m_url)) {
        const QString path = m_url.path();
        if (path.isEmpty() || path == QStringLiteral("/")) {
            return QCoreApplication::translate("FolderModel", "Recycle Bin");
        }
        QString name = m_url.fileName();
        const auto dash = name.indexOf(QLatin1Char('-'));
        if (m_url.path().count(QLatin1Char('/')) == 1 && dash > 0) {
            name = name.mid(dash + 1);
        }
        return name;
    }
    if (m_url.isLocalFile()) {
        const QString path = m_url.toLocalFile();
        const QString clean = QDir::cleanPath(path);
        if (clean == homePath()) {
            return QCoreApplication::translate("FolderModel", "Home");
        }
        const auto drives = Drives::instance()->list();
        for (const Drive &drive : drives) {
            if (drive.mounted && QDir::cleanPath(drive.mountPath) == clean) {
                return drive.name;
            }
        }
        return QFileInfo(clean).fileName();
    }
    const QString name = m_url.fileName();
    return name.isEmpty() ? m_url.host() : name;
}

QString FolderModel::iconName() const
{
    if (isComputer(m_url)) {
        return QStringLiteral("computer");
    }
    if (isBin(m_url) && (m_url.path().isEmpty() || m_url.path() == QStringLiteral("/"))) {
        return m_rows.isEmpty() ? QStringLiteral("user-trash") : QStringLiteral("user-trash-full");
    }
    if (m_url.isLocalFile()) {
        const QString clean = QDir::cleanPath(m_url.toLocalFile());
        const auto drives = Drives::instance()->list();
        for (const Drive &drive : drives) {
            if (drive.mounted && QDir::cleanPath(drive.mountPath) == clean) {
                return drive.themeIcon;
            }
        }
    }
    return QStringLiteral("folder");
}

QString FolderModel::editText() const
{
    if (isComputer(m_url)) {
        return QCoreApplication::translate("FolderModel", "This computer");
    }
    if (isBin(m_url)) {
        return QCoreApplication::translate("FolderModel", "Recycle Bin");
    }
    if (m_url.isLocalFile()) {
        // A real path is typed and pasted here, so it stays a real path,
        // with the home folder written the short way.
        const QString path = QDir::cleanPath(m_url.toLocalFile());
        if (isUnder(path, homePath())) {
            return QStringLiteral("~") + path.mid(homePath().size());
        }
        // The system disk is "Wild OS" everywhere, even here.
        return path == QStringLiteral("/") ? QStringLiteral("Wild OS") : path;
    }
    return m_url.toDisplayString();
}

QVariantList FolderModel::crumbs() const
{
    QVariantList list;
    auto add = [&list](const QString &name, const QUrl &url) {
        list.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("url"), url}});
    };
    if (isComputer(m_url)) {
        add(QCoreApplication::translate("FolderModel", "This computer"), computerUrl());
        return list;
    }
    if (isBin(m_url)) {
        add(QCoreApplication::translate("FolderModel", "Recycle Bin"), binUrl());
        const QStringList parts = m_url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QUrl url = binUrl();
        for (int i = 0; i < parts.size(); ++i) {
            QString name = parts.at(i);
            const auto dash = name.indexOf(QLatin1Char('-'));
            if (i == 0 && dash > 0) {
                name = name.mid(dash + 1);
            }
            url.setPath(url.path() + (url.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + parts.at(i));
            add(name, url);
        }
        return list;
    }
    if (m_url.isLocalFile()) {
        const QString path = QDir::cleanPath(m_url.toLocalFile());
        QString base;
        if (isUnder(path, homePath())) {
            base = homePath();
            add(QCoreApplication::translate("FolderModel", "Home"), QUrl::fromLocalFile(base));
        } else {
            const Drive drive = Drives::instance()->driveFor(path);
            base = drive.mountPath;
            add(QCoreApplication::translate("FolderModel", "This computer"), computerUrl());
            add(drive.name, QUrl::fromLocalFile(base));
        }
        const QStringList parts = path.mid(base.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString current = base == QStringLiteral("/") ? QString() : base;
        for (const QString &part : parts) {
            current += QLatin1Char('/') + part;
            add(part, QUrl::fromLocalFile(current));
        }
        return list;
    }
    QUrl url = m_url;
    url.setPath(QStringLiteral("/"));
    add(m_url.host().isEmpty() ? m_url.scheme() : m_url.host(), url);
    const QStringList parts = m_url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString current;
    for (const QString &part : parts) {
        current += QLatin1Char('/') + part;
        url.setPath(current);
        add(part, url);
    }
    return list;
}

bool FolderModel::writable() const
{
    if (m_url.isLocalFile()) {
        return QFileInfo(m_url.toLocalFile()).isWritable();
    }
    if (isBin(m_url) || isComputer(m_url)) {
        return false;
    }
    const KFileItem root = m_lister->rootItem();
    return root.isNull() || root.isWritable();
}

QString FolderModel::scope() const
{
    if (isComputer(m_url)) {
        return QString();
    }
    if (isBin(m_url)) {
        return QCoreApplication::translate("FolderModel", "Your Home folder only");
    }
    if (m_url.isLocalFile()) {
        const QString path = QDir::cleanPath(m_url.toLocalFile());
        if (isUnder(path, homePath())) {
            return QCoreApplication::translate("FolderModel", "Your Home folder only");
        }
        const Drive drive = Drives::instance()->driveFor(path);
        if (drive.removable) {
            return QCoreApplication::translate("FolderModel", "Removable drive");
        }
        if (!drive.system) {
            return drive.name;
        }
        return writable() ? QCoreApplication::translate("FolderModel", "Shared on this computer") : QCoreApplication::translate("FolderModel", "System files");
    }
    return QCoreApplication::translate("FolderModel", "Network");
}

QString FolderModel::scopeDetail() const
{
    if (isComputer(m_url)) {
        return QString();
    }
    if (isBin(m_url) || (m_url.isLocalFile() && isUnder(QDir::cleanPath(m_url.toLocalFile()), homePath()))) {
        const QFileInfo home(homePath());
        const bool others = home.permissions() & (QFileDevice::ReadOther | QFileDevice::ExeOther);
        return others ? QCoreApplication::translate("FolderModel", "You are in your Home folder. Other accounts on this computer can open it too, because its sharing settings allow that.")
                      : QCoreApplication::translate("FolderModel", "You are in your Home folder. Only your account can open the files here; other people who use this computer cannot.");
    }
    const QString s = scope();
    if (s == QCoreApplication::translate("FolderModel", "Removable drive")) {
        return QCoreApplication::translate("FolderModel", "This folder is on a drive you can unplug. Anyone who plugs it into another computer can open these files.");
    }
    if (s == QCoreApplication::translate("FolderModel", "System files")) {
        return QCoreApplication::translate("FolderModel", "These folders belong to Wild OS. You can look around, but only an administrator can change them.");
    }
    if (s == QCoreApplication::translate("FolderModel", "Network")) {
        return QCoreApplication::translate("FolderModel", "This folder is on another computer. Whoever runs that computer decides who can open it.");
    }
    return QCoreApplication::translate("FolderModel", "This folder is outside your Home folder, so other accounts on this computer may be able to open it.");
}

QStringList FolderModel::columnTitles() const
{
    if (isComputer(m_url)) {
        return {QCoreApplication::translate("FolderModel", "Name"), QCoreApplication::translate("FolderModel", "Type"), QCoreApplication::translate("FolderModel", "Free space"), QCoreApplication::translate("FolderModel", "Total size")};
    }
    if (isBin(m_url)) {
        return {QCoreApplication::translate("FolderModel", "Name"), QCoreApplication::translate("FolderModel", "Date deleted"), QCoreApplication::translate("FolderModel", "Original location"), QCoreApplication::translate("FolderModel", "Size")};
    }
    if (!m_searchText.isEmpty()) {
        return {QCoreApplication::translate("FolderModel", "Name"), QCoreApplication::translate("FolderModel", "Date modified"), QCoreApplication::translate("FolderModel", "Folder"), QCoreApplication::translate("FolderModel", "Size")};
    }
    return {QCoreApplication::translate("FolderModel", "Name"), QCoreApplication::translate("FolderModel", "Date modified"), QCoreApplication::translate("FolderModel", "Type"), QCoreApplication::translate("FolderModel", "Size")};
}

bool FolderModel::canGoUp() const
{
    if (isComputer(m_url)) {
        return false;
    }
    if (isBin(m_url)) {
        return !(m_url.path().isEmpty() || m_url.path() == QStringLiteral("/"));
    }
    if (m_url.isLocalFile()) {
        // From the top of a drive, up goes to This computer, as on Windows.
        return true;
    }
    return m_url.path().size() > 1;
}

QVariantList FolderModel::recent() const
{
    QVariantList list;
    QList<QUrl> seen{m_url};
    for (auto it = m_back.crbegin(); it != m_back.crend() && list.size() < 10; ++it) {
        if (seen.contains(*it)) {
            continue;
        }
        seen.append(*it);
        list.append(QVariantMap{{QStringLiteral("url"), *it},
                                {QStringLiteral("name"), it->isLocalFile() ? friendlyPath(QDir::cleanPath(it->toLocalFile()))
                                                         : isComputer(*it) ? QCoreApplication::translate("FolderModel", "This computer")
                                                         : isBin(*it) ? QCoreApplication::translate("FolderModel", "Recycle Bin")
                                                                      : it->toDisplayString()}});
    }
    return list;
}

QString FolderModel::viewMode() const
{
    return Settings::instance()->viewFor(m_url);
}

void FolderModel::setViewMode(const QString &mode)
{
    if (mode != viewMode()) {
        Settings::instance()->setViewFor(m_url, mode);
    }
}

void FolderModel::openUrl(const QUrl &address)
{
    // "Show in folder" for a folder (ShowItems, --select) marks the address
    // so the folder is shown selected in its parent rather than opened.
    QUrl url = address;
    const bool select = url.fragment() == QLatin1String("wildos-select");
    if (select) {
        url.setFragment(QString());
    }
    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        // Asked to open a file, Files opens its folder with it selected,
        // the way "Show in folder" works everywhere else.
        if (info.exists() && (!info.isDir() || (select && info.absoluteFilePath() != QStringLiteral("/")))) {
            m_pending = {QUrl::fromLocalFile(info.absoluteFilePath())};
            navigate(QUrl::fromLocalFile(info.absolutePath()), true);
            return;
        }
    }
    navigate(url, true);
}

bool FolderModel::openText(const QString &input)
{
    QString text = input.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    const QString lower = text.toLower();
    // The names this window shows, in the language it shows them in, are
    // typed back too.
    if (lower == QStringLiteral("this computer") || lower == QStringLiteral("computer") || lower == tr("This computer").toLower()) {
        openUrl(computerUrl());
        return true;
    }
    if (lower == QStringLiteral("bin") || lower == QStringLiteral("recycle bin") || lower == tr("Recycle Bin").toLower()) {
        openUrl(binUrl());
        return true;
    }
    if (lower == QStringLiteral("wild os") || lower == QStringLiteral("local disk")) {
        openUrl(QUrl::fromLocalFile(QStringLiteral("/")));
        return true;
    }
    if (lower == QStringLiteral("home") || lower == tr("Home").toLower()) {
        openUrl(QUrl::fromLocalFile(homePath()));
        return true;
    }
    if (text.startsWith(QLatin1Char('~'))) {
        text = homePath() + text.mid(1);
    }
    // A Windows network path, \\nas\share\folder, is the same place as
    // smb://nas/share/folder.
    if (text.startsWith(QStringLiteral("\\\\")) && text.size() > 2) {
        QUrl smb;
        smb.setScheme(QStringLiteral("smb"));
        const QString rest = text.mid(2).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const int slash = rest.indexOf(QLatin1Char('/'));
        smb.setHost(slash < 0 ? rest : rest.left(slash));
        if (slash >= 0) {
            smb.setPath(rest.mid(slash));
        }
        if (!smb.isValid() || smb.host().isEmpty()) {
            return false;
        }
        openUrl(smb);
        return true;
    }
    QUrl url;
    if (QDir::isAbsolutePath(text)) {
        url = QUrl::fromLocalFile(QDir::cleanPath(text));
    } else if (text.contains(QStringLiteral(":/"))) {
        url = QUrl::fromUserInput(text);
    } else if (m_url.isLocalFile()) {
        url = QUrl::fromLocalFile(QDir::cleanPath(QDir(m_url.toLocalFile()).filePath(text)));
    } else {
        return false;
    }
    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        if (!info.exists()) {
            return false;
        }
        if (!info.isDir()) {
            FileOps::instance()->open({url});
            return true;
        }
    }
    openUrl(url);
    return true;
}

void FolderModel::navigate(const QUrl &target, bool record)
{
    QUrl url = target.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments);
    if (url.isEmpty()) {
        return;
    }
    if (isBin(target)) {
        url = target;
        if (url.path().isEmpty()) {
            url.setPath(QStringLiteral("/"));
        }
    }
    if (url.isLocalFile() && url.toLocalFile().isEmpty()) {
        url = QUrl::fromLocalFile(QStringLiteral("/"));
    }
    if (url == m_url && m_listed) {
        refresh();
        return;
    }
    if (record && !m_url.isEmpty()) {
        m_back.append(m_url);
        m_forward.clear();
    }

    stopSearch();
    m_searchText.clear();
    m_deep.clear();
    m_base.clear();
    m_url = url;
    m_current = -1;
    m_anchor = -1;
    m_listed = true;
    const int sortedBy = m_sortColumn;
    const bool wasDescending = m_descending;
    Settings::instance()->sortFor(m_url, &m_sortColumn, &m_descending);
    setError(QString());
    rebuild();

    if (m_sortColumn != sortedBy || m_descending != wasDescending) {
        Q_EMIT sortChanged();
    }
    Q_EMIT searchTextChanged();
    Q_EMIT urlChanged();
    Q_EMIT columnsChanged();
    Q_EMIT historyChanged();
    Q_EMIT viewModeChanged();

    if (isComputer(url)) {
        Drives::instance()->refresh();
        m_lister->stop();
        setLoading(false);
        rebuild();
        return;
    }
    m_lister->openUrl(url, KCoreDirLister::NoFlags);
}

void FolderModel::back()
{
    if (m_back.isEmpty()) {
        return;
    }
    m_forward.prepend(m_url);
    const QUrl previous = m_back.takeLast();
    const QUrl from = m_url;
    navigate(previous, false);
    // Coming back out of a folder leaves it selected, so the way back in
    // is under the cursor.
    m_pending = {from};
}

void FolderModel::forward()
{
    if (m_forward.isEmpty()) {
        return;
    }
    m_back.append(m_url);
    navigate(m_forward.takeFirst(), false);
    Q_EMIT historyChanged();
}

void FolderModel::up()
{
    if (!canGoUp()) {
        return;
    }
    const QUrl from = m_url;
    if (m_url.isLocalFile()) {
        const QString path = QDir::cleanPath(m_url.toLocalFile());
        const auto drives = Drives::instance()->list();
        for (const Drive &drive : drives) {
            if (drive.mounted && QDir::cleanPath(drive.mountPath) == path) {
                openUrl(computerUrl());
                return;
            }
        }
    }
    openUrl(KIO::upUrl(m_url));
    m_pending = {from};
}

void FolderModel::refresh()
{
    if (isComputer(m_url)) {
        Drives::instance()->refresh();
        rebuild();
        return;
    }
    m_lister->updateDirectory(m_url);
    if (!m_searchText.isEmpty()) {
        startSearch();
    }
}

void FolderModel::goToRecent(int index)
{
    const QVariantList list = recent();
    if (index >= 0 && index < list.size()) {
        openUrl(list.at(index).toMap().value(QStringLiteral("url")).toUrl());
    }
}

void FolderModel::activate(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    const Entry &e = m_rows.at(row);
    if (e.isDrive) {
        if (e.url.isValid() && !e.url.isEmpty()) {
            openUrl(e.url);
        } else {
            openDrive(e.udi);
        }
        return;
    }
    if (e.isDir) {
        openUrl(e.url);
        return;
    }
    if (isBin(m_url)) {
        Q_EMIT failed(QCoreApplication::translate("FolderModel", "Files in the Recycle Bin cannot be opened. Restore it first, then open it from where it was."));
        return;
    }
    // A shortcut to a folder opens that folder right here, as a .lnk to a
    // folder does in File Explorer.
    if (e.url.isLocalFile() && e.url.path().endsWith(QLatin1String(".desktop"))) {
        const KDesktopFile desktop(e.url.toLocalFile());
        if (desktop.hasLinkType()) {
            const QUrl target(desktop.readUrl());
            if (target.isLocalFile() && QFileInfo(target.toLocalFile()).isDir()) {
                openUrl(target);
                return;
            }
        }
    }
    FileOps::instance()->open({e.url});
}

void FolderModel::openDrive(const QString &udi)
{
    m_pendingUdi = udi;
    Drives::instance()->mount(udi);
}

// Files that belong to the system, not to the person: Windows' recycle bin
// and folder settings in the folders shared with Windows mode, the thumbnail
// cache, and KDE's own folder files. Like Windows' protected system files,
// they stay out of sight even with hidden items shown.
static bool isSystemItem(const QString &name)
{
    static const QStringList names = {QStringLiteral("$RECYCLE.BIN"), QStringLiteral("desktop.ini"),
                                      QStringLiteral("Thumbs.db"), QStringLiteral(".directory"), QStringLiteral(".hidden")};
    return names.contains(name, Qt::CaseInsensitive);
}

FolderModel::Entry FolderModel::makeEntry(const KFileItem &item, const QString &relativeDir) const
{
    Entry e;
    e.item = item;
    e.url = item.url();
    e.isDir = item.isDir();
    e.hidden = item.isHidden();
    e.fileName = relativeDir.isNull() ? item.text() : item.url().fileName();
    const QMimeType mime = item.determineMimeType();
    e.mime = mime.name();
    QString icon = item.iconName();
    if (icon.isEmpty()) {
        icon = e.isDir ? QStringLiteral("folder") : mime.iconName();
    }
    e.icon = icon + QLatin1Char(',') + (e.isDir ? QStringLiteral("folder") : mime.genericIconName());
    const QDateTime modified = item.time(KFileItem::ModificationTime);
    e.modified = modified.isValid() ? modified.toSecsSinceEpoch() : 0;
    e.sizeKey = e.isDir ? -1 : qint64(item.size());
    e.sizeText = e.isDir ? QString() : Format::size(e.sizeKey);
    e.preview = !e.isDir && item.isLocalFile() && previewable(mime) && e.sizeKey < 100LL * 1024 * 1024;

    if (isBin(m_url)) {
        const KIO::UDSEntry uds = item.entry();
        const QString original = uds.stringValue(KIO::UDSEntry::UDS_EXTRA);
        const QDateTime deleted = QDateTime::fromString(uds.stringValue(KIO::UDSEntry::UDS_EXTRA + 1), Qt::ISODate);
        e.col2 = Format::date(deleted);
        e.col2Key = deleted.isValid() ? deleted.toSecsSinceEpoch() : 0;
        e.col3 = original.isEmpty() ? QString() : friendlyPath(QFileInfo(original).absolutePath());
    } else {
        e.col2 = Format::date(modified);
        e.col2Key = e.modified;
        if (!relativeDir.isNull()) {
            e.col3 = relativeDir.isEmpty() ? title() : title() + QLatin1Char('/') + relativeDir;
        } else {
            e.col3 = Format::typeName(mime, e.isDir);
        }
    }
    applyName(e);
    return e;
}

void FolderModel::applyName(Entry &e) const
{
    if (e.isDrive) {
        return;
    }
    // Shortcuts show the name of what they open, as they do on Windows.
    if (e.mime == QStringLiteral("application/x-desktop") && e.url.isLocalFile()) {
        const QString name = KDesktopFile(e.url.toLocalFile()).readName();
        if (!name.isEmpty()) {
            e.name = name;
            return;
        }
    }
    e.name = Format::visibleName(e.fileName, e.isDir, Settings::instance()->showExtensions());
}

QList<FolderModel::Entry> FolderModel::driveEntries() const
{
    QList<Entry> list;
    const auto drives = Drives::instance()->list();
    for (const Drive &drive : drives) {
        Entry e;
        e.isDir = true;
        e.isDrive = true;
        e.udi = drive.udi;
        e.name = drive.name;
        e.fileName = drive.name;
        e.icon = drive.themeIcon + QStringLiteral(",drive-harddisk");
        e.url = drive.mounted ? QUrl::fromLocalFile(drive.mountPath) : QUrl();
        e.col2 = drive.system ? QCoreApplication::translate("FolderModel", "Local disk") : drive.removable ? QCoreApplication::translate("FolderModel", "Removable drive") : QCoreApplication::translate("FolderModel", "Drive");
        e.chip = drive.chip;
        e.removable = drive.removable;
        e.col3 = drive.free >= 0 ? Format::size(drive.free) : QString();
        e.col2Key = 0;
        e.sizeKey = drive.total;
        e.sizeText = drive.total > 0 ? Format::size(drive.total) : QString();
        if (drive.blank) {
            e.subtitle = QCoreApplication::translate("FolderModel", "%1, needs formatting before it can be used").arg(Format::size(drive.total));
        } else if (drive.free >= 0 && drive.total > 0) {
            e.subtitle = drive.removable
                ? QCoreApplication::translate("FolderModel", "%1 free of %2, remove safely before unplugging").arg(Format::size(drive.free), Format::size(drive.total))
                : QCoreApplication::translate("FolderModel", "%1 free of %2").arg(Format::size(drive.free), Format::size(drive.total));
            e.usage = 1.0 - double(drive.free) / double(drive.total);
        } else {
            e.subtitle = QCoreApplication::translate("FolderModel", "Not opened yet");
        }
        list.append(e);
    }
    return list;
}

bool FolderModel::lessThan(const Entry &a, const Entry &b) const
{
    if (a.isDrive != b.isDrive) {
        return a.isDrive;
    }
    if (a.isDrive) {
        // The system disk leads, then drives in the order they appeared.
        return false;
    }
    if (a.isDir != b.isDir) {
        return a.isDir;
    }
    int c = 0;
    switch (m_sortColumn) {
    case 1:
        c = a.col2Key < b.col2Key ? -1 : a.col2Key > b.col2Key ? 1 : 0;
        break;
    case 2:
        c = m_collator.compare(a.col3, b.col3);
        break;
    case 3:
        c = a.sizeKey < b.sizeKey ? -1 : a.sizeKey > b.sizeKey ? 1 : 0;
        break;
    default:
        break;
    }
    if (c == 0) {
        c = m_collator.compare(a.name, b.name);
        if (m_sortColumn != 0) {
            return c < 0;
        }
    }
    return m_descending ? c > 0 : c < 0;
}

bool FolderModel::matchesSearch(const Entry &e) const
{
    return m_searchText.isEmpty() || e.name.contains(m_searchText, Qt::CaseInsensitive) || e.fileName.contains(m_searchText, Qt::CaseInsensitive);
}

void FolderModel::rebuild()
{
    QSet<QUrl> selected;
    for (const Entry &e : std::as_const(m_rows)) {
        if (e.selected) {
            selected.insert(e.url);
        }
    }
    const QUrl currentUrl = (m_current >= 0 && m_current < m_rows.size()) ? m_rows.at(m_current).url : QUrl();
    const QUrl anchorUrl = (m_anchor >= 0 && m_anchor < m_rows.size()) ? m_rows.at(m_anchor).url : QUrl();

    QList<Entry> rows;
    if (isComputer(m_url)) {
        rows = driveEntries();
    } else {
        rows.reserve(m_base.size() + m_deep.size());
        const bool searching = !m_searchText.isEmpty();
        const QString here = title();
        for (const Entry &base : std::as_const(m_base)) {
            if (!matchesSearch(base)) {
                continue;
            }
            rows.append(base);
            if (searching) {
                // Search results say which folder they are in.
                rows.last().col3 = here;
            }
        }
        for (const Entry &e : std::as_const(m_deep)) {
            if (matchesSearch(e)) {
                rows.append(e);
            }
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [this](const Entry &a, const Entry &b) {
        return lessThan(a, b);
    });
    for (Entry &e : rows) {
        e.selected = selected.contains(e.url);
    }

    beginResetModel();
    m_rows = rows;
    m_current = -1;
    m_anchor = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).url == currentUrl && !currentUrl.isEmpty()) {
            m_current = i;
        }
        if (m_rows.at(i).url == anchorUrl && !anchorUrl.isEmpty()) {
            m_anchor = i;
        }
    }
    endResetModel();

    for (int i = 0; i < m_rows.size(); ++i) {
        takePending(i);
    }
    Q_EMIT countChanged();
    Q_EMIT currentIndexChanged();
    Q_EMIT selectionChanged();
}

void FolderModel::insertEntry(Entry entry)
{
    auto pos = std::lower_bound(m_rows.begin(), m_rows.end(), entry, [this](const Entry &a, const Entry &b) {
        return lessThan(a, b);
    });
    const int row = int(pos - m_rows.begin());
    beginInsertRows(QModelIndex(), row, row);
    m_rows.insert(row, entry);
    if (m_current >= row) {
        ++m_current;
    }
    if (m_anchor >= row) {
        ++m_anchor;
    }
    endInsertRows();
    takePending(row);
    Q_EMIT countChanged();
    Q_EMIT currentIndexChanged();
}

void FolderModel::removeUrl(const QUrl &url)
{
    const int row = rowForUrl(url);
    if (row < 0) {
        return;
    }
    const bool wasSelected = m_rows.at(row).selected;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    if (m_current == row) {
        // Keep the keyboard where it was, on the item that slid up.
        m_current = m_rows.isEmpty() ? -1 : std::min(row, int(m_rows.size()) - 1);
    } else if (m_current > row) {
        --m_current;
    }
    if (m_anchor == row) {
        m_anchor = m_current;
    } else if (m_anchor > row) {
        --m_anchor;
    }
    endRemoveRows();
    Q_EMIT countChanged();
    Q_EMIT currentIndexChanged();
    if (wasSelected) {
        Q_EMIT selectionChanged();
    }
}

void FolderModel::takePending(int row)
{
    if (m_pending.isEmpty()) {
        return;
    }
    Entry &e = m_rows[row];
    if (!m_pending.contains(e.url)) {
        return;
    }
    m_pending.remove(e.url);
    e.selected = true;
    m_current = row;
    m_anchor = row;
    Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
    Q_EMIT currentIndexChanged();
    Q_EMIT selectionChanged();
    if (e.url == m_pendingRename) {
        m_pendingRename.clear();
        Q_EMIT renameRequested(row);
    }
}

void FolderModel::onNewItems(const KFileItemList &items)
{
    const qsizetype first = m_base.size();
    m_base.reserve(first + items.size());
    for (const KFileItem &item : items) {
        if (isSystemItem(item.name())) {
            continue;
        }
        m_base.append(makeEntry(item));
    }
    if (m_base.size() == first) {
        return;
    }
    // The first listing arrives in batches; one reset per batch is cheaper
    // than inserting a thousand rows one by one.
    if (m_loading || !m_searchText.isEmpty() || items.size() > 20) {
        rebuild();
        return;
    }
    for (qsizetype i = first; i < m_base.size(); ++i) {
        insertEntry(m_base.at(i));
    }
}

void FolderModel::onItemsDeleted(const KFileItemList &items)
{
    for (const KFileItem &item : items) {
        // The open folder itself went away (deleted, or its drive pulled
        // out): step up to the nearest folder that still exists.
        if (item.url().adjusted(QUrl::StripTrailingSlash) == m_url.adjusted(QUrl::StripTrailingSlash) || item.url().isParentOf(m_url)) {
            QUrl target = KIO::upUrl(item.url());
            while (target.isLocalFile() && !QFileInfo::exists(target.toLocalFile()) && target.path() != QStringLiteral("/")) {
                target = KIO::upUrl(target);
            }
            QMetaObject::invokeMethod(this, [this, target] {
                openUrl(target);
            }, Qt::QueuedConnection);
            return;
        }
        for (qsizetype i = 0; i < m_base.size(); ++i) {
            if (m_base.at(i).url == item.url()) {
                m_base.removeAt(i);
                break;
            }
        }
        removeUrl(item.url());
    }
    if (isBin(m_url)) {
        Q_EMIT urlChanged();
    }
}

void FolderModel::onRefreshItems(const QList<QPair<KFileItem, KFileItem>> &items)
{
    for (const auto &pair : items) {
        Entry e = makeEntry(pair.second);
        for (Entry &base : m_base) {
            if (base.url == pair.first.url()) {
                base = e;
                break;
            }
        }
        if (!m_searchText.isEmpty()) {
            continue;
        }
        const int row = rowForUrl(pair.first.url());
        if (row < 0) {
            continue;
        }
        const bool selected = m_rows.at(row).selected;
        const bool current = m_current == row;
        e.selected = selected;
        removeUrl(pair.first.url());
        insertEntry(e);
        if (current) {
            setCurrentIndex(rowForUrl(e.url));
        }
        if (selected) {
            Q_EMIT selectionChanged();
        }
    }
    if (!m_searchText.isEmpty()) {
        rebuild();
    }
}

void FolderModel::onCompleted()
{
    setLoading(false);
    if (isBin(m_url)) {
        Q_EMIT urlChanged();
    }
}

void FolderModel::onJobError(KIO::Job *job)
{
    setLoading(false);
    switch (job->error()) {
    case KIO::ERR_ACCESS_DENIED:
    case KIO::ERR_CANNOT_ENTER_DIRECTORY:
    case KIO::ERR_CANNOT_OPEN_FOR_READING:
        setError(QCoreApplication::translate("FolderModel", "You do not have permission to open this folder."));
        break;
    case KIO::ERR_DOES_NOT_EXIST:
        setError(QCoreApplication::translate("FolderModel", "This folder does not exist any more. It may have been moved or deleted."));
        break;
    case KIO::ERR_IS_FILE:
        setError(QCoreApplication::translate("FolderModel", "This is a file, not a folder."));
        break;
    default:
        setError(QCoreApplication::translate("FolderModel", "Wild OS could not open this folder."), job->errorString());
        break;
    }
}

void FolderModel::setLoading(bool on)
{
    if (on != m_loading) {
        m_loading = on;
        Q_EMIT loadingChanged();
    }
}

void FolderModel::setError(const QString &text, const QString &detail)
{
    if (text != m_error || detail != m_errorDetail) {
        m_error = text;
        m_errorDetail = detail;
        Q_EMIT errorChanged();
    }
}

void FolderModel::setSearchText(const QString &text)
{
    if (text == m_searchText) {
        return;
    }
    const bool wasEmpty = m_searchText.isEmpty();
    m_searchText = text;
    stopSearch();
    m_deep.clear();
    rebuild();
    Q_EMIT searchTextChanged();
    if (wasEmpty != text.isEmpty()) {
        Q_EMIT columnsChanged();
    }
    if (!text.isEmpty() && !isComputer(m_url) && !isBin(m_url)) {
        m_searchDelay.start();
    }
}

void FolderModel::startSearch()
{
    stopSearch();
    m_deep.clear();
    if (m_searchText.isEmpty()) {
        return;
    }
    // Windows searches the folders inside the current one too. KIO lists
    // the whole tree in the background; matches stream in as found.
    m_searchJob = KIO::listRecursive(m_url, KIO::HideProgressInfo, Settings::instance()->showHidden() ? KIO::ListJob::ListFlags(KIO::ListJob::ListFlag::IncludeHidden) : KIO::ListJob::ListFlags());
    connect(m_searchJob, &KIO::ListJob::entries, this, &FolderModel::onSearchEntries);
    connect(m_searchJob, &KJob::result, this, [this] {
        m_searchJob = nullptr;
        Q_EMIT searchingChanged();
    });
    Q_EMIT searchingChanged();
}

void FolderModel::stopSearch()
{
    m_searchDelay.stop();
    if (m_searchJob) {
        m_searchJob->disconnect(this);
        m_searchJob->kill();
        m_searchJob = nullptr;
        Q_EMIT searchingChanged();
    }
}

void FolderModel::onSearchEntries(KIO::Job *, const QList<KIO::UDSEntry> &entries)
{
    bool added = false;
    const QString base = m_url.path().endsWith(QLatin1Char('/')) ? m_url.path() : m_url.path() + QLatin1Char('/');
    for (const KIO::UDSEntry &entry : entries) {
        const QString relative = entry.stringValue(KIO::UDSEntry::UDS_NAME);
        const auto slash = relative.lastIndexOf(QLatin1Char('/'));
        // Direct children are already in the listing.
        if (slash < 0 || relative == QStringLiteral(".") || relative == QStringLiteral("..")) {
            continue;
        }
        const QString leaf = relative.mid(slash + 1);
        if (isSystemItem(leaf) || relative.startsWith(QStringLiteral("$RECYCLE.BIN/"), Qt::CaseInsensitive) || relative.contains(QStringLiteral("/$RECYCLE.BIN/"), Qt::CaseInsensitive)) {
            continue;
        }
        if (!leaf.contains(m_searchText, Qt::CaseInsensitive)) {
            continue;
        }
        QUrl url = m_url;
        url.setPath(base + relative);
        KIO::UDSEntry copy = entry;
        copy.replace(KIO::UDSEntry::UDS_NAME, leaf);
        const KFileItem item(copy, url, false, false);
        m_deep.append(makeEntry(item, relative.left(slash)));
        added = true;
        if (m_deep.size() >= searchLimit) {
            stopSearch();
            break;
        }
    }
    if (added) {
        rebuild();
    }
}

int FolderModel::selectedCount() const
{
    int n = 0;
    for (const Entry &e : m_rows) {
        n += e.selected ? 1 : 0;
    }
    return n;
}

QString FolderModel::selectionSize() const
{
    qint64 total = 0;
    bool any = false;
    for (const Entry &e : m_rows) {
        if (e.selected) {
            if (e.isDir) {
                return QString();
            }
            total += std::max<qint64>(0, e.sizeKey);
            any = true;
        }
    }
    return any ? Format::size(total) : QString();
}

void FolderModel::setCurrentIndex(int row)
{
    if (row < -1 || row >= m_rows.size()) {
        row = -1;
    }
    if (row != m_current) {
        m_current = row;
        Q_EMIT currentIndexChanged();
    }
}

void FolderModel::setSortColumn(int column)
{
    column = std::clamp(column, 0, 3);
    if (column == m_sortColumn) {
        return;
    }
    m_sortColumn = column;
    // Dates and sizes read best newest and largest first.
    m_descending = column == 1 || column == 3;
    Settings::instance()->setSortFor(m_url, m_sortColumn, m_descending);
    rebuild();
    Q_EMIT sortChanged();
}

void FolderModel::setSortDescending(bool descending)
{
    if (descending != m_descending) {
        m_descending = descending;
        Settings::instance()->setSortFor(m_url, m_sortColumn, m_descending);
        rebuild();
        Q_EMIT sortChanged();
    }
}

void FolderModel::notifySelection(int first, int last)
{
    if (m_rows.isEmpty()) {
        Q_EMIT selectionChanged();
        return;
    }
    if (first < 0) {
        first = 0;
        last = int(m_rows.size()) - 1;
    }
    Q_EMIT dataChanged(index(first), index(last), {SelectedRole});
    Q_EMIT selectionChanged();
}

void FolderModel::select(int row, int modifiers)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    const bool ctrl = modifiers & Qt::ControlModifier;
    const bool shift = modifiers & Qt::ShiftModifier;
    if (shift && m_anchor >= 0 && m_anchor < m_rows.size()) {
        const int from = std::min(m_anchor, row);
        const int to = std::max(m_anchor, row);
        for (int i = 0; i < m_rows.size(); ++i) {
            if (i >= from && i <= to) {
                m_rows[i].selected = true;
            } else if (!ctrl) {
                m_rows[i].selected = false;
            }
        }
    } else if (ctrl) {
        m_rows[row].selected = !m_rows[row].selected;
        m_anchor = row;
    } else {
        for (Entry &e : m_rows) {
            e.selected = false;
        }
        m_rows[row].selected = true;
        m_anchor = row;
    }
    setCurrentIndex(row);
    notifySelection();
}

void FolderModel::moveCurrent(int row, int modifiers)
{
    if (m_rows.isEmpty()) {
        return;
    }
    row = std::clamp(row, 0, int(m_rows.size()) - 1);
    if ((modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
        setCurrentIndex(row);
        return;
    }
    select(row, modifiers);
}

void FolderModel::selectRows(const QList<int> &rows, bool additive)
{
    QSet<int> wanted(rows.cbegin(), rows.cend());
    for (int i = 0; i < m_rows.size(); ++i) {
        if (wanted.contains(i)) {
            m_rows[i].selected = true;
        } else if (!additive) {
            m_rows[i].selected = false;
        }
    }
    if (!rows.isEmpty()) {
        setCurrentIndex(rows.constLast());
        m_anchor = rows.constFirst();
    }
    notifySelection();
}

void FolderModel::selectAll()
{
    for (Entry &e : m_rows) {
        e.selected = true;
    }
    notifySelection();
}

void FolderModel::clearSelection()
{
    bool any = false;
    for (Entry &e : m_rows) {
        any = any || e.selected;
        e.selected = false;
    }
    if (any) {
        notifySelection();
    }
}

void FolderModel::invertSelection()
{
    for (Entry &e : m_rows) {
        e.selected = !e.selected;
    }
    notifySelection();
}

bool FolderModel::isSelected(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).selected;
}

QList<QUrl> FolderModel::selectedUrls() const
{
    QList<QUrl> urls;
    for (const Entry &e : m_rows) {
        if (e.selected && !e.isDrive) {
            urls.append(e.url);
        }
    }
    return urls;
}

bool FolderModel::selectionHasFolders() const
{
    for (const Entry &e : m_rows) {
        if (e.selected && e.isDir) {
            return true;
        }
    }
    return false;
}

QUrl FolderModel::urlAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).url : QUrl();
}

bool FolderModel::isDirAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).isDir;
}

bool FolderModel::isDriveAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).isDrive;
}

QString FolderModel::fileNameAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).fileName : QString();
}

QString FolderModel::nameAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).name : QString();
}

QString FolderModel::udiAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).udi : QString();
}

bool FolderModel::isRemovableAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).removable;
}

QString FolderModel::iconAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).icon : QString();
}

QString FolderModel::mimeAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).mime : QString();
}

int FolderModel::rowForUrl(const QUrl &url) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).url == url) {
            return i;
        }
    }
    return -1;
}

int FolderModel::findPrefix(const QString &prefix, int from) const
{
    const int n = int(m_rows.size());
    for (int k = 0; k < n; ++k) {
        const int i = (from + k) % n;
        if (m_rows.at(i).name.startsWith(prefix, Qt::CaseInsensitive)) {
            return i;
        }
    }
    return -1;
}

void FolderModel::selectWhenListed(const QList<QUrl> &urls, bool rename)
{
    bool cleared = false;
    for (const QUrl &url : urls) {
        const int row = rowForUrl(url);
        if (!cleared) {
            clearSelection();
            cleared = true;
        }
        if (rename && url == urls.constFirst()) {
            m_pendingRename = url;
        }
        if (row >= 0) {
            m_pending.insert(url);
            takePending(row);
        } else {
            m_pending.insert(url);
        }
    }
}

QStringList FolderModel::completions(const QString &input) const
{
    // Up to the last slash is the folder to look in, the rest the start of
    // a name inside it. With no slash, it is a name in the open folder.
    const auto slash = input.lastIndexOf(QLatin1Char('/'));
    const QString typedFolder = input.left(slash + 1);
    const QString start = input.mid(slash + 1);
    QString path;
    if (typedFolder.startsWith(QLatin1String("~/"))) {
        path = homePath() + typedFolder.mid(1);
    } else if (typedFolder.startsWith(QLatin1Char('/'))) {
        path = typedFolder;
    } else if (m_url.isLocalFile() && !input.isEmpty() && !input.contains(QLatin1String(":/"))) {
        path = QDir(m_url.toLocalFile()).filePath(typedFolder);
    } else {
        return {};
    }
    const QDir dir(path);
    if (!dir.exists()) {
        return {};
    }
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (Settings::instance()->showHidden() || start.startsWith(QLatin1Char('.'))) {
        filters |= QDir::Hidden;
    }
    QStringList names = dir.entryList(filters);
    names.removeIf([&start](const QString &name) {
        return isSystemItem(name) || !name.startsWith(start, Qt::CaseInsensitive) || name == start;
    });
    std::sort(names.begin(), names.end(), [this](const QString &a, const QString &b) {
        return m_collator.compare(a, b) < 0;
    });
    QStringList list;
    for (const QString &name : std::as_const(names)) {
        if (list.size() >= 8) {
            break;
        }
        list.append(typedFolder + name);
    }
    return list;
}

QVariantList FolderModel::subfolders(const QUrl &url) const
{
    QVariantList list;
    if (url == computerUrl()) {
        const auto drives = Drives::instance()->list();
        for (const Drive &drive : drives) {
            if (drive.mounted) {
                list.append(QVariantMap{{QStringLiteral("name"), drive.name}, {QStringLiteral("url"), QUrl::fromLocalFile(drive.mountPath)}});
            }
        }
        return list;
    }
    if (!url.isLocalFile()) {
        return list;
    }
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (Settings::instance()->showHidden()) {
        filters |= QDir::Hidden;
    }
    QDir dir(url.toLocalFile());
    QStringList names = dir.entryList(filters);
    names.removeIf(isSystemItem);
    std::sort(names.begin(), names.end(), [this](const QString &a, const QString &b) {
        return m_collator.compare(a, b) < 0;
    });
    for (const QString &name : std::as_const(names)) {
        if (list.size() >= 40) {
            break;
        }
        list.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("url"), QUrl::fromLocalFile(dir.filePath(name))}});
    }
    return list;
}
