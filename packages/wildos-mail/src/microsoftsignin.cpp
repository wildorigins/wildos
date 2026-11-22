#include "microsoftsignin.h"
#include "mailengine.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QEventLoop>
#include <QHostAddress>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace
{
const QString appId = QStringLiteral("5796eb5f-f489-4c16-a06c-328a371a6f16");
// Any organisation's accounts and personal ones.
const QString authority = QStringLiteral("common");
const QString authorizeUri = QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize");
const QString tokenUri = QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token");

// A token is for one service: Outlook's mail servers, or Graph. The sign-in
// asks the person to allow both at once, and each token is then asked for
// with its own service's permissions. The Graph list is exactly what
// evolution-data-server's Microsoft 365 module asks for when it renews its
// token (cut down to these in build.sh), since Microsoft refuses a renewal
// that asks for anything the person didn't allow.
const QString mailScope = QStringLiteral("https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access");
const QString graphScope = QStringLiteral(
    "https://graph.microsoft.com/Calendars.ReadWrite https://graph.microsoft.com/Contacts.ReadWrite https://graph.microsoft.com/Mail.ReadBasic "
    "https://graph.microsoft.com/MailboxSettings.Read https://graph.microsoft.com/MailboxSettings.ReadWrite https://graph.microsoft.com/Tasks.ReadWrite "
    "https://graph.microsoft.com/User.Read");

QByteArray randomUrlSafe(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(raw.data()), reinterpret_cast<quint32 *>(raw.data() + (bytes / 4) * 4));
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString errorOf(const QJsonObject &json, QNetworkReply *reply)
{
    QString detail = json.value(QStringLiteral("error_description")).toString();
    // Microsoft's descriptions run on with a trace and correlation id.
    const int trace = detail.indexOf(QLatin1String("\r\nTrace ID"));
    if (trace > 0) {
        detail.truncate(trace);
    }
    return detail.isEmpty() ? reply->errorString() : detail;
}

// The page the browser shows once Microsoft has sent it back to Mail.
QByteArray page(const QString &title, const QString &text)
{
    const QString html = QStringLiteral(
                             "<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title>"
                             "<meta name=\"viewport\" content=\"width=device-width\">"
                             "<style>body{font-family:system-ui,sans-serif;background:#f3f3f3;color:#1b1b1b;display:grid;place-items:center;"
                             "min-height:100vh;margin:0}main{max-width:420px;padding:32px;background:#fff;border-radius:12px;"
                             "box-shadow:0 2px 12px rgba(0,0,0,.08)}h1{font-size:22px;font-weight:600;margin:0 0 8px}"
                             "p{font-size:14px;line-height:1.5;color:#5d5d5d;margin:0}"
                             "@media (prefers-color-scheme:dark){body{background:#1c1c1c;color:#f3f3f3}main{background:#2b2b2b}p{color:#c5c5c5}}"
                             "</style></head><body><main><h1>%1</h1><p>%2</p></main></body></html>")
                             .arg(title.toHtmlEscaped(), text.toHtmlEscaped());
    const QByteArray body = html.toUtf8();
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
}

MicrosoftSignIn *MicrosoftSignIn::instance()
{
    static MicrosoftSignIn *signIn = new MicrosoftSignIn();
    return signIn;
}

MicrosoftSignIn *MicrosoftSignIn::create(QQmlEngine *, QJSEngine *)
{
    MicrosoftSignIn *signIn = instance();
    QJSEngine::setObjectOwnership(signIn, QJSEngine::CppOwnership);
    return signIn;
}

MicrosoftSignIn::MicrosoftSignIn(QObject *parent)
    : QObject(parent)
    , m_network(nullptr)
{
}

QString MicrosoftSignIn::clientId()
{
    return appId;
}

QString MicrosoftSignIn::redirectUri()
{
    return QStringLiteral("http://localhost");
}

QString MicrosoftSignIn::tenant()
{
    return authority;
}

// Microsoft allows any port on http://localhost for an app like Mail, so
// the port is whatever the system gives. The browser may reach "localhost"
// over IPv4 or IPv6, so both listen on it.
bool MicrosoftSignIn::listen()
{
    stopListening();
    auto *v4 = new QTcpServer(this);
    if (!v4->listen(QHostAddress::LocalHost, 0)) {
        delete v4;
        return false;
    }
    m_servers.append(v4);
    auto *v6 = new QTcpServer(this);
    if (v6->listen(QHostAddress::LocalHostIPv6, v4->serverPort())) {
        m_servers.append(v6);
    } else {
        delete v6;
    }
    for (QTcpServer *server : std::as_const(m_servers)) {
        connect(server, &QTcpServer::newConnection, this, [this, server]() {
            while (QTcpSocket *socket = server->nextPendingConnection()) {
                // The server closes as soon as Microsoft's answer is read;
                // the page telling the person they're signed in still has
                // to reach the browser, so the connection outlives it.
                socket->setParent(this);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    readRequest(socket);
                });
                // A browser that opens a connection and says nothing.
                QTimer::singleShot(10000, socket, [socket]() {
                    socket->abort();
                    socket->deleteLater();
                });
            }
        });
    }
    m_redirect = QStringLiteral("http://localhost:%1").arg(v4->serverPort());
    return true;
}

void MicrosoftSignIn::stopListening()
{
    for (QTcpServer *server : std::as_const(m_servers)) {
        server->close();
        server->deleteLater();
    }
    m_servers.clear();
}

void MicrosoftSignIn::start(const QString &email)
{
    if (!listen()) {
        Q_EMIT failed(tr("Mail couldn't wait for Microsoft's answer on this PC."));
        return;
    }
    m_email = email.trimmed();
    m_state = QString::fromLatin1(randomUrlSafe(24));
    // PKCE: only the copy of Mail that started the sign-in can finish it.
    m_verifier = randomUrlSafe(48);
    const QByteArray challenge = QCryptographicHash::hash(m_verifier, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    QUrl url(authorizeUri);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), appId);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirect);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("response_mode"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("scope"), QStringLiteral("openid email profile ") + mailScope + QLatin1Char(' ') + graphScope);
    query.addQueryItem(QStringLiteral("state"), m_state);
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    // The address goes to Microsoft as a hint, so its page opens on that
    // account. Forcing the account picker (prompt=select_account) loops
    // in Brave: choosing an account already signed in there brings the
    // picker straight back.
    if (!m_email.isEmpty()) {
        query.addQueryItem(QStringLiteral("login_hint"), m_email);
    }
    url.setQuery(query);
    m_url = url.toString(QUrl::FullyEncoded);
    qCInfo(mailLog) << "opening Microsoft sign-in in the browser";
    Q_EMIT waitingChanged();
    QDesktopServices::openUrl(QUrl(m_url));
}

void MicrosoftSignIn::reopen()
{
    if (!m_url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_url));
    }
}

void MicrosoftSignIn::cancel()
{
    stopListening();
    const bool was = waiting();
    m_state.clear();
    m_verifier.clear();
    m_url.clear();
    if (was) {
        Q_EMIT waitingChanged();
    }
}

void MicrosoftSignIn::readRequest(QTcpSocket *socket)
{
    QByteArray buffered = socket->property("request").toByteArray() + socket->readAll();
    if (!buffered.contains("\r\n\r\n")) {
        if (buffered.size() > 16384) {
            socket->abort();
        } else {
            socket->setProperty("request", buffered);
        }
        return;
    }
    // GET /?code=...&state=... HTTP/1.1
    const QList<QByteArray> line = buffered.left(buffered.indexOf("\r\n")).split(' ');
    const QUrl target(QString::fromLatin1(line.value(1)));
    const QUrlQuery query(target.query());
    const bool ours = line.value(0) == "GET" && target.path() == QLatin1String("/") && !m_state.isEmpty()
        && query.queryItemValue(QStringLiteral("state")) == m_state;
    if (!ours) {
        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    const bool ok = !query.queryItemValue(QStringLiteral("code")).isEmpty();
    socket->write(ok ? page(tr("You're signed in"), tr("You can close this tab and go back to Mail."))
                     : page(tr("You weren't signed in"), tr("Go back to Mail to try again.")));
    socket->disconnectFromHost();
    answer(target.query());
}

void MicrosoftSignIn::answer(const QString &target)
{
    qCInfo(mailLog) << "Microsoft sent the browser back to Mail";
    const QUrlQuery query(target);
    const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    const QString error = query.queryItemValue(QStringLiteral("error"));
    const QString description = query.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded).replace(QLatin1Char('+'), QLatin1Char(' '));
    const QByteArray verifier = m_verifier;
    const QString email = m_email;
    const QString redirect = m_redirect;
    cancel();
    if (!error.isEmpty() || code.isEmpty()) {
        qCWarning(mailLog) << "Microsoft sign-in refused:" << error << description;
        Q_EMIT failed(error == QLatin1String("access_denied") ? tr("Microsoft sign-in was cancelled.")
                                                              : tr("Microsoft didn't sign you in. %1").arg(description.isEmpty() ? error : description));
        return;
    }

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), appId);
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), redirect);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(verifier));
    body.addQueryItem(QStringLiteral("scope"), mailScope);
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setCookieJar(nullptr);
    }
    QNetworkRequest request{QUrl(tokenUri)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_network->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, email]() {
        reply->deleteLater();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        const QString refresh = json.value(QStringLiteral("refresh_token")).toString();
        if (reply->error() != QNetworkReply::NoError || refresh.isEmpty()) {
            const QString detail = errorOf(json, reply);
            qCWarning(mailLog) << "Microsoft token exchange failed:" << detail;
            Q_EMIT failed(tr("Microsoft didn't finish signing you in. %1").arg(detail));
            return;
        }
        qCInfo(mailLog) << "Microsoft sign-in finished";
        Q_EMIT signedIn(email, refresh);
    });
}

bool MicrosoftSignIn::settingsToken(const QString &refreshToken, QString *token, QString *error, bool *consent)
{
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), appId);
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("scope"), QStringLiteral("https://graph.microsoft.com/MailboxSettings.ReadWrite"));
    QNetworkAccessManager network;
    network.setCookieJar(nullptr);
    QNetworkRequest request{QUrl(tokenUri)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(30000);
    QNetworkReply *reply = network.post(request, body.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
    const bool ok = reply->error() == QNetworkReply::NoError;
    if (!ok) {
        *error = errorOf(json, reply);
        // AADSTS65001: the permission was never allowed.
        *consent = json.value(QStringLiteral("error")).toString() == QLatin1String("invalid_grant")
            && json.value(QStringLiteral("error_description")).toString().contains(QLatin1String("AADSTS65001"));
    }
    delete reply;
    if (!ok) {
        return false;
    }
    *token = json.value(QStringLiteral("access_token")).toString();
    return !token->isEmpty();
}

bool MicrosoftSignIn::mailToken(const QString &refreshToken, QString *token, int *expiresIn, QString *newRefreshToken, QString *error)
{
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), appId);
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("scope"), mailScope);
    // Camel asks from its own threads, so this has its own network access
    // and event loop.
    QNetworkAccessManager network;
    network.setCookieJar(nullptr);
    QNetworkRequest request{QUrl(tokenUri)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(30000);
    QNetworkReply *reply = network.post(request, body.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
    const bool ok = reply->error() == QNetworkReply::NoError;
    if (!ok && error) {
        *error = errorOf(json, reply);
    }
    delete reply;
    if (!ok) {
        return false;
    }
    *token = json.value(QStringLiteral("access_token")).toString();
    *expiresIn = json.value(QStringLiteral("expires_in")).toInt(3600);
    *newRefreshToken = json.value(QStringLiteral("refresh_token")).toString();
    return !token->isEmpty();
}
