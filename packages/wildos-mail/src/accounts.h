#pragma once

#include <QList>
#include <QObject>
#include <QDateTime>
#include <QString>
#include <QVariantMap>

#include <functional>

// A mail account as Mail keeps it: where its mail lives and how to reach
// it. Saved in ~/.config/wildos-mail/accounts.json; the password is not in
// there, it is in the system's password store (KWallet) through QtKeychain.
struct Account {
    QString id;
    // What the folder pane calls it ("Personal", "Work"); the email address
    // until the person renames it.
    QString name;
    QString email;
    // How the person's name appears on mail they send.
    QString fullName;
    // gmail, icloud, yahoo, outlook or other: picks the help text and
    // provider quirks (Gmail files sent mail itself).
    QString provider;
    QString imapHost;
    int imapPort = 993;
    // ssl, starttls or none
    QString imapSecurity = QStringLiteral("ssl");
    QString smtpHost;
    int smtpPort = 465;
    QString smtpSecurity = QStringLiteral("ssl");
    QString user;
    // "password" (an ordinary or app password), "google" or "microsoft"
    // (Sign in with Google or Microsoft: the stored secret is the refresh
    // token).
    QString auth = QStringLiteral("password");
    bool signsInWithOAuth() const
    {
        return auth == QLatin1String("google") || auth == QLatin1String("microsoft");
    }
    // Keep every message on this PC for offline reading and search, or only
    // the newest.
    bool keepAll = true;
    // How far back keepAll keeps mail, in months; 0 is everything.
    int keepMonths = 0;
    // How often to look for new mail, in minutes: 0 as it arrives (IMAP
    // IDLE), -1 only when asked.
    int checkEvery = 0;
    QString signature;
    // The named signatures (MailEngine::signatures) added to new messages
    // and to replies and forwards; empty for none.
    QString signatureNew;
    QString signatureReply;
    // Automatic replies Mail sends itself (accounts other than Microsoft's,
    // whose replies Microsoft sends): on, the words, and an optional window.
    bool autoReply = false;
    QString autoReplyMessage;
    QDateTime autoReplyStart;
    QDateTime autoReplyEnd;
    QString autoReplySubject;
    // Who gets one: "all" or "contacts".
    QString autoReplyAudience = QStringLiteral("all");

    QVariantMap toMap() const;
    static Account fromMap(const QVariantMap &map);
};

class AccountStore : public QObject
{
    Q_OBJECT

public:
    explicit AccountStore(QObject *parent = nullptr);

    const QList<Account> &accounts() const
    {
        return m_accounts;
    }
    const Account *find(const QString &id) const;

    void add(const Account &account);
    void update(const Account &account);
    void remove(const QString &id);
    // Puts the account at this place in the list, which is the order the
    // folder pane shows and accounts.json keeps.
    void move(const QString &id, int to);

    // The password store answers asynchronously; done gets "" when there is
    // none or the store is locked and the person said no.
    void readPassword(const QString &id, std::function<void(const QString &)> done);
    void writePassword(const QString &id, const QString &password, std::function<void(bool ok)> done = {});

    static QString newId();

    // KWallet when the account has a wallet. Asking KWallet when it has
    // none makes it ask the person to create one (the Blowfish or GPG
    // dialog), which is what happens after Wild OS's automatic first
    // sign-in; then the password goes in a file only this account can read.
    static bool walletExists();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;

    QList<Account> m_accounts;
};
