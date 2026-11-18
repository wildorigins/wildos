#include "entrymodel.h"

#include "format.h"

#include <QCollator>
#include <QCoreApplication>

EntryModel::EntryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant EntryModel::data(const QModelIndex &index, int role) const
{
    if (!m_listing || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const int i = m_rows[index.row()];
    const Entry &e = m_listing->entries[i];
    switch (role) {
    case IndexRole:
        return i;
    case NameRole:
        return m_searching && !e.unsafe ? e.path : e.name;
    case IsDirRole:
        return e.dir;
    case SizeRole:
        return e.dir ? QString() : Format::size(e.size);
    case PackedRole:
        return e.dir || e.packed < 0 ? QString() : Format::size(e.packed);
    case ModifiedRole:
        return Format::date(e.modified);
    case TypeRole:
        if (e.unsafe) {
            return QCoreApplication::translate("EntryModel", "Unsafe path");
        }
        return e.symlink ? QCoreApplication::translate("EntryModel", "Link") : Format::typeName(e.name, e.dir);
    case KindRole:
        if (e.unsafe) {
            return QStringLiteral("unsafe");
        }
        if (e.dir) {
            return QStringLiteral("folder");
        }
        if (e.symlink) {
            return QStringLiteral("link");
        }
        return e.image ? QStringLiteral("image") : QStringLiteral("file");
    case CheckedRole:
        return m_checked.contains(i);
    case ThumbRole:
        return m_thumbs.contains(i) ? QStringLiteral("image://archivethumb/%1/%2").arg(m_thumbKey).arg(e.order) : QString();
    case LocationRole:
        return e.parent >= 0 ? m_listing->entries[e.parent].path : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> EntryModel::roleNames() const
{
    return {
        {IndexRole, "entry"},
        {NameRole, "name"},
        {IsDirRole, "isDir"},
        {SizeRole, "sizeText"},
        {PackedRole, "packedText"},
        {ModifiedRole, "modifiedText"},
        {TypeRole, "typeText"},
        {KindRole, "kind"},
        {CheckedRole, "checked"},
        {ThumbRole, "thumb"},
        {LocationRole, "location"},
    };
}

void EntryModel::setListing(const Listing *listing, const QString &thumbKey)
{
    beginResetModel();
    m_listing = listing;
    m_thumbKey = thumbKey;
    m_shown.clear();
    m_rows.clear();
    m_checked.clear();
    m_thumbs.clear();
    endResetModel();
}

void EntryModel::show(const QList<int> &entries, bool searching)
{
    beginResetModel();
    m_shown = entries;
    m_searching = searching;
    m_checked.clear();
    sortRows();
    endResetModel();
}

void EntryModel::setSort(const QString &key, bool ascending)
{
    m_sortKey = key;
    m_ascending = ascending;
    Q_EMIT layoutAboutToBeChanged();
    sortRows();
    Q_EMIT layoutChanged();
}

void EntryModel::sortRows()
{
    m_rows = m_shown;
    if (!m_listing) {
        return;
    }
    const QList<Entry> &all = m_listing->entries;
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const QString key = m_sortKey;
    const bool up = m_ascending;
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](int a, int b) {
        const Entry &x = all[a];
        const Entry &y = all[b];
        if (x.dir != y.dir) {
            return x.dir;
        }
        int c = 0;
        if (key == QLatin1String("name")) {
            c = collator.compare(x.name, y.name);
        } else if (key == QLatin1String("size")) {
            c = x.size < y.size ? -1 : x.size > y.size ? 1 : 0;
        } else if (key == QLatin1String("packed")) {
            c = x.packed < y.packed ? -1 : x.packed > y.packed ? 1 : 0;
        } else if (key == QLatin1String("modified")) {
            c = x.modified < y.modified ? -1 : x.modified > y.modified ? 1 : 0;
        } else if (key == QLatin1String("type")) {
            c = collator.compare(Format::typeName(x.name, x.dir), Format::typeName(y.name, y.dir));
        } else {
            return x.order >= 0 && y.order >= 0 ? x.order < y.order : a < b;
        }
        if (c == 0) {
            return collator.compare(x.name, y.name) < 0;
        }
        return up ? c < 0 : c > 0;
    });
}

void EntryModel::thumbnailReady(int entry)
{
    m_thumbs.insert(entry);
    const int row = rowOf(entry);
    if (row >= 0) {
        const QModelIndex at = index(row);
        Q_EMIT dataChanged(at, at, {ThumbRole});
    }
}

int EntryModel::entryAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row] : -1;
}

int EntryModel::rowOf(int entry) const
{
    return int(m_rows.indexOf(entry));
}

bool EntryModel::isChecked(int row) const
{
    return m_checked.contains(entryAt(row));
}

void EntryModel::setChecked(const QSet<int> &entries)
{
    m_checked = entries;
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {CheckedRole});
    }
}
