#include "mailengine.h"

#include <QDir>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QFile>
#include <QNetworkInformation>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>

#include "camelsession.h"

#include <gio/gio.h>

#include <pwd.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(mailLog, "wildos.mail", QtInfoMsg)

namespace
{
const QChar keySep = QLatin1Char('\x1f');

QString str(const gchar *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

CamelNetworkSecurityMethod securityOf(const QString &security)
{
    if (security == QLatin1String("ssl")) {
        return CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT;
    }
    if (security == QLatin1String("starttls")) {
        return CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT;
    }
    return CAMEL_NETWORK_SECURITY_METHOD_NONE;
}

// The folder's job, from the server's special-use flags or, for servers
// that don't send them, from its name.
QString roleOf(const CamelFolderInfo *fi, const QString &provider)
{
    switch (fi->flags & CAMEL_FOLDER_TYPE_MASK) {
    case CAMEL_FOLDER_TYPE_INBOX:
        return QStringLiteral("inbox");
    case CAMEL_FOLDER_TYPE_SENT:
        return QStringLiteral("sent");
    case CAMEL_FOLDER_TYPE_DRAFTS:
        return QStringLiteral("drafts");
    case CAMEL_FOLDER_TYPE_TRASH:
        return QStringLiteral("trash");
    case CAMEL_FOLDER_TYPE_JUNK:
        return QStringLiteral("junk");
    case CAMEL_FOLDER_TYPE_ARCHIVE:
        return QStringLiteral("archive");
    case CAMEL_FOLDER_TYPE_ALL:
        // Gmail archives by taking the Inbox label off, which leaves the
        // message in All Mail.
        return provider == QLatin1String("gmail") ? QStringLiteral("archive") : QStringLiteral("all");
    default:
        break;
    }
    const QString name = str(fi->display_name).toLower();
    const QString full = str(fi->full_name).toLower();
    if (full == QLatin1String("inbox")) {
        return QStringLiteral("inbox");
    }
    if (name == QLatin1String("sent") || name == QLatin1String("sent items") || name == QLatin1String("sent messages") || name == QLatin1String("sent mail")) {
        return QStringLiteral("sent");
    }
    if (name == QLatin1String("drafts") || name == QLatin1String("draft")) {
        return QStringLiteral("drafts");
    }
    if (name == QLatin1String("trash") || name == QLatin1String("deleted items") || name == QLatin1String("deleted messages") || name == QLatin1String("bin")) {
        return QStringLiteral("trash");
    }
    if (name == QLatin1String("junk") || name == QLatin1String("spam") || name == QLatin1String("junk e-mail") || name == QLatin1String("junk email") || name == QLatin1String("bulk mail")) {
        return QStringLiteral("junk");
    }
    if (name == QLatin1String("archive") || name == QLatin1String("archives")) {
        return QStringLiteral("archive");
    }
    return QStringLiteral("folder");
}

QString roleName(const QString &role)
{
    if (role == QLatin1String("inbox")) {
        return MailEngine::tr("Inbox");
    }
    if (role == QLatin1String("sent")) {
        return MailEngine::tr("Sent items");
    }
    if (role == QLatin1String("drafts")) {
        return MailEngine::tr("Drafts");
    }
    if (role == QLatin1String("trash")) {
        return MailEngine::tr("Deleted items");
    }
    if (role == QLatin1String("junk")) {
        return MailEngine::tr("Junk email");
    }
    if (role == QLatin1String("archive")) {
        return MailEngine::tr("Archive");
    }
    if (role == QLatin1String("outbox")) {
        return MailEngine::tr("Outbox");
    }
    return {};
}

int roleOrder(const QString &role)
{
    static const QStringList order = {QStringLiteral("inbox"), QStringLiteral("drafts"), QStringLiteral("outbox"), QStringLiteral("sent"),
                                      QStringLiteral("archive"), QStringLiteral("trash"), QStringLiteral("junk")};
    const int i = order.indexOf(role);
    return i < 0 ? order.size() : i;
}

void parseAddress(const gchar *raw, QString *name, QString *address)
{
    name->clear();
    address->clear();
    if (!raw || !*raw) {
        return;
    }
    CamelInternetAddress *addr = camel_internet_address_new();
    if (camel_address_decode(CAMEL_ADDRESS(addr), raw) > 0) {
        const gchar *n = nullptr;
        const gchar *a = nullptr;
        camel_internet_address_get(addr, 0, &n, &a);
        *name = str(n);
        *address = str(a);
    }
    g_object_unref(addr);
    if (name->isEmpty()) {
        *name = address->isEmpty() ? str(raw) : *address;
    }
}

QString firstNames(const gchar *raw)
{
    if (!raw || !*raw) {
        return {};
    }
    CamelInternetAddress *addr = camel_internet_address_new();
    QStringList names;
    if (camel_address_decode(CAMEL_ADDRESS(addr), raw) > 0) {
        for (int i = 0; i < camel_address_length(CAMEL_ADDRESS(addr)); ++i) {
            const gchar *n = nullptr;
            const gchar *a = nullptr;
            camel_internet_address_get(addr, i, &n, &a);
            names << (n && *n ? str(n) : str(a));
        }
    }
    g_object_unref(addr);
    return names.join(QStringLiteral(", "));
}

QString outboxDir(const QString &accountId)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/outbox/") + accountId;
}

MessageRow rowFrom(const QString &accountId, const QString &folder, CamelMessageInfo *mi)
{
    MessageRow r;
    r.accountId = accountId;
    r.folder = folder;
    r.uid = str(camel_message_info_get_uid(mi));
    parseAddress(camel_message_info_get_from(mi), &r.fromName, &r.fromAddress);
    r.to = firstNames(camel_message_info_get_to(mi));
    QString toName;
    parseAddress(camel_message_info_get_to(mi), &toName, &r.toAddress);
    r.subject = str(camel_message_info_get_subject(mi));
    r.preview = str(camel_message_info_get_preview(mi)).simplified();
    gint64 when = camel_message_info_get_date_received(mi);
    if (when <= 0) {
        when = camel_message_info_get_date_sent(mi);
    }
    r.date = QDateTime::fromSecsSinceEpoch(when);
    const guint32 flags = camel_message_info_get_flags(mi);
    r.unread = !(flags & CAMEL_MESSAGE_SEEN);
    r.flagged = flags & CAMEL_MESSAGE_FLAGGED;
    r.attachment = flags & CAMEL_MESSAGE_ATTACHMENTS;
    r.draft = flags & CAMEL_MESSAGE_DRAFT;
    r.pinned = camel_message_info_get_user_flag(mi, "wildos-pinned");
    if (const gchar *snooze = camel_message_info_get_user_tag(mi, "wildos-snooze")) {
        r.snoozedUntil = QDateTime::fromString(str(snooze), Qt::ISODate);
    }
    return r;
}

const char *trackerDayKey(const QDate &d)
{
    static thread_local QByteArray key;
    key = "trackers/" + d.toString(Qt::ISODate).toLatin1();
    return key.constData();
}
}

// What went wrong, in words the person can act on. What the server or Camel
// said goes to the log and not the screen: it is English, technical and can
// carry server names and addresses. `otherwise` is the sentence for a reason
// that isn't one of the common ones.
QString MailEngine::plainError(const GError *error, const QString &otherwise)
{
    if (!error) {
        return otherwise;
    }
    qCWarning(mailLog) << g_quark_to_string(error->domain) << error->code << error->message;
    const QString text = QString::fromUtf8(error->message).toLower();
    const auto says = [&text](const char *words) {
        return text.contains(QLatin1String(words));
    };
    const auto io = [error](int code) {
        return g_error_matches(error, G_IO_ERROR, code);
    };
    if (g_error_matches(error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE) || says("authenticationfailed") || says("invalid credentials")) {
        return tr("Your email provider didn't accept the password. Sign in again.");
    }
    if (says("alreadyexists") || says("already exists")) {
        return tr("There is already a folder with that name.");
    }
    if (says("overquota") || says("quota")) {
        return tr("Your mailbox is full. Delete some mail to make room.");
    }
    if (says("nonexistent") || g_error_matches(error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER)) {
        return tr("That folder isn't there any more.");
    }
    if (says("noperm") || io(G_IO_ERROR_PERMISSION_DENIED) || g_error_matches(error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INSUFFICIENT_PERMISSION)) {
        return tr("Your email provider doesn't allow that.");
    }
    if (io(G_IO_ERROR_NO_SPACE)) {
        return tr("This PC has run out of space.");
    }
    if (io(G_IO_ERROR_TIMED_OUT)) {
        return tr("Your email provider took too long to answer. Try again in a moment.");
    }
    if (error->domain == G_TLS_ERROR) {
        return tr("Couldn't make a secure connection to your email provider.");
    }
    if (error->domain == G_RESOLVER_ERROR || io(G_IO_ERROR_HOST_NOT_FOUND)) {
        return tr("Couldn't find your email provider's server. Check that this PC is online.");
    }
    if (io(G_IO_ERROR_HOST_UNREACHABLE) || io(G_IO_ERROR_NETWORK_UNREACHABLE) || io(G_IO_ERROR_CONNECTION_REFUSED) || io(G_IO_ERROR_NOT_CONNECTED)
        || io(G_IO_ERROR_CONNECTION_CLOSED) || g_error_matches(error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE)
        || g_error_matches(error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED)) {
        return tr("Couldn't reach your email provider. Check that this PC is online.");
    }
    return otherwise;
}

MailEngine *MailEngine::instance()
{
    static MailEngine *engine = new MailEngine();
    return engine;
}

MailEngine *MailEngine::create(QQmlEngine *, QJSEngine *)
{
    MailEngine *engine = instance();
    QJSEngine::setObjectOwnership(engine, QJSEngine::CppOwnership);
    return engine;
}

MailEngine::MailEngine(QObject *parent)
    : QObject(parent)
    , m_folderModel(new FolderModel(this))
    , m_messageModel(new MessageModel(this))
{
    m_syncHold.setSingleShot(true);
    m_syncHold.setInterval(700);
    connect(&m_syncHold, &QTimer::timeout, this, &MailEngine::syncingChanged);
    Session::instance();
    m_messageModel->folderName = [this](const QString &accountId, const QString &folder) {
        return folderLabel(accountId, folder);
    };

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(250);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this]() {
        reloadMessages();
        for (const QString &id : m_live.keys()) {
            listFolders(id);
        }
    });

    // IDLE brings new mail to the Inbox as it arrives; the other folders
    // are looked at every five minutes.
    m_pollTimer.setInterval(5 * 60 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &MailEngine::refresh);
    m_pollTimer.start();

    // Snoozed mail comes back on time, unread, at the top.
    m_snoozeTimer.setInterval(60 * 1000);
    connect(&m_snoozeTimer, &QTimer::timeout, this, [this]() {
        reloadMessages();
    });
    m_snoozeTimer.start();

    if (QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        auto *info = QNetworkInformation::instance();
        m_online = info->reachability() != QNetworkInformation::Reachability::Disconnected;
        connect(info, &QNetworkInformation::reachabilityChanged, this, [this](QNetworkInformation::Reachability r) {
            setOnline(r != QNetworkInformation::Reachability::Disconnected);
        });
    }

    connect(&m_store, &AccountStore::changed, this, &MailEngine::accountsChanged);
    for (const Account &account : m_store.accounts()) {
        startAccount(account);
    }
    rebuildFolderModel();
}

QVariantList MailEngine::accountList() const
{
    QVariantList list;
    for (const Account &a : m_store.accounts()) {
        QVariantMap map = a.toMap();
        const Live live = m_live.value(a.id);
        map.insert(QStringLiteral("problem"), live.problem);
        map.insert(QStringLiteral("problemKind"), live.problemKind);
        map.insert(QStringLiteral("connected"), live.connected);
        list.append(map);
    }
    return list;
}

QString MailEngine::folderTitle() const
{
    if (searching()) {
        return tr("Search results");
    }
    if (m_folderAccount.isEmpty()) {
        return roleName(m_folderRole);
    }
    return folderLabel(m_folderAccount, m_folderFullName);
}

QString MailEngine::folderLabel(const QString &accountId, const QString &fullName) const
{
    for (const FolderEntry &e : m_live.value(accountId).folders) {
        if (e.fullName == fullName) {
            return e.displayName;
        }
    }
    return fullName.section(QLatin1Char('/'), -1);
}

void MailEngine::select(const QString &key)
{
    if (key == m_selected) {
        return;
    }
    m_selected = key;
    m_selection = key.isEmpty() ? QStringList() : QStringList{key};
    m_anchor = key;
    Q_EMIT selectedChanged();
    // A message counts as read once it has been open for two seconds, so
    // arrowing past it doesn't (Settings > Mail > Mark as read).
    QTimer::singleShot(2000, this, [this, key]() {
        if (key != m_selected) {
            return;
        }
        if (const MessageRow *r = m_messageModel->row(key); r && r->unread) {
            setRead(key, true);
        }
    });
}

int MailEngine::trackersThisWeek() const
{
    QSettings settings;
    int sum = 0;
    const QDate today = QDate::currentDate();
    for (int i = 0; i < 7; ++i) {
        sum += settings.value(QLatin1String(trackerDayKey(today.addDays(-i)))).toInt();
    }
    return sum;
}

void MailEngine::countTrackers(int blocked, const QString &messageId)
{
    if (blocked <= 0) {
        return;
    }
    QSettings settings;
    const QString seen = QStringLiteral("trackersCounted/") + QString::number(qHash(messageId), 16);
    if (settings.contains(seen)) {
        return;
    }
    settings.setValue(seen, QDate::currentDate());
    // Only this week's are worth remembering.
    settings.beginGroup(QStringLiteral("trackersCounted"));
    for (const QString &k : settings.childKeys()) {
        if (settings.value(k).toDate() < QDate::currentDate().addDays(-8)) {
            settings.remove(k);
        }
    }
    settings.endGroup();
    const char *key = trackerDayKey(QDate::currentDate());
    settings.setValue(QLatin1String(key), settings.value(QLatin1String(key)).toInt() + blocked);
    Q_EMIT trackersChanged();
}

QString MailEngine::systemFullName()
{
    const passwd *pw = getpwuid(getuid());
    if (!pw || !pw->pw_gecos) {
        return {};
    }
    return QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
}

int MailEngine::photosCleaned() const
{
    return QSettings().value(QStringLiteral("privacy/photosCleaned")).toInt();
}

void MailEngine::countCleanedPhotos(int photos)
{
    if (photos > 0) {
        QSettings settings;
        settings.setValue(QStringLiteral("privacy/photosCleaned"), settings.value(QStringLiteral("privacy/photosCleaned")).toInt() + photos);
        Q_EMIT trackersChanged();
    }
}

QVariantList MailEngine::problems() const
{
    QVariantList list;
    for (const Account &a : m_store.accounts()) {
        const Live &live = m_live[a.id];
        if (!live.problem.isEmpty()) {
            list.append(QVariantMap{{QStringLiteral("accountId"), a.id},
                                    {QStringLiteral("name"), a.name},
                                    {QStringLiteral("kind"), live.problemKind},
                                    {QStringLiteral("text"), live.problem}});
        }
    }
    return list;
}

int MailEngine::outboxCount() const
{
    int n = 0;
    for (const Account &a : m_store.accounts()) {
        n += QDir(outboxDir(a.id)).entryList({QStringLiteral("*.eml")}, QDir::Files).size();
    }
    return n;
}

void MailEngine::run(std::function<void()> work, std::function<void()> done)
{
    QPointer<MailEngine> self(this);
    QThreadPool::globalInstance()->start([self, work, done]() {
        work();
        if (done && self) {
            QMetaObject::invokeMethod(self, done, Qt::QueuedConnection);
        }
    });
}

void MailEngine::configure(CamelService *store, CamelService *smtp, const Account &account) const
{
    if (store) {
        CamelSettings *settings = camel_service_ref_settings(store);
        g_object_set(settings,
                     "host", account.imapHost.toUtf8().constData(),
                     "port", guint(account.imapPort),
                     "user", account.user.toUtf8().constData(),
                     "security-method", securityOf(account.imapSecurity),
                     "use-idle", TRUE,
                     "check-all", TRUE,
                     "use-subscriptions", FALSE,
                     "stay-synchronized", account.keepAll,
                     "auth-mechanism", account.auth == QLatin1String("google") ? "XOAUTH2" : nullptr,
                     nullptr);
        g_object_unref(settings);
    }
    if (smtp) {
        CamelSettings *settings = camel_service_ref_settings(smtp);
        g_object_set(settings,
                     "host", account.smtpHost.toUtf8().constData(),
                     "port", guint(account.smtpPort),
                     "user", account.user.toUtf8().constData(),
                     "security-method", securityOf(account.smtpSecurity),
                     "auth-mechanism", account.auth == QLatin1String("google") ? "XOAUTH2" : "PLAIN",
                     nullptr);
        g_object_unref(settings);
    }
}

void MailEngine::startAccount(const Account &account)
{
    CamelSession *session = Session::instance().camel();
    // Workers read m_live under the folder lock, so changes to it here take
    // the lock too.
    QMutexLocker lock(&m_folderLock);
    Live &live = m_live[account.id];
    GError *error = nullptr;
    if (!live.store) {
        CamelService *service = camel_session_add_service(session, account.id.toUtf8().constData(), "imapx", CAMEL_PROVIDER_STORE, &error);
        if (!service) {
            const QString text = plainError(error, tr("Mail couldn't start this account. Close Mail and open it again."));
            g_clear_error(&error);
            lock.unlock();
            setProblem(account.id, QStringLiteral("unreachable"), text);
            return;
        }
        live.store = CAMEL_STORE(service);
        watchStore(live.store);
        // Offline until it has its password: what is kept on this PC shows
        // at once, and nothing tries the server without a password.
        camel_offline_store_set_online_sync(CAMEL_OFFLINE_STORE(service), FALSE, nullptr, nullptr);
    }
    if (!live.smtp) {
        live.smtp = camel_session_add_service(session, (account.id + QStringLiteral(".smtp")).toUtf8().constData(), "smtp", CAMEL_PROVIDER_TRANSPORT, &error);
        g_clear_error(&error);
    }
    configure(CAMEL_SERVICE(live.store), live.smtp, account);
    lock.unlock();
    Session::instance().setOAuth(account.id, account.auth == QLatin1String("google"));

    // The cached folders and mail show straight away, before the password
    // store answers and the server is reached.
    listFolders(account.id);
    const QString id = account.id;
    m_store.readPassword(id, [this, id](const QString &password) {
        if (password.isEmpty()) {
            setProblem(id, QStringLiteral("password"), tr("Wild OS Mail needs your password again."));
            return;
        }
        Session::instance().setPassword(id, password);
        connectAccount(id);
    });
}

void MailEngine::stopAccount(const QString &id)
{
    Live live;
    {
        QMutexLocker lock(&m_folderLock);
        live = m_live.take(id);
        for (auto it = m_folderCache.begin(); it != m_folderCache.end();) {
            if (it.key().startsWith(id + keySep)) {
                m_watched.remove(it.value());
                g_object_unref(it.value());
                it = m_folderCache.erase(it);
            } else {
                ++it;
            }
        }
    }
    CamelSession *session = Session::instance().camel();
    if (live.store) {
        CamelService *service = CAMEL_SERVICE(live.store);
        run([service]() {
            camel_service_disconnect_sync(service, FALSE, nullptr, nullptr);
        }, [session, service]() {
            camel_session_remove_service(session, service);
        });
    }
    if (live.smtp) {
        camel_session_remove_service(session, live.smtp);
    }
}

void MailEngine::connectAccount(const QString &id)
{
    if (!m_live.contains(id) || !m_online) {
        return;
    }
    CamelStore *store = m_live[id].store;
    g_object_ref(store);
    auto problem = std::make_shared<QString>();
    auto kind = std::make_shared<QString>();
    ++m_loading;
    Q_EMIT loadingChanged();
    run([store, problem, kind]() {
        GError *error = nullptr;
        camel_offline_store_set_online_sync(CAMEL_OFFLINE_STORE(store), TRUE, nullptr, nullptr);
        if (!camel_service_connect_sync(CAMEL_SERVICE(store), nullptr, &error)) {
            *problem = plainError(error, tr("Couldn't reach your email provider."));
            const bool auth = error && error->domain == CAMEL_SERVICE_ERROR && error->code == CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE;
            *kind = auth ? QStringLiteral("password") : QStringLiteral("unreachable");
            g_clear_error(&error);
        }
    }, [this, id, store, problem, kind]() {
        g_object_unref(store);
        --m_loading;
        Q_EMIT loadingChanged();
        if (!m_live.contains(id)) {
            return;
        }
        if (!problem->isEmpty()) {
            qCWarning(mailLog) << "account" << id << "couldn't connect:" << *problem;
            setProblem(id, *kind, *kind == QLatin1String("password") ? tr("The password wasn't accepted. Sign in again to keep getting mail.") : *problem);
            return;
        }
        {
            QMutexLocker lock(&m_folderLock);
            m_live[id].connected = true;
            m_live[id].fetchedAfterConnect = false;
        }
        Q_EMIT accountsChanged();
        setProblem(id, {}, {});
        listFolders(id);
        reloadMessages();
        // Now it can reach the server: fetch what arrived while Mail was
        // closed, for the open folder and the counts of the rest.
        refresh();
        // Mail kept on this PC for reading offline and searching. Camel
        // downloads what is missing in the background.
        const Account *account = m_store.find(id);
        if (account && account->keepAll) {
            CamelStore *s = m_live[id].store;
            g_object_ref(s);
            setSyncStatus(tr("Downloading mail"));
            run([s]() {
                camel_offline_store_prepare_for_offline_sync(CAMEL_OFFLINE_STORE(s), nullptr, nullptr);
                g_object_unref(s);
            }, [this]() {
                setSyncStatus({});
            });
        }
    });
}

void MailEngine::setProblem(const QString &id, const QString &kind, const QString &text)
{
    if (!m_live.contains(id)) {
        return;
    }
    {
        QMutexLocker lock(&m_folderLock);
        Live &live = m_live[id];
        if (live.problem == text && live.problemKind == kind) {
            return;
        }
        live.problem = text;
        live.problemKind = kind;
    }
    Q_EMIT problemsChanged();
    Q_EMIT accountsChanged();
}

void MailEngine::setSyncStatus(const QString &status)
{
    if (status != m_syncStatus) {
        m_syncStatus = status;
        Q_EMIT syncStatusChanged();
    }
}

void MailEngine::setOnline(bool online)
{
    if (online == m_online) {
        return;
    }
    m_online = online;
    camel_session_set_online(Session::instance().camel(), online);
    Q_EMIT onlineChanged();
    if (online) {
        for (const QString &id : m_live.keys()) {
            if (!Session::instance().password(id).isEmpty()) {
                connectAccount(id);
            }
        }
    } else {
        QMutexLocker lock(&m_folderLock);
        for (Live &live : m_live) {
            live.connected = false;
        }
    }
}

void MailEngine::listFolders(const QString &id)
{
    if (!m_live.contains(id)) {
        return;
    }
    const Account *account = m_store.find(id);
    if (!account) {
        return;
    }
    CamelStore *store = m_live[id].store;
    g_object_ref(store);
    const QString provider = account->provider;
    auto entries = std::make_shared<QList<FolderEntry>>();
    run([store, id, provider, entries]() {
        GError *error = nullptr;
        CamelFolderInfo *root = camel_store_get_folder_info_sync(store, nullptr,
                                                                 CamelStoreGetFolderInfoFlags(CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_SUBSCRIBED),
                                                                 nullptr, &error);
        g_clear_error(&error);
        QList<FolderEntry> special;
        QList<FolderEntry> rest;
        std::function<void(CamelFolderInfo *, int)> walk = [&](CamelFolderInfo *fi, int depth) {
            for (; fi; fi = fi->next) {
                const QString full = str(fi->full_name);
                // Camel's own virtual Trash and Junk stand in for servers
                // without them; the server's real folders are what people
                // know.
                if (full.startsWith(QLatin1String(".#evolution")) || (fi->flags & CAMEL_FOLDER_VIRTUAL)) {
                    continue;
                }
                const bool container = fi->flags & CAMEL_FOLDER_NOSELECT;
                if (!container) {
                    FolderEntry e;
                    e.accountId = id;
                    e.fullName = full;
                    e.role = roleOf(fi, provider);
                    if (e.role == QLatin1String("all")) {
                        e.role = QStringLiteral("folder");
                    }
                    e.displayName = roleName(e.role);
                    if (e.displayName.isEmpty()) {
                        e.displayName = str(fi->display_name);
                    }
                    e.unread = qMax(0, fi->unread);
                    e.total = qMax(0, fi->total);
                    if (e.role == QLatin1String("folder")) {
                        e.depth = depth;
                        rest.append(e);
                    } else if (std::none_of(special.cbegin(), special.cend(), [&](const FolderEntry &s) { return s.role == e.role; })) {
                        special.append(e);
                    } else {
                        rest.append(e);
                    }
                }
                // Gmail's "[Gmail]" holds the special folders; its children
                // sit at the top level rather than under a folder that can't
                // be opened.
                walk(fi->child, container ? depth : depth + 1);
            }
        };
        walk(root, 0);
        if (root) {
            camel_folder_info_free(root);
        }
        // Drafts show how many there are; the server's count can still
        // include ones replaced a moment ago, so ask the folder itself.
        for (FolderEntry &e : special) {
            if (e.role != QLatin1String("drafts")) {
                continue;
            }
            CamelFolder *folder = camel_store_get_folder_sync(store, e.fullName.toUtf8().constData(), CamelStoreGetFolderFlags(0), nullptr, nullptr);
            if (folder) {
                e.total = qMax(0, camel_folder_get_message_count(folder) - camel_folder_get_deleted_message_count(folder));
                g_object_unref(folder);
            }
        }
        std::stable_sort(special.begin(), special.end(), [](const FolderEntry &a, const FolderEntry &b) {
            return roleOrder(a.role) < roleOrder(b.role);
        });
        *entries = special + rest;
        g_object_unref(store);
    }, [this, id, entries]() {
        if (!m_live.contains(id)) {
            return;
        }
        bool fetch = false;
        {
            QMutexLocker lock(&m_folderLock);
            Live &live = m_live[id];
            live.folders = *entries;
            fetch = live.connected && !live.fetchedAfterConnect && !entries->isEmpty();
            if (fetch) {
                live.fetchedAfterConnect = true;
            }
        }
        if (fetch) {
            for (const auto &[account, full] : currentFolders()) {
                if (account != id) {
                    continue;
                }
                const QString a = account;
                const QString f = full;
                run([this, a, f]() {
                    if (CamelFolder *folder = folderSync(a, f, nullptr)) {
                        camel_folder_refresh_info_sync(folder, nullptr, nullptr);
                    }
                }, [this]() {
                    reloadMessages();
                });
            }
        }
        // The first folder list after start-up: open the Inbox, or whichever
        // of Favourites was open last time.
        if (m_folderAccount.isEmpty() && m_folderFullName.isEmpty()) {
            reloadMessages();
        }
        if (m_restoringFolder && m_folderAccount == id && !entries->isEmpty()) {
            m_restoringFolder = false;
            const auto found = std::find_if(entries->cbegin(), entries->cend(), [this](const FolderEntry &e) {
                return e.fullName == m_folderFullName;
            });
            if (found == entries->cend()) {
                // Deleted or renamed somewhere else since.
                openFolder({}, QStringLiteral("inbox"));
            } else {
                m_folderRole = found->role;
                Q_EMIT folderChanged();
            }
        }
        rebuildFolderModel();
    });
}

void MailEngine::rebuildFolderModel()
{
    QList<FolderEntry> entries;
    const QList<Account> &accounts = m_store.accounts();
    if (accounts.isEmpty()) {
        m_folderModel->reset({});
        return;
    }

    // Favourites: Inbox, Sent items and Drafts across every account.
    FolderEntry fav;
    fav.kind = FolderEntry::Header;
    fav.displayName = tr("Favourites");
    fav.expanded = m_expanded.value(QString(), true);
    entries.append(fav);
    if (fav.expanded) {
        for (const QString &role : {QStringLiteral("inbox"), QStringLiteral("sent"), QStringLiteral("drafts")}) {
            FolderEntry e;
            e.fullName = role;
            e.role = role;
            e.displayName = roleName(role);
            e.depth = 1;
            for (const Account &a : accounts) {
                for (const FolderEntry &f : m_live.value(a.id).folders) {
                    if (f.role == role) {
                        e.unread += f.unread;
                        e.total += f.total;
                    }
                }
            }
            entries.append(e);
        }
    }

    for (const Account &a : accounts) {
        FolderEntry header;
        header.kind = FolderEntry::Header;
        header.accountId = a.id;
        header.displayName = a.name;
        header.expanded = m_expanded.value(a.id, accounts.size() == 1 || &a == &accounts.first());
        entries.append(header);
        if (!header.expanded) {
            continue;
        }
        QList<FolderEntry> folders = m_live.value(a.id).folders;
        const int queued = QDir(outboxDir(a.id)).entryList({QStringLiteral("*.eml")}, QDir::Files).size();
        if (queued > 0) {
            FolderEntry outbox;
            outbox.accountId = a.id;
            outbox.fullName = QStringLiteral(".outbox");
            outbox.role = QStringLiteral("outbox");
            outbox.displayName = roleName(outbox.role);
            outbox.total = queued;
            int at = 0;
            while (at < folders.size() && roleOrder(folders[at].role) <= roleOrder(outbox.role)) {
                ++at;
            }
            folders.insert(at, outbox);
        }
        for (FolderEntry f : folders) {
            f.depth += 1;
            entries.append(f);
        }
        FolderEntry create;
        create.kind = FolderEntry::Create;
        create.accountId = a.id;
        create.displayName = tr("Create new folder");
        entries.append(create);
    }
    m_folderModel->reset(entries);
}

void MailEngine::toggleExpanded(const QString &accountId)
{
    const bool now = m_expanded.value(accountId, accountId.isEmpty() || m_store.accounts().size() == 1
                                                     || (!m_store.accounts().isEmpty() && m_store.accounts().first().id == accountId));
    m_expanded.insert(accountId, !now);
    rebuildFolderModel();
}

int MailEngine::accountIndex(const QString &accountId) const
{
    const QList<Account> &accounts = m_store.accounts();
    for (int i = 0; i < accounts.size(); ++i) {
        if (accounts.at(i).id == accountId) {
            return i;
        }
    }
    return -1;
}

void MailEngine::moveAccount(const QString &accountId, int to)
{
    const QList<Account> &accounts = m_store.accounts();
    // An account nobody has opened or closed is open only while it is the
    // first one. Moving must not open or close any, so each keeps what it
    // shows now.
    for (const Account &a : accounts) {
        if (!m_expanded.contains(a.id)) {
            m_expanded.insert(a.id, accounts.size() == 1 || &a == &accounts.first());
        }
    }
    m_store.move(accountId, to);
    rebuildFolderModel();
}

QString MailEngine::roleFullName(const QString &accountId, const QString &role) const
{
    for (const FolderEntry &f : m_live.value(accountId).folders) {
        if (f.role == role) {
            return f.fullName;
        }
    }
    return {};
}

QList<QPair<QString, QString>> MailEngine::currentFolders() const
{
    QList<QPair<QString, QString>> list;
    if (m_folderAccount.isEmpty()) {
        for (const Account &a : m_store.accounts()) {
            const QString full = roleFullName(a.id, m_folderRole);
            if (!full.isEmpty()) {
                list.append({a.id, full});
            }
        }
    } else if (m_folderRole != QLatin1String("outbox")) {
        list.append({m_folderAccount, m_folderFullName});
    }
    return list;
}

void MailEngine::openLastFolder()
{
    QSettings settings;
    const QString account = settings.value(QStringLiteral("mail/folderAccount")).toString();
    const QString folder = settings.value(QStringLiteral("mail/folder")).toString();
    if (account.isEmpty()) {
        const bool favourite = folder == QLatin1String("sent") || folder == QLatin1String("drafts");
        openFolder({}, favourite ? folder : QStringLiteral("inbox"));
    } else if (!m_store.find(account) || folder.isEmpty() || folder == QLatin1String(".outbox")) {
        // The account has gone, or it was the Outbox, which is only there
        // while something waits in it.
        openFolder({}, QStringLiteral("inbox"));
    } else {
        openFolder(account, folder);
        m_restoringFolder = true;
        // Its account is shown open, so the folder can be seen in the pane.
        if (!m_expanded.contains(account)) {
            m_expanded.insert(account, true);
        }
    }
}

void MailEngine::openFolder(const QString &accountId, const QString &fullName)
{
    m_restoringFolder = false;
    QSettings settings;
    settings.setValue(QStringLiteral("mail/folderAccount"), accountId);
    settings.setValue(QStringLiteral("mail/folder"), fullName);
    m_searchText.clear();
    m_folderAccount = accountId;
    if (accountId.isEmpty()) {
        m_folderRole = fullName;
        m_folderFullName.clear();
    } else {
        m_folderFullName = fullName;
        m_folderRole = QStringLiteral("folder");
        for (const FolderEntry &f : m_live.value(accountId).folders) {
            if (f.fullName == fullName) {
                m_folderRole = f.role;
            }
        }
        if (fullName == QLatin1String(".outbox")) {
            m_folderRole = QStringLiteral("outbox");
        }
    }
    m_messageModel->setFilter(QStringLiteral("all"));
    m_messageModel->reset({});
    m_selected.clear();
    Q_EMIT selectedChanged();
    Q_EMIT searchChanged();
    Q_EMIT folderChanged();
    reloadMessages();

    // Opening a folder asks the server for anything new in it.
    for (const auto &[account, full] : currentFolders()) {
        if (!m_online || !m_live.value(account).connected) {
            continue;
        }
        const QString a = account;
        const QString f = full;
        run([this, a, f]() {
            GError *error = nullptr;
            if (CamelFolder *folder = folderSync(a, f, &error)) {
                camel_folder_refresh_info_sync(folder, nullptr, nullptr);
            }
            g_clear_error(&error);
        });
    }
}

void MailEngine::scheduleReload()
{
    m_reloadTimer.start();
}

CamelFolder *MailEngine::folderSync(const QString &accountId, const QString &fullName, GError **error)
{
    const QString cacheKey = accountId + keySep + fullName;
    CamelStore *store = nullptr;
    {
        QMutexLocker lock(&m_folderLock);
        if (CamelFolder *folder = m_folderCache.value(cacheKey)) {
            return folder;
        }
        store = m_live.value(accountId).store;
        if (!store) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, MailEngine::tr("No such account").toUtf8().constData());
            return nullptr;
        }
        g_object_ref(store);
    }
    CamelFolder *folder = camel_store_get_folder_sync(store, fullName.toUtf8().constData(), CamelStoreGetFolderFlags(0), nullptr, error);
    g_object_unref(store);
    if (!folder) {
        return nullptr;
    }
    QMutexLocker lock(&m_folderLock);
    if (CamelFolder *existing = m_folderCache.value(cacheKey)) {
        g_object_unref(folder);
        return existing;
    }
    m_folderCache.insert(cacheKey, folder);
    QMetaObject::invokeMethod(this, [this, folder]() {
        watchFolder(folder);
    }, Qt::QueuedConnection);
    return folder;
}

CamelFolder *MailEngine::roleFolderSync(const QString &accountId, const QString &role, GError **error)
{
    QString full;
    {
        QMutexLocker lock(&m_folderLock);
        full = roleFullName(accountId, role);
    }
    if (full.isEmpty()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, MailEngine::tr("This account has no %1 folder").arg(role).toUtf8().constData());
        return nullptr;
    }
    return folderSync(accountId, full, error);
}

CamelService *MailEngine::transport(const QString &accountId) const
{
    return m_live.value(accountId).smtp;
}

void MailEngine::watchFolder(CamelFolder *folder)
{
    if (m_watched.contains(folder)) {
        return;
    }
    m_watched.insert(folder);
    g_signal_connect(folder, "changed", G_CALLBACK(&MailEngine::onFolderChanged), this);
}

void MailEngine::watchStore(CamelStore *store)
{
    for (const char *signal : {"folder-created", "folder-deleted", "folder-renamed", "folder-info-stale"}) {
        g_signal_connect(store, signal, G_CALLBACK(&MailEngine::onStoreChanged), this);
    }
}

void MailEngine::onFolderChanged(CamelFolder *, CamelFolderChangeInfo *, gpointer self)
{
    auto *engine = static_cast<MailEngine *>(self);
    QMetaObject::invokeMethod(engine, &MailEngine::scheduleReload, Qt::QueuedConnection);
}

void MailEngine::onStoreChanged(CamelStore *, gpointer, gpointer self)
{
    auto *engine = static_cast<MailEngine *>(self);
    QMetaObject::invokeMethod(engine, &MailEngine::scheduleReload, Qt::QueuedConnection);
}

void MailEngine::reloadMessages()
{
    if (searching()) {
        search(m_searchText);
        return;
    }
    const int generation = ++m_generation;
    const QList<QPair<QString, QString>> folders = currentFolders();
    const bool outbox = m_folderRole == QLatin1String("outbox");
    const QString outboxAccount = m_folderAccount;
    auto rows = std::make_shared<QList<MessageRow>>();
    auto inbox = std::make_shared<QHash<QString, QList<MessageRow>>>();
    QSet<QString> outgoingFolders;
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it) {
        for (const FolderEntry &f : it->folders) {
            if (f.role == QLatin1String("sent") || f.role == QLatin1String("drafts")) {
                outgoingFolders.insert(it.key() + keySep + f.fullName);
            }
        }
    }
    run([this, folders, rows, inbox, outbox, outboxAccount, outgoingFolders]() {
        if (outbox) {
            const QDir dir(outboxDir(outboxAccount));
            for (const QFileInfo &file : dir.entryInfoList({QStringLiteral("*.eml")}, QDir::Files)) {
                CamelMimeMessage *message = camel_mime_message_new();
                CamelStream *stream = camel_stream_fs_new_with_name(file.filePath().toUtf8().constData(), O_RDONLY, 0, nullptr);
                if (stream && camel_data_wrapper_construct_from_stream_sync(CAMEL_DATA_WRAPPER(message), stream, nullptr, nullptr)) {
                    MessageRow r;
                    r.accountId = outboxAccount;
                    r.folder = QStringLiteral(".outbox");
                    r.uid = file.fileName();
                    r.to = firstNames(camel_address_format(CAMEL_ADDRESS(camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO))));
                    r.fromName = r.to;
                    r.draft = true;
                    r.subject = str(camel_mime_message_get_subject(message));
                    r.date = file.lastModified();
                    rows->append(r);
                }
                g_clear_object(&stream);
                g_object_unref(message);
            }
            return;
        }
        for (const auto &[accountId, full] : folders) {
            GError *error = nullptr;
            CamelFolder *folder = folderSync(accountId, full, &error);
            g_clear_error(&error);
            if (!folder) {
                continue;
            }
            camel_folder_summary_prepare_fetch_all(camel_folder_get_folder_summary(folder), nullptr);
            GPtrArray *uids = camel_folder_get_uids(folder);
            for (guint i = 0; i < uids->len; ++i) {
                CamelMessageInfo *mi = camel_folder_get_message_info(folder, static_cast<const char *>(uids->pdata[i]));
                if (!mi) {
                    continue;
                }
                const guint32 flags = camel_message_info_get_flags(mi);
                if (!(flags & (CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_JUNK)) || full.contains(QLatin1String("Junk"), Qt::CaseInsensitive)) {
                    MessageRow r = rowFrom(accountId, full, mi);
                    r.outgoing = outgoingFolders.contains(accountId + keySep + full);
                    rows->append(r);
                    if ((camel_folder_get_flags(folder) & CAMEL_FOLDER_IS_JUNK) == 0 && full.compare(QLatin1String("INBOX"), Qt::CaseInsensitive) == 0) {
                        (*inbox)[accountId].append(r);
                    }
                }
                g_object_unref(mi);
            }
            camel_folder_free_uids(folder, uids);
        }
    }, [this, generation, rows, inbox]() {
        for (auto it = inbox->cbegin(); it != inbox->cend(); ++it) {
            checkNewMail(it.key(), it.value());
        }
        if (generation != m_generation) {
            return;
        }
        m_messageModel->reset(*rows);
    });
}

// A message in the Inbox that wasn't there the last time Mail looked, and
// is unread, gets a notification. The first look after start-up only
// learns what is there.
void MailEngine::checkNewMail(const QString &accountId, const QList<MessageRow> &inboxRows)
{
    if (!m_live.contains(accountId)) {
        return;
    }
    Live &live = m_live[accountId];
    QSet<QString> now;
    for (const MessageRow &r : inboxRows) {
        now.insert(r.uid);
        if (live.inboxPrimed && r.unread && !live.knownInboxUids.contains(r.uid)
            && r.date > QDateTime::currentDateTime().addDays(-1)) {
            Q_EMIT newMail(r.key(), r.fromName, r.subject, r.preview);
        }
    }
    live.knownInboxUids.unite(now);
    live.inboxPrimed = live.connected || live.inboxPrimed;
}

void MailEngine::refresh()
{
    if (!m_online) {
        return;
    }
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it) {
        if (!it->connected) {
            if (!Session::instance().password(it.key()).isEmpty()) {
                connectAccount(it.key());
            }
            continue;
        }
        CamelStore *store = it->store;
        g_object_ref(store);
        if (m_syncing++ == 0) {
            m_syncHold.start();
            Q_EMIT syncingChanged();
        }
        run(
            [store]() {
                // Asks for each folder's counts; Camel fetches new mail for the
                // folders it has open.
                CamelFolderInfo *fi = camel_store_get_folder_info_sync(store, nullptr,
                                                                       CamelStoreGetFolderInfoFlags(CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_REFRESH),
                                                                       nullptr, nullptr);
                if (fi) {
                    camel_folder_info_free(fi);
                }
                g_object_unref(store);
            },
            [this]() {
                if (--m_syncing == 0) {
                    Q_EMIT syncingChanged();
                }
            });
    }
    for (const auto &[account, full] : currentFolders()) {
        const QString a = account;
        const QString f = full;
        run([this, a, f]() {
            if (CamelFolder *folder = folderSync(a, f, nullptr)) {
                camel_folder_refresh_info_sync(folder, nullptr, nullptr);
            }
        });
    }
    sendOutbox();
}

bool MailEngine::splitKey(const QString &key, QString *accountId, QString *folder, QString *uid)
{
    const QStringList parts = key.split(keySep);
    if (parts.size() != 3) {
        return false;
    }
    *accountId = parts[0];
    *folder = parts[1];
    *uid = parts[2];
    return true;
}

QString MailEngine::accountForKey(const QString &key) const
{
    return key.section(keySep, 0, 0);
}

void MailEngine::setFlags(const QString &key, guint32 mask, guint32 set)
{
    QString accountId, full, uid;
    if (!splitKey(key, &accountId, &full, &uid)) {
        return;
    }
    run([this, accountId, full, uid, mask, set]() {
        CamelFolder *folder = folderSync(accountId, full, nullptr);
        if (!folder) {
            return;
        }
        camel_folder_set_message_flags(folder, uid.toUtf8().constData(), mask, set);
        // Sent to the server now if online, or the next time it is.
        camel_folder_synchronize_sync(folder, FALSE, nullptr, nullptr);
    });
}

void MailEngine::setRead(const QString &key, bool read)
{
    m_messageModel->updateRow(key, [read](MessageRow &r) {
        r.unread = !read;
    });
    setFlags(key, CAMEL_MESSAGE_SEEN, read ? CAMEL_MESSAGE_SEEN : 0);
}

void MailEngine::toggleRead(const QString &key)
{
    if (const MessageRow *r = m_messageModel->row(key)) {
        setRead(key, r->unread);
    }
}

void MailEngine::toggleFlag(const QString &key)
{
    if (const MessageRow *r = m_messageModel->row(key)) {
        setFlaggedKeys({key}, !r->flagged);
    }
}

void MailEngine::togglePin(const QString &key)
{
    QString accountId, full, uid;
    const MessageRow *r = m_messageModel->row(key);
    if (!r || !splitKey(key, &accountId, &full, &uid)) {
        return;
    }
    const bool pin = !r->pinned;
    m_messageModel->updateRow(key, [pin](MessageRow &r) {
        r.pinned = pin;
    });
    run([this, accountId, full, uid, pin]() {
        if (CamelFolder *folder = folderSync(accountId, full, nullptr)) {
            if (CamelMessageInfo *mi = camel_folder_get_message_info(folder, uid.toUtf8().constData())) {
                camel_message_info_set_user_flag(mi, "wildos-pinned", pin);
                g_object_unref(mi);
            }
        }
    });
}

void MailEngine::snooze(const QString &key, const QDateTime &until)
{
    QString accountId, full, uid;
    if (!splitKey(key, &accountId, &full, &uid)) {
        return;
    }
    const QString next = m_messageModel->neighbour(key);
    const QByteArray when = until.toString(Qt::ISODate).toUtf8();
    run([this, accountId, full, uid, when]() {
        if (CamelFolder *folder = folderSync(accountId, full, nullptr)) {
            if (CamelMessageInfo *mi = camel_folder_get_message_info(folder, uid.toUtf8().constData())) {
                camel_message_info_set_user_tag(mi, "wildos-snooze", when.constData());
                // It comes back unread.
                camel_message_info_set_flags(mi, CAMEL_MESSAGE_SEEN, 0);
                g_object_unref(mi);
            }
        }
    }, [this]() {
        reloadMessages();
    });
    select(next);
}

// Moves happen in the list at once; the server is told in the background.
// What was moved is remembered so Undo can put it back.
void MailEngine::transfer(const QStringList &keys, const QString &targetRole, const QString &targetFullName, bool junkFlag)
{
    struct Item {
        QString accountId, full, uid, target;
        bool permanent = false;
    };
    QList<Item> items;
    for (const QString &key : keys) {
        Item item;
        if (!splitKey(key, &item.accountId, &item.full, &item.uid) || item.full == QLatin1String(".outbox")) {
            continue;
        }
        item.target = targetFullName.isEmpty() ? roleFullName(item.accountId, targetRole) : targetFullName;
        item.permanent = targetRole == QLatin1String("trash") && (item.target.isEmpty() || item.target == item.full);
        if (item.target.isEmpty() && targetRole == QLatin1String("archive")) {
            // No Archive folder on this server yet: make one, as Outlook does.
            item.target = QStringLiteral("Archive");
        }
        if (item.target.isEmpty() && !item.permanent) {
            Q_EMIT notice(tr("This account has no folder for that."));
            return;
        }
        if (!item.permanent && item.target == item.full) {
            continue;
        }
        items.append(item);
    }
    if (items.isEmpty()) {
        return;
    }

    QStringList moved;
    for (const Item &item : items) {
        moved << item.accountId + keySep + item.full + keySep + item.uid;
    }
    // The selection moves on to the next message left in the list.
    QString next;
    for (const QString &key : moved) {
        const QString candidate = m_messageModel->neighbour(key);
        if (!moved.contains(candidate)) {
            next = candidate;
        }
    }
    m_messageModel->removeKeys(moved);
    select(next);

    const int count = items.size();
    const bool permanentAll = std::all_of(items.cbegin(), items.cend(), [](const Item &i) {
        return i.permanent;
    });
    const QString what = count == 1 ? tr("1 message") : tr("%1 messages").arg(count);
    const QString label = permanentAll                          ? tr("Deleted %1 for good").arg(what)
        : targetRole == QLatin1String("archive")                ? tr("Archived %1").arg(what)
        : targetRole == QLatin1String("trash")                  ? tr("Deleted %1").arg(what)
        : targetRole == QLatin1String("junk")                   ? tr("Moved %1 to Junk email").arg(what)
        : targetRole == QLatin1String("inbox")                  ? tr("Moved %1 to the Inbox").arg(what)
                                                                : tr("Moved %1").arg(what);
    if (permanentAll) {
        Q_EMIT notice(label);
    } else {
        Q_EMIT undoable(label);
    }

    auto error = std::make_shared<QString>();
    auto done = std::make_shared<QList<Moved>>();
    run([this, items, junkFlag, error, done]() {
        // One server request per folder, not one per message.
        QMap<QPair<QString, QString>, QList<Item>> groups;
        for (const Item &item : items) {
            groups[{item.accountId, item.full}].append(item);
        }
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            GError *gerror = nullptr;
            const QString accountId = it.key().first;
            CamelFolder *source = folderSync(accountId, it.key().second, &gerror);
            if (!source) {
                *error = plainError(gerror, tr("Those messages couldn't be moved. Try again in a moment."));
                g_clear_error(&gerror);
                continue;
            }
            QMap<QString, QList<Item>> byTarget;
            for (const Item &item : it.value()) {
                if (item.permanent) {
                    camel_folder_set_message_flags(source, item.uid.toUtf8().constData(), CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
                                                   CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
                } else {
                    byTarget[item.target].append(item);
                }
            }
            for (auto t = byTarget.cbegin(); t != byTarget.cend(); ++t) {
                CamelFolder *dest = folderSync(accountId, t.key(), &gerror);
                if (!dest && t.key() == QLatin1String("Archive")) {
                    g_clear_error(&gerror);
                    if (CamelFolderInfo *fi = camel_store_create_folder_sync(camel_folder_get_parent_store(source), nullptr, "Archive", nullptr, &gerror)) {
                        camel_folder_info_free(fi);
                        dest = folderSync(accountId, t.key(), &gerror);
                    }
                }
                if (!dest) {
                    *error = plainError(gerror, tr("Those messages couldn't be moved. Try again in a moment."));
                    g_clear_error(&gerror);
                    continue;
                }
                GPtrArray *uids = g_ptr_array_new();
                QList<QByteArray> keep;
                for (const Item &item : t.value()) {
                    keep << item.uid.toUtf8();
                }
                for (QByteArray &u : keep) {
                    camel_folder_set_message_flags(source, u.constData(), CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_NOTJUNK, junkFlag ? CAMEL_MESSAGE_JUNK : 0);
                    g_ptr_array_add(uids, u.data());
                }
                GPtrArray *newUids = nullptr;
                if (!camel_folder_transfer_messages_to_sync(source, uids, dest, TRUE, &newUids, nullptr, &gerror)) {
                    *error = plainError(gerror, tr("Those messages couldn't be moved. Try again in a moment."));
                    g_clear_error(&gerror);
                } else if (newUids) {
                    for (guint i = 0; i < newUids->len; ++i) {
                        const char *n = static_cast<const char *>(newUids->pdata[i]);
                        if (n && *n) {
                            done->append({accountId, it.key().second, t.key(), QString::fromUtf8(n)});
                        }
                    }
                }
                if (newUids) {
                    g_ptr_array_set_free_func(newUids, g_free);
                    g_ptr_array_free(newUids, TRUE);
                }
                g_ptr_array_free(uids, TRUE);
            }
            camel_folder_synchronize_sync(source, TRUE, nullptr, nullptr);
        }
    }, [this, error, done]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(*error);
        }
        m_undo = *done;
        Q_EMIT undoChanged();
        reloadMessages();
    });
}

void MailEngine::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    const QList<Moved> moves = m_undo;
    m_undo.clear();
    Q_EMIT undoChanged();
    auto error = std::make_shared<QString>();
    run([this, moves, error]() {
        for (const Moved &m : moves) {
            GError *gerror = nullptr;
            CamelFolder *from = folderSync(m.accountId, m.to, &gerror);
            CamelFolder *back = from ? folderSync(m.accountId, m.from, &gerror) : nullptr;
            if (!from || !back) {
                *error = plainError(gerror, tr("That couldn't be put back. Try again in a moment."));
                g_clear_error(&gerror);
                continue;
            }
            camel_folder_refresh_info_sync(from, nullptr, nullptr);
            GPtrArray *uids = g_ptr_array_new();
            QByteArray u = m.uid.toUtf8();
            g_ptr_array_add(uids, u.data());
            camel_folder_set_message_flags(from, u.constData(), CAMEL_MESSAGE_JUNK, 0);
            if (!camel_folder_transfer_messages_to_sync(from, uids, back, TRUE, nullptr, nullptr, &gerror)) {
                *error = plainError(gerror, tr("That couldn't be put back. Try again in a moment."));
                g_clear_error(&gerror);
            }
            g_ptr_array_free(uids, TRUE);
            camel_folder_synchronize_sync(from, TRUE, nullptr, nullptr);
        }
    }, [this, error]() {
        Q_EMIT notice(error->isEmpty() ? tr("Put back") : *error);
        reloadMessages();
    });
}

void MailEngine::remove(const QString &key)
{
    removeKeys({key});
}

void MailEngine::archive(const QString &key)
{
    archiveKeys({key});
}

void MailEngine::reportJunk(const QString &key)
{
    junkKeys({key});
}

void MailEngine::notJunk(const QString &key)
{
    transfer({key}, QStringLiteral("inbox"), {}, false);
}

void MailEngine::moveTo(const QString &key, const QString &fullName)
{
    moveKeys({key}, fullName);
}

void MailEngine::removeKeys(const QStringList &keys)
{
    transfer(keys, QStringLiteral("trash"), {}, false);
}

void MailEngine::archiveKeys(const QStringList &keys)
{
    transfer(keys, QStringLiteral("archive"), {}, false);
}

void MailEngine::junkKeys(const QStringList &keys)
{
    transfer(keys, QStringLiteral("junk"), {}, true);
}

void MailEngine::moveKeys(const QStringList &keys, const QString &fullName)
{
    transfer(keys, QStringLiteral("folder"), fullName, false);
}

void MailEngine::setReadKeys(const QStringList &keys, bool read)
{
    for (const QString &key : keys) {
        setRead(key, read);
    }
}

void MailEngine::setFlaggedKeys(const QStringList &keys, bool flagged)
{
    for (const QString &key : keys) {
        m_messageModel->updateRow(key, [flagged](MessageRow &r) {
            r.flagged = flagged;
        });
        setFlags(key, CAMEL_MESSAGE_FLAGGED, flagged ? CAMEL_MESSAGE_FLAGGED : 0);
    }
}

void MailEngine::toggleSelect(const QString &key)
{
    if (m_selection.contains(key)) {
        m_selection.removeAll(key);
        if (m_selected == key) {
            m_selected = m_selection.isEmpty() ? QString() : m_selection.last();
        }
    } else {
        m_selection.append(key);
        m_selected = key;
    }
    m_anchor = key;
    Q_EMIT selectedChanged();
}

void MailEngine::extendSelect(const QString &key)
{
    const int from = m_messageModel->indexOf(m_anchor.isEmpty() ? m_selected : m_anchor);
    const int to = m_messageModel->indexOf(key);
    if (from < 0 || to < 0) {
        select(key);
        return;
    }
    m_selection.clear();
    for (int i = qMin(from, to); i <= qMax(from, to); ++i) {
        m_selection.append(m_messageModel->keyAt(i));
    }
    m_selected = key;
    Q_EMIT selectedChanged();
}

void MailEngine::selectAll()
{
    m_selection.clear();
    for (int i = 0; i < m_messageModel->rowCount(); ++i) {
        m_selection.append(m_messageModel->keyAt(i));
    }
    Q_EMIT selectedChanged();
}

void MailEngine::markFolderRead(const QString &accountId, const QString &fullName)
{
    run([this, accountId, fullName]() {
        CamelFolder *folder = folderSync(accountId, fullName, nullptr);
        if (!folder) {
            return;
        }
        GPtrArray *uids = camel_folder_get_uids(folder);
        camel_folder_freeze(folder);
        for (guint i = 0; i < uids->len; ++i) {
            camel_folder_set_message_flags(folder, static_cast<const char *>(uids->pdata[i]), CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
        }
        camel_folder_thaw(folder);
        camel_folder_free_uids(folder, uids);
        camel_folder_synchronize_sync(folder, FALSE, nullptr, nullptr);
    }, [this]() {
        reloadMessages();
    });
}

void MailEngine::renameFolder(const QString &accountId, const QString &fullName, const QString &name)
{
    if (!m_live.contains(accountId) || name.trimmed().isEmpty() || name.contains(QLatin1Char('/'))) {
        return;
    }
    CamelStore *store = m_live[accountId].store;
    g_object_ref(store);
    const int slash = fullName.lastIndexOf(QLatin1Char('/'));
    const QString target = (slash >= 0 ? fullName.left(slash + 1) : QString()) + name.trimmed();
    auto error = std::make_shared<QString>();
    run([this, store, accountId, fullName, target, error]() {
        {
            // Camel's open folder goes with the old name.
            QMutexLocker lock(&m_folderLock);
            if (CamelFolder *f = m_folderCache.take(accountId + keySep + fullName)) {
                m_watched.remove(f);
                g_object_unref(f);
            }
        }
        GError *gerror = nullptr;
        if (!camel_store_rename_folder_sync(store, fullName.toUtf8().constData(), target.toUtf8().constData(), nullptr, &gerror)) {
            *error = plainError(gerror, tr("The folder couldn't be renamed. Try again in a moment."));
            g_clear_error(&gerror);
        }
        g_object_unref(store);
    }, [this, accountId, fullName, target, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(*error);
        } else if (m_folderAccount == accountId && m_folderFullName == fullName) {
            openFolder(accountId, target);
        }
        listFolders(accountId);
    });
}

void MailEngine::deleteFolder(const QString &accountId, const QString &fullName)
{
    if (!m_live.contains(accountId)) {
        return;
    }
    CamelStore *store = m_live[accountId].store;
    g_object_ref(store);
    auto error = std::make_shared<QString>();
    run([this, store, accountId, fullName, error]() {
        {
            QMutexLocker lock(&m_folderLock);
            if (CamelFolder *f = m_folderCache.take(accountId + keySep + fullName)) {
                m_watched.remove(f);
                g_object_unref(f);
            }
        }
        GError *gerror = nullptr;
        if (!camel_store_delete_folder_sync(store, fullName.toUtf8().constData(), nullptr, &gerror)) {
            *error = plainError(gerror, tr("The folder couldn't be deleted. Try again in a moment."));
            g_clear_error(&gerror);
        }
        g_object_unref(store);
    }, [this, accountId, fullName, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(*error);
        } else if (m_folderAccount == accountId && m_folderFullName == fullName) {
            openFolder({}, QStringLiteral("inbox"));
        }
        listFolders(accountId);
    });
}

// Deleted items and Junk email: everything in them goes for good.
void MailEngine::emptyFolder(const QString &accountId, const QString &fullName)
{
    if (m_folderAccount == accountId && m_folderFullName == fullName) {
        m_messageModel->reset({});
    }
    run([this, accountId, fullName]() {
        CamelFolder *folder = folderSync(accountId, fullName, nullptr);
        if (!folder) {
            return;
        }
        GPtrArray *uids = camel_folder_get_uids(folder);
        camel_folder_freeze(folder);
        for (guint i = 0; i < uids->len; ++i) {
            camel_folder_set_message_flags(folder, static_cast<const char *>(uids->pdata[i]), CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
        }
        camel_folder_thaw(folder);
        camel_folder_free_uids(folder, uids);
        camel_folder_synchronize_sync(folder, TRUE, nullptr, nullptr);
    }, [this]() {
        Q_EMIT notice(tr("Emptied"));
        reloadMessages();
    });
}

QVariantList MailEngine::moveTargets(const QString &accountId) const
{
    QVariantList list;
    for (const FolderEntry &f : m_live.value(accountId).folders) {
        list.append(QVariantMap{{QStringLiteral("fullName"), f.fullName},
                                {QStringLiteral("name"), f.displayName},
                                {QStringLiteral("role"), f.role},
                                {QStringLiteral("depth"), f.depth}});
    }
    return list;
}

void MailEngine::createFolder(const QString &accountId, const QString &name)
{
    if (!m_live.contains(accountId) || name.trimmed().isEmpty()) {
        return;
    }
    CamelStore *store = m_live[accountId].store;
    g_object_ref(store);
    auto error = std::make_shared<QString>();
    const QByteArray n = name.trimmed().toUtf8();
    run([store, n, error]() {
        GError *gerror = nullptr;
        if (CamelFolderInfo *fi = camel_store_create_folder_sync(store, nullptr, n.constData(), nullptr, &gerror)) {
            camel_folder_info_free(fi);
        } else {
            *error = plainError(gerror, tr("The folder couldn't be created. Try again in a moment."));
            g_clear_error(&gerror);
        }
        g_object_unref(store);
    }, [this, accountId, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(*error);
        }
        listFolders(accountId);
    });
}

// Searches the summaries kept on this PC: sender, recipients and subject.
// Camel would send a body search to the server, so Mail doesn't ask for
// one; nothing typed here leaves the PC.
void MailEngine::search(const QString &text)
{
    const QString query = text.trimmed();
    if (query.isEmpty()) {
        clearSearch();
        return;
    }
    const bool started = m_searchText.isEmpty();
    m_searchText = query;
    if (started) {
        m_messageModel->setFilter(QStringLiteral("all"));
        m_selected.clear();
        Q_EMIT selectedChanged();
    }
    Q_EMIT searchChanged();
    Q_EMIT folderChanged();

    QList<QPair<QString, QString>> folders;
    for (const Account &a : m_store.accounts()) {
        for (const FolderEntry &f : m_live.value(a.id).folders) {
            if (f.role != QLatin1String("trash") && f.role != QLatin1String("junk")) {
                folders.append({a.id, f.fullName});
            }
        }
    }
    const int generation = ++m_generation;
    auto rows = std::make_shared<QList<MessageRow>>();
    run([this, folders, query, rows]() {
        QByteArray escaped = query.toUtf8();
        escaped.replace("\\", "\\\\").replace("\"", "\\\"");
        const QByteArray expr = "(match-all (or (header-contains \"subject\" \"" + escaped + "\") (header-contains \"from\" \"" + escaped
            + "\") (header-contains \"to\" \"" + escaped + "\") (header-contains \"cc\" \"" + escaped + "\")))";
        for (const auto &[accountId, full] : folders) {
            CamelFolder *folder = folderSync(accountId, full, nullptr);
            if (!folder) {
                continue;
            }
            GPtrArray *uids = camel_folder_search_by_expression(folder, expr.constData(), nullptr, nullptr);
            if (!uids) {
                continue;
            }
            for (guint i = 0; i < uids->len; ++i) {
                if (CamelMessageInfo *mi = camel_folder_get_message_info(folder, static_cast<const char *>(uids->pdata[i]))) {
                    if (!(camel_message_info_get_flags(mi) & CAMEL_MESSAGE_DELETED)) {
                        rows->append(rowFrom(accountId, full, mi));
                    }
                    g_object_unref(mi);
                }
            }
            camel_folder_search_free(folder, uids);
        }
    }, [this, generation, rows]() {
        if (generation == m_generation) {
            m_messageModel->reset(*rows);
        }
    });
}

namespace
{
QByteArray sexpString(const QString &text)
{
    QByteArray escaped = text.toUtf8();
    escaped.replace("\\", "\\\\").replace("\"", "\\\"");
    return "\"" + escaped + "\"";
}
}

void MailEngine::findWith(const QString &address, int limit)
{
    const QString who = address.trimmed().toLower();
    if (who.isEmpty()) {
        Q_EMIT foundWith(address, {});
        return;
    }
    QList<QPair<QString, QString>> folders;
    for (const Account &a : m_store.accounts()) {
        for (const FolderEntry &f : m_live.value(a.id).folders) {
            if (f.role != QLatin1String("trash") && f.role != QLatin1String("junk") && f.role != QLatin1String("drafts")) {
                folders.append({a.id, f.fullName});
            }
        }
    }
    auto rows = std::make_shared<QList<MessageRow>>();
    run([this, folders, who, rows]() {
        const QByteArray q = sexpString(who);
        const QByteArray expr = "(match-all (or (header-contains \"from\" " + q + ") (header-contains \"to\" " + q
            + ") (header-contains \"cc\" " + q + ")))";
        for (const auto &[accountId, full] : folders) {
            CamelFolder *folder = folderSync(accountId, full, nullptr);
            if (!folder) {
                continue;
            }
            GPtrArray *uids = camel_folder_search_by_expression(folder, expr.constData(), nullptr, nullptr);
            if (!uids) {
                continue;
            }
            for (guint i = 0; i < uids->len; ++i) {
                if (CamelMessageInfo *mi = camel_folder_get_message_info(folder, static_cast<const char *>(uids->pdata[i]))) {
                    if (!(camel_message_info_get_flags(mi) & CAMEL_MESSAGE_DELETED)) {
                        rows->append(rowFrom(accountId, full, mi));
                    }
                    g_object_unref(mi);
                }
            }
            camel_folder_search_free(folder, uids);
        }
    }, [this, address, limit, rows]() {
        std::sort(rows->begin(), rows->end(), [](const MessageRow &a, const MessageRow &b) {
            return a.date > b.date;
        });
        QVariantList out;
        for (const MessageRow &r : std::as_const(*rows)) {
            if (limit > 0 && out.size() >= limit) {
                break;
            }
            out << QVariantMap{
                {QStringLiteral("key"), r.key()},
                {QStringLiteral("subject"), r.subject.isEmpty() ? tr("(No subject)") : r.subject},
                {QStringLiteral("preview"), r.preview},
                {QStringLiteral("time"), MessageModel::timeLabel(r.date)},
                {QStringLiteral("unread"), r.unread},
            };
        }
        Q_EMIT foundWith(address, out);
    });
}

void MailEngine::findFlagged()
{
    QList<QPair<QString, QString>> folders;
    for (const Account &a : m_store.accounts()) {
        for (const FolderEntry &f : m_live.value(a.id).folders) {
            if (f.role != QLatin1String("trash") && f.role != QLatin1String("junk")) {
                folders.append({a.id, f.fullName});
            }
        }
    }
    auto rows = std::make_shared<QList<MessageRow>>();
    run([this, folders, rows]() {
        for (const auto &[accountId, full] : folders) {
            CamelFolder *folder = folderSync(accountId, full, nullptr);
            if (!folder) {
                continue;
            }
            GPtrArray *uids = camel_folder_search_by_expression(folder, "(match-all (system-flag \"Flagged\"))", nullptr, nullptr);
            if (!uids) {
                continue;
            }
            for (guint i = 0; i < uids->len; ++i) {
                if (CamelMessageInfo *mi = camel_folder_get_message_info(folder, static_cast<const char *>(uids->pdata[i]))) {
                    const guint32 flags = camel_message_info_get_flags(mi);
                    if ((flags & CAMEL_MESSAGE_FLAGGED) && !(flags & CAMEL_MESSAGE_DELETED)) {
                        rows->append(rowFrom(accountId, full, mi));
                    }
                    g_object_unref(mi);
                }
            }
            camel_folder_search_free(folder, uids);
        }
    }, [this, rows]() {
        std::sort(rows->begin(), rows->end(), [](const MessageRow &a, const MessageRow &b) {
            return a.date > b.date;
        });
        QVariantList out;
        for (const MessageRow &r : std::as_const(*rows)) {
            out << QVariantMap{
                {QStringLiteral("key"), r.key()},
                {QStringLiteral("subject"), r.subject.isEmpty() ? tr("(No subject)") : r.subject},
                {QStringLiteral("from"), r.fromName.isEmpty() ? r.fromAddress : r.fromName},
                {QStringLiteral("date"), r.date},
            };
        }
        Q_EMIT flaggedFound(out);
    });
}

void MailEngine::clearSearch()
{
    if (m_searchText.isEmpty()) {
        return;
    }
    m_searchText.clear();
    Q_EMIT searchChanged();
    Q_EMIT folderChanged();
    reloadMessages();
}

QVariantMap MailEngine::account(const QString &id) const
{
    const Account *a = m_store.find(id);
    return a ? a->toMap() : QVariantMap();
}

void MailEngine::testAccount(const QVariantMap &settings, const QString &password)
{
    Account account = Account::fromMap(settings);
    const QString testId = QStringLiteral("test-") + AccountStore::newId();
    Session::instance().setPassword(testId, password);
    Session::instance().setOAuth(testId, account.auth == QLatin1String("google"));
    CamelSession *session = Session::instance().camel();
    GError *error = nullptr;
    CamelService *store = camel_session_add_service(session, testId.toUtf8().constData(), "imapx", CAMEL_PROVIDER_STORE, &error);
    if (error) {
        qCWarning(mailLog) << "couldn't create the incoming mail service:" << error->message;
    }
    g_clear_error(&error);
    CamelService *smtp = camel_session_add_service(session, (testId + QStringLiteral(".smtp")).toUtf8().constData(), "smtp", CAMEL_PROVIDER_TRANSPORT, &error);
    if (error) {
        qCWarning(mailLog) << "couldn't create the outgoing mail service:" << error->message;
    }
    g_clear_error(&error);
    if (!store || !smtp) {
        g_clear_object(&store);
        g_clear_object(&smtp);
        Q_EMIT accountTested(false, tr("Mail couldn't start a connection. Close Mail and open it again."));
        return;
    }
    configure(store, smtp, account);
    auto message = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    // A server that never answers gets 45 seconds, then the person is told,
    // rather than left watching a spinner.
    GCancellable *cancellable = g_cancellable_new();
    g_object_ref(cancellable);
    QTimer::singleShot(45000, this, [cancellable]() {
        g_cancellable_cancel(cancellable);
        g_object_unref(cancellable);
    });
    qCInfo(mailLog) << "testing account" << account.imapHost << account.imapPort << account.imapSecurity << account.smtpHost << account.smtpPort
                    << account.smtpSecurity << "sign-in" << account.auth;
    run([store, smtp, message, ok, cancellable]() {
        GError *gerror = nullptr;
        camel_offline_store_set_online_sync(CAMEL_OFFLINE_STORE(store), TRUE, cancellable, nullptr);
        qCInfo(mailLog) << "connecting to incoming server";
        if (!camel_service_connect_sync(store, cancellable, &gerror)) {
            if (g_cancellable_is_cancelled(cancellable)) {
                *message = MailEngine::tr("The incoming mail server didn't answer. Check the server name and port, and that this PC is online.");
                g_clear_error(&gerror);
                return;
            }
            const bool auth = gerror->domain == CAMEL_SERVICE_ERROR && gerror->code == CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE;
            *message = auth ? MailEngine::tr("Your email provider didn't accept that password.") : MailEngine::tr("The incoming mail server couldn't be reached. %1").arg(plainError(gerror, MailEngine::tr("Check the server name, the port and the security setting.")));
            g_clear_error(&gerror);
            return;
        }
        qCInfo(mailLog) << "incoming server signed in";
        camel_service_disconnect_sync(store, TRUE, nullptr, nullptr);
        // Picks a sign-in method the outgoing server offers.
        // Google accounts keep XOAUTH2.
        CamelSettings *current = camel_service_ref_settings(smtp);
        gchar *chosen = nullptr;
        g_object_get(current, "auth-mechanism", &chosen, nullptr);
        g_object_unref(current);
        const bool keep = g_strcmp0(chosen, "XOAUTH2") == 0;
        g_free(chosen);
        GList *types = keep ? nullptr : camel_service_query_auth_types_sync(smtp, cancellable, nullptr);
        const char *mechanism = nullptr;
        for (GList *l = types; l; l = l->next) {
            auto *type = static_cast<CamelServiceAuthType *>(l->data);
            if (g_strcmp0(type->authproto, "PLAIN") == 0) {
                mechanism = "PLAIN";
                break;
            }
            if (g_strcmp0(type->authproto, "LOGIN") == 0) {
                mechanism = "LOGIN";
            }
        }
        g_list_free(types);
        if (mechanism) {
            CamelSettings *s = camel_service_ref_settings(smtp);
            g_object_set(s, "auth-mechanism", mechanism, nullptr);
            g_object_unref(s);
        }
        qCInfo(mailLog) << "connecting to outgoing server with" << (mechanism ? mechanism : (keep ? "XOAUTH2" : "default"));
        if (!camel_service_connect_sync(smtp, cancellable, &gerror)) {
            if (g_cancellable_is_cancelled(cancellable)) {
                *message = MailEngine::tr("The outgoing mail server didn't answer. Check the server name and port, and that this PC is online.");
                g_clear_error(&gerror);
                return;
            }
            const bool auth = gerror->domain == CAMEL_SERVICE_ERROR && gerror->code == CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE;
            *message = auth ? MailEngine::tr("The outgoing mail server didn't accept that password.") : MailEngine::tr("The outgoing mail server couldn't be reached. %1").arg(plainError(gerror, MailEngine::tr("Check the server name, the port and the security setting.")));
            g_clear_error(&gerror);
            return;
        }
        camel_service_disconnect_sync(smtp, TRUE, nullptr, nullptr);
        qCInfo(mailLog) << "outgoing server signed in";
        *ok = true;
    }, [this, session, store, smtp, testId, message, ok, cancellable]() {
        g_object_unref(cancellable);
        camel_session_remove_service(session, store);
        camel_session_remove_service(session, smtp);
        Session::instance().setPassword(testId, {});
        Q_EMIT accountTested(*ok, *message);
    });
}

void MailEngine::saveAccount(const QVariantMap &settings, const QString &password)
{
    Account account = Account::fromMap(settings);
    if (account.id.isEmpty()) {
        account.id = AccountStore::newId();
    }
    if (account.name == account.email && m_store.accounts().isEmpty()) {
        account.name = tr("Personal");
    }
    Session::instance().setPassword(account.id, password);
    Session::instance().setOAuth(account.id, account.auth == QLatin1String("google"));
    m_store.writePassword(account.id, password);
    m_store.add(account);
    startAccount(account);
    Session::instance().setPassword(account.id, password);
    connectAccount(account.id);
    rebuildFolderModel();
    if (m_store.accounts().size() == 1) {
        openFolder({}, QStringLiteral("inbox"));
    }
    Q_EMIT accountSaved(account.id);
}

void MailEngine::updateAccount(const QVariantMap &settings)
{
    Account account = Account::fromMap(settings);
    if (!m_store.find(account.id)) {
        return;
    }
    m_store.update(account);
    if (m_live.contains(account.id)) {
        configure(CAMEL_SERVICE(m_live[account.id].store), m_live[account.id].smtp, account);
    }
    rebuildFolderModel();
}

void MailEngine::removeAccount(const QString &id)
{
    stopAccount(id);
    m_store.remove(id);
    // What was kept on this PC for the account goes with it.
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/") + id).removeRecursively();
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/") + id).removeRecursively();
    QDir(outboxDir(id)).removeRecursively();
    if (m_folderAccount == id) {
        openFolder({}, QStringLiteral("inbox"));
    } else {
        reloadMessages();
    }
    rebuildFolderModel();
    Q_EMIT problemsChanged();
}

void MailEngine::signInAgain(const QString &id, const QString &password)
{
    Session::instance().setPassword(id, password);
    m_store.writePassword(id, password);
    setProblem(id, {}, {});
    connectAccount(id);
}

void MailEngine::queueOutbox(const QString &accountId, const QByteArray &message)
{
    QDir().mkpath(outboxDir(accountId));
    QFile file(outboxDir(accountId) + QStringLiteral("/") + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".eml"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(message);
    }
    Q_EMIT outboxChanged();
    rebuildFolderModel();
}

// Sends what waited in the Outbox, oldest first, once the account is
// reachable again.
void MailEngine::sendOutbox()
{
    if (!m_online) {
        return;
    }
    for (const Account &a : m_store.accounts()) {
        const QFileInfoList files = QDir(outboxDir(a.id)).entryInfoList({QStringLiteral("*.eml")}, QDir::Files, QDir::Time | QDir::Reversed);
        if (files.isEmpty() || !m_live.value(a.id).smtp || Session::instance().password(a.id).isEmpty()) {
            continue;
        }
        CamelService *smtp = m_live[a.id].smtp;
        g_object_ref(smtp);
        const QString id = a.id;
        const bool gmail = a.provider == QLatin1String("gmail");
        auto failed = std::make_shared<bool>(false);
        auto failure = std::make_shared<QString>();
        run([this, smtp, files, id, gmail, failed, failure]() {
            for (const QFileInfo &file : files) {
                CamelMimeMessage *message = camel_mime_message_new();
                CamelStream *stream = camel_stream_fs_new_with_name(file.filePath().toUtf8().constData(), O_RDONLY, 0, nullptr);
                bool sent = false;
                if (stream && camel_data_wrapper_construct_from_stream_sync(CAMEL_DATA_WRAPPER(message), stream, nullptr, nullptr)) {
                    CamelAddress *from = CAMEL_ADDRESS(camel_mime_message_get_from(message));
                    CamelAddress *recipients = CAMEL_ADDRESS(camel_internet_address_new());
                    for (const char *type : {CAMEL_RECIPIENT_TYPE_TO, CAMEL_RECIPIENT_TYPE_CC, CAMEL_RECIPIENT_TYPE_BCC}) {
                        if (CamelInternetAddress *list = camel_mime_message_get_recipients(message, type)) {
                            camel_address_cat(recipients, CAMEL_ADDRESS(list));
                        }
                    }
                    gboolean saved = FALSE;
                    GError *error = nullptr;
                    if (camel_service_connect_sync(smtp, nullptr, &error)) {
                        sent = camel_transport_send_to_sync(CAMEL_TRANSPORT(smtp), message, from, recipients, &saved, nullptr, &error);
                    }
                    if (!sent) {
                        *failed = true;
                        *failure = plainError(error, QString());
                    }
                    g_clear_error(&error);
                    g_object_unref(recipients);
                    if (sent && !gmail && !saved) {
                        if (CamelFolder *folder = roleFolderSync(id, QStringLiteral("sent"), nullptr)) {
                            CamelMessageInfo *info = camel_message_info_new(nullptr);
                            camel_message_info_set_flags(info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
                            camel_folder_append_message_sync(folder, message, info, nullptr, nullptr, nullptr);
                            g_object_unref(info);
                        }
                    }
                }
                g_clear_object(&stream);
                g_object_unref(message);
                if (!sent) {
                    break;
                }
                QFile::remove(file.filePath());
            }
            g_object_unref(smtp);
        }, [this, failed, failure]() {
            if (*failed) {
                Q_EMIT notice(tr("Couldn't send yet, so it's waiting in the Outbox. %1").arg(*failure).trimmed());
            }
            Q_EMIT outboxChanged();
            rebuildFolderModel();
            if (m_folderRole == QLatin1String("outbox")) {
                reloadMessages();
            }
        });
    }
}
