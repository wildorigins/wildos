#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <QtQml/qqmlregistration.h>

// One line of the folder pane: an account (or Favourites) heading, a folder
// under it, or the "Create new folder" link at the end of an account.
struct FolderEntry {
    enum Kind { Header, Folder, Create };
    Kind kind = Folder;
    // Empty for Favourites, which gather the same folder from every account.
    QString accountId;
    QString fullName;
    QString displayName;
    // inbox, drafts, sent, archive, trash, junk, outbox or folder
    QString role;
    int depth = 0;
    int unread = 0;
    int total = 0;
    bool expanded = true;
};

class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Mail makes these itself")

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        AccountRole,
        FullNameRole,
        NameRole,
        FolderRoleRole,
        DepthRole,
        UnreadRole,
        ExpandedRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QList<FolderEntry> &entries);
    // The row of an account's heading, for keeping the keyboard on it.
    Q_INVOKABLE int headerRow(const QString &accountId) const;
    const QList<FolderEntry> &entries() const
    {
        return m_entries;
    }

private:
    QList<FolderEntry> m_entries;
};

// A message as the list shows it, read from Camel's summary so the list
// never waits for the network.
struct MessageRow {
    QString accountId;
    QString folder;
    QString uid;
    QString fromName;
    QString fromAddress;
    QString to;
    QString subject;
    QString preview;
    QDateTime date;
    bool unread = false;
    bool flagged = false;
    bool pinned = false;
    bool attachment = false;
    bool draft = false;
    // Sent mail and drafts are listed by who they're to, as in Outlook.
    bool outgoing = false;
    QString toAddress;
    QDateTime snoozedUntil;

    QString key() const
    {
        return accountId + QLatin1Char('\x1f') + folder + QLatin1Char('\x1f') + uid;
    }
};

class MessageModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Mail makes these itself")
    // The list's filter button: all, unread, flagged or attachments. One
    // list, newest first, no conversations: the classic Outlook view.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    // Settings > Mail > Group messages into conversations, off by default:
    // one row per conversation topic, showing its newest message.
    Q_PROPERTY(bool conversations READ conversations WRITE setConversations NOTIFY conversationsChanged)

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        FromRole,
        FromAddressRole,
        InitialsRole,
        AvatarRole,
        SubjectRole,
        PreviewRole,
        TimeRole,
        SectionRole,
        UnreadRole,
        FlaggedRole,
        PinnedRole,
        AttachmentRole,
        DraftRole,
        FolderNameRole,
        AccountRole,
        ThreadCountRole,
    };

    explicit MessageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const
    {
        return m_filter;
    }
    void setFilter(const QString &filter);
    int total() const
    {
        return m_all.size();
    }
    bool conversations() const
    {
        return m_conversations;
    }
    void setConversations(bool on);
    static QString topic(const QString &subject);

    // Replaces every row; keeps the view where it was when the same rows
    // come back with a flag changed.
    void reset(const QList<MessageRow> &rows);
    const MessageRow *row(const QString &key) const;
    // Changes shown at once, before the server has done them; the next
    // reload from Camel confirms or corrects them.
    void removeKeys(const QStringList &keys);
    void updateRow(const QString &key, const std::function<void(MessageRow &)> &change);
    const MessageRow *rowAt(int index) const;
    Q_INVOKABLE int indexOf(const QString &key) const;
    Q_INVOKABLE QString keyAt(int index) const;
    Q_INVOKABLE bool unreadAt(int index) const
    {
        const MessageRow *r = rowAt(index);
        return r && r->unread;
    }
    Q_INVOKABLE bool flaggedAt(int index) const
    {
        const MessageRow *r = rowAt(index);
        return r && r->flagged;
    }
    // The row after (or before, at the end) the one going away, for moving
    // the selection along when a message is deleted or archived.
    QString neighbour(const QString &key) const;

    // How a folder is named in search results.
    std::function<QString(const QString &accountId, const QString &folder)> folderName;

    static QString initials(const QString &name);
    // The same sender gets the same colour every time Mail opens.
    static int colourOf(const QString &address);
    static QString timeLabel(const QDateTime &date);
    static QString sectionOf(const QDateTime &date, bool pinned);

Q_SIGNALS:
    void filterChanged();
    void conversationsChanged();
    void countChanged();

private:
    void applyFilter();

    QString m_filter = QStringLiteral("all");
    bool m_conversations = false;
    QHash<QString, int> m_threadCounts;
    QList<MessageRow> m_all;
    QList<int> m_visible;
};
