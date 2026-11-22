#include "camelsession.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include "googlesignin.h"
#include "microsoftsignin.h"

namespace
{
struct MailSession {
    CamelSession parent;
};
struct MailSessionClass {
    CamelSessionClass parent_class;
};

G_DEFINE_TYPE(MailSession, mail_session, CAMEL_TYPE_SESSION)

gchar *getPassword(CamelSession *, CamelService *service, const gchar *, const gchar *, guint32, GError **error)
{
    const QString password = Session::instance().password(Session::accountOf(service));
    if (password.isEmpty()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, QCoreApplication::translate("MailSession", "No password saved for this account").toUtf8().constData());
        return nullptr;
    }
    return g_strdup(password.toUtf8().constData());
}

// One try with the saved password. A wrong one is reported, not retried:
// asking again belongs to the window, which shows "Sign in again".
gboolean authenticateSync(CamelSession *, CamelService *service, const gchar *mechanism, GCancellable *cancellable, GError **error)
{
    const QString account = Session::accountOf(service);
    // Google and Microsoft accounts sign in with XOAUTH2, which asks the session for a
    // token itself; there is no password to hand over.
    if (Session::instance().oauth(account)) {
        return camel_service_authenticate_sync(service, mechanism, cancellable, error) == CAMEL_AUTHENTICATION_ACCEPTED;
    }
    const QString password = Session::instance().password(account);
    if (password.isEmpty()) {
        g_set_error_literal(error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, QCoreApplication::translate("MailSession", "Waiting for the password store").toUtf8().constData());
        return FALSE;
    }
    camel_service_set_password(service, password.toUtf8().constData());
    const CamelAuthenticationResult result = camel_service_authenticate_sync(service, mechanism, cancellable, error);
    if (result == CAMEL_AUTHENTICATION_REJECTED && error && !*error) {
        g_set_error_literal(error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE, QCoreApplication::translate("MailSession", "The password was not accepted").toUtf8().constData());
    }
    return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

// A certificate the system doesn't trust is refused. Mail never offers to
// accept one anyway, since that is exactly what an attacker in the middle of
// the connection would hope for.
CamelCertTrust trustPrompt(CamelSession *, CamelService *, GTlsCertificate *, GTlsCertificateFlags errors)
{
    return errors == 0 ? CAMEL_CERT_TRUST_FULLY : CAMEL_CERT_TRUST_NEVER;
}

gboolean getOAuth2AccessToken(CamelSession *, CamelService *service, gchar **outToken, gint *outExpiresIn, GCancellable *, GError **error)
{
    QString token;
    QString message;
    int expires = 0;
    if (!Session::instance().accessToken(Session::accountOf(service), &token, &expires, &message)) {
        g_set_error_literal(error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
                            (message.isEmpty() ? QCoreApplication::translate("MailSession", "The sign-in has expired") : message).toUtf8().constData());
        return FALSE;
    }
    if (outToken) {
        *outToken = g_strdup(token.toUtf8().constData());
    }
    if (outExpiresIn) {
        *outExpiresIn = expires;
    }
    return TRUE;
}

CamelFilterDriver *getFilterDriver(CamelSession *session, const gchar *, CamelFolder *, GError **)
{
    return camel_filter_driver_new(session);
}

void mail_session_class_init(MailSessionClass *klass)
{
    auto *session = CAMEL_SESSION_CLASS(klass);
    session->get_password = getPassword;
    session->authenticate_sync = authenticateSync;
    session->trust_prompt = trustPrompt;
    session->get_filter_driver = getFilterDriver;
    session->get_oauth2_access_token_sync = getOAuth2AccessToken;
}

void mail_session_init(MailSession *)
{
}
}

Session &Session::instance()
{
    static Session session;
    return session;
}

Session::Session()
{
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(data);
    QDir().mkpath(cache);
    camel_init(data.toUtf8().constData(), FALSE);
    camel_provider_init();
    m_session = CAMEL_SESSION(g_object_new(mail_session_get_type(),
                                           "user-data-dir", data.toUtf8().constData(),
                                           "user-cache-dir", cache.toUtf8().constData(),
                                           nullptr));
    camel_session_set_online(m_session, TRUE);
}

void Session::setPassword(const QString &accountId, const QString &password)
{
    QMutexLocker lock(&m_lock);
    m_passwords.insert(accountId, password);
}

QString Session::password(const QString &accountId) const
{
    QMutexLocker lock(&m_lock);
    return m_passwords.value(accountId);
}

void Session::setOAuth(const QString &accountId, const QString &auth)
{
    QMutexLocker lock(&m_lock);
    if (auth == QLatin1String("google") || auth == QLatin1String("microsoft")) {
        m_oauth.insert(accountId, auth);
    } else {
        m_oauth.remove(accountId);
    }
    m_tokens.remove(accountId);
}

bool Session::oauth(const QString &accountId) const
{
    QMutexLocker lock(&m_lock);
    return m_oauth.contains(accountId);
}

bool Session::accessToken(const QString &accountId, QString *token, int *expiresIn, QString *error)
{
    QString refresh;
    QString kind;
    {
        QMutexLocker lock(&m_lock);
        const Token cached = m_tokens.value(accountId);
        // Reused until a minute before the provider says it runs out.
        if (!cached.value.isEmpty() && cached.expires > QDateTime::currentDateTimeUtc().addSecs(60)) {
            *token = cached.value;
            *expiresIn = int(QDateTime::currentDateTimeUtc().secsTo(cached.expires));
            return true;
        }
        refresh = m_passwords.value(accountId);
        kind = m_oauth.value(accountId);
    }
    if (refresh.isEmpty()) {
        *error = QCoreApplication::translate("MailSession", "Waiting for the password store");
        return false;
    }
    QString newRefresh;
    const bool ok = kind == QLatin1String("microsoft") ? MicrosoftSignIn::mailToken(refresh, token, expiresIn, &newRefresh, error)
                                                       : GoogleSignIn::accessToken(refresh, token, expiresIn, error);
    if (!ok) {
        return false;
    }
    std::function<void(const QString &, const QString &)> save;
    {
        QMutexLocker lock(&m_lock);
        m_tokens.insert(accountId, {*token, QDateTime::currentDateTimeUtc().addSecs(*expiresIn)});
        if (!newRefresh.isEmpty() && newRefresh != refresh && m_passwords.value(accountId) == refresh) {
            m_passwords.insert(accountId, newRefresh);
            save = m_saveRefresh;
        }
    }
    if (save) {
        save(accountId, newRefresh);
    }
    return true;
}

void Session::setRefreshSaver(std::function<void(const QString &, const QString &)> saver)
{
    QMutexLocker lock(&m_lock);
    m_saveRefresh = std::move(saver);
}

QString Session::accountOf(CamelService *service)
{
    QString uid = QString::fromUtf8(camel_service_get_uid(service));
    if (uid.endsWith(QLatin1String(".smtp"))) {
        uid.chop(5);
    }
    return uid;
}
