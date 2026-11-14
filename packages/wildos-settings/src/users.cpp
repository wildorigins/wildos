#include "users.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPasswordDigestor>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimeZone>
#include <QUrl>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

namespace
{
const QString accountsServiceName = QStringLiteral("org.freedesktop.Accounts");
const QString userInterface = QStringLiteral("org.freedesktop.Accounts.User");
const QString usersHelper = QStringLiteral("/usr/libexec/wildos/settings-users");
const QString walletKeysName = QStringLiteral("org.wildos.WalletKeys1");

bool isAdmin(const char *name, gid_t gid)
{
    int count = 64;
    QList<gid_t> groups(count);
    if (getgrouplist(name, gid, groups.data(), &count) < 0) {
        groups.resize(count);
        getgrouplist(name, gid, groups.data(), &count);
    }
    groups.resize(count);
    for (gid_t g : std::as_const(groups)) {
        if (struct group *gr = getgrgid(g)) {
            if (qstrcmp(gr->gr_name, "sudo") == 0 || qstrcmp(gr->gr_name, "wheel") == 0) {
                return true;
            }
        }
    }
    return false;
}

QString iconFor(const QString &name, const QString &home, bool own)
{
    QStringList candidates = {QStringLiteral("/var/lib/AccountsService/icons/") + name};
    if (own) {
        candidates.prepend(home + QStringLiteral("/.face.icon"));
    }
    for (const QString &path : std::as_const(candidates)) {
        const QFileInfo info(path);
        if (info.isReadable() && info.size() > 0) {
            QUrl url = QUrl::fromLocalFile(path);
            // The picture keeps its path when it changes; the timestamp
            // keeps the image cache from showing the old one.
            url.setQuery(QStringLiteral("v=") + QString::number(info.lastModified().toMSecsSinceEpoch()));
            return url.toString();
        }
    }
    return {};
}

// SDDM reads its drop-ins in order, then sddm.conf, and the last
// [Autologin] User= wins (the same reading as boot-handoff).
QString sddmAutoLogin()
{
    QStringList files;
    for (const QString &dir : {QStringLiteral("/usr/lib/sddm/sddm.conf.d"), QStringLiteral("/etc/sddm.conf.d")}) {
        const auto names = QDir(dir).entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
        for (const QString &n : names) {
            files << dir + QLatin1Char('/') + n;
        }
    }
    files << QStringLiteral("/etc/sddm.conf");
    QString user;
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString section;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                section = line;
            } else if (section == QLatin1String("[Autologin]") && line.startsWith(QLatin1String("User="))) {
                user = line.mid(5).trimmed();
            }
        }
    }
    return user;
}

// Changes your password through wildos-wallet-keys, as root: the hardened
// system keeps passwd without setuid, so an account cannot run it on its
// own password. The service checks the current password and applies the
// password rules as passwd would. Returns an empty string on success,
// otherwise the sentence to show.
QString changeOwnPassword(const QByteArray &current, const QByteArray &next)
{
    QDBusMessage change = QDBusMessage::createMethodCall(walletKeysName, QStringLiteral("/org/wildos/WalletKeys1"), walletKeysName,
                                                         QStringLiteral("ChangePassword"));
    change << QString::fromUtf8(current) << QString::fromUtf8(next);
    const QDBusMessage reply = QDBusConnection::systemBus().call(change, QDBus::Block, 90000);
    if (reply.type() == QDBusMessage::ReplyMessage) {
        return {};
    }
    if (reply.errorName().startsWith(QLatin1String("org.wildos.WalletKeys1.Error.")) && !reply.errorMessage().isEmpty()) {
        return reply.errorMessage();
    }
    return QStringLiteral("Wild OS could not change the password.");
}

// pam_kwallet's key: PBKDF2 SHA-512, 50000 rounds, 56 bytes (kwallet-pam's
// pam_kwallet.c), from the password and the salt in kdewallet.salt.
QByteArray walletKey(const QByteArray &password, const QByteArray &salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha512, password, salt, 50000, 56);
}

// After a password change, moves the wallet from the key made from the old
// password to the one made from the new password, through kwalletd's
// pamChangeHash (a Wild OS patch, build/patches/kwallet). kwalletd refuses
// when the old key does not open the wallet, which happens when someone
// gave it a password of its own. Returns false only for a wallet that
// exists and could not be moved.
constexpr int kwalletdUnreachable = -1000;

// Moves the wallet from the key made from one password to the key made from
// another (the same password checks the wallet opens with it), opening it
// afterwards when asked. Returns pamChangeHash's answer: 0 done, 1 no
// wallet (nor salt), a Backend error such as -9 when the first key does not
// open it, or kwalletdUnreachable.
int rekeyWallet(const QByteArray &from, const QByteArray &to, bool openAfter)
{
    // Where pam_kwallet looks, whatever XDG_DATA_HOME says.
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/kwalletd");
    QFile saltFile(dir + QStringLiteral("/kdewallet.salt"));
    const QByteArray salt = saltFile.open(QIODevice::ReadOnly) ? saltFile.read(57) : QByteArray();
    if (salt.size() != 56) {
        return 1;
    }
    QDBusConnection session = QDBusConnection::sessionBus();
    auto kwallet = [](const QString &method) {
        return QDBusMessage::createMethodCall(QStringLiteral("org.kde.kwalletd6"), QStringLiteral("/modules/kwalletd6"), QStringLiteral("org.kde.KWallet"),
                                              method);
    };
    const QDBusReply<QString> local = session.call(kwallet(QStringLiteral("localWallet")), QDBus::Block, 10000);
    const QString wallet = local.isValid() && !local.value().isEmpty() ? local.value() : QStringLiteral("kdewallet");
    if (!QFileInfo::exists(dir + QLatin1Char('/') + wallet + QStringLiteral(".kwl"))) {
        return 1;
    }
    const QByteArray toKey = walletKey(to, salt);
    QDBusMessage change = kwallet(QStringLiteral("pamChangeHash"));
    change << wallet << walletKey(from, salt) << toKey;
    const QDBusReply<int> changed = session.call(change, QDBus::Block, 30000);
    if (!changed.isValid()) {
        return kwalletdUnreachable;
    }
    if (changed.value() == 0 && openAfter) {
        // pamOpen never replies; isOpen, sent after it on the same
        // connection, answers once it has run.
        QDBusMessage open = kwallet(QStringLiteral("pamOpen"));
        open << wallet << toKey << 0;
        session.send(open);
        QDBusMessage isOpen = kwallet(QStringLiteral("isOpen"));
        isOpen << wallet;
        session.call(isOpen, QDBus::Block, 10000);
    }
    return changed.value();
}

// kwalletd's answers when a key does not open the wallet.
bool wrongWalletKey(int code)
{
    return code == -5 || code == -7 || code == -8 || code == -9;
}

// KWallet's own create dialog takes an empty password, and people press
// Enter on it without thinking they chose one. Such a wallet is tried with
// no password whenever the account's does not open it.
int rekeyWalletOrEmpty(const QByteArray &from, const QByteArray &to, bool openAfter)
{
    const int code = rekeyWallet(from, to, openAfter);
    if (!wrongWalletKey(code) || from.isEmpty()) {
        return code;
    }
    return rekeyWallet(QByteArray(""), to, openAfter) == 0 ? 0 : code;
}

bool moveWallet(const QByteArray &current, const QByteArray &next)
{
    const int code = rekeyWalletOrEmpty(current, next, false);
    return code == 0 || code == 1;
}

// The system's copy of the key, for the account that signs in
// automatically, follows the new password. A copy that cannot be refreshed
// is removed, since it no longer opens the wallet; the next start then asks
// for the password instead.
void refreshKeptKey(const QByteArray &next)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage store = QDBusMessage::createMethodCall(walletKeysName, QStringLiteral("/org/wildos/WalletKeys1"), walletKeysName, QStringLiteral("Store"));
    store << QString::fromUtf8(next);
    if (bus.call(store, QDBus::Block, 60000).type() != QDBusMessage::ReplyMessage) {
        bus.call(QDBusMessage::createMethodCall(walletKeysName, QStringLiteral("/org/wildos/WalletKeys1"), walletKeysName, QStringLiteral("Forget")),
                 QDBus::Block, 10000);
    }
}
}

Users::Users(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    m_accountsService = bus.isConnected() && bus.interface()
        && (bus.interface()->isServiceRegistered(accountsServiceName) || bus.interface()->activatableServiceNames().value().contains(accountsServiceName));
    bus.connect(accountsServiceName, userPath(), userInterface, QStringLiteral("Changed"), this, SLOT(reload()));
    bus.connect(accountsServiceName, QStringLiteral("/org/freedesktop/Accounts"), accountsServiceName, QStringLiteral("UserAdded"), this, SLOT(reload()));
    bus.connect(accountsServiceName, QStringLiteral("/org/freedesktop/Accounts"), accountsServiceName, QStringLiteral("UserDeleted"), this, SLOT(reload()));
    reload();
}

QString Users::userPath() const
{
    return QStringLiteral("/org/freedesktop/Accounts/User%1").arg(getuid());
}

void Users::reload()
{
    const uid_t self = getuid();
    m_me.clear();
    m_others.clear();
    m_adminCount = 0;

    // Real people: the ids useradd hands out, with a shell to sign in with.
    setpwent();
    while (struct passwd *pw = getpwent()) {
        const bool own = pw->pw_uid == self;
        const QString shell = QString::fromUtf8(pw->pw_shell);
        if (!own && (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || shell.endsWith(QLatin1String("nologin")) || shell.endsWith(QLatin1String("/false")))) {
            continue;
        }
        const QString name = QString::fromUtf8(pw->pw_name);
        // The guest session's account comes and goes with its switch on
        // Advanced protection (wildos-base guest-account); making it an
        // administrator or removing it here would undo what that promises.
        if (!own && name == QLatin1String("guest") && pw->pw_passwd[0] == '\0') {
            continue;
        }
        const QString full = QString::fromUtf8(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
        const bool admin = isAdmin(pw->pw_name, pw->pw_gid);
        if (admin && pw->pw_uid >= 1000) {
            ++m_adminCount;
        }
        const QVariantMap user = {
            {QStringLiteral("userName"), name},
            {QStringLiteral("realName"), full.isEmpty() ? name : full},
            {QStringLiteral("icon"), iconFor(name, QString::fromUtf8(pw->pw_dir), own)},
            {QStringLiteral("admin"), admin},
            {QStringLiteral("uid"), uint(pw->pw_uid)},
        };
        if (own) {
            m_me = user;
        } else {
            m_others << user;
        }
    }
    endpwent();
    std::sort(m_others.begin(), m_others.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("realName")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("realName")).toString()) < 0;
    });
    m_autoLogin = sddmAutoLogin();

    if (m_accountsService) {
        auto message = QDBusMessage::createMethodCall(accountsServiceName, userPath(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        message << userInterface << QStringLiteral("PasswordHint");
        const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 2000);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_me.insert(QStringLiteral("hint"), reply.arguments().constFirst().value<QDBusVariant>().variant().toString());
        }
        // When the password last changed, for Sign-in options. The shadow
        // file only keeps the day; 0 means it must change at next sign-in.
        auto policy = QDBusMessage::createMethodCall(accountsServiceName, userPath(), userInterface, QStringLiteral("GetPasswordExpirationPolicy"));
        const QDBusMessage expiry = QDBusConnection::systemBus().call(policy, QDBus::Block, 2000);
        if (expiry.type() == QDBusMessage::ReplyMessage && expiry.arguments().size() >= 2) {
            const qint64 changed = expiry.arguments().at(1).toLongLong();
            if (changed > 0) {
                m_me.insert(QStringLiteral("passwordChanged"), QDateTime::fromSecsSinceEpoch(changed, QTimeZone::UTC).date().toString(Qt::ISODate));
            }
        }
    }
    Q_EMIT changed();
}

void Users::setBusy(const QString &busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void Users::callUser(const QString &method, const QVariant &argument, const QString &what)
{
    auto message = QDBusMessage::createMethodCall(accountsServiceName, userPath(), userInterface, method);
    message << argument;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, what] {
        watcher->deleteLater();
        QDBusPendingReply<> reply = *watcher;
        reload();
        if (reply.isError()) {
            Q_EMIT failed(what, reply.error().type() == QDBusError::AccessDenied ? QStringLiteral("Wild OS was not allowed to change it.")
                                                                                 : QStringLiteral("Wild OS could not save the change."));
        } else {
            Q_EMIT done(what, QString());
        }
    });
}

void Users::setRealName(const QString &name)
{
    const QString clean = name.simplified();
    if (clean.isEmpty() || clean.contains(QLatin1Char(':')) || clean.contains(QLatin1Char(','))) {
        Q_EMIT failed(QStringLiteral("name"), QStringLiteral("A name can't be empty or contain a colon or comma."));
        return;
    }
    callUser(QStringLiteral("SetRealName"), clean, QStringLiteral("name"));
}

QSize Users::pictureSize(const QString &file) const
{
    QImageReader reader(QUrl(file).isLocalFile() ? QUrl(file).toLocalFile() : file);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    // Rotated photos report their stored size; the transform swaps it.
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
        size.transpose();
    }
    return size;
}

void Users::setPicture(const QString &file, qreal x, qreal y, qreal size)
{
    const QString path = QUrl(file).isLocalFile() ? QUrl(file).toLocalFile() : file;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        Q_EMIT failed(QStringLiteral("picture"), QStringLiteral("That file isn't a picture Wild OS can open."));
        return;
    }
    const QRect square = QRectF(x, y, size, size).toRect().intersected(image.rect());
    // 512px is plenty for the 200px sign-in picture on a high-density screen
    // and well under AccountsService's size limit.
    const QImage face = image.copy(square).scaled(512, 512, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-settings");
    QDir().mkpath(dir);
    const QString staged = dir + QStringLiteral("/account-picture.png");
    if (!face.save(staged, "PNG")) {
        Q_EMIT failed(QStringLiteral("picture"), QStringLiteral("Wild OS could not save the picture."));
        return;
    }
    // ~/.face.icon is what KDE parts and the sign-in screen read for the
    // account's own picture; AccountsService keeps the copy other accounts
    // and the sign-in screen see.
    const QString faceIcon = QDir::homePath() + QStringLiteral("/.face.icon");
    QFile::remove(faceIcon);
    QFile::copy(staged, faceIcon);
    QFile::setPermissions(faceIcon, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    if (m_accountsService) {
        callUser(QStringLiteral("SetIconFile"), staged, QStringLiteral("picture"));
    } else {
        reload();
        Q_EMIT done(QStringLiteral("picture"), QString());
    }
}

void Users::clearPicture()
{
    QFile::remove(QDir::homePath() + QStringLiteral("/.face.icon"));
    if (m_accountsService) {
        callUser(QStringLiteral("SetIconFile"), QString(), QStringLiteral("picture"));
    } else {
        reload();
        Q_EMIT done(QStringLiteral("picture"), QString());
    }
}

void Users::changePassword(const QString &current, const QString &next, const QString &hint)
{
    setBusy(QStringLiteral("password"));
    QPointer<Users> self(this);
    const bool automatic = !m_autoLogin.isEmpty() && m_autoLogin == m_me.value(QStringLiteral("userName")).toString();
    QThread *thread = QThread::create([self, current = current.toUtf8(), next = next.toUtf8(), hint, automatic] {
        const QString error = changeOwnPassword(current, next);
        bool walletMoved = true;
        if (error.isEmpty()) {
            walletMoved = moveWallet(current, next);
            // A wallet left on the old key still opens with the kept key,
            // so that key stays as it is.
            if (automatic && walletMoved) {
                refreshKeptKey(next);
            }
        }
        QMetaObject::invokeMethod(qApp, [self, error, hint, walletMoved] {
            if (!self) {
                return;
            }
            self->setBusy({});
            if (!error.isEmpty()) {
                Q_EMIT self->failed(QStringLiteral("password"), error);
                return;
            }
            if (self->m_accountsService && hint != self->m_me.value(QStringLiteral("hint")).toString()) {
                self->callUser(QStringLiteral("SetPasswordHint"), hint, QStringLiteral("password"));
            }
            Q_EMIT self->done(QStringLiteral("password"),
                              walletMoved ? QStringLiteral("Your password was changed. Use the new one next time you sign in.")
                                          : QStringLiteral("Your password was changed. Your saved passwords still open with the old one, so apps may ask "
                                                           "you for it."));
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Users::openWallet(const QString &password, const QString &walletPassword)
{
    setBusy(QStringLiteral("wallet"));
    const bool automatic = !m_autoLogin.isEmpty() && m_autoLogin == m_me.value(QStringLiteral("userName")).toString();
    QPointer<Users> self(this);
    QThread *thread = QThread::create([self, password = password.toUtf8(), walletPassword = walletPassword.toUtf8(), automatic] {
        // The account password first, checked by PAM: a wallet moved to a
        // mistyped password would open with nothing. Store also keeps the
        // key for the automatic sign-in.
        QDBusMessage check = QDBusMessage::createMethodCall(walletKeysName, QStringLiteral("/org/wildos/WalletKeys1"), walletKeysName,
                                                            automatic ? QStringLiteral("Store") : QStringLiteral("CheckPassword"));
        check << QString::fromUtf8(password);
        const QDBusMessage checked = QDBusConnection::systemBus().call(check, QDBus::Block, 60000);
        QString what = QStringLiteral("wallet");
        QString error;
        if (checked.type() != QDBusMessage::ReplyMessage) {
            error = checked.errorName().endsWith(QLatin1String("WrongPassword")) ? QStringLiteral("That password isn't right.")
                                                                                  : QStringLiteral("Wild OS could not check your password.");
        } else {
            const int code = walletPassword.isEmpty() ? rekeyWalletOrEmpty(password, password, true) : rekeyWallet(walletPassword, password, true);
            if (wrongWalletKey(code)) {
                what = QStringLiteral("wallet-other");
                error = walletPassword.isEmpty()
                    ? QStringLiteral("Your saved passwords use a different password from your account. Type that one too, and they will move to your account password.")
                    : QStringLiteral("That isn't the password your saved passwords use.");
            } else if (code != 0 && code != 1) {
                error = QStringLiteral("Wild OS could not open your saved passwords.");
            }
        }
        QMetaObject::invokeMethod(qApp, [self, what, error, automatic] {
            if (!self) {
                return;
            }
            self->setBusy({});
            if (!error.isEmpty()) {
                Q_EMIT self->failed(what, error);
            } else {
                Q_EMIT self->done(QStringLiteral("wallet"),
                                  automatic ? QStringLiteral("Your saved passwords are open, and open by themselves from now on.")
                                            : QStringLiteral("Your saved passwords are open, and open by themselves when you sign in with your password."));
            }
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Users::helper(const QString &what, const QStringList &arguments, const QByteArray &input, const QString &success)
{
    setBusy(what);
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, what, success](int code, QProcess::ExitStatus status) {
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
        process->deleteLater();
        setBusy({});
        reload();
        if (status == QProcess::NormalExit && code == 0) {
            Q_EMIT done(what, success);
        } else if (code == 126 || code == 127) {
            Q_EMIT failed(what, QStringLiteral("An administrator's password is needed for this."));
        } else {
            Q_EMIT failed(what, error.isEmpty() ? QStringLiteral("Wild OS could not make the change.") : error);
        }
    });
    process->start(QStringLiteral("pkexec"), QStringList{usersHelper} + arguments);
    if (!input.isEmpty()) {
        process->write(input);
    }
    process->closeWriteChannel();
}

void Users::addUser(const QString &fullName, const QString &userName, const QString &password, bool admin)
{
    helper(QStringLiteral("add"), {QStringLiteral("add"), userName, admin ? QStringLiteral("admin") : QStringLiteral("standard"), fullName.simplified()},
           password.toUtf8() + '\n', fullName.simplified() + QStringLiteral(" can now sign in to this computer."));
}

void Users::removeUser(const QString &userName)
{
    helper(QStringLiteral("remove"), {QStringLiteral("remove"), userName}, {}, QStringLiteral("The account and its files were removed."));
}

void Users::setAdmin(const QString &userName, bool admin)
{
    helper(QStringLiteral("type"), {QStringLiteral("admin"), userName, admin ? QStringLiteral("on") : QStringLiteral("off")}, {},
           admin ? QStringLiteral("The account is now an administrator.") : QStringLiteral("The account is now a standard user."));
}

void Users::setAutoLogin(bool on, const QString &password)
{
    helper(QStringLiteral("autologin"), {QStringLiteral("autologin"), on ? m_me.value(QStringLiteral("userName")).toString() : QStringLiteral("off")},
           on ? password.toUtf8() + '\n' : QByteArray(),
           on ? QStringLiteral("This computer will sign you in when it starts, with your saved passwords ready.")
              : QStringLiteral("This computer will ask for your password when it starts."));
}

QString Users::suggestUserName(const QString &fullName) const
{
    QString first = fullName.simplified().section(QLatin1Char(' '), 0, 0).normalized(QString::NormalizationForm_KD).toLower();
    first.remove(QRegularExpression(QStringLiteral("[^a-z0-9_-]")));
    first.remove(QRegularExpression(QStringLiteral("^[^a-z_]+")));
    first.truncate(32);
    if (first.isEmpty()) {
        return {};
    }
    QString candidate = first;
    for (int i = 2; getpwnam(candidate.toUtf8().constData()) != nullptr; ++i) {
        candidate = first + QString::number(i);
    }
    return candidate;
}

QString Users::checkUserName(const QString &userName) const
{
    static const QRegularExpression valid(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    if (userName.isEmpty()) {
        return QStringLiteral("Choose a user name.");
    }
    if (!valid.match(userName).hasMatch()) {
        return QStringLiteral("Use small letters, numbers, - and _ only, starting with a letter.");
    }
    if (getpwnam(userName.toUtf8().constData()) != nullptr || getgrnam(userName.toUtf8().constData()) != nullptr) {
        return QStringLiteral("That user name is taken.");
    }
    return {};
}
