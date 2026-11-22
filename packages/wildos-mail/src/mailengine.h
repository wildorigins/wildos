#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <camel/camel.h>

#include <atomic>
#include <functional>
#include <memory>

#include "accounts.h"
#include "models.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(mailLog)

class QQmlEngine;
class QJSEngine;

// An import, export, backup or restore under way, shared with the worker
// thread doing it: the worker counts and names what it is on, the window
// pauses or cancels it.
struct Transfer {
    QString kind;
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<bool> paused{false};
    std::atomic<bool> cancelled{false};
    mutable QMutex lock;
    QString title;
    QString account;
    void setTitle(const QString &text, const QString &address = {});
    // Waits while paused; false once cancelled.
    bool carryOn() const;
};

// Everything Mail knows about accounts, folders and messages, on top of
// Camel (evolution-data-server's mail library). Camel's calls block, so they
// run on worker threads; their results come back to the window's thread as
// plain rows for the models.
class MailEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList accounts READ accountList NOTIFY accountsChanged)
    Q_PROPERTY(bool hasAccounts READ hasAccounts NOTIFY accountsChanged)
    Q_PROPERTY(FolderModel *folders READ folders CONSTANT)
    Q_PROPERTY(MessageModel *messages READ messages CONSTANT)
    Q_PROPERTY(QString folderTitle READ folderTitle NOTIFY folderChanged)
    Q_PROPERTY(QString folderRole READ folderRole NOTIFY folderChanged)
    Q_PROPERTY(QString folderAccount READ folderAccount NOTIFY folderChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY folderChanged)
    Q_PROPERTY(QString selected READ selected WRITE select NOTIFY selectedChanged)
    // Every selected message: one, or several picked with Ctrl and Shift.
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int trackersThisWeek READ trackersThisWeek NOTIFY trackersChanged)
    Q_PROPERTY(int photosCleaned READ photosCleaned NOTIFY trackersChanged)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(int outboxCount READ outboxCount NOTIFY outboxChanged)
    Q_PROPERTY(QString syncStatus READ syncStatus NOTIFY syncStatusChanged)
    Q_PROPERTY(bool passwordInWallet READ passwordInWallet CONSTANT)

public:
    // True while a refresh asked of the accounts is still running, held for
    // at least a moment so a quick one still shows the icon turning.
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    // Rules for mail arriving in an Inbox, applied on this PC in order, the
    // first that matches winning: [{name, enabled, field (from, to or
    // subject), contains, action (move, read, flag, archive or delete),
    // accountId and folder for move}]. Kept in rules.json beside
    // accounts.json.
    Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
    // The account new mail is sent from unless another is chosen ("Send new
    // mail from this account"); the first account when none is set.
    Q_PROPERTY(QString defaultAccount READ defaultAccount WRITE setDefaultAccount NOTIFY accountsChanged)
    // Import, export, backup or restore under way (one at a time): what it
    // is doing ("Importing Old laptop.mbox"), empty when idle; its kind
    // (import, export, backup or restore), the address mail is going into
    // or coming out of, and how far it is (total 0 while still counting).
    Q_PROPERTY(QString transferStatus READ transferStatus NOTIFY transferChanged)
    Q_PROPERTY(QString transferKind READ transferKind NOTIFY transferChanged)
    Q_PROPERTY(QString transferAccount READ transferAccount NOTIFY transferChanged)
    Q_PROPERTY(int transferDone READ transferDone NOTIFY transferChanged)
    Q_PROPERTY(int transferTotal READ transferTotal NOTIFY transferChanged)
    Q_PROPERTY(bool transferPaused READ transferPaused NOTIFY transferChanged)
    // Named signatures (MailMsSetSignatures.dc): [{id, name, html}], in
    // signatures.json beside accounts.json.
    Q_PROPERTY(QVariantList signatures READ signatures NOTIFY signaturesChanged)
    static MailEngine *instance();
    static MailEngine *create(QQmlEngine *, QJSEngine *);

    QVariantList accountList() const;
    bool hasAccounts() const
    {
        return !m_store.accounts().isEmpty();
    }
    FolderModel *folders() const
    {
        return m_folderModel;
    }
    MessageModel *messages() const
    {
        return m_messageModel;
    }
    QString folderTitle() const;
    QString folderRole() const
    {
        return m_folderRole;
    }
    QString folderAccount() const
    {
        return m_folderAccount;
    }
    QString folderName() const
    {
        return m_folderFullName;
    }
    QString selected() const
    {
        return m_selected;
    }
    void select(const QString &key);
    QStringList selection() const
    {
        return m_selection;
    }
    bool canUndo() const
    {
        return !m_undoStack.isEmpty();
    }
    bool online() const
    {
        return m_online;
    }
    bool loading() const
    {
        return m_loading > 0;
    }
    int trackersThisWeek() const;
    QVariantList problems() const;
    QString searchText() const
    {
        return m_searchText;
    }
    bool searching() const
    {
        return !m_searchText.isEmpty();
    }
    int outboxCount() const;
    QString syncStatus() const
    {
        return m_syncStatus;
    }
    bool passwordInWallet() const
    {
        return AccountStore::walletExists();
    }

    // Favourites have no account: openFolder("", "inbox") shows every
    // account's inbox together.
    Q_INVOKABLE void openFolder(const QString &accountId, const QString &fullName);
    // The folder that was open when Mail last closed, or the Inbox.
    Q_INVOKABLE void openLastFolder();
    Q_INVOKABLE void toggleExpanded(const QString &accountId);
    // The folder pane's order of accounts: this one goes to place `to`,
    // counting from 0, and stays there the next time Mail opens.
    Q_INVOKABLE void moveAccount(const QString &accountId, int to);
    Q_INVOKABLE int accountIndex(const QString &accountId) const;
    // Ctrl+click adds or takes away one message; Shift+click takes
    // everything from the last one clicked.
    Q_INVOKABLE void toggleSelect(const QString &key);
    Q_INVOKABLE void extendSelect(const QString &key);
    Q_INVOKABLE void selectAll();

    // The same actions on several messages at once.
    Q_INVOKABLE void removeKeys(const QStringList &keys);
    Q_INVOKABLE void archiveKeys(const QStringList &keys);
    Q_INVOKABLE void junkKeys(const QStringList &keys);
    Q_INVOKABLE void moveKeys(const QStringList &keys, const QString &fullName);
    Q_INVOKABLE void setReadKeys(const QStringList &keys, bool read);
    Q_INVOKABLE void setFlaggedKeys(const QStringList &keys, bool flagged);
    // Puts the last delete, archive or move back.
    Q_INVOKABLE void undo();

    // A folder's right-click menu.
    Q_INVOKABLE void markFolderRead(const QString &accountId, const QString &fullName);
    Q_INVOKABLE void renameFolder(const QString &accountId, const QString &fullName, const QString &name);
    Q_INVOKABLE void deleteFolder(const QString &accountId, const QString &fullName);
    Q_INVOKABLE void emptyFolder(const QString &accountId, const QString &fullName);
    Q_INVOKABLE void refresh();
    bool syncing() const { return m_syncing > 0 || m_syncHold.isActive(); }
    int m_syncing = 0;
    QTimer m_syncHold;

    Q_INVOKABLE void setRead(const QString &key, bool read);
    Q_INVOKABLE void toggleRead(const QString &key);
    Q_INVOKABLE void toggleFlag(const QString &key);
    Q_INVOKABLE void togglePin(const QString &key);
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE void archive(const QString &key);
    Q_INVOKABLE void reportJunk(const QString &key);
    Q_INVOKABLE void notJunk(const QString &key);
    Q_INVOKABLE void moveTo(const QString &key, const QString &fullName);
    Q_INVOKABLE void snooze(const QString &key, const QDateTime &until);
    Q_INVOKABLE QVariantList moveTargets(const QString &accountId) const;
    Q_INVOKABLE void createFolder(const QString &accountId, const QString &name);
    Q_INVOKABLE QString accountForKey(const QString &key) const;

    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void clearSearch();
    // For People and Tasks, from the summaries on this PC as search is,
    // without touching the message list. Mail from or to an address,
    // newest first, answered with foundWith: [{key, subject, preview,
    // time, unread}].
    Q_INVOKABLE void findWith(const QString &address, int limit);
    // Every flagged message, answered with flaggedFound: [{key, subject,
    // from, date}].
    Q_INVOKABLE void findFlagged();

    // Account setup. testAccount connects with the settings given and
    // answers through accountTested; saveAccount keeps an account that
    // passed.
    Q_INVOKABLE void testAccount(const QVariantMap &settings, const QString &password);
    Q_INVOKABLE void saveAccount(const QVariantMap &settings, const QString &password);
    Q_INVOKABLE void updateAccount(const QVariantMap &settings);
    Q_INVOKABLE void removeAccount(const QString &id);
    Q_INVOKABLE void signInAgain(const QString &id, const QString &password);
    // The calendars, contacts and task lists of an account signed in with
    // Microsoft, from the sign-in Mail already keeps.
    Q_INVOKABLE void addMicrosoftCalendar(const QString &id);
    QVariantList rules() const
    {
        return m_rules;
    }
    Q_INVOKABLE void setRules(const QVariantList &rules);
    // The rules applied to everything in the account's Inbox now (every
    // account's when empty), not only to new mail.
    Q_INVOKABLE void runRules(const QString &accountId = {});
    // One rule run on the Inbox now, whether or not it is on.
    Q_INVOKABLE void runRule(const QString &id);
    // How many messages in the account's Inbox a rule (as being edited) fits.
    Q_INVOKABLE int countRuleMatches(const QVariantMap &rule) const;
    // Automatic replies: from Microsoft's server for a Microsoft account,
    // otherwise sent by Mail. Answered with autoReplyLoaded and
    // autoReplySaved: {enabled, message, start, end, outside, server,
    // personal, needsSignIn, error}.
    QString defaultAccount() const;
    void setDefaultAccount(const QString &id);
    QVariantList signatures() const
    {
        return m_signatures;
    }
    Q_INVOKABLE QString addSignature(const QString &name);
    Q_INVOKABLE void saveSignature(const QString &id, const QString &name, const QString &html);
    Q_INVOKABLE void removeSignature(const QString &id);
    // The signature an account adds to a new message or a reply, as HTML.
    QString signatureFor(const QString &accountId, bool reply) const;
    QString transferStatus() const;
    QString transferKind() const;
    QString transferAccount() const;
    int transferDone() const;
    int transferTotal() const;
    bool transferPaused() const;
    Q_INVOKABLE void pauseTransfer(bool paused);
    // Stops after the message it is on; what already went across stays.
    Q_INVOKABLE void cancelTransfer();
    // Mail from Outlook data files (.pst, through readpst), mbox files
    // (Thunderbird keeps each folder as one), .eml files, or a folder of
    // any of them, copied into an "Imported" folder of the account with
    // their folders kept, so it lands on its server too.
    Q_INVOKABLE void importMail(const QString &accountId, const QList<QUrl> &files);
    // The other mail apps on this PC whose mail can be brought in:
    // [{id, name}] for Thunderbird and Evolution when they hold any.
    Q_INVOKABLE QVariantList mailApps() const;
    // Every mail folder of that app into Imported, under the app's name.
    Q_INVOKABLE void importApp(const QString &accountId, const QString &appId);
    // The folders chosen as .mbox files, which every mail app opens, in a
    // folder per account inside the folder chosen: {accountId: [full
    // names]}, an empty list for every folder of that account.
    Q_INVOKABLE void exportMail(const QVariantMap &choice, const QUrl &folder);
    // One .tar.gz with every account's mail as mbox, contacts as vCard,
    // calendars as iCalendar, the accounts' settings, rules, signatures and
    // Mail's settings. No password or sign-in token goes in it.
    Q_INVOKABLE void backUp(const QUrl &file);
    // Puts a backup back alongside what is here: rules, signatures and
    // settings are added to, mail goes into Imported in the account with
    // the same address, contacts and events are added.
    Q_INVOKABLE void restore(const QUrl &file);
    Q_INVOKABLE void loadAutoReply(const QString &accountId);
    Q_INVOKABLE void saveAutoReply(const QString &accountId, const QVariantMap &settings);
    Q_INVOKABLE QVariantMap account(const QString &id) const;
    Q_INVOKABLE static QString initialsOf(const QString &name)
    {
        return MessageModel::initials(name);
    }
    // The person's name from their Wild OS account, to start "Your name"
    // with.
    Q_INVOKABLE static QString systemFullName();
    Q_INVOKABLE static int colourOf(const QString &address)
    {
        return MessageModel::colourOf(address);
    }

    // Called by the message view when it keeps trackers out of a message;
    // each message counts once, however often it is opened.
    void countTrackers(int blocked, const QString &messageId);
    int photosCleaned() const;
    void countCleanedPhotos(int photos);

    // For the message view and the composer, on a worker thread.
    CamelFolder *folderSync(const QString &accountId, const QString &fullName, GError **error);
    CamelFolder *roleFolderSync(const QString &accountId, const QString &role, GError **error);
    CamelService *transport(const QString &accountId) const;
    const Account *accountData(const QString &id) const
    {
        return m_store.find(id);
    }
    const MessageRow *row(const QString &key) const
    {
        return m_messageModel->row(key);
    }
    static bool splitKey(const QString &key, QString *accountId, QString *folder, QString *uid);
    // A failure as a plain sentence; the server's own words go to the log.
    static QString plainError(const GError *error, const QString &otherwise);
    QString folderLabel(const QString &accountId, const QString &fullName) const;
    void queueOutbox(const QString &accountId, const QByteArray &message);
    void sendOutbox();

    // Runs work on a worker thread, then done on this one.
    void run(std::function<void()> work, std::function<void()> done = {});

Q_SIGNALS:
    void accountsChanged();
    void folderChanged();
    void selectedChanged();
    void onlineChanged();
    void loadingChanged();
    void trackersChanged();
    void problemsChanged();
    void rulesChanged();
    void transferChanged();
    void signaturesChanged();
    void transferFinished(bool ok, const QString &message);
    void autoReplyLoaded(const QString &accountId, const QVariantMap &settings);
    void autoReplySaved(const QString &accountId, bool ok, const QString &message, bool needsSignIn);
    void searchChanged();
    void outboxChanged();
    void syncStatusChanged();
    void syncingChanged();
    void accountTested(bool ok, const QString &message);
    void accountSaved(const QString &id);
    void notice(const QString &text);
    // A notice with Undo.
    void undoable(const QString &text);
    void undoChanged();
    void newMail(const QString &key, const QString &from, const QString &subject, const QString &preview);
    void foundWith(const QString &address, const QVariantList &messages);
    void flaggedFound(const QVariantList &messages);

private:
    struct Live {
        CamelStore *store = nullptr;
        CamelService *smtp = nullptr;
        QString problem;
        // ok, password or unreachable
        QString problemKind;
        bool connected = false;
        QList<CamelFolderInfo *> infos;
        QList<FolderEntry> folders;
        QSet<QString> knownInboxUids;
        bool inboxPrimed = false;
        // The first folder list after connecting fetches the open folders.
        bool fetchedAfterConnect = false;
    };

    explicit MailEngine(QObject *parent = nullptr);

    void startAccount(const Account &account);
    void stopAccount(const QString &id);
    void connectAccount(const QString &id);
    void configure(CamelService *store, CamelService *smtp, const Account &account) const;
    void listFolders(const QString &id);
    void rebuildFolderModel();
    void reloadMessages();
    void scheduleReload();
    void watchFolder(CamelFolder *folder);
    void watchStore(CamelStore *store);
    QList<QPair<QString, QString>> currentFolders() const;
    void transfer(const QStringList &keys, const QString &targetRole, const QString &targetFullName, bool junkFlag);
    struct Moved {
        QString accountId;
        QString from;
        QString to;
        QString uid;
    };
    // The last ten moves, newest last. Each can be undone the moment it is
    // made: its rows come back on screen at once, and if the server is still
    // moving them they go back as soon as it has.
    struct UndoBatch {
        int id = 0;
        QList<MessageRow> rows;
        QList<Moved> moved;
        bool done = false;
        bool undone = false;
    };
    QList<UndoBatch> m_undoStack;
    QList<UndoBatch> m_undoing;
    int m_nextUndoId = 1;
    // Rows put back by Undo, shown until the server has them again.
    QHash<QString, MessageRow> m_pendingBack;
    void moveBack(const UndoBatch &batch);
    QStringList m_selection;
    QString m_anchor;
    void setFlags(const QString &key, guint32 mask, guint32 set);
    void setOnline(bool online);
    void setProblem(const QString &id, const QString &kind, const QString &text);
    void setSyncStatus(const QString &status);
    void checkNewMail(const QString &accountId, const QList<MessageRow> &inboxRows);
    // Applies the rules to these Inbox messages; returns the ones a rule
    // took care of, which get no notification.
    QSet<QString> applyRules(const QList<MessageRow> &rows, const QString &onlyRule = {});
    void loadRules();
    void sendAutoReplies(const QString &accountId, const QList<MessageRow> &arrived, const QSet<QString> &handled);
    QVariantList m_rules;
    QVariantList m_signatures;
    void loadSignatures();
    void writeSignatures() const;
    std::shared_ptr<Transfer> m_transfer;
    QTimer m_transferTick;
    // Starts a transfer unless one is under way.
    std::shared_ptr<Transfer> startTransfer(const QString &kind, const QString &title, const QString &account);
    void finishTransfer(bool ok, const QString &message);
    void importRoots(const QString &accountId, const QList<QPair<QString, QStringList>> &roots, const QString &label);
    void restoreSettings(const QString &dir, QStringList *notes);
    QHash<QString, QList<MessageRow>> m_lastInbox;
    // Set while rules move mail: no toast, no undo, selection left alone.
    bool m_quiet = false;
    // Messages moved or deleted on screen whose server move is still under
    // way: reloads leave them out, so they never flash back into the list.
    QSet<QString> m_pendingGone;
    // The last rows read from each folder ("account\x1ffolder"), shown at
    // once when the folder is viewed again while it is read afresh, so one
    // slow account never holds up the others in a combined view.
    QHash<QString, QList<MessageRow>> m_folderRows;
    void showFolderRows(const QString &fresh = {});
    QString roleFullName(const QString &accountId, const QString &role) const;

    static void onFolderChanged(CamelFolder *folder, CamelFolderChangeInfo *info, gpointer self);
    static void onStoreChanged(CamelStore *store, gpointer arg, gpointer self);

    AccountStore m_store;
    FolderModel *m_folderModel;
    MessageModel *m_messageModel;
    QHash<QString, Live> m_live;
    QHash<QString, bool> m_expanded;

    mutable QMutex m_folderLock;
    QHash<QString, CamelFolder *> m_folderCache;
    QSet<CamelFolder *> m_watched;

    QString m_folderAccount;
    QString m_folderFullName;
    QString m_folderRole = QStringLiteral("inbox");
    // The folder from last time is open, and its account hasn't listed its
    // folders yet to say what it is or whether it is still there.
    bool m_restoringFolder = false;
    QString m_selected;
    QString m_searchText;
    QString m_syncStatus;
    int m_generation = 0;
    int m_loading = 0;
    bool m_online = true;
    QTimer m_reloadTimer;
    QTimer m_pollTimer;
    QHash<QString, QDateTime> m_lastCheck;
    void pollDue();
    void refreshAccount(const QString &id);
    QTimer m_snoozeTimer;
};
