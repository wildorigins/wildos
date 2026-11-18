#include "models.h"
#include "formats.h"

#include <QLocale>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const FolderEntry &e = m_entries.at(index.row());
    switch (role) {
    case KindRole:
        return e.kind == FolderEntry::Header ? QStringLiteral("header") : e.kind == FolderEntry::Create ? QStringLiteral("create") : QStringLiteral("folder");
    case AccountRole:
        return e.accountId;
    case FullNameRole:
        return e.fullName;
    case NameRole:
        return e.displayName;
    case FolderRoleRole:
        return e.role;
    case DepthRole:
        return e.depth;
    case UnreadRole:
        // Drafts and the Outbox count what is in them, the way the design
        // shows "Drafts 1"; everything else counts unread mail.
        return e.role == QLatin1String("drafts") || e.role == QLatin1String("outbox") ? e.total : e.unread;
    case ExpandedRole:
        return e.expanded;
    }
    return {};
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {AccountRole, "accountId"},
        {FullNameRole, "fullName"},
        {NameRole, "name"},
        {FolderRoleRole, "folderRole"},
        {DepthRole, "depth"},
        {UnreadRole, "count"},
        {ExpandedRole, "expanded"},
    };
}

int FolderModel::headerRow(const QString &accountId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).kind == FolderEntry::Header && m_entries.at(i).accountId == accountId) {
            return i;
        }
    }
    return -1;
}

void FolderModel::reset(const QList<FolderEntry> &entries)
{
    // Only the counts changed: update in place so the pane doesn't flicker.
    if (entries.size() == m_entries.size()) {
        bool same = true;
        for (int i = 0; i < entries.size() && same; ++i) {
            same = entries[i].kind == m_entries[i].kind && entries[i].accountId == m_entries[i].accountId
                && entries[i].fullName == m_entries[i].fullName && entries[i].expanded == m_entries[i].expanded
                && entries[i].displayName == m_entries[i].displayName;
        }
        if (same) {
            m_entries = entries;
            Q_EMIT dataChanged(index(0), index(m_entries.size() - 1), {UnreadRole});
            return;
        }
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

MessageModel::MessageModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_conversations = QSettings().value(QStringLiteral("mail/conversations"), false).toBool();
}

void MessageModel::setConversations(bool on)
{
    if (on == m_conversations) {
        return;
    }
    m_conversations = on;
    QSettings().setValue(QStringLiteral("mail/conversations"), on);
    beginResetModel();
    applyFilter();
    endResetModel();
    Q_EMIT conversationsChanged();
    Q_EMIT countChanged();
}

// The subject without its Re:, Fw: and their translations, which is how
// Outlook decides two messages belong to one conversation.
QString MessageModel::topic(const QString &subject)
{
    static const QRegularExpression prefix(QStringLiteral(R"(^\s*((re|fw|fwd|aw|wg|sv|vs|antw|tr|rif|r)\s*(\[\d+\])?\s*:\s*)+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    QString t = subject;
    t.remove(prefix);
    return t.simplified().toLower();
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    const MessageRow *r = rowAt(index.row());
    if (!r) {
        return {};
    }
    switch (role) {
    case KeyRole:
        return r->key();
    case FromRole:
        return r->draft || r->outgoing ? (r->to.isEmpty() ? tr("(No recipient)") : r->to) : r->fromName;
    case FromAddressRole:
        return r->outgoing ? r->toAddress : r->fromAddress;
    case InitialsRole:
        return r->outgoing ? initials(r->to.isEmpty() ? r->toAddress : r->to) : initials(r->fromName.isEmpty() ? r->fromAddress : r->fromName);
    case AvatarRole:
        return colourOf(r->outgoing ? r->toAddress : r->fromAddress);
    case SubjectRole:
        return r->subject.isEmpty() ? tr("(No subject)") : r->subject;
    case PreviewRole:
        return r->preview;
    case TimeRole:
        return timeLabel(r->date);
    case SectionRole:
        return sectionOf(r->date, r->pinned);
    case UnreadRole:
        return r->unread;
    case FlaggedRole:
        return r->flagged;
    case PinnedRole:
        return r->pinned;
    case AttachmentRole:
        return r->attachment;
    case DraftRole:
        return r->draft;
    case FolderNameRole:
        return folderName ? folderName(r->accountId, r->folder) : r->folder;
    case AccountRole:
        return r->accountId;
    case ThreadCountRole:
        return m_conversations ? m_threadCounts.value(topic(r->subject), 1) : 1;
    }
    return {};
}

QHash<int, QByteArray> MessageModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {FromRole, "from"},
        {FromAddressRole, "fromAddress"},
        {InitialsRole, "initials"},
        {AvatarRole, "avatar"},
        {SubjectRole, "subject"},
        {PreviewRole, "preview"},
        {TimeRole, "time"},
        {SectionRole, "section"},
        {UnreadRole, "unread"},
        {FlaggedRole, "flagged"},
        {PinnedRole, "pinned"},
        {AttachmentRole, "attachment"},
        {DraftRole, "draft"},
        {FolderNameRole, "folderName"},
        {AccountRole, "accountId"},
        {ThreadCountRole, "threadCount"},
    };
}

void MessageModel::setFilter(const QString &filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    beginResetModel();
    applyFilter();
    endResetModel();
    Q_EMIT filterChanged();
    Q_EMIT countChanged();
}

void MessageModel::reset(const QList<MessageRow> &rows)
{
    QList<MessageRow> sorted = rows;
    std::stable_sort(sorted.begin(), sorted.end(), [](const MessageRow &a, const MessageRow &b) {
        if (a.pinned != b.pinned) {
            return a.pinned;
        }
        return a.date > b.date;
    });

    QList<int> oldVisible = m_visible;
    QList<MessageRow> oldAll = m_all;
    m_all = sorted;
    QList<int> newVisible;
    std::swap(newVisible, m_visible);
    applyFilter();

    bool sameKeys = oldVisible.size() == m_visible.size();
    for (int i = 0; sameKeys && i < m_visible.size(); ++i) {
        sameKeys = oldAll[oldVisible[i]].key() == m_all[m_visible[i]].key();
    }
    if (sameKeys) {
        if (!m_visible.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(m_visible.size() - 1));
        }
    } else {
        beginResetModel();
        endResetModel();
    }
    Q_EMIT countChanged();
}

void MessageModel::removeKeys(const QStringList &keys)
{
    const QSet<QString> gone(keys.cbegin(), keys.cend());
    for (int i = m_visible.size() - 1; i >= 0; --i) {
        if (gone.contains(m_all.at(m_visible.at(i)).key())) {
            beginRemoveRows({}, i, i);
            m_visible.removeAt(i);
            endRemoveRows();
        }
    }
    QList<MessageRow> kept;
    QHash<int, int> newIndex;
    for (int i = 0; i < m_all.size(); ++i) {
        if (!gone.contains(m_all.at(i).key())) {
            newIndex.insert(i, kept.size());
            kept.append(m_all.at(i));
        }
    }
    for (int &v : m_visible) {
        v = newIndex.value(v);
    }
    m_all = kept;
    Q_EMIT countChanged();
}

void MessageModel::updateRow(const QString &key, const std::function<void(MessageRow &)> &change)
{
    for (int i = 0; i < m_all.size(); ++i) {
        if (m_all[i].key() == key) {
            change(m_all[i]);
            const int shown = m_visible.indexOf(i);
            if (shown >= 0) {
                Q_EMIT dataChanged(index(shown), index(shown));
            }
            return;
        }
    }
}

const MessageRow *MessageModel::row(const QString &key) const
{
    for (const MessageRow &r : m_all) {
        if (r.key() == key) {
            return &r;
        }
    }
    return nullptr;
}

const MessageRow *MessageModel::rowAt(int i) const
{
    if (i < 0 || i >= m_visible.size()) {
        return nullptr;
    }
    return &m_all.at(m_visible.at(i));
}

int MessageModel::indexOf(const QString &key) const
{
    for (int i = 0; i < m_visible.size(); ++i) {
        if (m_all.at(m_visible.at(i)).key() == key) {
            return i;
        }
    }
    return -1;
}

QString MessageModel::keyAt(int i) const
{
    const MessageRow *r = rowAt(i);
    return r ? r->key() : QString();
}

QString MessageModel::neighbour(const QString &key) const
{
    const int i = indexOf(key);
    if (i < 0) {
        return {};
    }
    if (i + 1 < m_visible.size()) {
        return keyAt(i + 1);
    }
    return i > 0 ? keyAt(i - 1) : QString();
}

void MessageModel::applyFilter()
{
    m_visible.clear();
    m_threadCounts.clear();
    QSet<QString> shown;
    const QDateTime now = QDateTime::currentDateTime();
    for (int i = 0; i < m_all.size(); ++i) {
        const MessageRow &r = m_all.at(i);
        if (r.snoozedUntil.isValid() && r.snoozedUntil > now) {
            continue;
        }
        if ((m_filter == QLatin1String("unread") && !r.unread) || (m_filter == QLatin1String("flagged") && !r.flagged)
            || (m_filter == QLatin1String("attachments") && !r.attachment)) {
            continue;
        }
        // m_all is newest first, so the first of each topic is the one
        // the conversation shows.
        if (m_conversations && !r.subject.trimmed().isEmpty()) {
            const QString t = topic(r.subject);
            m_threadCounts[t] += 1;
            if (shown.contains(t)) {
                continue;
            }
            shown.insert(t);
        }
        m_visible.append(i);
    }
}

int MessageModel::colourOf(const QString &address)
{
    quint32 h = 2166136261u;
    for (const QChar c : address.toLower()) {
        h = (h ^ c.unicode()) * 16777619u;
    }
    return int(h % 8);
}

QString MessageModel::initials(const QString &name)
{
    QString clean = name;
    clean.remove(QLatin1Char('"'));
    const int at = clean.indexOf(QLatin1Char('@'));
    if (at > 0) {
        clean = clean.left(at).replace(QLatin1Char('.'), QLatin1Char(' '));
    }
    const QStringList words = clean.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString out;
    for (const QString &w : words) {
        if (w.at(0).isLetterOrNumber()) {
            out += w.at(0).toUpper();
        }
        if (out.size() == 2) {
            break;
        }
    }
    if (out.isEmpty() && !clean.isEmpty()) {
        out = clean.left(1).toUpper();
    }
    return out;
}

QString MessageModel::timeLabel(const QDateTime &date)
{
    const QDate today = QDate::currentDate();
    const QDate d = date.date();
    if (d == today) {
        return Formats::time(date);
    }
    if (d == today.addDays(-1)) {
        return tr("Yesterday");
    }
    if (d > today.addDays(-7)) {
        return Formats::names().dayName(d.dayOfWeek(), QLocale::ShortFormat);
    }
    return Formats::shortDate(d);
}

QString MessageModel::sectionOf(const QDateTime &date, bool pinned)
{
    if (pinned) {
        return tr("Pinned");
    }
    const QDate today = QDate::currentDate();
    const QDate d = date.date();
    if (d == today) {
        return tr("Today");
    }
    if (d == today.addDays(-1)) {
        return tr("Yesterday");
    }
    // The last seven days, as the design groups Saturday and Friday under
    // "This week" on a Monday.
    if (d > today.addDays(-7)) {
        return tr("This week");
    }
    if (d > today.addDays(-14)) {
        return tr("Last week");
    }
    if (d.year() == today.year()) {
        return Formats::names().standaloneMonthName(d.month());
    }
    return QString::number(d.year());
}
