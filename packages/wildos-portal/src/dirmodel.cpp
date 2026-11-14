#include "dirmodel.h"

#include "format.h"

#include <QLocale>
#include <KConfigGroup>
#include <KCoreDirLister>
#include <KIO/MkdirJob>
#include <KIO/CopyJob>
#include <KIO/PreviewJob>
#include <KSharedConfig>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
// Whether KIO can draw a picture of this type for Large icons, as Files
// decides it.
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

bool isUnder(const QString &path, const QString &root)
{
    return path == root || path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
}

// Removable and second drives, as Files names them in This computer.
struct Volume {
    QString name;
    QString path;
    QString type;
    qint64 total = 0;
    qint64 free = 0;
};

QList<Volume> volumes()
{
    QList<Volume> list;
    const QStorageInfo root(QStringLiteral("/"));
    list.append({QStringLiteral("Wild OS"), QStringLiteral("/"), QStringLiteral("Local disk"), root.bytesTotal(), root.bytesAvailable()});
    static const QStringList mountRoots = {QStringLiteral("/media/"), QStringLiteral("/run/media/"), QStringLiteral("/mnt/")};
    const auto mounted = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &info : mounted) {
        const QString path = info.rootPath();
        if (!info.isValid() || !info.isReady() || !info.device().startsWith("/dev/")) {
            continue;
        }
        const bool removable = std::any_of(mountRoots.cbegin(), mountRoots.cend(), [&path](const QString &r) { return path.startsWith(r); });
        if (!removable) {
            continue;
        }
        const QString label = info.displayName() != path ? info.displayName() : QString();
        const QString fs = QString::fromLatin1(info.fileSystemType());
        QString name = label;
        if (name.isEmpty()) {
            name = fs == QStringLiteral("ntfs") || fs == QStringLiteral("ntfs3") ? QStringLiteral("Windows") : QStringLiteral("USB drive");
        }
        list.append({name, path, QStringLiteral("Drive"), info.bytesTotal(), info.bytesAvailable()});
    }
    return list;
}

QString friendlyRoot(const QString &path, QString *rootPath)
{
    const QString home = QDir::homePath();
    if (isUnder(path, home)) {
        *rootPath = home;
        return QStringLiteral("Home");
    }
    QString best = QStringLiteral("/");
    QString name = QStringLiteral("Wild OS");
    for (const Volume &v : volumes()) {
        if (v.path != QStringLiteral("/") && isUnder(path, v.path) && v.path.size() > best.size()) {
            best = v.path;
            name = v.name;
        }
    }
    *rootPath = best;
    return name;
}
}

DirModel::DirModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_lister(new KCoreDirLister(this))
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    const KConfigGroup view(KSharedConfig::openConfig(QStringLiteral("wildosfilesrc")), QStringLiteral("View"));
    m_showHidden = view.readEntry("ShowHidden", false);
    m_lister->setAutoErrorHandlingEnabled(false);
    m_lister->setShowHiddenFiles(true);
    m_lister->setDelayedMimeTypes(false);

    connect(m_lister, &KCoreDirLister::newItems, this, [this](const KFileItemList &items) {
        for (const KFileItem &item : items) {
            m_all.append(makeEntry(item));
        }
        rebuild();
    });
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, [this](const KFileItemList &items) {
        for (const KFileItem &item : items) {
            m_all.removeIf([&item](const Entry &e) { return e.url == item.url(); });
        }
        rebuild();
    });
    connect(m_lister, &KCoreDirLister::refreshItems, this, [this](const QList<QPair<KFileItem, KFileItem>> &items) {
        for (const auto &pair : items) {
            for (Entry &e : m_all) {
                if (e.url == pair.first.url()) {
                    const bool selected = e.selected;
                    e = makeEntry(pair.second);
                    e.selected = selected;
                }
            }
        }
        rebuild();
    });
    connect(m_lister, &KCoreDirLister::clear, this, [this] {
        m_all.clear();
        rebuild();
    });
    connect(m_lister, &KCoreDirLister::completed, this, [this] {
        setLoading(false);
    });
    connect(m_lister, &KCoreDirLister::jobError, this, [this](KIO::Job *job) {
        setLoading(false);
        setError(job->errorString());
    });
}

DirModel::~DirModel() = default;

QUrl DirModel::computerUrl()
{
    return QUrl(QStringLiteral("wildos-computer:/"));
}

bool DirModel::showExtensions()
{
    const KConfigGroup view(KSharedConfig::openConfig(QStringLiteral("wildosfilesrc")), QStringLiteral("View"));
    return view.readEntry("ShowExtensions", false);
}

int DirModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant DirModel::data(const QModelIndex &index, int role) const
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
    case DateRole:
        return e.date;
    case TypeRole:
        return e.type;
    case SizeRole:
        return e.size;
    case SelectedRole:
        return e.selected;
    case HiddenRole:
        return e.hidden;
    case PreviewRole:
        return e.preview;
    case StampRole:
        return e.dateKey;
    }
    return {};
}

QHash<int, QByteArray> DirModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {FileNameRole, "fileName"},
        {IconRole, "iconName"},
        {UrlRole, "url"},
        {IsDirRole, "isDir"},
        {DateRole, "date"},
        {TypeRole, "type"},
        {SizeRole, "size"},
        {SelectedRole, "selected"},
        {HiddenRole, "hidden"},
        {PreviewRole, "preview"},
        {StampRole, "stamp"},
    };
}

QString DirModel::title() const
{
    if (isComputer()) {
        return QStringLiteral("This computer");
    }
    if (m_url.isLocalFile()) {
        const QString path = QDir::cleanPath(m_url.toLocalFile());
        QString root;
        const QString name = friendlyRoot(path, &root);
        if (path == root) {
            return name;
        }
    }
    const QString name = m_url.adjusted(QUrl::StripTrailingSlash).fileName();
    return name.isEmpty() ? m_url.host() : name;
}

QString DirModel::pathText() const
{
    if (isComputer()) {
        return QStringLiteral("This computer");
    }
    return m_url.isLocalFile() ? QDir::cleanPath(m_url.toLocalFile()) : m_url.toDisplayString();
}

QVariantList DirModel::crumbs() const
{
    QVariantList list;
    auto add = [&list](const QString &name, const QUrl &url) {
        list.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("url"), url}});
    };
    if (isComputer()) {
        add(QStringLiteral("This computer"), m_url);
        return list;
    }
    if (!m_url.isLocalFile()) {
        add(m_url.host().isEmpty() ? m_url.scheme() : m_url.host(), m_url.adjusted(QUrl::RemovePath));
        const QStringList parts = m_url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString path;
        for (const QString &part : parts) {
            path += QLatin1Char('/') + part;
            QUrl u = m_url;
            u.setPath(path);
            add(part, u);
        }
        return list;
    }
    const QString path = QDir::cleanPath(m_url.toLocalFile());
    QString root;
    const QString rootName = friendlyRoot(path, &root);
    add(rootName, QUrl::fromLocalFile(root));
    const QStringList parts = path.mid(root.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString walk = root == QStringLiteral("/") ? QString() : root;
    for (const QString &part : parts) {
        walk += QLatin1Char('/') + part;
        add(part, QUrl::fromLocalFile(walk));
    }
    return list;
}

bool DirModel::writable() const
{
    if (isComputer()) {
        return false;
    }
    if (m_url.isLocalFile()) {
        return QFileInfo(m_url.toLocalFile()).isWritable();
    }
    const KFileItem root = m_lister->rootItem();
    return root.isNull() || root.isWritable();
}

bool DirModel::canGoUp() const
{
    if (isComputer()) {
        return false;
    }
    if (m_url.isLocalFile()) {
        return true;
    }
    return m_url.path().length() > 1;
}

int DirModel::selectedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Entry &e) { return e.selected; }));
}

void DirModel::setCurrentIndex(int row)
{
    if (row != m_current) {
        m_current = row;
        Q_EMIT currentIndexChanged();
    }
}

void DirModel::setSortColumn(int column)
{
    if (column == m_sortColumn) {
        return;
    }
    m_sortColumn = column;
    m_descending = column == 1;
    Q_EMIT sortChanged();
    rebuild();
}

void DirModel::setSortDescending(bool descending)
{
    if (descending != m_descending) {
        m_descending = descending;
        Q_EMIT sortChanged();
        rebuild();
    }
}

void DirModel::setSearchText(const QString &text)
{
    if (text != m_search) {
        m_search = text;
        Q_EMIT searchTextChanged();
        rebuild();
    }
}

void DirModel::setFoldersOnly(bool on)
{
    if (on != m_foldersOnly) {
        m_foldersOnly = on;
        Q_EMIT filterChanged();
        rebuild();
    }
}

void DirModel::setMultiple(bool on)
{
    if (on != m_multiple) {
        m_multiple = on;
        Q_EMIT multipleChanged();
    }
}

void DirModel::setFilter(const Portal::Filter &filter)
{
    m_filter = filter;
    Q_EMIT filterChanged();
    rebuild();
}

void DirModel::openUrl(const QUrl &url)
{
    navigate(url, true);
}

bool DirModel::openText(const QString &typed)
{
    QString text = typed.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    if (text.compare(QStringLiteral("This computer"), Qt::CaseInsensitive) == 0) {
        navigate(computerUrl(), true);
        return true;
    }
    if (text.compare(QStringLiteral("Home"), Qt::CaseInsensitive) == 0 || text == QStringLiteral("~")) {
        navigate(QUrl::fromLocalFile(QDir::homePath()), true);
        return true;
    }
    if (text.startsWith(QStringLiteral("~/"))) {
        text = QDir::homePath() + text.mid(1);
    }
    QUrl url;
    if (text.startsWith(QLatin1Char('/'))) {
        url = QUrl::fromLocalFile(text);
    } else if (text.contains(QStringLiteral(":/"))) {
        url = QUrl::fromUserInput(text);
    } else if (m_url.isLocalFile()) {
        url = QUrl::fromLocalFile(QDir(m_url.toLocalFile()).absoluteFilePath(text));
    } else {
        return false;
    }
    if (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isDir()) {
        return false;
    }
    navigate(url, true);
    return true;
}

void DirModel::back()
{
    if (m_back.isEmpty()) {
        return;
    }
    m_forward.prepend(m_url);
    navigate(m_back.takeLast(), false);
    Q_EMIT historyChanged();
}

void DirModel::forward()
{
    if (m_forward.isEmpty()) {
        return;
    }
    m_back.append(m_url);
    navigate(m_forward.takeFirst(), false);
    Q_EMIT historyChanged();
}

void DirModel::up()
{
    if (!canGoUp()) {
        return;
    }
    if (m_url.isLocalFile()) {
        const QString path = QDir::cleanPath(m_url.toLocalFile());
        QString root;
        friendlyRoot(path, &root);
        // Above Home or a drive is This computer, as in Files.
        if (path == root || path == QStringLiteral("/")) {
            navigate(computerUrl(), true);
            return;
        }
    }
    navigate(KIO::upUrl(m_url), true);
}

void DirModel::refresh()
{
    navigate(m_url, false);
}

void DirModel::navigate(const QUrl &target, bool record)
{
    QUrl url = target.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments);
    if (url.isLocalFile() && url.toLocalFile().isEmpty()) {
        url = QUrl::fromLocalFile(QStringLiteral("/"));
    }
    if (record && url != m_url && !m_url.isEmpty()) {
        m_back.append(m_url);
        m_forward.clear();
        Q_EMIT historyChanged();
    }
    m_url = url;
    m_current = -1;
    m_anchor = -1;
    setError(QString());
    Q_EMIT urlChanged();
    Q_EMIT currentIndexChanged();
    m_all.clear();
    if (isComputer()) {
        m_lister->stop();
        m_all = driveEntries();
        setLoading(false);
        rebuild();
        return;
    }
    rebuild();
    setLoading(true);
    m_lister->openUrl(url, KCoreDirLister::Reload);
}

DirModel::Entry DirModel::makeEntry(const KFileItem &item) const
{
    Entry e;
    e.item = item;
    e.url = item.url();
    e.fileName = item.name();
    e.isDir = item.isDir();
    e.name = Format::visibleName(e.fileName, e.isDir, showExtensions());
    e.icon = item.iconName();
    e.hidden = item.isHidden();
    const QDateTime modified = item.time(KFileItem::ModificationTime);
    e.dateKey = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;
    e.date = modified.isValid() ? QLocale::system().toString(modified.toLocalTime(), QLocale::ShortFormat) : QString();
    e.type = Format::typeName(item.currentMimeType(), e.isDir);
    if (!e.isDir) {
        e.sizeKey = qint64(item.size());
        e.size = Format::size(e.sizeKey);
        e.preview = item.isLocalFile() && e.sizeKey < 100LL * 1024 * 1024 && previewable(item.currentMimeType());
    }
    return e;
}

QList<DirModel::Entry> DirModel::driveEntries() const
{
    QList<Entry> list;
    for (const Volume &v : volumes()) {
        Entry e;
        e.url = QUrl::fromLocalFile(v.path);
        e.fileName = v.name;
        e.name = v.name;
        e.icon = v.path == QStringLiteral("/") ? QStringLiteral("drive-harddisk") : QStringLiteral("drive-removable-media");
        e.isDir = true;
        e.type = v.type;
        e.size = Format::size(v.free) + QStringLiteral(" free");
        e.sizeKey = v.free;
        list.append(e);
    }
    const QString home = QDir::homePath();
    Entry h;
    h.url = QUrl::fromLocalFile(home);
    h.fileName = QStringLiteral("Home");
    h.name = h.fileName;
    h.icon = QStringLiteral("user-home");
    h.isDir = true;
    h.type = QStringLiteral("Folder");
    list.prepend(h);
    return list;
}

bool DirModel::passes(const Entry &e) const
{
    if (e.hidden && !m_showHidden) {
        return false;
    }
    if (!m_search.isEmpty() && !e.name.contains(m_search, Qt::CaseInsensitive)) {
        return false;
    }
    if (e.isDir || isComputer()) {
        return true;
    }
    if (m_foldersOnly) {
        return false;
    }
    if (m_filter.rules.isEmpty()) {
        return true;
    }
    for (const Portal::FilterRule &rule : m_filter.rules) {
        if (rule.type == 0) {
            if (rule.pattern == QStringLiteral("*") || rule.pattern == QStringLiteral("*.*")) {
                return true;
            }
            const auto re = QRegularExpression::fromWildcard(rule.pattern, Qt::CaseInsensitive, QRegularExpression::UnanchoredWildcardConversion);
            const QRegularExpression anchored(QRegularExpression::anchoredPattern(re.pattern()), QRegularExpression::CaseInsensitiveOption);
            if (anchored.match(e.fileName).hasMatch()) {
                return true;
            }
        } else if (!e.item.isNull()) {
            const QMimeType mime = e.item.currentMimeType();
            if (rule.pattern == QStringLiteral("*/*") || rule.pattern == QStringLiteral("application/octet-stream") || mime.inherits(rule.pattern)
                || (rule.pattern.endsWith(QStringLiteral("/*")) && mime.name().startsWith(rule.pattern.chopped(1)))) {
                return true;
            }
        }
    }
    return false;
}

bool DirModel::lessThan(const Entry &a, const Entry &b) const
{
    if (a.isDir != b.isDir) {
        return a.isDir;
    }
    int c = 0;
    switch (m_sortColumn) {
    case 1:
        c = a.dateKey < b.dateKey ? -1 : a.dateKey > b.dateKey ? 1 : 0;
        break;
    case 2:
        c = m_collator.compare(a.type, b.type);
        break;
    case 3:
        c = a.sizeKey < b.sizeKey ? -1 : a.sizeKey > b.sizeKey ? 1 : 0;
        break;
    default:
        break;
    }
    if (c == 0) {
        c = m_collator.compare(a.name, b.name);
    }
    return m_descending ? c > 0 : c < 0;
}

void DirModel::rebuild()
{
    QSet<QUrl> selected;
    for (const Entry &e : std::as_const(m_rows)) {
        if (e.selected) {
            selected.insert(e.url);
        }
    }
    const QUrl currentUrl = m_current >= 0 && m_current < m_rows.size() ? m_rows.at(m_current).url : QUrl();
    beginResetModel();
    m_rows.clear();
    for (const Entry &e : std::as_const(m_all)) {
        if (passes(e)) {
            Entry row = e;
            row.selected = selected.contains(e.url);
            m_rows.append(row);
        }
    }
    if (!isComputer()) {
        std::sort(m_rows.begin(), m_rows.end(), [this](const Entry &a, const Entry &b) { return lessThan(a, b); });
    }
    m_current = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).url == currentUrl) {
            m_current = i;
        }
    }
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentIndexChanged();
    notifySelection();
    if (!m_madeFolder.isEmpty()) {
        const int row = rowForFileName(m_madeFolder);
        if (row >= 0) {
            m_madeFolder.clear();
            select(row, 0);
            Q_EMIT folderMade(row);
        }
    }
}

void DirModel::setLoading(bool on)
{
    if (on != m_loading) {
        m_loading = on;
        Q_EMIT loadingChanged();
    }
}

void DirModel::setError(const QString &text)
{
    if (text != m_error) {
        m_error = text;
        Q_EMIT errorChanged();
    }
}

void DirModel::notifySelection()
{
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {SelectedRole});
    }
    Q_EMIT selectionChanged();
}

void DirModel::select(int row, int modifiers)
{
    if (row < 0 || row >= m_rows.size()) {
        clearSelection();
        return;
    }
    const bool ctrl = m_multiple && (modifiers & Qt::ControlModifier);
    const bool shift = m_multiple && (modifiers & Qt::ShiftModifier) && m_anchor >= 0;
    if (shift) {
        const int lo = std::min(m_anchor, row);
        const int hi = std::max(m_anchor, row);
        for (int i = 0; i < m_rows.size(); ++i) {
            m_rows[i].selected = (i >= lo && i <= hi) || (ctrl && m_rows[i].selected);
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

void DirModel::moveCurrent(int row, int modifiers)
{
    if (m_rows.isEmpty()) {
        return;
    }
    select(std::clamp(row, 0, int(m_rows.size()) - 1), modifiers);
}

void DirModel::clearSelection()
{
    for (Entry &e : m_rows) {
        e.selected = false;
    }
    m_anchor = -1;
    notifySelection();
}

QList<QUrl> DirModel::selectedUrls() const
{
    QList<QUrl> urls;
    for (const Entry &e : m_rows) {
        if (e.selected) {
            urls.append(e.url);
        }
    }
    return urls;
}

QStringList DirModel::selectedNames() const
{
    QStringList names;
    for (const Entry &e : m_rows) {
        if (e.selected) {
            names.append(e.name);
        }
    }
    return names;
}

QUrl DirModel::urlAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).url : QUrl();
}

bool DirModel::isDirAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).isDir;
}

QString DirModel::nameAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).name : QString();
}

QString DirModel::fileNameAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).fileName : QString();
}

int DirModel::rowForFileName(const QString &fileName) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).fileName == fileName) {
            return i;
        }
    }
    return -1;
}

int DirModel::findPrefix(const QString &prefix, int from) const
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

QUrl DirModel::childUrl(const QString &fileName) const
{
    QUrl url = m_url;
    url.setPath(m_url.path() + (m_url.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + fileName);
    return url;
}

QString DirModel::uniqueName(const QString &base, const QString &suffix) const
{
    QString name = base + suffix;
    for (int n = 2; rowForFileName(name) >= 0 || std::any_of(m_all.cbegin(), m_all.cend(), [&name](const Entry &e) { return e.fileName == name; }); ++n) {
        name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
    }
    return name;
}

void DirModel::newFolder()
{
    if (!writable()) {
        Q_EMIT failed(QStringLiteral("You cannot make a folder here. Pick a folder you can save in."));
        return;
    }
    const QString name = uniqueName(QStringLiteral("New folder"), QString());
    auto job = KIO::mkdir(childUrl(name));
    connect(job, &KJob::result, this, [this, job, name] {
        if (job->error()) {
            Q_EMIT failed(QStringLiteral("The folder could not be made: %1").arg(job->errorString()));
            return;
        }
        m_madeFolder = name;
        const int row = rowForFileName(name);
        if (row >= 0) {
            m_madeFolder.clear();
            select(row, 0);
            Q_EMIT folderMade(row);
        }
    });
}

bool DirModel::renameItem(int row, const QString &typed)
{
    const QString name = typed.trimmed();
    if (row < 0 || row >= m_rows.size() || name.isEmpty() || name == m_rows.at(row).name) {
        return true;
    }
    if (name.contains(QLatin1Char('/')) || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        Q_EMIT failed(QStringLiteral("A name cannot contain / or be just dots. Try a different name."));
        return false;
    }
    const Entry &e = m_rows.at(row);
    const QString fileName = name + e.fileName.mid(e.name.size());
    if (rowForFileName(fileName) >= 0) {
        Q_EMIT failed(QStringLiteral("There is already an item called \"%1\" here. Pick a different name.").arg(name));
        return false;
    }
    auto job = KIO::moveAs(e.url, childUrl(fileName), KIO::HideProgressInfo);
    connect(job, &KJob::result, this, [this, job] {
        if (job->error()) {
            Q_EMIT failed(job->errorString());
        }
    });
    return true;
}
