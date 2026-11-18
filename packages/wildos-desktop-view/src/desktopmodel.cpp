#include "desktopmodel.h"

#include "deskops.h"
#include "format.h"

#include <KCoreDirLister>
#include <KDesktopFile>
#include <KIO/PreviewJob>
#include <KUser>
#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QStandardPaths>

#include <cmath>

namespace
{
const QString computerKey = QStringLiteral("::computer");
const QString homeKey = QStringLiteral("::home");
const QString binKey = QStringLiteral("::bin");

quint64 cellKey(int column, int row)
{
    return (quint64(quint32(column)) << 32) | quint32(row);
}

int systemRank(const QString &kind)
{
    if (kind == QLatin1String("computer")) {
        return 0;
    }
    if (kind == QLatin1String("home")) {
        return 1;
    }
    if (kind == QLatin1String("bin")) {
        return 2;
    }
    return 3;
}

// The home folder holds Documents, Downloads, Pictures and the rest, so it is
// "Alex's files" rather than a bare name that reads like a single folder.
// Without a name on the account it is just "My files".
QString homeLabel()
{
    const KUser user;
    const QString full = user.property(KUser::FullName).toString().section(QLatin1Char(','), 0, 0).trimmed();
    const QString first = full.section(QLatin1Char(' '), 0, 0);
    return first.isEmpty() ? QCoreApplication::translate("DesktopModel", "My files") : QCoreApplication::translate("DesktopModel", "%1's files").arg(first);
}

QUrl stripped(const QUrl &url)
{
    return url.adjusted(QUrl::StripTrailingSlash);
}
}

DesktopModel::DesktopModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_lister(new KCoreDirLister(this))
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QDir().mkpath(path);
    m_folder = QUrl::fromLocalFile(path);

    m_layoutTimer.setSingleShot(true);
    m_layoutTimer.setInterval(0);
    connect(&m_layoutTimer, &QTimer::timeout, this, &DesktopModel::layout);
    m_hintTimer.setSingleShot(true);
    m_hintTimer.setInterval(8000);
    connect(&m_hintTimer, &QTimer::timeout, this, [this] {
        m_hintValid = false;
    });

    makeSystemEntries();

    DeskOps *ops = DeskOps::instance();
    connect(ops, &DeskOps::clipboardChanged, this, [this] {
        emitAll({CutRole});
    });
    connect(ops, &DeskOps::viewChanged, this, [this, ops] {
        if (m_lister->showHiddenFiles() != ops->showHidden()) {
            m_lister->setShowHiddenFiles(ops->showHidden());
            m_lister->emitChanges();
        }
        refreshNames();
    });
    connect(ops, &DeskOps::created, this, &DesktopModel::onCreated);

    m_lister->setAutoErrorHandlingEnabled(false);
    m_lister->setShowHiddenFiles(ops->showHidden());
    connect(m_lister, &KCoreDirLister::newItems, this, &DesktopModel::addItems);
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, &DesktopModel::removeItems);
    connect(m_lister, &KCoreDirLister::refreshItems, this, &DesktopModel::updateItems);
    connect(m_lister, &KCoreDirLister::started, this, [this] {
        m_listing = true;
        Q_EMIT listingChanged();
    });
    connect(m_lister, qOverload<>(&KCoreDirLister::completed), this, [this] {
        m_listing = false;
        Q_EMIT listingChanged();
    });
    connect(m_lister, qOverload<>(&KCoreDirLister::clear), this, [this] {
        beginResetModel();
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [](const Entry &e) {
                            return e.kind == QLatin1String("item");
                        }),
                        m_entries.end());
        endResetModel();
        Q_EMIT countChanged();
        scheduleLayout();
    });
    m_lister->openUrl(m_folder);
}

void DesktopModel::makeSystemEntries()
{
    Entry computer;
    computer.key = computerKey;
    computer.kind = QStringLiteral("computer");
    computer.url = QUrl(QStringLiteral("wildos:computer"));
    computer.name = QCoreApplication::translate("DesktopModel", "This computer");
    computer.type = QCoreApplication::translate("DesktopModel", "System folder");

    Entry home;
    home.key = homeKey;
    home.kind = QStringLiteral("home");
    home.url = QUrl::fromLocalFile(QDir::homePath());
    home.name = homeLabel();
    home.fileName = home.name;
    home.isDir = true;
    home.type = QCoreApplication::translate("DesktopModel", "System folder");

    Entry bin;
    bin.key = binKey;
    bin.kind = QStringLiteral("bin");
    bin.url = QUrl(QStringLiteral("trash:/"));
    bin.name = QCoreApplication::translate("DesktopModel", "Recycle Bin");
    bin.type = QCoreApplication::translate("DesktopModel", "System folder");

    beginResetModel();
    m_entries = {computer, home, bin};
    endResetModel();
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

DesktopModel::Entry DesktopModel::entryFor(const KFileItem &item) const
{
    Entry e;
    e.kind = QStringLiteral("item");
    e.item = item;
    e.url = item.url();
    e.fileName = item.name();
    e.key = e.fileName;
    e.isDir = item.isDir();
    const QMimeType mime = item.determineMimeType();
    e.type = Format::typeName(mime, e.isDir);
    if (mime.name() == QLatin1String("application/x-desktop") && item.isLocalFile()) {
        // A shortcut shows the name written in it, the way a .lnk does.
        const KDesktopFile desktop(item.localPath());
        e.name = desktop.readName();
    }
    if (e.name.isEmpty()) {
        e.name = Format::visibleName(e.fileName, e.isDir, DeskOps::instance()->showExtensions());
    }
    e.iconName = item.iconName();
    e.size = e.isDir ? -1 : qint64(item.size());
    e.modified = item.time(KFileItem::ModificationTime);
    static const QStringList previewTypes = KIO::PreviewJob::supportedMimeTypes();
    // Pictures and videos show what is in them; documents keep their icon,
    // as on the Windows desktop.
    const bool visual = mime.name().startsWith(QLatin1String("image/")) || mime.name().startsWith(QLatin1String("video/"));
    e.preview = !e.isDir && visual && item.isLocalFile() && std::any_of(previewTypes.cbegin(), previewTypes.cend(), [&mime](const QString &t) {
        return t == mime.name() || (t.endsWith(QStringLiteral("/*")) && mime.name().startsWith(t.left(t.size() - 1)));
    });
    return e;
}

void DesktopModel::refreshNames()
{
    for (Entry &e : m_entries) {
        if (e.kind == QLatin1String("item")) {
            const QPointF cell = e.cell;
            e = entryFor(e.item);
            e.cell = cell;
        }
    }
    emitAll({NameRole, IconNameRole});
    if (m_sortKey == QLatin1String("name")) {
        resort();
    }
}

void DesktopModel::addItems(const KFileItemList &items)
{
    QList<Entry> added;
    for (const KFileItem &item : items) {
        if (isSystemItem(item.name()) || rowForUrl(item.url()) >= 0) {
            continue;
        }
        added.append(entryFor(item));
    }
    if (added.isEmpty()) {
        return;
    }

    // Items made or dropped by the user land where they asked, in the
    // nearest free cells.
    if (m_hintValid && !m_autoArrange) {
        QSet<quint64> taken;
        for (const Entry &e : std::as_const(m_entries)) {
            taken.insert(cellKey(qRound(e.cell.x()), qRound(e.cell.y())));
        }
        bool placed = false;
        for (const Entry &e : std::as_const(added)) {
            if (m_positions.contains(e.key)) {
                continue;
            }
            const QPoint cell = nearestFree(hintCell(), taken);
            taken.insert(cellKey(cell.x(), cell.y()));
            m_positions.insert(e.key, cell);
            placed = true;
        }
        if (placed) {
            savePositions();
        }
    }

    beginInsertRows(QModelIndex(), int(m_entries.size()), int(m_entries.size() + added.size() - 1));
    m_entries.append(added);
    endInsertRows();
    Q_EMIT countChanged();
    resort();
    claimPending();
}

void DesktopModel::removeItems(const KFileItemList &items)
{
    bool selection = false;
    for (const KFileItem &item : items) {
        const int row = rowForUrl(item.url());
        if (row < 0) {
            continue;
        }
        selection |= m_selected.remove(m_entries.at(row).key);
        beginRemoveRows(QModelIndex(), row, row);
        m_entries.removeAt(row);
        endRemoveRows();
        if (m_current >= int(m_entries.size())) {
            m_current = int(m_entries.size()) - 1;
            Q_EMIT currentIndexChanged();
        }
    }
    Q_EMIT countChanged();
    if (selection) {
        Q_EMIT selectionChanged();
    }
    scheduleLayout();
}

void DesktopModel::updateItems(const QList<QPair<KFileItem, KFileItem>> &items)
{
    for (const auto &pair : items) {
        const int row = rowForUrl(pair.first.url());
        if (row < 0) {
            continue;
        }
        Entry &e = m_entries[row];
        const QString oldKey = e.key;
        const QPointF cell = e.cell;
        e = entryFor(pair.second);
        e.cell = cell;
        // A renamed icon keeps its place and its selection.
        if (oldKey != e.key) {
            if (m_positions.contains(oldKey)) {
                m_positions.insert(e.key, m_positions.take(oldKey));
                savePositions();
            }
            if (m_selected.remove(oldKey)) {
                m_selected.insert(e.key);
            }
        }
        Q_EMIT dataChanged(index(row), index(row));
    }
    resort();
    claimPending();
}

bool DesktopModel::lessThan(const Entry &a, const Entry &b) const
{
    const int ra = systemRank(a.kind);
    const int rb = systemRank(b.kind);
    if (ra != rb || ra < 3) {
        return ra < rb;
    }
    if (m_sortKey != QLatin1String("modified") && a.isDir != b.isDir) {
        return a.isDir;
    }
    static QCollator collator = [] {
        QCollator c(QLocale(QLocale::English, QLocale::UnitedKingdom));
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    int order = 0;
    if (m_sortKey == QLatin1String("size")) {
        order = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
    } else if (m_sortKey == QLatin1String("type")) {
        order = collator.compare(a.type, b.type);
    } else if (m_sortKey == QLatin1String("modified")) {
        order = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0;
    }
    if (order == 0) {
        order = collator.compare(a.name, b.name);
    }
    return m_sortDescending ? order > 0 : order < 0;
}

void DesktopModel::resort()
{
    QList<Entry> sorted = m_entries;
    std::stable_sort(sorted.begin(), sorted.end(), [this](const Entry &a, const Entry &b) {
        return lessThan(a, b);
    });
    bool same = true;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted.at(i).key != m_entries.at(i).key) {
            same = false;
            break;
        }
    }
    if (!same) {
        const QString current = m_current >= 0 && m_current < m_entries.size() ? m_entries.at(m_current).key : QString();
        const QString anchor = m_anchor >= 0 && m_anchor < m_entries.size() ? m_entries.at(m_anchor).key : QString();
        beginResetModel();
        m_entries = sorted;
        endResetModel();
        m_current = -1;
        m_anchor = -1;
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).key == current) {
                m_current = i;
            }
            if (m_entries.at(i).key == anchor) {
                m_anchor = i;
            }
        }
        Q_EMIT currentIndexChanged();
        Q_EMIT countChanged();
    }
    scheduleLayout();
}

int DesktopModel::rows() const
{
    return std::max(1, int(std::floor(m_areaHeight / m_cellHeight)));
}

int DesktopModel::columns() const
{
    return std::max(1, int(std::floor(m_areaWidth / m_cellWidth)));
}

QPoint DesktopModel::firstFree(const QSet<quint64> &taken) const
{
    const int r = rows();
    for (int c = 0;; ++c) {
        for (int y = 0; y < r; ++y) {
            if (!taken.contains(cellKey(c, y))) {
                return QPoint(c, y);
            }
        }
    }
}

QPoint DesktopModel::nearestFree(const QPointF &cell, const QSet<quint64> &taken) const
{
    const int r = rows();
    const int c = columns();
    const int cx = std::clamp(qRound(cell.x()), 0, c - 1);
    const int cy = std::clamp(qRound(cell.y()), 0, r - 1);
    QPoint best(-1, -1);
    qreal bestDistance = 0;
    for (int ring = 0; ring < std::max(r, c); ++ring) {
        for (int x = cx - ring; x <= cx + ring; ++x) {
            for (int y = cy - ring; y <= cy + ring; ++y) {
                if (std::max(std::abs(x - cx), std::abs(y - cy)) != ring || x < 0 || y < 0 || x >= c || y >= r || taken.contains(cellKey(x, y))) {
                    continue;
                }
                const qreal d = std::hypot((x - cell.x()) * m_cellWidth, (y - cell.y()) * m_cellHeight);
                if (best.x() < 0 || d < bestDistance) {
                    best = QPoint(x, y);
                    bestDistance = d;
                }
            }
        }
        if (best.x() >= 0) {
            return best;
        }
    }
    return firstFree(taken);
}

void DesktopModel::scheduleLayout()
{
    m_layoutTimer.start();
}

void DesktopModel::layout()
{
    if (m_areaWidth <= 0 || m_areaHeight <= 0) {
        return;
    }
    const int r = rows();
    const int c = columns();
    QSet<quint64> taken;
    QList<bool> placed(m_entries.size(), false);

    if (m_autoArrange) {
        for (int i = 0; i < m_entries.size(); ++i) {
            m_entries[i].cell = QPointF(i / r, i % r);
        }
    } else {
        for (int i = 0; i < m_entries.size(); ++i) {
            Entry &e = m_entries[i];
            const auto it = m_positions.constFind(e.key);
            if (it == m_positions.constEnd()) {
                continue;
            }
            QPointF p = it.value();
            // A place off the edge of a smaller screen is kept for when the
            // bigger screen comes back; until then the icon flows.
            if (p.x() < 0 || p.y() < 0 || p.x() > c - 1 + 0.01 || p.y() > r - 1 + 0.01) {
                continue;
            }
            const quint64 k = cellKey(qRound(p.x()), qRound(p.y()));
            if (m_alignToGrid) {
                if (taken.contains(k)) {
                    continue;
                }
                p = QPointF(qRound(p.x()), qRound(p.y()));
            }
            taken.insert(k);
            e.cell = p;
            placed[i] = true;
        }
        for (int i = 0; i < m_entries.size(); ++i) {
            if (placed.at(i)) {
                continue;
            }
            const QPoint cell = firstFree(taken);
            taken.insert(cellKey(cell.x(), cell.y()));
            m_entries[i].cell = cell;
        }
    }
    emitAll({PosXRole, PosYRole});
}

void DesktopModel::emitAll(const QList<int> &roles)
{
    if (!m_entries.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(int(m_entries.size()) - 1), roles);
    }
}

int DesktopModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant DesktopModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case FileNameRole:
        return e.fileName;
    case UrlRole:
        return e.url;
    case IconNameRole:
        return e.iconName;
    case KindRole:
        return e.kind;
    case IsDirRole:
        return e.isDir;
    case SelectedRole:
        return m_selected.contains(e.key);
    case CutRole:
        return e.kind == QLatin1String("item") && DeskOps::instance()->isCut(e.url);
    case PreviewRole:
        return e.preview;
    case StampRole:
        return e.modified.isValid() ? e.modified.toSecsSinceEpoch() : 0;
    case PosXRole:
        return e.cell.x() * m_cellWidth + (m_cellWidth - m_itemWidth) / 2;
    case PosYRole:
        return e.cell.y() * m_cellHeight;
    case TypeRole:
        return e.type;
    case DropTargetRole:
        return e.kind == QLatin1String("home") || e.kind == QLatin1String("bin") || (e.kind == QLatin1String("item") && e.isDir);
    }
    return {};
}

QHash<int, QByteArray> DesktopModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {FileNameRole, "fileName"},
        {UrlRole, "url"},
        {IconNameRole, "iconName"},
        {KindRole, "kind"},
        {IsDirRole, "isDir"},
        {SelectedRole, "selected"},
        {CutRole, "cut"},
        {PreviewRole, "preview"},
        {StampRole, "stamp"},
        {PosXRole, "posX"},
        {PosYRole, "posY"},
        {TypeRole, "typeName"},
        {DropTargetRole, "dropTarget"},
    };
}

void DesktopModel::setCurrentIndex(int row)
{
    if (row < -1 || row >= m_entries.size()) {
        row = -1;
    }
    if (row != m_current) {
        m_current = row;
        Q_EMIT currentIndexChanged();
    }
}

void DesktopModel::setSortKey(const QString &key)
{
    if (key != m_sortKey) {
        m_sortKey = key;
        Q_EMIT sortChanged();
        resort();
    }
}

void DesktopModel::setSortDescending(bool on)
{
    if (on != m_sortDescending) {
        m_sortDescending = on;
        Q_EMIT sortChanged();
        resort();
    }
}

void DesktopModel::setAutoArrange(bool on)
{
    if (on == m_autoArrange) {
        return;
    }
    if (!on) {
        // Icons stay where auto arrange left them.
        for (const Entry &e : std::as_const(m_entries)) {
            m_positions.insert(e.key, e.cell);
        }
        savePositions();
    }
    m_autoArrange = on;
    Q_EMIT layoutOptionsChanged();
    scheduleLayout();
}

void DesktopModel::setAlignToGrid(bool on)
{
    if (on == m_alignToGrid) {
        return;
    }
    m_alignToGrid = on;
    if (on) {
        layout();
        for (const Entry &e : std::as_const(m_entries)) {
            m_positions.insert(e.key, e.cell);
        }
        savePositions();
    }
    Q_EMIT layoutOptionsChanged();
    scheduleLayout();
}

void DesktopModel::setCellWidth(qreal w)
{
    if (!qFuzzyCompare(w, m_cellWidth) && w > 0) {
        m_cellWidth = w;
        Q_EMIT geometryChanged();
        scheduleLayout();
    }
}

void DesktopModel::setCellHeight(qreal h)
{
    if (!qFuzzyCompare(h, m_cellHeight) && h > 0) {
        m_cellHeight = h;
        Q_EMIT geometryChanged();
        scheduleLayout();
    }
}

void DesktopModel::setItemWidth(qreal w)
{
    if (!qFuzzyCompare(w, m_itemWidth) && w > 0) {
        m_itemWidth = w;
        Q_EMIT geometryChanged();
        scheduleLayout();
    }
}

void DesktopModel::setItemHeight(qreal h)
{
    if (!qFuzzyCompare(h, m_itemHeight) && h > 0) {
        m_itemHeight = h;
        Q_EMIT geometryChanged();
    }
}

void DesktopModel::setAreaWidth(qreal w)
{
    if (!qFuzzyCompare(w, m_areaWidth)) {
        m_areaWidth = w;
        Q_EMIT geometryChanged();
        scheduleLayout();
    }
}

void DesktopModel::setAreaHeight(qreal h)
{
    if (!qFuzzyCompare(h, m_areaHeight)) {
        m_areaHeight = h;
        Q_EMIT geometryChanged();
        scheduleLayout();
    }
}

QString DesktopModel::positions() const
{
    QJsonObject object;
    for (auto it = m_positions.constBegin(); it != m_positions.constEnd(); ++it) {
        object.insert(it.key(), QJsonArray{it.value().x(), it.value().y()});
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void DesktopModel::setPositions(const QString &json)
{
    if (json == positions()) {
        return;
    }
    m_positions.clear();
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QJsonArray pair = it.value().toArray();
        if (pair.size() == 2) {
            m_positions.insert(it.key(), QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
        }
    }
    Q_EMIT positionsChanged();
    scheduleLayout();
}

void DesktopModel::savePositions()
{
    // Places of files that are gone are dropped, the system icons' kept.
    const QStringList keys = m_positions.keys();
    if (!m_listing) {
        QSet<QString> present;
        for (const Entry &e : std::as_const(m_entries)) {
            present.insert(e.key);
        }
        for (const QString &key : keys) {
            if (!present.contains(key) && !key.startsWith(QLatin1String("::"))) {
                m_positions.remove(key);
            }
        }
    }
    Q_EMIT positionsChanged();
}

QUrl DesktopModel::urlAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).url : QUrl();
}

QString DesktopModel::kindAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).kind : QString();
}

QString DesktopModel::fileNameAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).fileName : QString();
}

QString DesktopModel::nameAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).name : QString();
}

bool DesktopModel::isDirAt(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).isDir;
}

int DesktopModel::rowForUrl(const QUrl &url) const
{
    const QUrl wanted = stripped(url);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).kind == QLatin1String("item") && stripped(m_entries.at(i).url) == wanted) {
            return i;
        }
    }
    return -1;
}

bool DesktopModel::isSelected(int row) const
{
    return row >= 0 && row < m_entries.size() && m_selected.contains(m_entries.at(row).key);
}

QRectF DesktopModel::itemRect(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return {};
    }
    const Entry &e = m_entries.at(row);
    return QRectF(e.cell.x() * m_cellWidth + (m_cellWidth - m_itemWidth) / 2, e.cell.y() * m_cellHeight, m_itemWidth, m_itemHeight);
}

QList<QUrl> DesktopModel::selectedUrls() const
{
    QList<QUrl> urls;
    for (const Entry &e : std::as_const(m_entries)) {
        if (e.kind == QLatin1String("item") && m_selected.contains(e.key)) {
            urls.append(e.url);
        }
    }
    return urls;
}

QList<int> DesktopModel::selectedRows() const
{
    QList<int> rows;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_selected.contains(m_entries.at(i).key)) {
            rows.append(i);
        }
    }
    return rows;
}

bool DesktopModel::selectionHasSystemIcons() const
{
    return m_selected.contains(computerKey) || m_selected.contains(homeKey) || m_selected.contains(binKey);
}

void DesktopModel::setSelection(const QSet<QString> &keys)
{
    if (keys == m_selected) {
        return;
    }
    m_selected = keys;
    emitAll({SelectedRole});
    Q_EMIT selectionChanged();
}

void DesktopModel::select(int row, int modifiers)
{
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    const QString key = m_entries.at(row).key;
    if (modifiers & Qt::ShiftModifier && m_anchor >= 0 && m_anchor < m_entries.size()) {
        QSet<QString> keys = (modifiers & Qt::ControlModifier) ? m_selected : QSet<QString>();
        for (int i = std::min(m_anchor, row); i <= std::max(m_anchor, row); ++i) {
            keys.insert(m_entries.at(i).key);
        }
        setSelection(keys);
    } else if (modifiers & Qt::ControlModifier) {
        QSet<QString> keys = m_selected;
        if (!keys.remove(key)) {
            keys.insert(key);
        }
        setSelection(keys);
        m_anchor = row;
    } else {
        setSelection({key});
        m_anchor = row;
    }
    setCurrentIndex(row);
}

void DesktopModel::selectOnly(int row)
{
    select(row, 0);
}

void DesktopModel::clearSelection()
{
    setSelection({});
}

void DesktopModel::selectAll()
{
    QSet<QString> keys;
    for (const Entry &e : std::as_const(m_entries)) {
        keys.insert(e.key);
    }
    setSelection(keys);
}

void DesktopModel::beginRubberBand(bool additive)
{
    m_bandAdditive = additive;
    m_bandBase = additive ? m_selected : QSet<QString>();
    if (!additive) {
        setSelection({});
    }
}

void DesktopModel::updateRubberBand(const QRectF &rect)
{
    QSet<QString> keys = m_bandBase;
    int last = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!itemRect(i).intersects(rect)) {
            continue;
        }
        const QString &key = m_entries.at(i).key;
        // With Ctrl held, the band flips what it passes over.
        if (m_bandAdditive && m_bandBase.contains(key)) {
            keys.remove(key);
        } else {
            keys.insert(key);
        }
        last = i;
    }
    setSelection(keys);
    if (last >= 0) {
        setCurrentIndex(last);
        m_anchor = last;
    }
}

void DesktopModel::endRubberBand()
{
    m_bandBase.clear();
}

int DesktopModel::rowAt(qreal x, qreal y) const
{
    for (int i = int(m_entries.size()) - 1; i >= 0; --i) {
        if (itemRect(i).contains(QPointF(x, y))) {
            return i;
        }
    }
    return -1;
}

int DesktopModel::neighbour(int row, int key) const
{
    if (m_entries.isEmpty()) {
        return -1;
    }
    if (row < 0 || row >= m_entries.size()) {
        return 0;
    }
    const QPointF from = m_entries.at(row).cell;
    int best = -1;
    qreal bestScore = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i == row) {
            continue;
        }
        const QPointF d = m_entries.at(i).cell - from;
        qreal along = 0;
        qreal across = 0;
        switch (key) {
        case Qt::Key_Left:
            along = -d.x();
            across = d.y();
            break;
        case Qt::Key_Right:
            along = d.x();
            across = d.y();
            break;
        case Qt::Key_Up:
            along = -d.y();
            across = d.x();
            break;
        case Qt::Key_Down:
            along = d.y();
            across = d.x();
            break;
        default:
            return row;
        }
        if (along < 0.5) {
            continue;
        }
        const qreal score = along + 4 * std::abs(across);
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best < 0 ? row : best;
}

int DesktopModel::findByPrefix(const QString &prefix, int from) const
{
    if (prefix.isEmpty() || m_entries.isEmpty()) {
        return -1;
    }
    const int n = int(m_entries.size());
    for (int step = 1; step <= n; ++step) {
        const int i = ((from < 0 ? -1 : from) + step) % n;
        if (m_entries.at(i).name.startsWith(prefix, Qt::CaseInsensitive)) {
            return i;
        }
    }
    return -1;
}

void DesktopModel::moveSelection(int anchorRow, qreal dx, qreal dy)
{
    if (m_autoArrange || m_selected.isEmpty()) {
        return;
    }
    const int r = rows();
    const int c = columns();
    // Every icon is pinned where it is first, so moving one never makes
    // the others shuffle up into the gap.
    QSet<quint64> taken;
    for (const Entry &e : std::as_const(m_entries)) {
        m_positions.insert(e.key, e.cell);
        if (!m_selected.contains(e.key)) {
            taken.insert(cellKey(qRound(e.cell.x()), qRound(e.cell.y())));
        }
    }
    QList<int> order;
    if (anchorRow >= 0 && anchorRow < m_entries.size() && m_selected.contains(m_entries.at(anchorRow).key)) {
        order.append(anchorRow);
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i != anchorRow && m_selected.contains(m_entries.at(i).key)) {
            order.append(i);
        }
    }
    for (int i : std::as_const(order)) {
        const Entry &e = m_entries.at(i);
        QPointF target(e.cell.x() + dx / m_cellWidth, e.cell.y() + dy / m_cellHeight);
        target.setX(std::clamp(target.x(), 0.0, qreal(c - 1)));
        target.setY(std::clamp(target.y(), 0.0, qreal(r - 1)));
        if (m_alignToGrid) {
            QPoint cell(qRound(target.x()), qRound(target.y()));
            if (taken.contains(cellKey(cell.x(), cell.y()))) {
                cell = nearestFree(target, taken);
            }
            taken.insert(cellKey(cell.x(), cell.y()));
            m_positions.insert(e.key, cell);
        } else {
            m_positions.insert(e.key, target);
        }
    }
    layout();
    savePositions();
}

void DesktopModel::setPlaceHint(qreal x, qreal y)
{
    m_hint = QPointF(x, y);
    m_hintValid = true;
    m_hintTimer.start();
}

QPointF DesktopModel::hintCell() const
{
    return QPointF((m_hint.x() - m_cellWidth / 2) / m_cellWidth, (m_hint.y() - m_itemHeight / 2) / m_cellHeight);
}

void DesktopModel::sortBy(const QString &key)
{
    // Choosing the same order again turns it round, as on Windows.
    if (key == m_sortKey) {
        m_sortDescending = !m_sortDescending;
    } else {
        m_sortKey = key;
        m_sortDescending = false;
    }
    Q_EMIT sortChanged();
    for (const Entry &e : std::as_const(m_entries)) {
        m_positions.remove(e.key);
    }
    resort();
    layout();
    if (!m_autoArrange) {
        for (const Entry &e : std::as_const(m_entries)) {
            m_positions.insert(e.key, e.cell);
        }
    }
    savePositions();
}

void DesktopModel::refresh()
{
    makeSystemEntries();
    m_lister->openUrl(m_folder, KCoreDirLister::Reload);
    Q_EMIT countChanged();
}

void DesktopModel::onCreated(const QUrl &folder, const QList<QUrl> &urls, bool rename)
{
    if (stripped(folder) != stripped(m_folder)) {
        return;
    }
    for (const QUrl &url : urls) {
        m_pending.insert(stripped(url), rename && urls.size() == 1);
    }
    claimPending();
}

void DesktopModel::claimPending()
{
    if (m_pending.isEmpty()) {
        return;
    }
    QSet<QString> keys;
    int first = -1;
    int renameRow = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        if (e.kind != QLatin1String("item")) {
            continue;
        }
        const auto it = m_pending.constFind(stripped(e.url));
        if (it == m_pending.constEnd()) {
            continue;
        }
        keys.insert(e.key);
        if (first < 0) {
            first = i;
        }
        if (it.value()) {
            renameRow = i;
        }
        m_pending.erase(it);
    }
    if (keys.isEmpty()) {
        return;
    }
    setSelection(keys);
    setCurrentIndex(first);
    m_anchor = first;
    if (renameRow >= 0) {
        Q_EMIT renameRequested(renameRow);
    }
}
