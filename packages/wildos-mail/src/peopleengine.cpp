#include "peopleengine.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

#include <sqlite3.h>

#include "senderimages.h"

#include <algorithm>
#include <memory>

Q_LOGGING_CATEGORY(peopleLog, "wildos.people", QtInfoMsg)

namespace
{
const QChar sep(0x1f);

QString str(const gchar *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

QString takeStr(gchar *s)
{
    const QString out = str(s);
    g_free(s);
    return out;
}

QString keyOf(const QString &book, const QString &uid)
{
    return book + sep + uid;
}

bool splitKey(const QString &key, QString *book, QString *uid)
{
    const int at = key.indexOf(sep);
    if (at < 0) {
        return false;
    }
    *book = key.left(at);
    *uid = key.mid(at + 1);
    return true;
}

QString everyone()
{
    EBookQuery *query = e_book_query_any_field_contains("");
    const QString sexp = takeStr(e_book_query_to_string(query));
    e_book_query_unref(query);
    return sexp;
}

QStringList stringList(GList *list)
{
    QStringList out;
    for (GList *l = list; l; l = l->next) {
        const QString s = str(static_cast<const gchar *>(l->data)).trimmed();
        if (!s.isEmpty()) {
            out << s;
        }
    }
    g_list_free_full(list, g_free);
    return out;
}

// The contact's photo as Mail's sender badge, written once per address to
// the photo cache under a name that changes with the photo, so a new photo
// is a new file and the list never shows a stale copy. Only photos on this
// PC are used: a photo given as a web address is left alone.
void savePhotos(EContact *contact, const QStringList &emails, QHash<QString, QString> *out)
{
    if (emails.isEmpty()) {
        return;
    }
    auto *photo = static_cast<EContactPhoto *>(e_contact_get(contact, E_CONTACT_PHOTO));
    if (!photo) {
        return;
    }
    QByteArray data;
    if (photo->type == E_CONTACT_PHOTO_TYPE_INLINED && photo->data.inlined.data) {
        data = QByteArray(reinterpret_cast<const char *>(photo->data.inlined.data), qsizetype(photo->data.inlined.length));
    } else if (photo->type == E_CONTACT_PHOTO_TYPE_URI && photo->data.uri) {
        const QUrl url(QString::fromUtf8(photo->data.uri));
        QFile file(url.toLocalFile());
        if (url.isLocalFile() && file.size() < 8 * 1024 * 1024 && file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
        }
    }
    e_contact_photo_free(photo);
    if (data.isEmpty()) {
        return;
    }
    const QString version = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(10));
    const QString dir = SenderImages::photoDir();
    QImage badge;
    for (const QString &email : emails) {
        const QString address = email.trimmed().toLower();
        if (!address.contains(QLatin1Char('@'))) {
            continue;
        }
        const QString name = QString::fromLatin1(QCryptographicHash::hash(address.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        const QString path = dir + QLatin1Char('/') + name + QLatin1Char('-') + version + QStringLiteral(".png");
        if (!QFileInfo::exists(path)) {
            if (badge.isNull()) {
                const QImage image = QImage::fromData(data);
                if (image.isNull()) {
                    return;
                }
                badge = SenderImages::photoBadge(image);
                QDir().mkpath(dir);
            }
            if (!badge.save(path, "PNG")) {
                continue;
            }
        }
        out->insert(address, path);
    }
}

// Each EMAIL or TEL line with the first of its TYPE parameters.
QList<QPair<QString, QString>> typedValues(EContact *contact, EContactField field)
{
    QList<QPair<QString, QString>> out;
    GList *attrs = e_contact_get_attributes(contact, field);
    for (GList *l = attrs; l; l = l->next) {
        auto *attr = static_cast<EVCardAttribute *>(l->data);
        const QString value = takeStr(e_vcard_attribute_get_value(attr)).trimmed();
        if (value.isEmpty()) {
            continue;
        }
        QString type;
        for (GList *t = e_vcard_attribute_get_param(attr, EVC_TYPE); t; t = t->next) {
            const QString v = str(static_cast<const gchar *>(t->data)).toUpper();
            // INTERNET, VOICE and PREF say nothing a person would read.
            if (v != QLatin1String("INTERNET") && v != QLatin1String("VOICE") && v != QLatin1String("PREF")) {
                type = v;
                break;
            }
        }
        out << qMakePair(type, value);
    }
    g_list_free_full(attrs, reinterpret_cast<GDestroyNotify>(e_vcard_attribute_free));
    return out;
}

void setTypedValues(EContact *contact, EContactField field, const char *attrName, const QVariantList &values)
{
    GList *attrs = nullptr;
    for (const QVariant &v : values) {
        const QVariantMap m = v.toMap();
        const QString value = m.value(QStringLiteral("value")).toString().trimmed();
        if (value.isEmpty()) {
            continue;
        }
        EVCardAttribute *attr = e_vcard_attribute_new(nullptr, attrName);
        const QString type = m.value(QStringLiteral("type")).toString();
        if (!type.isEmpty()) {
            e_vcard_attribute_add_param_with_value(attr, e_vcard_attribute_param_new(EVC_TYPE), type.toUtf8().constData());
        }
        e_vcard_attribute_add_value(attr, value.toUtf8().constData());
        attrs = g_list_append(attrs, attr);
    }
    e_contact_set_attributes(contact, field, attrs);
    g_list_free_full(attrs, reinterpret_cast<GDestroyNotify>(e_vcard_attribute_free));
}

QString displayName(EContact *contact)
{
    for (EContactField f : {E_CONTACT_FULL_NAME, E_CONTACT_FILE_AS, E_CONTACT_NICKNAME, E_CONTACT_ORG}) {
        const QString s = str(static_cast<const gchar *>(e_contact_get_const(contact, f))).trimmed();
        if (!s.isEmpty()) {
            return s;
        }
    }
    return {};
}

QStringList categoriesOf(EContact *contact)
{
    return stringList(static_cast<GList *>(e_contact_get(contact, E_CONTACT_CATEGORY_LIST)));
}

void setCategories(EContact *contact, const QStringList &categories)
{
    GList *list = nullptr;
    for (const QString &c : categories) {
        list = g_list_append(list, g_strdup(c.toUtf8().constData()));
    }
    e_contact_set(contact, E_CONTACT_CATEGORY_LIST, list);
    g_list_free_full(list, g_free);
}

QString initialsOf(const QString &name)
{
    const QStringList words = name.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QString out;
    for (const QString &w : words) {
        if (w.at(0).isLetterOrNumber()) {
            out += w.at(0).toUpper();
        }
        if (out.size() == 2) {
            break;
        }
    }
    return out;
}

// The same eight colours as a sender's initials in Mail (SenderBadge).
int colourOf(const QString &text)
{
    uint h = 0;
    for (const QChar c : text.toLower()) {
        h = h * 31 + c.unicode();
    }
    return int(h % 8);
}

QString letterOf(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || !n.at(0).isLetter()) {
        return QStringLiteral("#");
    }
    // Accents sort under their letter: Élan under E.
    return n.left(1).normalized(QString::NormalizationForm_D).left(1).toUpper();
}

// BDAY as "yyyy-MM-dd", or "--MM-dd" for a birthday kept without its
// year (vCard 4's form, which Google and iCloud both write), or empty.
QString birthdayOf(EContact *contact)
{
    EVCardAttribute *attr = e_vcard_get_attribute(E_VCARD(contact), EVC_BDAY);
    if (!attr) {
        return {};
    }
    QString v = takeStr(e_vcard_attribute_get_value(attr)).trimmed();
    v = v.section(QLatin1Char('T'), 0, 0);
    static const QRegularExpression full(QStringLiteral("^(\\d{4})-?(\\d{2})-?(\\d{2})$"));
    static const QRegularExpression partial(QStringLiteral("^--(\\d{2})-?(\\d{2})$"));
    if (const auto m = full.match(v); m.hasMatch()) {
        // Apple's stand-in year for a birthday without one.
        if (m.captured(1) == QLatin1String("1604") || m.captured(1) == QLatin1String("0000")) {
            return QStringLiteral("--%1-%2").arg(m.captured(2), m.captured(3));
        }
        return QStringLiteral("%1-%2-%3").arg(m.captured(1), m.captured(2), m.captured(3));
    }
    if (const auto m = partial.match(v); m.hasMatch()) {
        return QStringLiteral("--%1-%2").arg(m.captured(1), m.captured(2));
    }
    return {};
}

void setBirthday(EContact *contact, const QString &value)
{
    e_vcard_remove_attributes(E_VCARD(contact), nullptr, EVC_BDAY);
    if (value.isEmpty()) {
        return;
    }
    EVCardAttribute *attr = e_vcard_attribute_new(nullptr, EVC_BDAY);
    e_vcard_attribute_add_value(attr, value.toUtf8().constData());
    e_vcard_append_attribute(E_VCARD(contact), attr);
}

QList<QByteArray> splitCards(const QByteArray &data)
{
    QList<QByteArray> cards;
    static const QRegularExpression card(QStringLiteral("BEGIN:VCARD.*?END:VCARD"),
                                         QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    auto it = card.globalMatch(QString::fromUtf8(data));
    while (it.hasNext()) {
        cards << it.next().captured(0).toUtf8();
    }
    return cards;
}
}

QString PeopleEngine::favouriteCategory()
{
    return QStringLiteral("Favourites");
}

PeopleEngine *PeopleEngine::instance()
{
    static PeopleEngine *engine = new PeopleEngine();
    return engine;
}

PeopleEngine *PeopleEngine::create(QQmlEngine *, QJSEngine *)
{
    PeopleEngine *engine = instance();
    QJSEngine::setObjectOwnership(engine, QJSEngine::CppOwnership);
    return engine;
}

PeopleEngine::PeopleEngine(QObject *parent)
    : QObject(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(150);
    connect(&m_reloadTimer, &QTimer::timeout, this, &PeopleEngine::reloadPending);
    m_cancel = g_cancellable_new();
    e_source_registry_new(m_cancel, &PeopleEngine::onRegistryReady, this);
    m_googleTimer.setInterval(15 * 60 * 1000);
    m_googleTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_googleTimer, &QTimer::timeout, this, &PeopleEngine::refresh);
    m_googleTimer.start();
    m_syncHold.setSingleShot(true);
    m_syncHold.setInterval(700);
    connect(&m_syncHold, &QTimer::timeout, this, &PeopleEngine::syncingChanged);
}

PeopleEngine::~PeopleEngine()
{
    g_cancellable_cancel(m_cancel);
    for (EBookClientView *view : std::as_const(m_views)) {
        e_book_client_view_stop(view, nullptr);
        g_object_unref(view);
    }
    for (EBookClient *client : std::as_const(m_clients)) {
        if (client) {
            g_object_unref(client);
        }
    }
    g_clear_object(&m_registry);
    g_clear_object(&m_cancel);
}

void PeopleEngine::onRegistryReady(GObject *, GAsyncResult *result, gpointer data)
{
    auto *self = static_cast<PeopleEngine *>(data);
    GError *error = nullptr;
    ESourceRegistry *registry = e_source_registry_new_finish(result, &error);
    if (!registry) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            qCWarning(peopleLog) << "the address book service didn't start:" << (error ? error->message : "");
            self->m_problem = tr("The contacts service didn't start.");
            Q_EMIT self->readyChanged();
        }
        g_clear_error(&error);
        return;
    }
    self->m_registry = registry;
    for (const char *signal : {"source-added", "source-removed", "source-changed", "source-enabled", "source-disabled"}) {
        g_signal_connect(registry, signal, G_CALLBACK(&PeopleEngine::onSourceChanged), self);
    }
    self->rebuildBooks();
    Q_EMIT self->readyChanged();
}

void PeopleEngine::onSourceChanged(ESourceRegistry *, ESource *, gpointer data)
{
    auto *self = static_cast<PeopleEngine *>(data);
    QMetaObject::invokeMethod(self, &PeopleEngine::rebuildBooks, Qt::QueuedConnection);
}

void PeopleEngine::rebuildBooks()
{
    if (!m_registry) {
        return;
    }
    QList<Book> books;
    QSet<QString> present;
    QHash<QString, int> perAccount;
    GList *sources = e_source_registry_list_enabled(m_registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);
    for (GList *l = sources; l; l = l->next) {
        auto *source = E_SOURCE(l->data);
        Book b;
        b.uid = str(e_source_get_uid(source));
        b.name = str(e_source_get_display_name(source));
        const QString parentUid = str(e_source_get_parent(source));
        if (!parentUid.isEmpty() && !parentUid.endsWith(QLatin1String("-stub"))) {
            if (ESource *parent = e_source_registry_ref_source(m_registry, parentUid.toUtf8().constData())) {
                b.account = str(e_source_get_display_name(parent));
                g_object_unref(parent);
            }
        }
        if (b.uid == QLatin1String("system-address-book")) {
            b.name = tr("On this PC");
        }
        if (e_source_has_extension(source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
            auto *auth = static_cast<ESourceAuthentication *>(e_source_get_extension(source, E_SOURCE_EXTENSION_AUTHENTICATION));
            b.google = str(e_source_authentication_get_host(auth)).endsWith(QLatin1String("googleapis.com"));
        }
        EBookClient *c = m_clients.value(b.uid);
        b.readOnly = c && e_client_is_readonly(E_CLIENT(c));
        if (!b.account.isEmpty()) {
            ++perAccount[b.account];
        }
        books << b;
        present.insert(b.uid);
        if (!m_clients.contains(b.uid)) {
            connectClient(source);
        }
    }
    g_list_free_full(sources, g_object_unref);

    // One list per account, named after it; an account with several
    // address books names each after the account too.
    for (Book &b : books) {
        if (!b.account.isEmpty()) {
            b.name = perAccount.value(b.account) > 1 ? tr("%1, %2").arg(b.account, b.name) : b.account;
        }
    }
    std::stable_sort(books.begin(), books.end(), [](const Book &a, const Book &b) {
        // This PC's book last, the accounts A to Z before it.
        const bool la = a.account.isEmpty();
        const bool lb = b.account.isEmpty();
        if (la != lb) {
            return lb;
        }
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    bool dropped = false;
    for (const QString &uid : m_clients.keys()) {
        if (!present.contains(uid)) {
            dropClient(uid);
            dropped = true;
        }
    }
    m_books = books;
    if (dropped) {
        flatten();
    } else {
        ++m_revision;
        Q_EMIT contactsChanged();
    }
}

void PeopleEngine::connectClient(ESource *source)
{
    const QString uid = str(e_source_get_uid(source));
    m_clients.insert(uid, nullptr);
    e_book_client_connect(source, 30, m_cancel, &PeopleEngine::onClientConnected, new QString(uid));
}

void PeopleEngine::onClientConnected(GObject *, GAsyncResult *result, gpointer data)
{
    const QString uid = *static_cast<QString *>(data);
    delete static_cast<QString *>(data);
    GError *error = nullptr;
    EClient *client = e_book_client_connect_finish(result, &error);
    PeopleEngine *self = instance();
    if (!client) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            qCWarning(peopleLog) << "couldn't open address book" << uid << (error ? error->message : "");
            self->m_clients.remove(uid);
        }
        g_clear_error(&error);
        return;
    }
    if (!self->m_clients.contains(uid)) {
        g_object_unref(client);
        return;
    }
    self->m_clients.insert(uid, E_BOOK_CLIENT(client));
    self->rebuildBooks();
    self->startView(uid);
    self->reload(uid);
    for (const Book &b : std::as_const(self->m_books)) {
        if (b.uid == uid && b.google) {
            self->syncGoogle(E_BOOK_CLIENT(client), uid);
        }
    }
}

// Google's CardDAV server never changes its getctag, the "has anything
// changed" mark evolution-data-server compares before listing a book, so
// after the first sync no new or edited Google contact ever came down
// (its sync-token does change, but the CardDAV backend uses the ctag).
// Forgetting the stored mark makes the next refresh list the book in full;
// unchanged contacts are skipped by their etags, so it stays cheap.
void PeopleEngine::syncGoogle(EBookClient *client, const QString &uid)
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/evolution/addressbook/%1/cache.db").arg(uid);
    sqlite3 *db = nullptr;
    if (QFile::exists(path) && sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK) {
        sqlite3_busy_timeout(db, 2000);
        if (sqlite3_exec(db, "DELETE FROM ECacheKeys WHERE key = 'user::ebmb::sync-tag'", nullptr, nullptr, nullptr) != SQLITE_OK) {
            qCWarning(peopleLog) << "couldn't reset the Google sync mark" << sqlite3_errmsg(db);
        }
    }
    sqlite3_close(db);
    startRefresh(E_CLIENT(client));
}

void PeopleEngine::startRefresh(EClient *client)
{
    if (m_syncing++ == 0) {
        m_syncHold.start();
        Q_EMIT syncingChanged();
    }
    e_client_refresh(client, m_cancel, &PeopleEngine::onRefreshed, nullptr);
}

void PeopleEngine::onRefreshed(GObject *object, GAsyncResult *result, gpointer)
{
    GError *error = nullptr;
    e_client_refresh_finish(E_CLIENT(object), result, &error);
    const bool cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    if (cancelled) {
        return;
    }
    PeopleEngine *self = instance();
    if (--self->m_syncing == 0) {
        Q_EMIT self->syncingChanged();
    }
}


void PeopleEngine::dropClient(const QString &uid)
{
    if (EBookClientView *view = m_views.take(uid)) {
        g_signal_handlers_disconnect_by_data(view, this);
        e_book_client_view_stop(view, nullptr);
        g_object_unref(view);
    }
    if (EBookClient *client = m_clients.take(uid)) {
        g_object_unref(client);
    }
    m_perBook.remove(uid);
    SenderImages::instance()->setContactPhotos(uid, {});
}

// Only a sign that the book changed; its contacts are fetched again.
void PeopleEngine::startView(const QString &uid)
{
    EBookClient *client = m_clients.value(uid);
    if (!client) {
        return;
    }
    e_book_client_get_view(client, everyone().toUtf8().constData(), m_cancel, &PeopleEngine::onViewReady, new QString(uid));
}

void PeopleEngine::onViewReady(GObject *object, GAsyncResult *result, gpointer data)
{
    const QString uid = *static_cast<QString *>(data);
    delete static_cast<QString *>(data);
    EBookClientView *view = nullptr;
    GError *error = nullptr;
    if (!e_book_client_get_view_finish(E_BOOK_CLIENT(object), result, &view, &error)) {
        g_clear_error(&error);
        return;
    }
    PeopleEngine *self = instance();
    if (!self->m_clients.value(uid)) {
        g_object_unref(view);
        return;
    }
    if (EBookClientView *old = self->m_views.take(uid)) {
        g_signal_handlers_disconnect_by_data(old, self);
        e_book_client_view_stop(old, nullptr);
        g_object_unref(old);
    }
    // The first batch is what reload already has; only changes matter.
    e_book_client_view_set_flags(view, E_BOOK_CLIENT_VIEW_FLAGS_NONE, nullptr);
    for (const char *signal : {"objects-added", "objects-modified", "objects-removed"}) {
        g_signal_connect(view, signal, G_CALLBACK(&PeopleEngine::onViewObjects), self);
    }
    e_book_client_view_start(view, nullptr);
    self->m_views.insert(uid, view);
}

void PeopleEngine::onViewObjects(EBookClientView *view, gpointer, gpointer data)
{
    auto *self = static_cast<PeopleEngine *>(data);
    for (auto it = self->m_views.cbegin(); it != self->m_views.cend(); ++it) {
        if (it.value() == view) {
            self->scheduleReload(it.key());
            return;
        }
    }
}

void PeopleEngine::scheduleReload(const QString &uid)
{
    m_pending.insert(uid);
    m_reloadTimer.start();
}

void PeopleEngine::reloadPending()
{
    const QSet<QString> pending = m_pending;
    m_pending.clear();
    for (const QString &uid : pending) {
        reload(uid);
    }
}

void PeopleEngine::run(std::function<void()> work, std::function<void()> done)
{
    QPointer<PeopleEngine> self(this);
    QThreadPool::globalInstance()->start([self, work, done]() {
        work();
        if (done && self) {
            QMetaObject::invokeMethod(self, done, Qt::QueuedConnection);
        }
    });
}

void PeopleEngine::reload(const QString &uid)
{
    EBookClient *client = m_clients.value(uid);
    if (!client) {
        return;
    }
    g_object_ref(client);
    auto result = std::make_shared<QList<Person>>();
    auto photos = std::make_shared<QHash<QString, QString>>();
    const QString sexp = everyone();
    run([client, uid, sexp, result, photos]() {
        GSList *contacts = nullptr;
        GError *error = nullptr;
        if (!e_book_client_get_contacts_sync(client, sexp.toUtf8().constData(), &contacts, nullptr, &error)) {
            qCWarning(peopleLog) << "couldn't read address book" << uid << (error ? error->message : "");
            g_clear_error(&error);
        }
        const QString favourite = favouriteCategory();
        for (GSList *l = contacts; l; l = l->next) {
            auto *contact = E_CONTACT(l->data);
            // Contact lists are groups in Evolution; here a group is a category.
            if (e_contact_get(contact, E_CONTACT_IS_LIST)) {
                continue;
            }
            Person p;
            p.bookUid = uid;
            p.uid = str(static_cast<const gchar *>(e_contact_get_const(contact, E_CONTACT_UID)));
            p.key = keyOf(uid, p.uid);
            for (const auto &e : typedValues(contact, E_CONTACT_EMAIL)) {
                p.emails << e.second;
            }
            for (const auto &t : typedValues(contact, E_CONTACT_TEL)) {
                p.phones << t.second;
            }
            savePhotos(contact, p.emails, photos.get());
            p.name = displayName(contact);
            if (p.name.isEmpty()) {
                p.name = !p.emails.isEmpty() ? p.emails.first() : !p.phones.isEmpty() ? p.phones.first() : tr("(No name)");
            }
            const QStringList categories = categoriesOf(contact);
            p.favourite = categories.contains(favourite, Qt::CaseInsensitive);
            for (const QString &c : categories) {
                if (c.compare(favourite, Qt::CaseInsensitive) != 0) {
                    p.groups << c;
                }
            }
            result->append(p);
        }
        g_slist_free_full(contacts, g_object_unref);
        g_object_unref(client);
    }, [this, uid, result, photos]() {
        if (!m_clients.value(uid)) {
            return;
        }
        m_perBook.insert(uid, *result);
        SenderImages::instance()->setContactPhotos(uid, *photos);
        flatten();
    });
}

void PeopleEngine::flatten()
{
    m_contacts.clear();
    m_byKey.clear();
    for (const QList<Person> &list : std::as_const(m_perBook)) {
        m_contacts << list;
    }
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedKingdom));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setIgnorePunctuation(true);
    std::sort(m_contacts.begin(), m_contacts.end(), [&collator](const Person &a, const Person &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    for (int i = 0; i < m_contacts.size(); ++i) {
        m_byKey.insert(m_contacts[i].key, i);
    }
    ++m_revision;
    Q_EMIT contactsChanged();
}

EBookClient *PeopleEngine::client(const QString &uid) const
{
    return m_clients.value(uid);
}

const PeopleEngine::Person *PeopleEngine::find(const QString &key) const
{
    const int i = m_byKey.value(key, -1);
    return i >= 0 ? &m_contacts[i] : nullptr;
}

QVariantList PeopleEngine::books() const
{
    QHash<QString, int> counts;
    for (const Person &p : m_contacts) {
        ++counts[p.bookUid];
    }
    QVariantList out;
    for (const Book &b : m_books) {
        out << QVariantMap{
            {QStringLiteral("uid"), b.uid},
            {QStringLiteral("name"), b.name},
            {QStringLiteral("account"), b.account},
            {QStringLiteral("readOnly"), b.readOnly},
            {QStringLiteral("count"), counts.value(b.uid)},
        };
    }
    return out;
}

QVariantList PeopleEngine::groups() const
{
    QMap<QString, int> counts;
    QHash<QString, QString> spelling;
    for (const Person &p : m_contacts) {
        for (const QString &g : p.groups) {
            const QString k = g.toLower();
            ++counts[k];
            spelling.insert(k, g);
        }
    }
    for (const QString &g : QSettings().value(QStringLiteral("people/emptyGroups")).toStringList()) {
        const QString k = g.toLower();
        if (!counts.contains(k)) {
            counts.insert(k, 0);
            spelling.insert(k, g);
        }
    }
    QVariantList out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        out << QVariantMap{{QStringLiteral("name"), spelling.value(it.key())}, {QStringLiteral("count"), it.value()}};
    }
    return out;
}

int PeopleEngine::favourites() const
{
    return int(std::count_if(m_contacts.cbegin(), m_contacts.cend(), [](const Person &p) {
        return p.favourite;
    }));
}

QString PeopleEngine::defaultBook() const
{
    for (const Book &b : m_books) {
        if (!b.account.isEmpty() && !b.readOnly && m_clients.value(b.uid)) {
            return b.uid;
        }
    }
    return QStringLiteral("system-address-book");
}

QVariantList PeopleEngine::list(const QString &kind, const QString &value, const QString &text) const
{
    const QString needle = text.trimmed();
    QVariantList out;
    for (const Person &p : m_contacts) {
        if (kind == QLatin1String("favourites") && !p.favourite) {
            continue;
        }
        if (kind == QLatin1String("group") && !p.groups.contains(value, Qt::CaseInsensitive)) {
            continue;
        }
        if (kind == QLatin1String("book") && p.bookUid != value) {
            continue;
        }
        if (!needle.isEmpty()) {
            bool hit = p.name.contains(needle, Qt::CaseInsensitive);
            for (const QString &e : p.emails) {
                hit = hit || e.contains(needle, Qt::CaseInsensitive);
            }
            const QString digits = QString(needle).remove(QRegularExpression(QStringLiteral("[^0-9+]")));
            for (const QString &t : p.phones) {
                hit = hit || (!digits.isEmpty() && QString(t).remove(QRegularExpression(QStringLiteral("[^0-9+]"))).contains(digits));
            }
            if (!hit) {
                continue;
            }
        }
        out << QVariantMap{
            {QStringLiteral("key"), p.key},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("initials"), initialsOf(p.name)},
            {QStringLiteral("colour"), colourOf(p.emails.value(0, p.name))},
            {QStringLiteral("detail"), !p.emails.isEmpty() ? p.emails.first() : p.phones.value(0)},
            {QStringLiteral("favourite"), p.favourite},
            {QStringLiteral("letter"), letterOf(p.name)},
            {QStringLiteral("hasEmail"), !p.emails.isEmpty()},
            {QStringLiteral("hasPhone"), !p.phones.isEmpty()},
        };
    }
    return out;
}

QVariantMap PeopleEngine::contact(const QString &key) const
{
    QString bookUid, uid;
    if (!splitKey(key, &bookUid, &uid)) {
        return {};
    }
    EBookClient *c = client(bookUid);
    if (!c) {
        return {};
    }
    EContact *contact = nullptr;
    GError *error = nullptr;
    if (!e_book_client_get_contact_sync(c, uid.toUtf8().constData(), &contact, nullptr, &error)) {
        qCWarning(peopleLog) << "couldn't read contact:" << (error ? error->message : "");
        g_clear_error(&error);
        return {};
    }
    QVariantList emails, phones;
    for (const auto &e : typedValues(contact, E_CONTACT_EMAIL)) {
        emails << QVariantMap{{QStringLiteral("type"), e.first}, {QStringLiteral("value"), e.second}};
    }
    for (const auto &t : typedValues(contact, E_CONTACT_TEL)) {
        phones << QVariantMap{{QStringLiteral("type"), t.first}, {QStringLiteral("value"), t.second}};
    }
    QString given, family;
    if (auto *n = static_cast<EContactName *>(e_contact_get(contact, E_CONTACT_NAME))) {
        given = str(n->given);
        family = str(n->family);
        e_contact_name_free(n);
    }
    const QStringList categories = categoriesOf(contact);
    QStringList groups;
    for (const QString &g : categories) {
        if (g.compare(favouriteCategory(), Qt::CaseInsensitive) != 0) {
            groups << g;
        }
    }
    QString bookName;
    for (const Book &b : m_books) {
        if (b.uid == bookUid) {
            bookName = b.name;
        }
    }
    QString name = displayName(contact);
    QVariantMap m{
        {QStringLiteral("key"), key},
        {QStringLiteral("name"), name},
        {QStringLiteral("given"), given},
        {QStringLiteral("family"), family},
        {QStringLiteral("company"), str(static_cast<const gchar *>(e_contact_get_const(contact, E_CONTACT_ORG)))},
        {QStringLiteral("initials"), initialsOf(name)},
        {QStringLiteral("emails"), emails},
        {QStringLiteral("phones"), phones},
        {QStringLiteral("birthday"), birthdayOf(contact)},
        {QStringLiteral("notes"), str(static_cast<const gchar *>(e_contact_get_const(contact, E_CONTACT_NOTE)))},
        {QStringLiteral("groups"), groups},
        {QStringLiteral("favourite"), categories.contains(favouriteCategory(), Qt::CaseInsensitive)},
        {QStringLiteral("bookUid"), bookUid},
        {QStringLiteral("bookName"), bookName},
        {QStringLiteral("readOnly"), e_client_is_readonly(E_CLIENT(c)) != FALSE},
    };
    const Person *p = find(key);
    m.insert(QStringLiteral("colour"), colourOf(p && !p->emails.isEmpty() ? p->emails.first() : name));
    g_object_unref(contact);
    return m;
}

namespace
{
void fill(EContact *contact, const QVariantMap &data)
{
    const QString name = data.value(QStringLiteral("name")).toString().simplified();
    e_contact_set(contact, E_CONTACT_FULL_NAME, name.isEmpty() ? nullptr : name.toUtf8().constData());
    e_contact_set(contact, E_CONTACT_FILE_AS, name.isEmpty() ? nullptr : name.toUtf8().constData());
    EContactName *n = e_contact_name_from_string(name.toUtf8().constData());
    e_contact_set(contact, E_CONTACT_NAME, n);
    e_contact_name_free(n);
    const QString company = data.value(QStringLiteral("company")).toString().trimmed();
    e_contact_set(contact, E_CONTACT_ORG, company.isEmpty() ? nullptr : company.toUtf8().constData());
    setTypedValues(contact, E_CONTACT_EMAIL, EVC_EMAIL, data.value(QStringLiteral("emails")).toList());
    setTypedValues(contact, E_CONTACT_TEL, EVC_TEL, data.value(QStringLiteral("phones")).toList());
    const QString notes = data.value(QStringLiteral("notes")).toString().trimmed();
    e_contact_set(contact, E_CONTACT_NOTE, notes.isEmpty() ? nullptr : notes.toUtf8().constData());
    if (data.contains(QStringLiteral("birthday"))) {
        setBirthday(contact, data.value(QStringLiteral("birthday")).toString());
    }
    if (data.contains(QStringLiteral("groups"))) {
        QStringList categories = data.value(QStringLiteral("groups")).toStringList();
        if (data.value(QStringLiteral("favourite")).toBool()) {
            categories << PeopleEngine::favouriteCategory();
        }
        setCategories(contact, categories);
    }
}
}

void PeopleEngine::saveContact(const QVariantMap &data)
{
    const QString key = data.value(QStringLiteral("key")).toString();
    if (!key.isEmpty()) {
        change(key, [data](EContact *contact) {
            fill(contact, data);
        });
        return;
    }
    QString bookUid = data.value(QStringLiteral("bookUid")).toString();
    if (bookUid.isEmpty()) {
        bookUid = defaultBook();
    }
    EBookClient *c = client(bookUid);
    if (!c) {
        Q_EMIT notice(tr("That address book isn't open yet. Try again in a moment."));
        return;
    }
    g_object_ref(c);
    auto added = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    run([c, data, added, error]() {
        EContact *contact = e_contact_new();
        fill(contact, data);
        gchar *uid = nullptr;
        GError *err = nullptr;
        if (e_book_client_add_contact_sync(c, contact, E_BOOK_OPERATION_FLAG_NONE, &uid, nullptr, &err)) {
            *added = takeStr(uid);
        } else {
            *error = str(err ? err->message : "");
        }
        g_clear_error(&err);
        g_object_unref(contact);
        g_object_unref(c);
    }, [this, bookUid, added, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(tr("The contact wasn't saved. %1").arg(*error));
            return;
        }
        scheduleReload(bookUid);
        Q_EMIT saved(keyOf(bookUid, *added));
    });
}

void PeopleEngine::change(const QString &key, std::function<void(EContact *)> edit)
{
    QString bookUid, uid;
    if (!splitKey(key, &bookUid, &uid)) {
        return;
    }
    EBookClient *c = client(bookUid);
    if (!c) {
        return;
    }
    g_object_ref(c);
    auto error = std::make_shared<QString>();
    run([c, uid, edit, error]() {
        EContact *contact = nullptr;
        GError *err = nullptr;
        if (e_book_client_get_contact_sync(c, uid.toUtf8().constData(), &contact, nullptr, &err)) {
            edit(contact);
            if (!e_book_client_modify_contact_sync(c, contact, E_BOOK_OPERATION_FLAG_NONE, nullptr, &err)) {
                *error = str(err ? err->message : "");
            }
            g_object_unref(contact);
        } else {
            *error = str(err ? err->message : "");
        }
        g_clear_error(&err);
        g_object_unref(c);
    }, [this, key, bookUid, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(tr("The contact wasn't saved. %1").arg(*error));
            return;
        }
        scheduleReload(bookUid);
        Q_EMIT saved(key);
    });
}

void PeopleEngine::removeContacts(const QStringList &keys)
{
    QHash<QString, QStringList> perBook;
    for (const QString &key : keys) {
        QString bookUid, uid;
        if (splitKey(key, &bookUid, &uid) && client(bookUid)) {
            perBook[bookUid] << uid;
        }
    }
    for (auto it = perBook.cbegin(); it != perBook.cend(); ++it) {
        const QString bookUid = it.key();
        const QStringList uids = it.value();
        // Gone from the list at once; the service catches up.
        if (m_perBook.contains(bookUid)) {
            auto &list = m_perBook[bookUid];
            list.erase(std::remove_if(list.begin(), list.end(), [&uids](const Person &p) {
                return uids.contains(p.uid);
            }), list.end());
        }
        EBookClient *c = client(bookUid);
        g_object_ref(c);
        auto error = std::make_shared<QString>();
        run([c, uids, error]() {
            for (const QString &uid : uids) {
                GError *err = nullptr;
                if (!e_book_client_remove_contact_by_uid_sync(c, uid.toUtf8().constData(), E_BOOK_OPERATION_FLAG_NONE, nullptr, &err)) {
                    *error = str(err ? err->message : "");
                }
                g_clear_error(&err);
            }
            g_object_unref(c);
        }, [this, bookUid, error]() {
            if (!error->isEmpty()) {
                Q_EMIT notice(tr("Not every contact was deleted. %1").arg(*error));
            }
            scheduleReload(bookUid);
        });
    }
    flatten();
}

void PeopleEngine::setFavourite(const QString &key, bool favourite)
{
    if (m_byKey.contains(key)) {
        m_contacts[m_byKey.value(key)].favourite = favourite;
        ++m_revision;
        Q_EMIT contactsChanged();
    }
    change(key, [favourite](EContact *contact) {
        QStringList categories = categoriesOf(contact);
        categories.removeIf([](const QString &c) {
            return c.compare(favouriteCategory(), Qt::CaseInsensitive) == 0;
        });
        if (favourite) {
            categories << favouriteCategory();
        }
        setCategories(contact, categories);
    });
}

void PeopleEngine::addToGroup(const QString &key, const QString &group)
{
    const QString g = group.trimmed();
    if (g.isEmpty() || g.compare(favouriteCategory(), Qt::CaseInsensitive) == 0) {
        return;
    }
    change(key, [g](EContact *contact) {
        QStringList categories = categoriesOf(contact);
        if (!categories.contains(g, Qt::CaseInsensitive)) {
            categories << g;
            setCategories(contact, categories);
        }
    });
    QStringList empty = QSettings().value(QStringLiteral("people/emptyGroups")).toStringList();
    if (empty.removeAll(g) > 0) {
        QSettings().setValue(QStringLiteral("people/emptyGroups"), empty);
    }
}

void PeopleEngine::removeFromGroup(const QString &key, const QString &group)
{
    change(key, [group](EContact *contact) {
        QStringList categories = categoriesOf(contact);
        categories.removeIf([&group](const QString &c) {
            return c.compare(group, Qt::CaseInsensitive) == 0;
        });
        setCategories(contact, categories);
    });
}

void PeopleEngine::addGroup(const QString &name)
{
    const QString g = name.trimmed();
    if (g.isEmpty() || g.compare(favouriteCategory(), Qt::CaseInsensitive) == 0) {
        return;
    }
    QStringList empty = QSettings().value(QStringLiteral("people/emptyGroups")).toStringList();
    if (!empty.contains(g, Qt::CaseInsensitive)) {
        empty << g;
        QSettings().setValue(QStringLiteral("people/emptyGroups"), empty);
    }
    ++m_revision;
    Q_EMIT contactsChanged();
}

QString PeopleEngine::vcard(const QString &key) const
{
    QString bookUid, uid;
    EBookClient *c = splitKey(key, &bookUid, &uid) ? client(bookUid) : nullptr;
    if (!c) {
        return {};
    }
    EContact *contact = nullptr;
    if (!e_book_client_get_contact_sync(c, uid.toUtf8().constData(), &contact, nullptr, nullptr)) {
        return {};
    }
    const QString out = takeStr(e_vcard_to_string(E_VCARD(contact), EVC_FORMAT_VCARD_30));
    g_object_unref(contact);
    return out;
}

bool PeopleEngine::exportTo(const QStringList &keys, const QUrl &url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT notice(tr("The file couldn't be written."));
        return false;
    }
    int written = 0;
    for (const QString &key : keys) {
        const QString card = vcard(key);
        if (!card.isEmpty()) {
            file.write(card.toUtf8());
            file.write("\r\n");
            ++written;
        }
    }
    file.close();
    Q_EMIT notice(written == 1 ? tr("1 contact saved to %1").arg(file.fileName()) : tr("%1 contacts saved to %2").arg(written).arg(file.fileName()));
    return written > 0;
}

QUrl PeopleEngine::shareFile(const QString &key) const
{
    const QString card = vcard(key);
    const Person *p = find(key);
    if (card.isEmpty() || !p) {
        return {};
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/share");
    QDir().mkpath(dir);
    QString name = p->name;
    name.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral(" "));
    QFile file(dir + QLatin1Char('/') + name.simplified() + QStringLiteral(".vcf"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(card.toUtf8());
    return QUrl::fromLocalFile(file.fileName());
}

void PeopleEngine::importFrom(const QUrl &url, const QString &bookUid)
{
    const QString target = bookUid.isEmpty() ? defaultBook() : bookUid;
    EBookClient *c = client(target);
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!c || !file.open(QIODevice::ReadOnly)) {
        Q_EMIT notice(tr("The file couldn't be read."));
        return;
    }
    const QList<QByteArray> cards = splitCards(file.readAll());
    if (cards.isEmpty()) {
        Q_EMIT notice(tr("There are no contacts in that file."));
        return;
    }
    g_object_ref(c);
    auto added = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();
    run([c, cards, added, error]() {
        GSList *contacts = nullptr;
        for (const QByteArray &card : cards) {
            if (EContact *contact = e_contact_new_from_vcard(card.constData())) {
                // A new UID each, so importing the same file twice can't
                // overwrite cards already here.
                e_contact_set(contact, E_CONTACT_UID, nullptr);
                contacts = g_slist_append(contacts, contact);
            }
        }
        GError *err = nullptr;
        GSList *uids = nullptr;
        if (e_book_client_add_contacts_sync(c, contacts, E_BOOK_OPERATION_FLAG_NONE, &uids, nullptr, &err)) {
            *added = int(g_slist_length(uids));
        } else {
            *error = str(err ? err->message : "");
        }
        g_slist_free_full(uids, g_free);
        g_clear_error(&err);
        g_slist_free_full(contacts, g_object_unref);
        g_object_unref(c);
    }, [this, target, added, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(tr("The contacts weren't imported. %1").arg(*error));
            return;
        }
        scheduleReload(target);
        Q_EMIT notice(*added == 1 ? tr("1 contact imported") : tr("%1 contacts imported").arg(*added));
    });
}

int PeopleEngine::writeVcf(const QString &path, QString *error)
{
    GError *err = nullptr;
    ESourceRegistry *registry = e_source_registry_new_sync(nullptr, &err);
    if (!registry) {
        *error = str(err ? err->message : "");
        g_clear_error(&err);
        return -1;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = tr("Mail couldn't write to %1.").arg(QFileInfo(path).absolutePath());
        g_object_unref(registry);
        return -1;
    }
    const QByteArray sexp = everyone().toUtf8();
    int written = 0;
    GList *sources = e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);
    for (GList *l = sources; l; l = l->next) {
        EClient *client = e_book_client_connect_sync(E_SOURCE(l->data), 30, nullptr, nullptr);
        if (!client) {
            continue;
        }
        GSList *contacts = nullptr;
        if (e_book_client_get_contacts_sync(E_BOOK_CLIENT(client), sexp.constData(), &contacts, nullptr, nullptr)) {
            for (GSList *c = contacts; c; c = c->next) {
                file.write(takeStr(e_vcard_to_string(E_VCARD(c->data), EVC_FORMAT_VCARD_30)).toUtf8());
                file.write("\r\n");
                ++written;
            }
            g_slist_free_full(contacts, g_object_unref);
        }
        g_object_unref(client);
    }
    g_list_free_full(sources, g_object_unref);
    g_object_unref(registry);
    return written;
}

int PeopleEngine::readVcf(const QString &path, const QString &bookUid, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = tr("The file couldn't be read.");
        return -1;
    }
    const QList<QByteArray> cards = splitCards(file.readAll());
    if (cards.isEmpty()) {
        return 0;
    }
    GError *err = nullptr;
    ESourceRegistry *registry = e_source_registry_new_sync(nullptr, &err);
    ESource *source = registry ? e_source_registry_ref_source(registry, bookUid.toUtf8().constData()) : nullptr;
    EClient *client = source ? e_book_client_connect_sync(source, 30, nullptr, &err) : nullptr;
    g_clear_object(&source);
    g_clear_object(&registry);
    if (!client) {
        *error = err ? str(err->message) : tr("The address book couldn't be opened.");
        g_clear_error(&err);
        return -1;
    }
    GSList *contacts = nullptr;
    for (const QByteArray &card : cards) {
        if (EContact *contact = e_contact_new_from_vcard(card.constData())) {
            // A new UID each, as importFrom gives them.
            e_contact_set(contact, E_CONTACT_UID, nullptr);
            contacts = g_slist_append(contacts, contact);
        }
    }
    int added = -1;
    GSList *uids = nullptr;
    if (e_book_client_add_contacts_sync(E_BOOK_CLIENT(client), contacts, E_BOOK_OPERATION_FLAG_NONE, &uids, nullptr, &err)) {
        added = int(g_slist_length(uids));
    } else {
        *error = str(err ? err->message : "");
    }
    g_slist_free_full(uids, g_free);
    g_clear_error(&err);
    g_slist_free_full(contacts, g_object_unref);
    g_object_unref(client);
    return added;
}

QStringList PeopleEngine::accountAddresses() const
{
    QStringList out;
    if (!m_registry) {
        return out;
    }
    GList *collections = e_source_registry_list_enabled(m_registry, E_SOURCE_EXTENSION_COLLECTION);
    for (GList *l = collections; l; l = l->next) {
        auto *ext = E_SOURCE_COLLECTION(e_source_get_extension(E_SOURCE(l->data), E_SOURCE_EXTENSION_COLLECTION));
        if (e_source_collection_get_contacts_enabled(ext)) {
            out << takeStr(e_source_collection_dup_identity(ext)).toLower();
        }
    }
    g_list_free_full(collections, g_object_unref);
    return out;
}

void PeopleEngine::refresh()
{
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        EBookClient *c = it.value();
        if (!c) {
            continue;
        }
        const bool google = std::any_of(m_books.cbegin(), m_books.cend(), [&](const Book &b) {
            return b.uid == it.key() && b.google;
        });
        if (google) {
            syncGoogle(c, it.key());
        } else {
            startRefresh(E_CLIENT(c));
        }
    }
}
