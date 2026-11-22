#include "mailengine.h"
#include "peopleengine.h"
#include "calendarengine.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QDirIterator>
#include <QPointer>
#include <QGuiApplication>
#include <fcntl.h>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimeZone>
#include "composer.h"
#include "microsoftsignin.h"
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QFile>
#include <QNetworkInformation>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
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
    r.list = camel_message_info_get_mlist(mi) && *camel_message_info_get_mlist(mi);
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
    // Microsoft's rotated refresh token goes to the password store in place
    // of the old one, which stops working 90 days after it was issued.
    QPointer<MailEngine> self(this);
    Session::instance().setRefreshSaver([self](const QString &id, const QString &refreshToken) {
        if (id.startsWith(QLatin1String("test-"))) {
            return;
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, refreshToken]() {
            if (self && self->m_store.find(id)) {
                self->m_store.writePassword(id, refreshToken);
            }
        }, Qt::QueuedConnection);
    });
    loadRules();
    loadSignatures();
    m_messageModel->folderName = [this](const QString &accountId, const QString &folder) {
        return folderLabel(accountId, folder);
    };

    // A transfer's counts come from its worker; the page reads them a few
    // times a second rather than being told of every message.
    m_transferTick.setInterval(250);
    connect(&m_transferTick, &QTimer::timeout, this, &MailEngine::transferChanged);

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
    // Each account's own "Check for new mail": looked at once a minute.
    m_pollTimer.setInterval(60 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &MailEngine::pollDue);
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
        map.insert(QStringLiteral("isDefault"), a.id == defaultAccount());
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
                     "use-idle", account.checkEvery == 0,
                     "check-all", TRUE,
                     "use-subscriptions", FALSE,
                     "stay-synchronized", account.keepAll,
                     "limit-by-age", account.keepAll && account.keepMonths > 0,
                     "limit-unit", CAMEL_TIME_UNIT_MONTHS,
                     "limit-value", account.keepMonths > 0 ? account.keepMonths : 1,
                     "auth-mechanism", account.signsInWithOAuth() ? "XOAUTH2" : nullptr,
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
                     "auth-mechanism", account.signsInWithOAuth() ? "XOAUTH2" : "PLAIN",
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
    Session::instance().setOAuth(account.id, account.auth);

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
    QSet<QString> outgoingFolders;
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it) {
        for (const FolderEntry &f : it->folders) {
            if (f.role == QLatin1String("sent") || f.role == QLatin1String("drafts")) {
                outgoingFolders.insert(it.key() + keySep + f.fullName);
            }
        }
    }
    // The Outbox is files on this PC, read in one go.
    if (outbox) {
        run([outboxAccount, rows]() {
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
        }, [this, generation, rows]() {
            if (generation == m_generation) {
                m_messageModel->reset(*rows);
            }
        });
        return;
    }

    // What this PC already has shows straight away; each folder is then
    // read on its own and replaces its rows as soon as it is done.
    showFolderRows();
    for (const auto &[accountId, full] : folders) {
        const QString folderKey = accountId + keySep + full;
        auto folderRows = std::make_shared<QList<MessageRow>>();
        auto isInbox = std::make_shared<bool>(false);
        const bool outgoing = outgoingFolders.contains(folderKey);
        run([this, accountId, full, folderRows, isInbox, outgoing]() {
            GError *error = nullptr;
            CamelFolder *folder = folderSync(accountId, full, &error);
            g_clear_error(&error);
            if (!folder) {
                return;
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
                    r.outgoing = outgoing;
                    folderRows->append(r);
                }
                g_object_unref(mi);
            }
            camel_folder_free_uids(folder, uids);
            *isInbox = (camel_folder_get_flags(folder) & CAMEL_FOLDER_IS_JUNK) == 0 && full.compare(QLatin1String("INBOX"), Qt::CaseInsensitive) == 0;
        }, [this, generation, accountId, folderKey, folderRows, isInbox]() {
            if (*isInbox) {
                checkNewMail(accountId, *folderRows);
            }
            m_folderRows.insert(folderKey, *folderRows);
            if (generation == m_generation) {
                showFolderRows(folderKey);
            }
        });
    }
}

// The rows of the folders on view, from each folder's last read, with
// moves still on their way to the server left out and Undo's put back.
void MailEngine::showFolderRows(const QString &fresh)
{
    const QList<QPair<QString, QString>> viewed = currentFolders();
    QList<MessageRow> rows;
    for (const auto &[accountId, full] : viewed) {
        rows += m_folderRows.value(accountId + keySep + full);
    }
    // A row already on screen may carry a change made since its folder was
    // last read (marked read, flagged): that copy wins until the folder has
    // been read again. The folder just read is shown as the server has it.
    for (MessageRow &r : rows) {
        if (r.accountId + keySep + r.folder == fresh) {
            continue;
        }
        if (const MessageRow *shown = m_messageModel->row(r.key())) {
            r.unread = shown->unread;
            r.flagged = shown->flagged;
            r.pinned = shown->pinned;
            r.snoozedUntil = shown->snoozedUntil;
        }
    }
    if (!m_pendingGone.isEmpty()) {
        rows.erase(std::remove_if(rows.begin(), rows.end(), [this](const MessageRow &r) {
            return m_pendingGone.contains(r.key());
        }), rows.end());
    }
    for (const MessageRow &r : std::as_const(m_pendingBack)) {
        if (viewed.contains(QPair<QString, QString>{r.accountId, r.folder})) {
            rows.append(r);
        }
    }
    m_messageModel->reset(rows);
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
    m_lastInbox.insert(accountId, inboxRows);
    QList<MessageRow> arrived;
    for (const MessageRow &r : inboxRows) {
        if (live.inboxPrimed && !live.knownInboxUids.contains(r.uid)) {
            arrived.append(r);
        }
    }
    const QSet<QString> handled = applyRules(arrived);
    sendAutoReplies(accountId, arrived, handled);
    QSet<QString> now;
    for (const MessageRow &r : inboxRows) {
        now.insert(r.uid);
        if (live.inboxPrimed && r.unread && !live.knownInboxUids.contains(r.uid) && !handled.contains(r.key())
            && r.date > QDateTime::currentDateTime().addDays(-1)) {
            Q_EMIT newMail(r.key(), r.fromName, r.subject, r.preview);
        }
    }
    live.knownInboxUids.unite(now);
    live.inboxPrimed = live.connected || live.inboxPrimed;
}

QString MailEngine::defaultAccount() const
{
    const QString id = QSettings().value(QStringLiteral("mail/defaultAccount")).toString();
    if (m_store.find(id)) {
        return id;
    }
    return m_store.accounts().isEmpty() ? QString() : m_store.accounts().first().id;
}

void MailEngine::setDefaultAccount(const QString &id)
{
    if (!m_store.find(id) || id == defaultAccount()) {
        return;
    }
    QSettings().setValue(QStringLiteral("mail/defaultAccount"), id);
    Q_EMIT accountsChanged();
}

// Every minute: the accounts whose "Check for new mail" time has come. One
// that gets mail as it arrives still has its other folders looked at every
// five minutes; one set to "only when I check" waits for F5.
void MailEngine::pollDue()
{
    const QDateTime now = QDateTime::currentDateTime();
    for (const Account &a : m_store.accounts()) {
        const int every = a.checkEvery == 0 ? 5 : a.checkEvery;
        if (a.checkEvery < 0) {
            continue;
        }
        const QDateTime last = m_lastCheck.value(a.id);
        if (!last.isValid() || last.addSecs(every * 60 - 5) <= now) {
            refreshAccount(a.id);
        }
    }
}

void MailEngine::refresh()
{
    for (const Account &a : m_store.accounts()) {
        refreshAccount(a.id);
    }
}

void MailEngine::refreshAccount(const QString &id)
{
    if (!m_online) {
        return;
    }
    m_lastCheck.insert(id, QDateTime::currentDateTime());
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it) {
        if (it.key() != id) {
            continue;
        }
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
    QList<MessageRow> movedRows;
    for (const QString &key : std::as_const(moved)) {
        if (const MessageRow *r = m_messageModel->row(key)) {
            movedRows.append(*r);
        }
    }
    m_messageModel->removeKeys(moved);
    for (const QString &key : std::as_const(moved)) {
        m_pendingGone.insert(key);
    }
    for (QList<MessageRow> &cached : m_folderRows) {
        cached.erase(std::remove_if(cached.begin(), cached.end(), [&moved](const MessageRow &r) {
            return moved.contains(r.key());
        }), cached.end());
    }
    const bool quiet = m_quiet;
    int undoId = 0;

    if (!quiet) {
        select(next);
    }

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
    if (quiet) {
        // A rule's work: nothing to announce or undo.
    } else if (permanentAll) {
        Q_EMIT notice(label);
    } else {
        UndoBatch batch;
        batch.id = undoId = m_nextUndoId++;
        batch.rows = movedRows;
        m_undoStack.append(batch);
        while (m_undoStack.size() > 10) {
            m_undoStack.removeFirst();
        }
        Q_EMIT undoChanged();
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
    }, [this, error, done, moved, quiet, undoId]() {
        for (const QString &key : moved) {
            m_pendingGone.remove(key);
        }
        if (!error->isEmpty()) {
            Q_EMIT notice(*error);
        }
        if (quiet || undoId == 0) {
            reloadMessages();
            return;
        }
        for (UndoBatch &b : m_undoStack) {
            if (b.id == undoId) {
                b.moved = *done;
                b.done = true;
            }
        }
        // Undone while the server was still moving it: now it can go back.
        for (int i = 0; i < m_undoing.size(); ++i) {
            if (m_undoing[i].id == undoId) {
                UndoBatch b = m_undoing.takeAt(i);
                b.moved = *done;
                moveBack(b);
                break;
            }
        }
        reloadMessages();
    });
}

void MailEngine::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    UndoBatch batch = m_undoStack.takeLast();
    Q_EMIT undoChanged();
    // On screen again straight away.
    for (const MessageRow &r : std::as_const(batch.rows)) {
        m_pendingBack.insert(r.key(), r);
        m_pendingGone.remove(r.key());
    }
    reloadMessages();
    if (batch.done) {
        moveBack(batch);
    } else {
        m_undoing.append(batch);
    }
}

void MailEngine::moveBack(const UndoBatch &batch)
{
    const QList<Moved> moves = batch.moved;
    QStringList keys;
    for (const MessageRow &r : batch.rows) {
        keys.append(r.key());
    }
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
    }, [this, error, keys]() {
        for (const QString &key : keys) {
            m_pendingBack.remove(key);
        }
        if (!error->isEmpty()) {
            Q_EMIT notice(*error);
        }
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
    Session::instance().setOAuth(testId, account.auth);
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
        // Google and Microsoft accounts keep XOAUTH2.
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
    Session::instance().setOAuth(account.id, account.auth);
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

namespace
{
QString rulesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/rules.json");
}
}

void MailEngine::loadRules()
{
    QFile file(rulesPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    m_rules = QJsonDocument::fromJson(file.readAll()).array().toVariantList();
    // The first rules (one condition, one action, no name or account) in
    // the shape of the rule editor's.
    bool changed = false;
    for (QVariant &v : m_rules) {
        QVariantMap rule = v.toMap();
        if (rule.contains(QStringLiteral("conditions"))) {
            continue;
        }
        const QString field = rule.value(QStringLiteral("field"), QStringLiteral("from")).toString();
        const QString text = rule.value(QStringLiteral("contains")).toString();
        QVariantMap action{{QStringLiteral("type"), rule.value(QStringLiteral("action")).toString()}};
        if (action.value(QStringLiteral("type")) == QLatin1String("move")) {
            action.insert(QStringLiteral("folder"), rule.value(QStringLiteral("folder")));
        }
        QString accountId = rule.value(QStringLiteral("accountId")).toString();
        if (accountId.isEmpty() && !m_store.accounts().isEmpty()) {
            accountId = m_store.accounts().first().id;
        }
        v = QVariantMap{{QStringLiteral("id"), AccountStore::newId()},
                        {QStringLiteral("name"), text},
                        {QStringLiteral("enabled"), rule.value(QStringLiteral("enabled"), true)},
                        {QStringLiteral("accountId"), accountId},
                        {QStringLiteral("match"), QStringLiteral("any")},
                        {QStringLiteral("conditions"), QVariantList{QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("text"), text}}}},
                        {QStringLiteral("actions"), QVariantList{action}},
                        {QStringLiteral("stop"), true}};
        changed = true;
    }
    if (changed) {
        setRules(m_rules);
    }
}

void MailEngine::setRules(const QVariantList &rules)
{
    m_rules = rules;
    QDir().mkpath(QFileInfo(rulesPath()).absolutePath());
    QSaveFile file(rulesPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonArray::fromVariantList(rules)).toJson());
        file.commit();
    }
    Q_EMIT rulesChanged();
}

namespace
{
// Whether one rule's conditions hold for a message: any of them, or all.
bool ruleMatches(const QVariantMap &rule, const MessageRow &r)
{
    const QVariantList conditions = rule.value(QStringLiteral("conditions")).toList();
    if (conditions.isEmpty()) {
        return false;
    }
    const bool all = rule.value(QStringLiteral("match")).toString() == QLatin1String("all");
    for (const QVariant &c : conditions) {
        const QVariantMap condition = c.toMap();
        const QString field = condition.value(QStringLiteral("field")).toString();
        const QString text = condition.value(QStringLiteral("text")).toString().trimmed();
        bool holds = false;
        if (field == QLatin1String("list")) {
            holds = r.list;
        } else if (field == QLatin1String("attachment")) {
            holds = r.attachment;
        } else if (!text.isEmpty()) {
            const QString haystack = field == QLatin1String("subject") ? r.subject
                : field == QLatin1String("to")                         ? r.to + QLatin1Char(' ') + r.toAddress
                                                                       : r.fromName + QLatin1Char(' ') + r.fromAddress;
            holds = haystack.contains(text, Qt::CaseInsensitive);
        }
        if (all && !holds) {
            return false;
        }
        if (!all && holds) {
            return true;
        }
    }
    return all;
}
}

QSet<QString> MailEngine::applyRules(const QList<MessageRow> &rows, const QString &onlyRule)
{
    QSet<QString> handled;
    if (m_rules.isEmpty() || rows.isEmpty()) {
        return handled;
    }
    // Keys per action, so each action runs once for all its messages. Marks
    // (read, flag, pin) go before the one move a message can make, which
    // the last rule to ask for one decides.
    QHash<QString, QStringList> marks;
    QHash<QString, QString> moveOf;
    for (const MessageRow &r : rows) {
        for (const QVariant &v : std::as_const(m_rules)) {
            const QVariantMap rule = v.toMap();
            if (!onlyRule.isEmpty() && rule.value(QStringLiteral("id")).toString() != onlyRule) {
                continue;
            }
            if ((onlyRule.isEmpty() && !rule.value(QStringLiteral("enabled"), true).toBool())
                || rule.value(QStringLiteral("accountId")).toString() != r.accountId || !ruleMatches(rule, r)) {
                continue;
            }
            for (const QVariant &a : rule.value(QStringLiteral("actions")).toList()) {
                const QVariantMap action = a.toMap();
                const QString type = action.value(QStringLiteral("type")).toString();
                if (type == QLatin1String("move")) {
                    const QString folder = action.value(QStringLiteral("folder")).toString();
                    if (!folder.isEmpty() && folder != r.folder) {
                        moveOf.insert(r.key(), QStringLiteral("move\x1f") + folder);
                    }
                } else if (type == QLatin1String("archive") || type == QLatin1String("delete")) {
                    moveOf.insert(r.key(), type);
                } else if (type == QLatin1String("pin")) {
                    if (!r.pinned) {
                        marks[type].append(r.key());
                    }
                } else if (!type.isEmpty()) {
                    marks[type].append(r.key());
                }
                if (type != QLatin1String("flag") && type != QLatin1String("pin")) {
                    handled.insert(r.key());
                }
            }
            if (rule.value(QStringLiteral("stop")).toBool()) {
                break;
            }
        }
    }
    QHash<QString, QStringList> moves;
    for (auto it = moveOf.cbegin(); it != moveOf.cend(); ++it) {
        moves[it.value()].append(it.key());
    }
    m_quiet = true;
    setReadKeys(marks.value(QStringLiteral("read")), true);
    setFlaggedKeys(marks.value(QStringLiteral("flag")), true);
    for (const QString &key : marks.value(QStringLiteral("pin"))) {
        togglePin(key);
    }
    for (auto it = moves.cbegin(); it != moves.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("move\x1f"))) {
            moveKeys(it.value(), it.key().mid(5));
        } else if (it.key() == QLatin1String("archive")) {
            archiveKeys(it.value());
        } else if (it.key() == QLatin1String("delete")) {
            removeKeys(it.value());
        }
    }
    m_quiet = false;
    return handled;
}

int MailEngine::countRuleMatches(const QVariantMap &rule) const
{
    int count = 0;
    for (const MessageRow &r : m_lastInbox.value(rule.value(QStringLiteral("accountId")).toString())) {
        count += ruleMatches(rule, r) ? 1 : 0;
    }
    return count;
}

void MailEngine::runRule(const QString &id)
{
    QString accountId;
    for (const QVariant &v : std::as_const(m_rules)) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id) {
            accountId = v.toMap().value(QStringLiteral("accountId")).toString();
        }
    }
    const int count = applyRules(m_lastInbox.value(accountId), id).size();
    Q_EMIT notice(count == 0 ? tr("No messages in the Inbox matched this rule.")
                  : count == 1 ? tr("The rule took care of 1 message.")
                               : tr("The rule took care of %1 messages.").arg(count));
}

namespace
{
// A Graph request for the signed-in person's mailbox settings, waited for
// here (it runs on a worker thread).
QJsonObject graphMailbox(const QString &token, const QByteArray &patch, QString *error)
{
    QNetworkAccessManager network;
    network.setCookieJar(nullptr);
    QNetworkRequest request{QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/mailboxSettings"))};
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(30000);
    QNetworkReply *reply = patch.isEmpty() ? network.get(request) : network.sendCustomRequest(request, "PATCH", patch);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() != QNetworkReply::NoError) {
        const QString message = json.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        *error = message.isEmpty() ? reply->errorString() : message;
    }
    delete reply;
    return json;
}

QJsonObject graphTime(const QDateTime &when)
{
    return {{QStringLiteral("dateTime"), when.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))}, {QStringLiteral("timeZone"), QStringLiteral("UTC")}};
}

QDateTime fromGraphTime(const QJsonObject &o)
{
    QDateTime t = QDateTime::fromString(o.value(QStringLiteral("dateTime")).toString().left(19), QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    if (o.value(QStringLiteral("timeZone")).toString() == QLatin1String("UTC")) {
        t.setTimeZone(QTimeZone::UTC);
    }
    return t.toLocalTime();
}

QString plainFromHtml(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QString htmlFromPlain(const QString &text)
{
    return QStringLiteral("<html><body><p>") + text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")) + QStringLiteral("</p></body></html>");
}
}

void MailEngine::loadAutoReply(const QString &accountId)
{
    const Account *account = m_store.find(accountId);
    if (!account) {
        return;
    }
    const bool personal = QRegularExpression(QStringLiteral("@(outlook|hotmail|live|msn)\\."), QRegularExpression::CaseInsensitiveOption).match(account->email).hasMatch();
    if (account->auth != QLatin1String("microsoft")) {
        Q_EMIT autoReplyLoaded(accountId, {{QStringLiteral("enabled"), account->autoReply},
                                           {QStringLiteral("message"), account->autoReplyMessage},
                                           {QStringLiteral("start"), account->autoReplyStart},
                                           {QStringLiteral("end"), account->autoReplyEnd},
                                           {QStringLiteral("subject"), account->autoReplySubject},
                                           {QStringLiteral("audience"), account->autoReplyAudience},
                                           {QStringLiteral("server"), false}});
        return;
    }
    const QString refresh = Session::instance().password(accountId);
    auto result = std::make_shared<QVariantMap>();
    run([refresh, result, personal]() {
        QVariantMap &r = *result;
        r.insert(QStringLiteral("server"), true);
        r.insert(QStringLiteral("personal"), personal);
        QString token, error;
        bool consent = false;
        if (!MicrosoftSignIn::settingsToken(refresh, &token, &error, &consent)) {
            r.insert(QStringLiteral("needsSignIn"), consent);
            r.insert(QStringLiteral("error"), consent ? MailEngine::tr("Sign in again to let Mail set automatic replies for this account.")
                                                      : MailEngine::tr("Microsoft didn't answer. %1").arg(error));
            return;
        }
        const QJsonObject settings = graphMailbox(token, {}, &error).value(QStringLiteral("automaticRepliesSetting")).toObject();
        if (!error.isEmpty()) {
            r.insert(QStringLiteral("error"), MailEngine::tr("Microsoft didn't answer. %1").arg(error));
            return;
        }
        const QString status = settings.value(QStringLiteral("status")).toString();
        r.insert(QStringLiteral("enabled"), status == QLatin1String("alwaysEnabled") || status == QLatin1String("scheduled"));
        r.insert(QStringLiteral("message"), plainFromHtml(settings.value(QStringLiteral("internalReplyMessage")).toString()));
        const QString external = settings.value(QStringLiteral("externalAudience")).toString();
        r.insert(QStringLiteral("audience"), external == QLatin1String("all") ? QStringLiteral("all")
                     : external == QLatin1String("contactsOnly") ? QStringLiteral("contacts") : QStringLiteral("organisation"));
        if (status == QLatin1String("scheduled")) {
            r.insert(QStringLiteral("start"), fromGraphTime(settings.value(QStringLiteral("scheduledStartDateTime")).toObject()));
            r.insert(QStringLiteral("end"), fromGraphTime(settings.value(QStringLiteral("scheduledEndDateTime")).toObject()));
        }
    }, [this, accountId, result]() {
        Q_EMIT autoReplyLoaded(accountId, *result);
    });
}

void MailEngine::saveAutoReply(const QString &accountId, const QVariantMap &settings)
{
    const Account *found = m_store.find(accountId);
    if (!found) {
        return;
    }
    const bool enabled = settings.value(QStringLiteral("enabled")).toBool();
    const QString message = settings.value(QStringLiteral("message")).toString().trimmed();
    const QDateTime start = settings.value(QStringLiteral("start")).toDateTime();
    const QDateTime end = settings.value(QStringLiteral("end")).toDateTime();
    if (found->auth != QLatin1String("microsoft")) {
        Account account = *found;
        // Turned on afresh: everyone gets one reply again.
        if (enabled && !account.autoReply) {
            QSettings().remove(QStringLiteral("autoReply/") + accountId);
        }
        account.autoReply = enabled;
        account.autoReplyMessage = message;
        account.autoReplyStart = start;
        account.autoReplyEnd = end;
        account.autoReplySubject = settings.value(QStringLiteral("subject")).toString().trimmed();
        account.autoReplyAudience = settings.value(QStringLiteral("audience"), QStringLiteral("all")).toString();
        m_store.update(account);
        Q_EMIT autoReplySaved(accountId, true, {}, false);
        return;
    }
    const QString audience = settings.value(QStringLiteral("audience"), QStringLiteral("all")).toString();
    QJsonObject reply{
        {QStringLiteral("status"), !enabled ? QStringLiteral("disabled") : (start.isValid() && end.isValid()) ? QStringLiteral("scheduled") : QStringLiteral("alwaysEnabled")},
        {QStringLiteral("externalAudience"), audience == QLatin1String("all") ? QStringLiteral("all")
                                                 : audience == QLatin1String("contacts") ? QStringLiteral("contactsOnly") : QStringLiteral("none")},
        {QStringLiteral("internalReplyMessage"), htmlFromPlain(message)},
        {QStringLiteral("externalReplyMessage"), htmlFromPlain(message)},
    };
    if (enabled && start.isValid() && end.isValid()) {
        reply.insert(QStringLiteral("scheduledStartDateTime"), graphTime(start));
        reply.insert(QStringLiteral("scheduledEndDateTime"), graphTime(end));
    }
    const QByteArray patch = QJsonDocument(QJsonObject{{QStringLiteral("automaticRepliesSetting"), reply}}).toJson(QJsonDocument::Compact);
    const QString refresh = Session::instance().password(accountId);
    auto error = std::make_shared<QString>();
    auto consent = std::make_shared<bool>(false);
    run([refresh, patch, error, consent]() {
        QString token;
        if (!MicrosoftSignIn::settingsToken(refresh, &token, error.get(), consent.get())) {
            return;
        }
        graphMailbox(token, patch, error.get());
    }, [this, accountId, error, consent]() {
        if (*consent) {
            Q_EMIT autoReplySaved(accountId, false, tr("Sign in again to let Mail set automatic replies for this account."), true);
        } else if (!error->isEmpty()) {
            Q_EMIT autoReplySaved(accountId, false, tr("Microsoft didn't save it. %1").arg(*error), false);
        } else {
            Q_EMIT autoReplySaved(accountId, true, {}, false);
        }
    });
}

// Automatic replies Mail sends itself: once to each person while they are
// on, never to mailing lists, automatic senders or mail a rule moved away.
void MailEngine::sendAutoReplies(const QString &accountId, const QList<MessageRow> &arrived, const QSet<QString> &handled)
{
    const Account *account = m_store.find(accountId);
    if (!account || !account->autoReply || account->auth == QLatin1String("microsoft") || arrived.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    if ((account->autoReplyStart.isValid() && now < account->autoReplyStart) || (account->autoReplyEnd.isValid() && now > account->autoReplyEnd)) {
        return;
    }
    static const QRegularExpression automatic(QStringLiteral("^(no-?reply|do-?not-?reply|mailer-daemon|postmaster|bounces?|notifications?)([+.-]|@)"),
                                              QRegularExpression::CaseInsensitiveOption);
    QSettings settings;
    const QString key = QStringLiteral("autoReply/") + accountId;
    QStringList answered = settings.value(key).toStringList();
    for (const MessageRow &r : arrived) {
        const QString address = r.fromAddress.toLower();
        if (address.isEmpty() || r.list || handled.contains(r.key()) || answered.contains(address)
            || address == account->email.toLower() || automatic.match(address).hasMatch()
            || r.date < now.addDays(-1)) {
            continue;
        }
        if (account->autoReplyAudience == QLatin1String("contacts") && PeopleEngine::instance()->list(QStringLiteral("all"), {}, address).isEmpty()) {
            continue;
        }
        Composer composer;
        composer.start({{QStringLiteral("accountId"), accountId}, {QStringLiteral("to"), QStringList{r.fromAddress}},
                        {QStringLiteral("subject"), account->autoReplySubject.isEmpty() ? tr("Automatic reply: %1").arg(r.subject) : account->autoReplySubject}});
        composer.setProperty("bodyText", account->autoReplyMessage);
        composer.setProperty("bodyHtml", account->autoReplyMessage.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        composer.setProperty("autoReply", true);
        if (composer.send()) {
            answered.append(address);
        }
    }
    settings.setValue(key, answered);
}

namespace
{
QString signaturesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/signatures.json");
}
}

void MailEngine::loadSignatures()
{
    QFile file(signaturesPath());
    if (file.open(QIODevice::ReadOnly)) {
        m_signatures = QJsonDocument::fromJson(file.readAll()).array().toVariantList();
        return;
    }
    // Before named signatures, each account had one of its own: those
    // become signatures, named after the account and used as before.
    for (const Account &a : m_store.accounts()) {
        if (a.signature.trimmed().isEmpty()) {
            continue;
        }
        const QString id = AccountStore::newId();
        m_signatures.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), a.name},
                                        {QStringLiteral("html"), a.signature.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))}});
        Account updated = a;
        updated.signatureNew = id;
        updated.signatureReply = id;
        m_store.update(updated);
    }
    writeSignatures();
}

void MailEngine::writeSignatures() const
{
    QDir().mkpath(QFileInfo(signaturesPath()).absolutePath());
    QSaveFile file(signaturesPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonArray::fromVariantList(m_signatures)).toJson());
        file.commit();
    }
}

QString MailEngine::addSignature(const QString &name)
{
    const QString id = AccountStore::newId();
    m_signatures.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("html"), QString()}});
    writeSignatures();
    Q_EMIT signaturesChanged();
    return id;
}

void MailEngine::saveSignature(const QString &id, const QString &name, const QString &html)
{
    for (QVariant &v : m_signatures) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id) {
            m.insert(QStringLiteral("name"), name);
            m.insert(QStringLiteral("html"), html);
            v = m;
        }
    }
    writeSignatures();
    Q_EMIT signaturesChanged();
}

void MailEngine::removeSignature(const QString &id)
{
    m_signatures.erase(std::remove_if(m_signatures.begin(), m_signatures.end(), [&id](const QVariant &v) {
        return v.toMap().value(QStringLiteral("id")).toString() == id;
    }), m_signatures.end());
    for (const Account &a : m_store.accounts()) {
        if (a.signatureNew == id || a.signatureReply == id) {
            Account updated = a;
            if (updated.signatureNew == id) {
                updated.signatureNew.clear();
            }
            if (updated.signatureReply == id) {
                updated.signatureReply.clear();
            }
            m_store.update(updated);
        }
    }
    writeSignatures();
    Q_EMIT signaturesChanged();
    Q_EMIT accountsChanged();
}

QString MailEngine::signatureFor(const QString &accountId, bool reply) const
{
    const Account *a = m_store.find(accountId);
    if (!a) {
        return {};
    }
    const QString id = reply ? a->signatureReply : a->signatureNew;
    for (const QVariant &v : m_signatures) {
        const QVariantMap m = v.toMap();
        if (!id.isEmpty() && m.value(QStringLiteral("id")).toString() == id) {
            return m.value(QStringLiteral("html")).toString();
        }
    }
    return {};
}

void Transfer::setTitle(const QString &text, const QString &address)
{
    QMutexLocker locker(&lock);
    title = text;
    if (!address.isEmpty()) {
        account = address;
    }
}

bool Transfer::carryOn() const
{
    while (paused && !cancelled) {
        QThread::msleep(150);
    }
    return !cancelled;
}

QString MailEngine::transferStatus() const
{
    if (!m_transfer) {
        return {};
    }
    QMutexLocker locker(&m_transfer->lock);
    return m_transfer->title;
}

QString MailEngine::transferKind() const
{
    return m_transfer ? m_transfer->kind : QString();
}

QString MailEngine::transferAccount() const
{
    if (!m_transfer) {
        return {};
    }
    QMutexLocker locker(&m_transfer->lock);
    return m_transfer->account;
}

int MailEngine::transferDone() const
{
    return m_transfer ? m_transfer->done.load() : 0;
}

int MailEngine::transferTotal() const
{
    return m_transfer ? m_transfer->total.load() : 0;
}

bool MailEngine::transferPaused() const
{
    return m_transfer && m_transfer->paused;
}

void MailEngine::pauseTransfer(bool paused)
{
    if (m_transfer) {
        m_transfer->paused = paused;
        Q_EMIT transferChanged();
    }
}

void MailEngine::cancelTransfer()
{
    if (m_transfer) {
        m_transfer->cancelled = true;
        m_transfer->paused = false;
        Q_EMIT transferChanged();
    }
}

std::shared_ptr<Transfer> MailEngine::startTransfer(const QString &kind, const QString &title, const QString &account)
{
    if (m_transfer) {
        return {};
    }
    auto job = std::make_shared<Transfer>();
    job->kind = kind;
    job->title = title;
    job->account = account;
    m_transfer = job;
    m_transferTick.start();
    Q_EMIT transferChanged();
    return job;
}

void MailEngine::finishTransfer(bool ok, const QString &message)
{
    m_transfer.reset();
    m_transferTick.stop();
    Q_EMIT transferChanged();
    Q_EMIT transferFinished(ok, message);
}

namespace
{
// The folder named under parent (empty for the top), made if it isn't there;
// its full name, or empty when the server wouldn't make it.
QString ensureFolder(CamelStore *store, const QString &parent, const QString &name, GError **error)
{
    CamelFolderInfo *top = camel_store_get_folder_info_sync(store, parent.isEmpty() ? nullptr : parent.toUtf8().constData(),
                                                            CamelStoreGetFolderInfoFlags(0), nullptr, nullptr);
    QString found;
    for (CamelFolderInfo *fi = parent.isEmpty() ? top : (top ? top->child : nullptr); fi; fi = fi->next) {
        if (QString::fromUtf8(fi->display_name).compare(name, Qt::CaseInsensitive) == 0) {
            found = QString::fromUtf8(fi->full_name);
            break;
        }
    }
    if (top) {
        camel_folder_info_free(top);
    }
    if (!found.isEmpty()) {
        return found;
    }
    CamelFolderInfo *made = camel_store_create_folder_sync(store, parent.isEmpty() ? nullptr : parent.toUtf8().constData(), name.toUtf8().constData(), nullptr, error);
    if (!made) {
        return {};
    }
    found = QString::fromUtf8(made->full_name);
    camel_folder_info_free(made);
    return found;
}

bool appendMessage(CamelFolder *folder, CamelMimeMessage *message)
{
    CamelMessageInfo *info = camel_message_info_new(nullptr);
    camel_message_info_set_flags(info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
    const bool ok = camel_folder_append_message_sync(folder, message, info, nullptr, nullptr, nullptr);
    g_object_unref(info);
    return ok;
}

// A message kept as a file of its own: an .eml file, or one message of a
// Maildir folder.
bool appendFile(CamelFolder *folder, const QString &path)
{
    CamelStream *stream = camel_stream_fs_new_with_name(QFile::encodeName(path).constData(), O_RDONLY, 0, nullptr);
    if (!stream) {
        return false;
    }
    CamelMimeMessage *message = camel_mime_message_new();
    const bool ok = camel_data_wrapper_construct_from_stream_sync(CAMEL_DATA_WRAPPER(message), stream, nullptr, nullptr)
        && appendMessage(folder, message);
    g_object_unref(stream);
    g_object_unref(message);
    return ok;
}

// Each message of an mbox file into folder, counted on the transfer and
// stopping when it is cancelled; how many went in.
int appendMbox(CamelFolder *folder, const QString &path, Transfer &job)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    CamelMimeParser *parser = camel_mime_parser_new();
    camel_mime_parser_scan_from(parser, TRUE);
    camel_mime_parser_init_with_fd(parser, fd);
    int count = 0;
    while (job.carryOn() && camel_mime_parser_step(parser, nullptr, nullptr) == CAMEL_MIME_PARSER_STATE_FROM) {
        CamelMimeMessage *message = camel_mime_message_new();
        if (!camel_mime_part_construct_from_parser_sync(CAMEL_MIME_PART(message), parser, nullptr, nullptr)) {
            g_object_unref(message);
            break;
        }
        if (appendMessage(folder, message)) {
            ++count;
        }
        ++job.done;
        g_object_unref(message);
        camel_mime_parser_step(parser, nullptr, nullptr);
    }
    g_object_unref(parser);
    return count;
}

// How many messages an mbox file holds, by its "From " lines, so progress
// has a total before the first is imported.
int countMbox(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    int count = 0;
    QByteArray carry("\n");
    while (!file.atEnd()) {
        const QByteArray chunk = carry + file.read(1 << 20);
        count += int(chunk.count("\nFrom "));
        carry = chunk.right(5);
    }
    return count;
}

bool startsWithFrom(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(5) == "From ";
}

bool isMaildir(const QString &dir)
{
    return QFileInfo(dir + QStringLiteral("/cur")).isDir() && QFileInfo(dir + QStringLiteral("/new")).isDir();
}

QStringList maildirFiles(const QString &dir)
{
    QStringList out;
    for (const QString &sub : {QStringLiteral("/cur"), QStringLiteral("/new")}) {
        for (const QFileInfo &fi : QDir(dir + sub).entryInfoList(QDir::Files, QDir::Name)) {
            out << fi.absoluteFilePath();
        }
    }
    return out;
}

// Something to import, and the folders under Imported it goes into.
struct ImportSource {
    enum Kind { Mbox, Eml, Maildir };
    Kind kind = Mbox;
    QString path;
    QStringList folder;
    // What the progress row says is being imported.
    QString label;
};

// A folder name as a server takes it: no separators.
QString folderPart(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    return name.trimmed().isEmpty() ? MailEngine::tr("Folder") : name.trimmed();
}

void collect(const QString &path, const QStringList &folder, QList<ImportSource> *out, QList<QPair<QString, QStringList>> *psts);

void collectFile(const QString &path, const QStringList &folder, QList<ImportSource> *out, QList<QPair<QString, QStringList>> *psts)
{
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    if (fi.size() == 0) {
        return;
    }
    if (suffix == QLatin1String("eml")) {
        out->append(ImportSource{ImportSource::Eml, path, folder, fi.fileName()});
    } else if (suffix == QLatin1String("pst") || suffix == QLatin1String("ost")) {
        psts->append(qMakePair(path, folder + QStringList{folderPart(fi.completeBaseName())}));
    } else if (suffix == QLatin1String("mbox") || suffix == QLatin1String("mbx")) {
        out->append(ImportSource{ImportSource::Mbox, path, folder + QStringList{folderPart(fi.completeBaseName())}, fi.fileName()});
    } else if (startsWithFrom(path)) {
        // Thunderbird's folders are mbox files with no extension at all.
        out->append(ImportSource{ImportSource::Mbox, path, folder + QStringList{folderPart(fi.fileName())}, fi.fileName()});
    }
}

// A folder picked to import, a Thunderbird account's folder or Evolution's
// mail: mbox, eml and pst files in it and in folders inside it, Thunderbird's
// ".sbd" subfolders and Maildir folders (Evolution's own, with its
// subfolders beside it as ".Name.Child").
void collect(const QString &path, const QStringList &folder, QList<ImportSource> *out, QList<QPair<QString, QStringList>> *psts)
{
    if (!QFileInfo(path).isDir()) {
        collectFile(path, folder, out, psts);
        return;
    }
    const bool maildir = isMaildir(path);
    const QFileInfoList entries = QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    bool plusPlus = false;
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (fi.isDir() && name.startsWith(QLatin1Char('.')) && isMaildir(fi.absoluteFilePath())) {
            plusPlus = true;
            QStringList parts;
            for (QString part : name.mid(1).split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
                parts << folderPart(part.replace(QStringLiteral("_2E"), QStringLiteral(".")));
            }
            out->append(ImportSource{ImportSource::Maildir, fi.absoluteFilePath(), folder + parts, parts.isEmpty() ? name : parts.last()});
        }
    }
    if (maildir) {
        // A Maildir++ store keeps its Inbox in the top folder.
        const QStringList at = plusPlus ? folder + QStringList{MailEngine::tr("Inbox")} : folder;
        out->append(ImportSource{ImportSource::Maildir, path, at, at.isEmpty() ? QFileInfo(path).fileName() : at.last()});
    }
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (name.startsWith(QLatin1Char('.'))) {
            continue;
        }
        if (fi.isDir()) {
            if (maildir && (name == QLatin1String("cur") || name == QLatin1String("new") || name == QLatin1String("tmp"))) {
                continue;
            }
            const QString part = name.endsWith(QLatin1String(".sbd")) ? name.chopped(4) : name;
            collect(fi.absoluteFilePath(), folder + QStringList{folderPart(part)}, out, psts);
        } else {
            collectFile(fi.absoluteFilePath(), folder, out, psts);
        }
    }
}

// Everything to import from these files and folders, with each Outlook
// file opened by readpst into a folder that lives in temps until the import
// is done. When label is set, the progress row names that throughout.
QList<ImportSource> gather(const QList<QPair<QString, QStringList>> &roots, const QString &label, QList<std::shared_ptr<QTemporaryDir>> &temps,
                           Transfer &job, QString *error)
{
    QList<ImportSource> out;
    QList<QPair<QString, QStringList>> psts;
    for (const auto &[path, folder] : roots) {
        collect(path, folder, &out, &psts);
    }
    for (const auto &[path, folder] : std::as_const(psts)) {
        if (!job.carryOn()) {
            break;
        }
        const QString name = QFileInfo(path).fileName();
        job.setTitle(MailEngine::tr("Reading %1").arg(label.isEmpty() ? name : label));
        auto dir = std::make_shared<QTemporaryDir>();
        QProcess readpst;
        readpst.start(QStringLiteral("readpst"), {QStringLiteral("-q"), QStringLiteral("-r"), QStringLiteral("-o"), dir->path(), path});
        if (!readpst.waitForFinished(-1) || readpst.exitCode() != 0) {
            *error = MailEngine::tr("%1 couldn't be read. It may be damaged, or protected by a password.").arg(name);
            continue;
        }
        temps << dir;
        // readpst makes a folder per Outlook folder, each with an "mbox"
        // file; the same folders go under Imported.
        QDirIterator it(dir->path(), {QStringLiteral("mbox")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString mbox = it.next();
            QStringList parts = folder;
            for (const QString &part : QDir(dir->path()).relativeFilePath(QFileInfo(mbox).absolutePath()).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
                if (part != QLatin1String(".")) {
                    parts << folderPart(part);
                }
            }
            out.append(ImportSource{ImportSource::Mbox, mbox, parts, name});
        }
    }
    if (!label.isEmpty()) {
        for (ImportSource &s : out) {
            s.label = label;
        }
    }
    return out;
}

int countOf(const QList<ImportSource> &sources)
{
    int total = 0;
    for (const ImportSource &s : sources) {
        switch (s.kind) {
        case ImportSource::Mbox:
            total += countMbox(s.path);
            break;
        case ImportSource::Eml:
            ++total;
            break;
        case ImportSource::Maildir:
            total += int(maildirFiles(s.path).size());
            break;
        }
    }
    return total;
}

// The sources into the account's Imported folder, their folders made
// inside it as they come; how many messages went in.
int importSources(CamelStore *store, const QString &importedName, const QList<ImportSource> &sources, Transfer &job, QString *error)
{
    if (sources.isEmpty()) {
        return 0;
    }
    GError *gerror = nullptr;
    const QString top = ensureFolder(store, {}, importedName, &gerror);
    if (top.isEmpty()) {
        *error = MailEngine::plainError(gerror, MailEngine::tr("The Imported folder couldn't be made. Try again in a moment."));
        g_clear_error(&gerror);
        return 0;
    }
    QHash<QString, QString> made;
    int count = 0;
    for (const ImportSource &s : sources) {
        if (!job.carryOn()) {
            break;
        }
        job.setTitle(MailEngine::tr("Importing %1").arg(s.label));
        QString full = top;
        for (int i = 0; i < s.folder.size() && !full.isEmpty(); ++i) {
            const QString key = s.folder.mid(0, i + 1).join(QChar(0x1f));
            if (!made.contains(key)) {
                made.insert(key, ensureFolder(store, full, s.folder.at(i), nullptr));
            }
            full = made.value(key);
        }
        CamelFolder *folder = full.isEmpty() ? nullptr : camel_store_get_folder_sync(store, full.toUtf8().constData(), CamelStoreGetFolderFlags(0), nullptr, nullptr);
        if (!folder) {
            *error = MailEngine::tr("A folder for %1 couldn't be made on the server.").arg(s.folder.isEmpty() ? s.label : s.folder.last());
            continue;
        }
        switch (s.kind) {
        case ImportSource::Mbox:
            count += appendMbox(folder, s.path, job);
            break;
        case ImportSource::Eml:
            if (appendFile(folder, s.path)) {
                ++count;
            }
            ++job.done;
            break;
        case ImportSource::Maildir:
            for (const QString &file : maildirFiles(s.path)) {
                if (!job.carryOn()) {
                    break;
                }
                if (appendFile(folder, file)) {
                    ++count;
                }
                ++job.done;
            }
            break;
        }
        g_object_unref(folder);
    }
    return count;
}

// The folder's messages as an mbox file, counted on the transfer; how many
// went out, or -1 when the file couldn't be written.
int writeMbox(CamelFolder *f, const QString &path, Transfer &job)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        return -1;
    }
    int count = 0;
    GPtrArray *uids = camel_folder_get_uids(f);
    for (guint i = 0; i < uids->len && job.carryOn(); ++i) {
        ++job.done;
        CamelMimeMessage *message = camel_folder_get_message_sync(f, static_cast<const char *>(uids->pdata[i]), nullptr, nullptr);
        if (!message) {
            continue;
        }
        GOutputStream *mem = g_memory_output_stream_new_resizable();
        camel_data_wrapper_write_to_output_stream_sync(CAMEL_DATA_WRAPPER(message), mem, nullptr, nullptr);
        g_output_stream_close(mem, nullptr, nullptr);
        QByteArray bytes(static_cast<const char *>(g_memory_output_stream_get_data(G_MEMORY_OUTPUT_STREAM(mem))),
                         qsizetype(g_memory_output_stream_get_data_size(G_MEMORY_OUTPUT_STREAM(mem))));
        g_object_unref(mem);
        // mbox: a "From " line before each message, and lines in it that
        // start "From " quoted with ">".
        bytes.replace("\nFrom ", "\n>From ");
        if (bytes.startsWith("From ")) {
            bytes.prepend('>');
        }
        gchar *from = camel_mime_message_build_mbox_from(message);
        out.write(from);
        g_free(from);
        out.write(bytes);
        if (!bytes.endsWith('\n')) {
            out.write("\n");
        }
        out.write("\n");
        g_object_unref(message);
        ++count;
    }
    camel_folder_free_uids(f, uids);
    return count;
}

// Where Thunderbird keeps each account's mail, whichever way it was
// installed: one folder per account under Mail (POP and Local Folders) or
// ImapMail.
QStringList thunderbirdAccounts()
{
    const QString home = QDir::homePath();
    QStringList out;
    for (const QString &base : {home + QStringLiteral("/.thunderbird"),
                                home + QStringLiteral("/.var/app/org.mozilla.Thunderbird/.thunderbird"),
                                home + QStringLiteral("/.var/app/net.thunderbird.Thunderbird/.thunderbird"),
                                home + QStringLiteral("/snap/thunderbird/common/.thunderbird")}) {
        for (const QFileInfo &profile : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            for (const QString &kind : {QStringLiteral("/Mail"), QStringLiteral("/ImapMail")}) {
                for (const QFileInfo &account : QDir(profile.absoluteFilePath() + kind).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    out << account.absoluteFilePath();
                }
            }
        }
    }
    return out;
}

QStringList evolutionStores()
{
    const QString home = QDir::homePath();
    QStringList out;
    for (const QString &dir : {home + QStringLiteral("/.local/share/evolution/mail/local"),
                               home + QStringLiteral("/.var/app/org.gnome.Evolution/data/evolution/mail/local")}) {
        if (isMaildir(dir)) {
            out << dir;
        }
    }
    return out;
}

// Whether any of these holds a message, without reading them all.
bool holdsMail(const QStringList &dirs)
{
    for (const QString &dir : dirs) {
        QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.nextFileInfo());
            const QString parent = fi.dir().dirName();
            if (fi.size() > 0 && (parent == QLatin1String("cur") || parent == QLatin1String("new") || startsWithFrom(fi.absoluteFilePath()))) {
                return true;
            }
        }
    }
    return false;
}

// What a backup holds, and what it is called inside the archive.
const QString backupFormat = QStringLiteral("wildos-mail-backup");

// Settings that name this PC's accounts, folders or windows, or only
// count things here: not carried to another PC by a backup.
bool settingStaysHere(const QString &key)
{
    static const QStringList here = {
        QStringLiteral("mail/folder"), QStringLiteral("mail/folderAccount"), QStringLiteral("calendar/holidaysUid"),
        QStringLiteral("privacy/photosCleaned"),
    };
    const QString lower = key.toLower();
    return here.contains(key) || key.startsWith(QLatin1String("Window/")) || key.startsWith(QLatin1String("trackersCounted/"))
        || lower.contains(QLatin1String("password")) || lower.contains(QLatin1String("token")) || lower.contains(QLatin1String("secret"));
}

bool writeJson(const QString &path, const QJsonDocument &doc)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(doc.toJson()) >= 0;
}

QJsonDocument readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
}
}

void MailEngine::importMail(const QString &accountId, const QList<QUrl> &files)
{
    QList<QPair<QString, QStringList>> roots;
    for (const QUrl &url : files) {
        const QFileInfo fi(url.toLocalFile());
        // A folder picked keeps its own name as the top folder inside Imported.
        roots.append(qMakePair(fi.absoluteFilePath(), fi.isDir() ? QStringList{folderPart(fi.fileName())} : QStringList{}));
    }
    const QString label = files.size() == 1 && QFileInfo(files.first().toLocalFile()).isDir() ? QFileInfo(files.first().toLocalFile()).fileName() : QString();
    importRoots(accountId, roots, label);
}

QVariantList MailEngine::mailApps() const
{
    QVariantList apps;
    if (holdsMail(thunderbirdAccounts())) {
        apps << QVariantMap{{QStringLiteral("id"), QStringLiteral("thunderbird")}, {QStringLiteral("name"), QStringLiteral("Thunderbird")}};
    }
    if (holdsMail(evolutionStores())) {
        apps << QVariantMap{{QStringLiteral("id"), QStringLiteral("evolution")}, {QStringLiteral("name"), QStringLiteral("Evolution")}};
    }
    return apps;
}

void MailEngine::importApp(const QString &accountId, const QString &appId)
{
    QList<QPair<QString, QStringList>> roots;
    QString name;
    if (appId == QLatin1String("thunderbird")) {
        name = QStringLiteral("Thunderbird");
        for (const QString &dir : thunderbirdAccounts()) {
            roots.append(qMakePair(dir, QStringList{name, folderPart(QFileInfo(dir).fileName())}));
        }
    } else if (appId == QLatin1String("evolution")) {
        name = QStringLiteral("Evolution");
        for (const QString &dir : evolutionStores()) {
            roots.append(qMakePair(dir, QStringList{name}));
        }
    }
    importRoots(accountId, roots, name);
}

void MailEngine::importRoots(const QString &accountId, const QList<QPair<QString, QStringList>> &roots, const QString &label)
{
    const Account *account = m_store.find(accountId);
    CamelStore *store = nullptr;
    {
        QMutexLocker lock(&m_folderLock);
        store = m_live.value(accountId).store;
    }
    if (!account || !store || roots.isEmpty()) {
        return;
    }
    auto job = startTransfer(QStringLiteral("import"), label.isEmpty() ? tr("Getting ready to import") : tr("Importing %1").arg(label), account->email);
    if (!job) {
        return;
    }
    g_object_ref(store);
    auto count = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();
    const QString importedName = tr("Imported");
    run([store, roots, label, job, count, error, importedName]() {
        QList<std::shared_ptr<QTemporaryDir>> temps;
        const QList<ImportSource> sources = gather(roots, label, temps, *job, error.get());
        job->total = countOf(sources);
        *count = importSources(store, importedName, sources, *job, error.get());
        g_object_unref(store);
    }, [this, accountId, job, count, error]() {
        listFolders(accountId);
        if (job->cancelled) {
            finishTransfer(true, *count == 1 ? tr("Import stopped. 1 message went into the Imported folder.")
                                             : tr("Import stopped. %1 messages went into the Imported folder.").arg(*count));
        } else if (*count == 0) {
            finishTransfer(false, error->isEmpty() ? tr("No messages were found to import.") : *error);
        } else {
            const QString done = *count == 1 ? tr("Imported 1 message into the Imported folder.") : tr("Imported %1 messages into the Imported folder.").arg(*count);
            finishTransfer(true, error->isEmpty() ? done : done + QLatin1Char(' ') + *error);
        }
    });
}

void MailEngine::exportMail(const QVariantMap &choice, const QUrl &folder)
{
    struct Part {
        QString accountId;
        QString email;
        QStringList folders;
    };
    QList<Part> parts;
    for (const Account &a : m_store.accounts()) {
        if (!choice.contains(a.id) || !m_live.contains(a.id)) {
            continue;
        }
        const QStringList chosen = choice.value(a.id).toStringList();
        Part part{a.id, a.email, {}};
        for (const FolderEntry &f : m_live.value(a.id).folders) {
            if (f.role != QLatin1String("outbox") && (chosen.isEmpty() || chosen.contains(f.fullName))) {
                part.folders << f.fullName;
            }
        }
        if (!part.folders.isEmpty()) {
            parts << part;
        }
    }
    if (parts.isEmpty()) {
        return;
    }
    const QString dir = folder.toLocalFile();
    auto job = startTransfer(QStringLiteral("export"), tr("Getting ready to export"), parts.size() == 1 ? parts.first().email : QString());
    if (!job) {
        return;
    }
    auto count = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();
    run([this, parts, dir, job, count, error]() {
        // Every folder opened first, so the total is known before any is
        // written.
        QList<QPair<CamelFolder *, QString>> open;
        for (const Part &part : parts) {
            for (const QString &full : part.folders) {
                GError *gerror = nullptr;
                if (CamelFolder *f = folderSync(part.accountId, full, &gerror)) {
                    job->total += int(camel_folder_get_message_count(f));
                    open.append(qMakePair(f, part.email + QLatin1Char('/') + full));
                }
                g_clear_error(&gerror);
            }
        }
        for (const auto &[f, name] : std::as_const(open)) {
            if (job->carryOn() && error->isEmpty()) {
                job->setTitle(tr("Exporting %1").arg(name.section(QLatin1Char('/'), -1)), name.section(QLatin1Char('/'), 0, 0));
                const int n = writeMbox(f, dir + QLatin1Char('/') + name + QStringLiteral(".mbox"), *job);
                if (n < 0) {
                    *error = tr("Mail couldn't write to %1.").arg(dir);
                } else {
                    *count += n;
                }
            }
            g_object_unref(f);
        }
    }, [this, job, count, error, dir]() {
        if (!error->isEmpty()) {
            finishTransfer(false, *error);
        } else if (job->cancelled) {
            finishTransfer(true, tr("Export stopped. %1 messages were saved to %2.").arg(*count).arg(dir));
        } else {
            finishTransfer(true, *count == 1 ? tr("Exported 1 message to %1.").arg(dir) : tr("Exported %1 messages to %2.").arg(*count).arg(dir));
        }
    });
}

void MailEngine::backUp(const QUrl &file)
{
    QString path = file.toLocalFile();
    if (!path.endsWith(QLatin1String(".tar.gz")) && !path.endsWith(QLatin1String(".tgz"))) {
        path += QStringLiteral(".tar.gz");
    }
    // Accounts as accounts.json keeps them: addresses and servers, never a
    // password or sign-in token, which live only in the password store.
    QJsonArray accounts;
    QList<QPair<QString, QString>> mailboxes;
    for (const Account &a : m_store.accounts()) {
        accounts.append(QJsonObject::fromVariantMap(a.toMap()));
        if (m_live.contains(a.id)) {
            for (const FolderEntry &f : m_live.value(a.id).folders) {
                if (f.role != QLatin1String("outbox")) {
                    mailboxes.append(qMakePair(a.id, f.fullName));
                }
            }
        }
    }
    QSettings current;
    current.sync();
    QVariantMap settings;
    for (const QString &key : current.allKeys()) {
        if (!settingStaysHere(key)) {
            settings.insert(key, current.value(key));
        }
    }
    QHash<QString, QString> emails;
    for (const Account &a : m_store.accounts()) {
        emails.insert(a.id, a.email);
    }
    const QJsonDocument rules(QJsonArray::fromVariantList(m_rules));
    const QJsonDocument signatures(QJsonArray::fromVariantList(m_signatures));
    auto job = startTransfer(QStringLiteral("backup"), tr("Backing up contacts and calendars"), QString());
    if (!job) {
        return;
    }
    auto count = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();
    auto notes = std::make_shared<QStringList>();
    run([this, path, accounts, mailboxes, settings, emails, rules, signatures, job, count, error, notes]() {
        QTemporaryDir tmp;
        const QString root = tmp.path() + QLatin1Char('/') + backupFormat;
        QDir().mkpath(root + QStringLiteral("/mail"));
        writeJson(root + QStringLiteral("/manifest.json"), QJsonDocument(QJsonObject{
            {QStringLiteral("format"), backupFormat},
            {QStringLiteral("version"), 1},
            {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        }));
        writeJson(root + QStringLiteral("/accounts.json"), QJsonDocument(accounts));
        writeJson(root + QStringLiteral("/rules.json"), rules);
        writeJson(root + QStringLiteral("/signatures.json"), signatures);
        {
            QSettings out(root + QStringLiteral("/settings.conf"), QSettings::IniFormat);
            for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
                out.setValue(it.key(), it.value());
            }
        }
        QString why;
        if (PeopleEngine::writeVcf(root + QStringLiteral("/contacts.vcf"), &why) < 0) {
            *notes << tr("Contacts weren't backed up. %1").arg(why);
        }
        if (CalendarEngine::writeAll(root + QStringLiteral("/calendars"), &why) < 0) {
            *notes << tr("Calendars weren't backed up. %1").arg(why);
        }
        QList<QPair<CamelFolder *, QString>> open;
        for (const auto &[accountId, full] : mailboxes) {
            GError *gerror = nullptr;
            if (CamelFolder *f = folderSync(accountId, full, &gerror)) {
                job->total += int(camel_folder_get_message_count(f));
                open.append(qMakePair(f, emails.value(accountId) + QLatin1Char('/') + full));
            }
            g_clear_error(&gerror);
        }
        for (const auto &[f, name] : std::as_const(open)) {
            if (job->carryOn() && error->isEmpty()) {
                job->setTitle(tr("Backing up %1").arg(name.section(QLatin1Char('/'), 0, 0)), name.section(QLatin1Char('/'), 0, 0));
                const int n = writeMbox(f, root + QStringLiteral("/mail/") + name + QStringLiteral(".mbox"), *job);
                if (n < 0) {
                    *error = tr("The backup couldn't be put together. Is there space on this PC?");
                } else {
                    *count += n;
                }
            }
            g_object_unref(f);
        }
        if (job->cancelled || !error->isEmpty()) {
            return;
        }
        job->setTitle(tr("Saving the backup"));
        // Written beside the file chosen, then renamed over it, so a backup
        // that fails half way never replaces a good one.
        const QString partial = path + QStringLiteral(".part");
        QProcess tar;
        tar.start(QStringLiteral("tar"), {QStringLiteral("-czf"), partial, QStringLiteral("-C"), tmp.path(), backupFormat});
        if (!tar.waitForFinished(-1) || tar.exitCode() != 0) {
            QFile::remove(partial);
            *error = tr("The backup couldn't be saved to %1.").arg(QFileInfo(path).absolutePath());
            return;
        }
        // It holds private mail: only this account can read it.
        QFile::setPermissions(partial, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QFile::remove(path);
        if (!QFile::rename(partial, path)) {
            QFile::remove(partial);
            *error = tr("The backup couldn't be saved to %1.").arg(QFileInfo(path).absolutePath());
        }
    }, [this, path, job, count, error, notes]() {
        if (!error->isEmpty()) {
            finishTransfer(false, *error);
        } else if (job->cancelled) {
            finishTransfer(true, tr("Backup stopped. Nothing was saved."));
        } else {
            QStringList words{*count == 1 ? tr("Backed up 1 message, your contacts, calendars and settings to %1.").arg(path)
                                          : tr("Backed up %1 messages, your contacts, calendars and settings to %2.").arg(*count).arg(path)};
            finishTransfer(notes->isEmpty(), (words + *notes).join(QLatin1Char(' ')));
        }
    });
}

void MailEngine::restoreSettings(const QString &root, QStringList *notes)
{
    // The backup's account ids are another PC's; addresses tie them to the
    // accounts here.
    QHash<QString, QString> idHere;
    QHash<QString, QVariantMap> backedUp;
    for (const QJsonValue &v : readJson(root + QStringLiteral("/accounts.json")).array()) {
        const QVariantMap a = v.toObject().toVariantMap();
        const QString email = a.value(QStringLiteral("email")).toString().toLower();
        for (const Account &here : m_store.accounts()) {
            if (here.email.toLower() == email) {
                idHere.insert(a.value(QStringLiteral("id")).toString(), here.id);
                backedUp.insert(here.id, a);
            }
        }
    }

    QSet<QString> signatureIds;
    for (const QVariant &v : std::as_const(m_signatures)) {
        signatureIds.insert(v.toMap().value(QStringLiteral("id")).toString());
    }
    bool signaturesAdded = false;
    for (const QVariant &v : readJson(root + QStringLiteral("/signatures.json")).array().toVariantList()) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !signatureIds.contains(id)) {
            m_signatures.append(v);
            signatureIds.insert(id);
            signaturesAdded = true;
        }
    }
    if (signaturesAdded) {
        writeSignatures();
        Q_EMIT signaturesChanged();
    }
    // An account with no signature chosen takes the one it had.
    for (auto it = backedUp.cbegin(); it != backedUp.cend(); ++it) {
        const Account *here = m_store.find(it.key());
        if (!here) {
            continue;
        }
        Account updated = *here;
        const QString forNew = it.value().value(QStringLiteral("signatureNew")).toString();
        const QString forReply = it.value().value(QStringLiteral("signatureReply")).toString();
        if (updated.signatureNew.isEmpty() && signatureIds.contains(forNew)) {
            updated.signatureNew = forNew;
        }
        if (updated.signatureReply.isEmpty() && signatureIds.contains(forReply)) {
            updated.signatureReply = forReply;
        }
        if (updated.signatureNew != here->signatureNew || updated.signatureReply != here->signatureReply) {
            m_store.update(updated);
            Q_EMIT accountsChanged();
        }
    }

    QSet<QString> ruleIds;
    for (const QVariant &v : std::as_const(m_rules)) {
        ruleIds.insert(v.toMap().value(QStringLiteral("id")).toString());
    }
    QVariantList rules = m_rules;
    int skippedRules = 0;
    for (const QVariant &v : readJson(root + QStringLiteral("/rules.json")).array().toVariantList()) {
        QVariantMap rule = v.toMap();
        const QString accountId = idHere.value(rule.value(QStringLiteral("accountId")).toString());
        if (accountId.isEmpty()) {
            ++skippedRules;
            continue;
        }
        if (ruleIds.contains(rule.value(QStringLiteral("id")).toString())) {
            continue;
        }
        rule.insert(QStringLiteral("accountId"), accountId);
        rules.append(rule);
    }
    if (rules.size() != m_rules.size()) {
        setRules(rules);
    }
    if (skippedRules > 0) {
        *notes << (skippedRules == 1 ? tr("1 rule was left out, as its account isn't on this PC.")
                                     : tr("%1 rules were left out, as their accounts aren't on this PC.").arg(skippedRules));
    }

    const QString settingsFile = root + QStringLiteral("/settings.conf");
    if (QFile::exists(settingsFile)) {
        const QSettings from(settingsFile, QSettings::IniFormat);
        QSettings to;
        for (const QString &key : from.allKeys()) {
            if (settingStaysHere(key)) {
                continue;
            }
            if (key == QLatin1String("mail/defaultAccount")) {
                const QString id = idHere.value(from.value(key).toString());
                if (!id.isEmpty()) {
                    to.setValue(key, id);
                }
            } else if (key == QLatin1String("people/emptyGroups")) {
                QStringList groups = to.value(key).toStringList();
                for (const QString &g : from.value(key).toStringList()) {
                    if (!groups.contains(g, Qt::CaseInsensitive)) {
                        groups << g;
                    }
                }
                to.setValue(key, groups);
            } else {
                to.setValue(key, from.value(key));
            }
        }
        Q_EMIT accountsChanged();
    }
}

void MailEngine::restore(const QUrl &file)
{
    const QString path = file.toLocalFile();
    auto job = startTransfer(QStringLiteral("restore"), tr("Opening %1").arg(QFileInfo(path).fileName()), QString());
    if (!job) {
        return;
    }
    auto tmp = std::make_shared<QTemporaryDir>();
    auto error = std::make_shared<QString>();
    run([path, tmp, error]() {
        QProcess tar;
        tar.start(QStringLiteral("tar"), {QStringLiteral("-xzf"), path, QStringLiteral("-C"), tmp->path(), QStringLiteral("--no-same-owner"),
                                          QStringLiteral("--no-same-permissions"), backupFormat});
        if (!tar.waitForFinished(-1) || tar.exitCode() != 0) {
            *error = tr("That file isn't a Mail backup, or it is damaged.");
        }
    }, [this, job, tmp, error]() {
        const QString root = tmp->path() + QLatin1Char('/') + backupFormat;
        if (error->isEmpty() && readJson(root + QStringLiteral("/manifest.json")).object().value(QStringLiteral("format")).toString() != backupFormat) {
            *error = tr("That file isn't a Mail backup, or it is damaged.");
        }
        if (!error->isEmpty()) {
            finishTransfer(false, *error);
            return;
        }
        auto notes = std::make_shared<QStringList>();
        restoreSettings(root, notes.get());

        // Mail goes back into the account with the same address, if it is
        // here; a backup never adds accounts, which need signing in to.
        struct Target {
            QString accountId;
            QString email;
            QString dir;
            CamelStore *store;
        };
        QList<Target> targets;
        for (const QFileInfo &fi : QDir(root + QStringLiteral("/mail")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const Account *here = nullptr;
            for (const Account &a : m_store.accounts()) {
                if (a.email.compare(fi.fileName(), Qt::CaseInsensitive) == 0) {
                    here = &a;
                }
            }
            CamelStore *store = nullptr;
            if (here) {
                QMutexLocker lock(&m_folderLock);
                store = m_live.value(here->id).store;
            }
            if (!store) {
                *notes << tr("Mail for %1 was left out. Add that account, then restore again to bring its mail back.").arg(fi.fileName());
                continue;
            }
            g_object_ref(store);
            targets.append(Target{here->id, here->email, fi.absoluteFilePath(), store});
        }
        const QString book = PeopleEngine::instance()->defaultBook();
        const QString calendar = CalendarEngine::instance()->defaultCalendar();
        const QString importedName = tr("Imported");
        auto messages = std::make_shared<int>(0);
        auto contacts = std::make_shared<int>(0);
        auto events = std::make_shared<int>(0);
        job->setTitle(tr("Restoring contacts and calendars"));
        run([root, tmp, targets, book, calendar, importedName, job, messages, contacts, events, notes]() {
            QString why;
            if (QFile::exists(root + QStringLiteral("/contacts.vcf"))) {
                *contacts = PeopleEngine::readVcf(root + QStringLiteral("/contacts.vcf"), book, &why);
                if (*contacts < 0) {
                    *notes << tr("Contacts weren't restored. %1").arg(why);
                    *contacts = 0;
                }
            }
            for (const QFileInfo &fi : QDir(root + QStringLiteral("/calendars")).entryInfoList({QStringLiteral("*.ics")}, QDir::Files, QDir::Name)) {
                const int n = CalendarEngine::readInto(fi.absoluteFilePath(), fi.completeBaseName(), calendar, &why);
                if (n < 0) {
                    *notes << tr("Events from %1 weren't restored. %2").arg(fi.completeBaseName(), why);
                } else {
                    *events += n;
                }
            }
            QList<QList<ImportSource>> perAccount;
            QList<std::shared_ptr<QTemporaryDir>> temps;
            QString error;
            for (const Target &t : targets) {
                perAccount << gather({qMakePair(t.dir, QStringList{})}, {}, temps, *job, &error);
                job->total += countOf(perAccount.last());
            }
            for (int i = 0; i < targets.size(); ++i) {
                job->setTitle(tr("Restoring mail"), targets.at(i).email);
                *messages += importSources(targets.at(i).store, importedName, perAccount.at(i), *job, &error);
                g_object_unref(targets.at(i).store);
            }
            if (!error.isEmpty()) {
                *notes << error;
            }
        }, [this, targets, job, messages, contacts, events, notes]() {
            for (const Target &t : targets) {
                listFolders(t.accountId);
            }
            const QString done = job->cancelled ? tr("Restore stopped.") : tr("Restored %1 messages into the Imported folder, %2 contacts and %3 events.")
                                                                               .arg(*messages).arg(*contacts).arg(*events);
            finishTransfer(notes->isEmpty(), (QStringList{done} + *notes).join(QLatin1Char(' ')));
        });
    });
}

void MailEngine::runRules(const QString &accountId)
{
    QList<MessageRow> rows;
    for (auto it = m_lastInbox.cbegin(); it != m_lastInbox.cend(); ++it) {
        if (accountId.isEmpty() || it.key() == accountId) {
            rows += it.value();
        }
    }
    const int count = applyRules(rows).size();
    Q_EMIT notice(count == 0 ? tr("No messages in the Inbox matched a rule.")
                  : count == 1 ? tr("Rules took care of 1 message.")
                               : tr("Rules took care of %1 messages.").arg(count));
}

void MailEngine::addMicrosoftCalendar(const QString &id)
{
    const Account *account = m_store.find(id);
    if (!account || account->auth != QLatin1String("microsoft")) {
        return;
    }
    const QString email = account->email;
    m_store.readPassword(id, [email](const QString &refreshToken) {
        if (!refreshToken.isEmpty()) {
            CalendarEngine::instance()->addMicrosoftAccount(email, refreshToken);
        }
    });
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
