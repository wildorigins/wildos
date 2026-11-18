#pragma once

#include <QByteArray>
#include <QString>

#include <sys/types.h>

struct passwd;

/* The key that opens an account's KWallet, and the root-held copy of it
 * kept for the account that signs in automatically and for accounts that
 * sign in with a PIN.
 *
 * pam_kwallet makes the key from the password typed at sign-in and the
 * salt in ~/.local/share/kwalletd/kdewallet.salt, then has kwalletd open
 * the wallet with it. An automatic sign-in types no password and a PIN
 * sign-in types only the PIN, so for those accounts Wild OS keeps the same
 * key in /var/lib/wildos/wallet/<uid>.key, readable by root only. On an
 * encrypted disk that is as safe as the disk; on a disk that is not
 * encrypted, whoever can read the disk from outside can read the key.
 * Everything here that writes there runs as root.
 */
namespace WalletKey
{
// pam_kwallet's own numbers (KWALLET_PAM_* in kwallet-pam's pam_kwallet.c).
// The key made here has to be the one it makes from the same password, or
// the wallet opens with one and not the other.
constexpr int saltSize = 56;
constexpr int keySize = 56;
constexpr int iterations = 50000;

QByteArray derive(const QByteArray &password, const QByteArray &salt);

/* The account's kdewallet.salt, made first when it has none, the way
 * pam_kwallet makes it: by a child running as the account, so a link in the
 * home folder cannot point root at another file. Empty on failure. */
QByteArray accountSalt(const struct passwd *pw);

/* The account SDDM signs in automatically, as SDDM reads its files: the
 * drop-ins in order, then sddm.conf, the last [Autologin] User= winning.
 * Empty when there is none. */
QString autoLoginUser();

/* Whether the account has a PIN (Settings > Accounts > Sign-in options),
 * which wildos-pin keeps in /var/lib/wildos/pin/NAME. */
bool hasPin(const QByteArray &name);

/* Takes the note wildos-pin leaves when the account signs in with its PIN
 * on the sign-in screen. True once per such sign-in. */
bool takePinSignIn(uid_t uid);

bool store(uid_t uid, const QByteArray &key);
QByteArray load(uid_t uid);
void forget(uid_t uid);
/* Removes every key but the auto sign-in account's and those of accounts
 * with a PIN, so a key never outlives the setting it was kept for. */
void forgetOthers();
}
