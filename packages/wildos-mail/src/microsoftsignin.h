#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

// "Sign in with Microsoft" for Outlook.com, Hotmail, Live and Microsoft 365,
// through Wild OS Mail's own app in Microsoft Entra. Microsoft's sign-in
// page opens in the browser; when the person has signed in, Microsoft sends
// the browser to http://localhost on a port Mail listens on for that one
// answer, and Mail trades the code in it for a refresh token. One sign-in
// allows mail (IMAP and SMTP on Outlook's servers) and the calendars,
// contacts and task lists evolution-data-server's Microsoft 365 module
// reads through Graph; each side gets its own access tokens from it.
class MicrosoftSignIn : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool waiting READ waiting NOTIFY waitingChanged)

public:
    static MicrosoftSignIn *instance();
    static MicrosoftSignIn *create(QQmlEngine *, QJSEngine *);

    bool waiting() const
    {
        return !m_state.isEmpty();
    }

    Q_INVOKABLE void start(const QString &email);
    // Opens the same page again, for a closed browser tab.
    Q_INVOKABLE void reopen();
    Q_INVOKABLE void cancel();

    // A fresh access token for IMAP and SMTP. Blocks: Camel calls it from
    // its own threads. Microsoft hands out a new refresh token each time and
    // the old one stops working 90 days after it was issued, so the new one
    // comes back in newRefreshToken, to be kept instead.
    static bool mailToken(const QString &refreshToken, QString *token, int *expiresIn, QString *newRefreshToken, QString *error);
    // A Graph token for the account's mailbox settings (automatic replies).
    // consent is set when the account's sign-in predates that permission
    // and the person has to sign in again to allow it.
    static bool settingsToken(const QString &refreshToken, QString *token, QString *error, bool *consent);

    // Wild OS Mail's app in Microsoft Entra, which evolution-data-server is
    // told to use in place of Evolution's.
    static QString clientId();
    static QString redirectUri();
    static QString tenant();

Q_SIGNALS:
    void waitingChanged();
    void signedIn(const QString &email, const QString &refreshToken);
    void failed(const QString &message);

private:
    explicit MicrosoftSignIn(QObject *parent = nullptr);
    bool listen();
    void stopListening();
    void readRequest(QTcpSocket *socket);
    void answer(const QString &query);

    QString m_email;
    QString m_state;
    QByteArray m_verifier;
    QString m_url;
    QString m_redirect;
    QList<QTcpServer *> m_servers;
    QNetworkAccessManager *m_network;
};
