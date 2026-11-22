#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include <libebook/libebook.h>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(peopleLog)

class QQmlEngine;
class QJSEngine;

// People (MailPeople.dc), on evolution-data-server's address book service:
// every address book in its source registry, the one on this PC and each
// account's (Google, iCloud and other CardDAV servers, added in Calendar's
// accounts, which bring contacts with them). The service does the syncing;
// this keeps the contacts in memory for the lists and hands the service
// what was changed.
//
// vCard has no field for a favourite, so a favourite carries the category
// "Favourites" (favouriteCategory), which CardDAV servers keep with the
// card. Groups are categories too, as Evolution and Thunderbird make them.
class PeopleEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // True while a refresh asked of the accounts is still running, held for
    // at least a moment so a quick one still shows the icon turning.
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY readyChanged)
    // [{uid, name, account, readOnly, count}]: one per address book.
    Q_PROPERTY(QVariantList books READ books NOTIFY contactsChanged)
    // [{name, count}]: every category but the favourites one, A to Z.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY contactsChanged)
    Q_PROPERTY(int total READ total NOTIFY contactsChanged)
    Q_PROPERTY(int favourites READ favourites NOTIFY contactsChanged)
    // Goes up whenever any contact changes; lists bind to it.
    Q_PROPERTY(int revision READ revision NOTIFY contactsChanged)
    // Where new contacts go: the first account's book, else this PC's.
    Q_PROPERTY(QString defaultBook READ defaultBook NOTIFY contactsChanged)

public:
    static PeopleEngine *instance();
    static PeopleEngine *create(QQmlEngine *, QJSEngine *);
    ~PeopleEngine() override;

    static QString favouriteCategory();

    bool ready() const
    {
        return m_registry != nullptr;
    }
    QString problem() const
    {
        return m_problem;
    }
    QVariantList books() const;
    QVariantList groups() const;
    int total() const
    {
        return int(m_contacts.size());
    }
    int favourites() const;
    int revision() const
    {
        return m_revision;
    }
    QString defaultBook() const;

    // The middle list: kind is all, favourites, group or book, value the
    // group's name or the book's uid; text narrows it to names, addresses
    // and numbers containing it. A to Z, each row with its letter:
    // [{key, name, initials, colour, detail, favourite, letter, hasEmail,
    // hasPhone}].
    Q_INVOKABLE QVariantList list(const QString &kind, const QString &value, const QString &text) const;
    // One contact in full: name, given, family, emails [{type, value}],
    // phones [{type, value}], birthday ("yyyy-MM-dd", "--MM-dd" without a
    // year, or empty), notes, groups,
    // favourite, bookUid, bookName, readOnly, company.
    Q_INVOKABLE QVariantMap contact(const QString &key) const;
    // Creates or changes a contact; data as contact() gives it, key empty
    // for a new one. Answered with saved(key).
    Q_INVOKABLE void saveContact(const QVariantMap &data);
    Q_INVOKABLE void removeContacts(const QStringList &keys);
    Q_INVOKABLE void setFavourite(const QString &key, bool favourite);
    Q_INVOKABLE void addToGroup(const QString &key, const QString &group);
    Q_INVOKABLE void removeFromGroup(const QString &key, const QString &group);
    // A group with nobody in it yet is remembered here until someone is.
    Q_INVOKABLE void addGroup(const QString &name);

    // vCard 3.0 files: one card per contact, several to a file.
    Q_INVOKABLE QString vcard(const QString &key) const;
    Q_INVOKABLE bool exportTo(const QStringList &keys, const QUrl &file);
    // A card written where Mail can attach it, for Share contact.
    Q_INVOKABLE QUrl shareFile(const QString &key) const;
    // Adds every card in the file to the book; answered with notice.
    Q_INVOKABLE void importFrom(const QUrl &file, const QString &bookUid);
    // For Mail's backup and restore, which run on a worker thread: every
    // contact in every address book to one vCard file, and every card in a
    // file into a book. Each blocks, asks the service itself rather than
    // this engine's lists (which may not have loaded yet), and gives how
    // many contacts it moved, or -1 with error set.
    static int writeVcf(const QString &path, QString *error);
    static int readVcf(const QString &path, const QString &bookUid, QString *error);

    // The address books' accounts, to offer a Google account's contacts.
    Q_INVOKABLE QStringList accountAddresses() const;
    Q_INVOKABLE void refresh();
    bool syncing() const { return m_syncing > 0 || m_syncHold.isActive(); }
    // Counts a refresh in and out; the callback ends it.
    void startRefresh(EClient *client);
    static void onRefreshed(GObject *, GAsyncResult *result, gpointer);
    int m_syncing = 0;
    QTimer m_syncHold;

Q_SIGNALS:
    void readyChanged();
    void contactsChanged();
    void syncingChanged();
    void notice(const QString &text);
    void saved(const QString &key);

private:
    explicit PeopleEngine(QObject *parent = nullptr);

    struct Person {
        QString key;
        QString bookUid;
        QString uid;
        QString name;
        QString sortName;
        QStringList emails;
        QStringList phones;
        QStringList groups;
        bool favourite = false;
    };
    struct Book {
        QString uid;
        QString name;
        QString account;
        bool readOnly = false;
        bool google = false;
    };

    static void onRegistryReady(GObject *, GAsyncResult *result, gpointer self);
    static void onSourceChanged(ESourceRegistry *, ESource *, gpointer self);
    static void onClientConnected(GObject *, GAsyncResult *result, gpointer data);
    static void onViewReady(GObject *, GAsyncResult *result, gpointer data);
    static void onViewObjects(EBookClientView *, gpointer, gpointer self);

    void rebuildBooks();
    void connectClient(ESource *source);
    void dropClient(const QString &uid);
    void startView(const QString &uid);
    void scheduleReload(const QString &uid);
    void reloadPending();
    void reload(const QString &uid);
    void flatten();
    void run(std::function<void()> work, std::function<void()> done);
    // Fetches the contact, changes it with edit on a worker thread and
    // hands it back to the service.
    void change(const QString &key, std::function<void(EContact *)> edit);
    EBookClient *client(const QString &uid) const;
    const Person *find(const QString &key) const;

    ESourceRegistry *m_registry = nullptr;
    GCancellable *m_cancel = nullptr;
    QString m_problem;
    QHash<QString, EBookClient *> m_clients;
    QHash<QString, EBookClientView *> m_views;
    QList<Book> m_books;
    QHash<QString, QList<Person>> m_perBook;
    QList<Person> m_contacts;
    QHash<QString, int> m_byKey;
    QSet<QString> m_pending;
    QTimer m_reloadTimer;
    // Google contacts are pulled on this schedule while People is open.
    QTimer m_googleTimer;
    void syncGoogle(EBookClient *client, const QString &uid);
    int m_revision = 0;
};
