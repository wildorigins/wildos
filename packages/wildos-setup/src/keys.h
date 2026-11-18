#pragma once

#include <QDBusContext>
#include <QObject>
#include <QTimer>

/* org.wildos.WalletKeys1: the root-held wallet key of the account that
 * signs in automatically and of accounts with a PIN (see walletkey.h), and
 * the root half of changing your own password, which changes what that key
 * is made from.
 *
 * Every call acts on the caller's own account, found from its uid on the
 * bus, never from an argument, so no account can reach another's key. The
 * key is handed out for wildos-setup-wallet to open the wallet with at the
 * start of a session that typed no password: once per boot to the account
 * SDDM signs in automatically, and once after each sign-in with the right
 * PIN. Anything asking later in the session is refused.
 */
class WalletKeys : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.WalletKeys1")

public:
    explicit WalletKeys(QObject *parent = nullptr);

public Q_SLOTS:
    /* Keeps the key made from the caller's password, after checking it
     * with PAM. Only for the auto sign-in account and accounts with a PIN:
     * Settings calls it when a PIN is saved and when such an account's
     * password changes. Turning automatic sign-in on keeps the key through
     * settings-users instead, as root. */
    void Store(const QString &password);

    /* The caller's key: after a sign-in with the PIN, or for the auto
     * sign-in account when nothing has taken the key since the computer
     * started. */
    QByteArray Key();

    /* Removes the caller's key, if there is one. */
    void Forget();

    /* Removes keys no setting asks for any more. Settings calls it after a
     * PIN is removed. */
    void Tidy();

    /* Checks the caller's own password with PAM (the same delays and
     * lockouts as signing in), so Settings can be sure of it before it
     * moves the wallet to the key made from it. Fails with WrongPassword. */
    void CheckPassword(const QString &password);

    /* Changes the caller's own sign-in password. The hardened system keeps
     * passwd without setuid, so an account cannot change its password by
     * itself; this does it as root, but as PAM would for the account: the
     * current password is checked first, and the change goes through the
     * passwd PAM stack with the account as the real user, so the password
     * rules (length, similarity, history) apply as they would to passwd.
     * The work happens in a child of this program (change-password), which
     * gets both passwords on its standard input. Fails with
     * WrongPassword or Refused, with a sentence fit to show. */
    void ChangePassword(const QString &current, const QString &next);

private:
    bool refuse(const QString &name, const QString &sentence);
    bool callerUid(uint *uid);

    QTimer m_idle;
};

/* PAM's verdict on the password of the account called user, through the
 * wildos-wallet-keys PAM service (common-auth). */
bool checkPassword(const QByteArray &user, const QByteArray &password);
