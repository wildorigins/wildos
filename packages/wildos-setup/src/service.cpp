#include "service.h"
#include "walletkey.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <cerrno>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
const QString marker = QStringLiteral("/var/lib/wildos/first-boot-setup");
// The account this setup made, so a second try (or a power cut halfway)
// can take it away again before making the real one.
const QString stateFile = QStringLiteral("/var/lib/wildos/first-boot-account");
const QString setupConf = QStringLiteral("/etc/sddm.conf.d/99-wildos-setup.conf");
const QString firstSignIn = QStringLiteral("/etc/sddm.conf.d/98-wildos-first-sign-in.conf");
// The file Settings > Accounts > Sign-in options writes (settings-users).
const QString autoLoginConf = QStringLiteral("/etc/sddm.conf.d/95-wildos-autologin.conf");
const QString setupUser = QStringLiteral("wildos-setup");

// The groups Settings gives every account and an administrator
// (wildos-settings data/settings-users). console above all: Kicksecure's
// console lockdown lets nobody outside it sign in, SDDM included.
const QStringList baseGroups = {QStringLiteral("console"), QStringLiteral("audio"), QStringLiteral("video"), QStringLiteral("plugdev"),
                                QStringLiteral("netdev"), QStringLiteral("bluetooth"), QStringLiteral("scanner"), QStringLiteral("privleap")};
const QStringList adminGroups = {QStringLiteral("sudo"), QStringLiteral("lpadmin")};

struct Result {
    int code = -1;
    QString error;
};

Result run(const QString &program, const QStringList &arguments, const QByteArray &input = {})
{
    QProcess process;
    QProcessEnvironment env;
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.start(program, arguments);
    if (!process.waitForStarted()) {
        return {-1, program + QStringLiteral(" could not start")};
    }
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        return {-1, program + QStringLiteral(" took too long")};
    }
    const int code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return {code, QString::fromUtf8(process.readAllStandardError()).trimmed()};
}

// Written beside the target, flushed, then renamed over it, so a power cut
// leaves either the old file or the new one and never half of either.
bool writeAtomically(const QString &path, const QByteArray &data, mode_t mode, uid_t uid = 0, gid_t gid = 0)
{
    const QByteArray target = QFile::encodeName(path);
    const QByteArray temp = target + ".wildos-new";
    const int fd = ::open(temp.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
    if (fd < 0) {
        return false;
    }
    bool ok = ::write(fd, data.constData(), data.size()) == data.size();
    ok = ok && ::fchmod(fd, mode) == 0 && ::fchown(fd, uid, gid) == 0 && ::fsync(fd) == 0;
    ::close(fd);
    if (ok && ::rename(temp.constData(), target.constData()) != 0) {
        // A file mounted over (/etc/hostname in a container) cannot be
        // replaced, only rewritten.
        ok = errno == EBUSY;
        if (ok) {
            const int in = ::open(target.constData(), O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
            ok = in >= 0 && ::write(in, data.constData(), data.size()) == data.size() && ::fsync(in) == 0;
            if (in >= 0) {
                ::close(in);
            }
        }
        ::unlink(temp.constData());
        return ok;
    }
    if (!ok) {
        ::unlink(temp.constData());
        return false;
    }
    const int dir = ::open(QFile::encodeName(QFileInfo(path).absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
    return true;
}

void removeDurably(const QString &path)
{
    if (QFile::remove(path)) {
        const int dir = ::open(QFile::encodeName(QFileInfo(path).absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir >= 0) {
            ::fsync(dir);
            ::close(dir);
        }
    }
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool userExists(const QString &name)
{
    return ::getpwnam(QFile::encodeName(name).constData()) != nullptr;
}

bool groupExists(const QString &name)
{
    return ::getgrnam(QFile::encodeName(name).constData()) != nullptr;
}

// "Alex Morgan" signs in as alex; "Zoë" as zoe. The name is never shown in
// the design, so it only has to be valid, short and free.
QString userNameFor(const QString &realName)
{
    const QString first = realName.simplified().section(QLatin1Char(' '), 0, 0).normalized(QString::NormalizationForm_KD).toLower();
    QString base;
    for (const QChar c : first) {
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9')) {
            base += c;
        }
    }
    if (base.isEmpty() || !base.at(0).isLetter()) {
        base.prepend(QStringLiteral("user"));
    }
    base.truncate(28);
    QString name = base;
    for (int n = 2; userExists(name) || groupExists(name) || name == setupUser; ++n) {
        name = base + QString::number(n);
    }
    return name;
}

QHash<QString, QString> shellAssignments(const QString &path)
{
    QHash<QString, QString> values;
    const auto lines = readText(path).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (line.startsWith(QLatin1Char('#')) || eq <= 0) {
            continue;
        }
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))) && value.endsWith(value.at(0))) {
            value = value.mid(1, value.size() - 2);
        }
        values.insert(line.left(eq).trimmed(), value);
    }
    return values;
}

// The keyboard picked in the installer, for the new account. Without
// kxkbrc KWin starts every Wayland session on the US layout.
void writeKeyboard(const struct passwd *pw)
{
    const auto keyboard = shellAssignments(QStringLiteral("/etc/default/keyboard"));
    const QString layouts = keyboard.value(QStringLiteral("XKBLAYOUT"));
    if (layouts.isEmpty()) {
        return;
    }
    const int count = layouts.split(QLatin1Char(',')).size();
    QByteArray text = "[Layout]\nUse=true\nResetOldOptions=true\n";
    text += "LayoutList=" + layouts.toUtf8() + "\n";
    text += "VariantList=" + keyboard.value(QStringLiteral("XKBVARIANT")).toUtf8() + "\n";
    text += "DisplayNames=" + QByteArray(count - 1, ',') + "\n";
    text += "Options=" + keyboard.value(QStringLiteral("XKBOPTIONS")).toUtf8() + "\n";
    if (!keyboard.value(QStringLiteral("XKBMODEL")).isEmpty()) {
        text += "Model=" + keyboard.value(QStringLiteral("XKBMODEL")).toUtf8() + "\n";
    }
    const QString config = QFile::decodeName(pw->pw_dir) + QStringLiteral("/.config");
    const QByteArray configPath = QFile::encodeName(config);
    if (::mkdir(configPath.constData(), 0700) != 0 && errno != EEXIST) {
        return;
    }
    struct stat st;
    // The home is brand new, but a link planted there must not be followed.
    if (::lstat(configPath.constData(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }
    if (::chown(configPath.constData(), pw->pw_uid, pw->pw_gid) != 0) {
        return;
    }
    writeAtomically(config + QStringLiteral("/kxkbrc"), text, 0644, pw->pw_uid, pw->pw_gid);
}

QString defaultAvatar()
{
    for (const QString &path : {QStringLiteral("/usr/share/wildos/avatar-default-256.png"), QStringLiteral("/usr/share/wildos/avatar-default.png")}) {
        if (QFileInfo(path).isReadable()) {
            return path;
        }
    }
    return {};
}

// The sign-in and lock screens cannot read a private home, so the picture
// goes where SDDM, the lock screen and Settings look first: AccountsService's
// icons folder. The daemon is then told, so its cache knows; without it its
// user file is written directly, as the installer did.
void setAvatar(const QString &userName, const QString &avatar)
{
    QFile source(avatar);
    if (avatar.isEmpty() || !source.open(QIODevice::ReadOnly)) {
        return;
    }
    const QString icons = QStringLiteral("/var/lib/AccountsService/icons");
    const QString users = QStringLiteral("/var/lib/AccountsService/users");
    const QString icon = icons + QLatin1Char('/') + userName;
    QDir().mkpath(icons);
    if (!writeAtomically(icon, source.readAll(), 0644)) {
        return;
    }

    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage find = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Accounts"), QStringLiteral("/org/freedesktop/Accounts"),
                                                       QStringLiteral("org.freedesktop.Accounts"), QStringLiteral("FindUserByName"));
    find << userName;
    const QDBusReply<QDBusObjectPath> path = bus.call(find, QDBus::Block, 10000);
    if (path.isValid()) {
        QDBusMessage set = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Accounts"), path.value().path(),
                                                          QStringLiteral("org.freedesktop.Accounts.User"), QStringLiteral("SetIconFile"));
        set << icon;
        if (bus.call(set, QDBus::Block, 10000).type() == QDBusMessage::ReplyMessage) {
            return;
        }
    }
    QDir().mkpath(users);
    ::chmod(QFile::encodeName(users).constData(), 0700);
    writeAtomically(users + QLatin1Char('/') + userName, "[User]\nIcon=" + QFile::encodeName(icon) + "\nSystemAccount=false\n", 0600);
}

// Only the pictures the setup page offers: Wild OS's own and the photos
// that come with the desktop. Anything else would let the setup account
// have root copy a file of its choosing.
QString allowedPicture(const QString &path)
{
    const QString real = QFileInfo(path).canonicalFilePath();
    const QStringList places = {QStringLiteral("/usr/share/wildos/"), QStringLiteral("/usr/share/plasma/avatars/photos/")};
    for (const QString &place : places) {
        if (real.startsWith(place) && (real.endsWith(QLatin1String(".png")) || real.endsWith(QLatin1String(".jpg")))
            && QFileInfo(real).size() < 4 * 1024 * 1024) {
            return real;
        }
    }
    return {};
}

// Shown by the sign-in screen after a wrong password, from AccountsService.
void setHint(const QString &userName, const QString &hint)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage find = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Accounts"), QStringLiteral("/org/freedesktop/Accounts"),
                                                       QStringLiteral("org.freedesktop.Accounts"), QStringLiteral("FindUserByName"));
    find << userName;
    const QDBusReply<QDBusObjectPath> path = bus.call(find, QDBus::Block, 10000);
    if (!path.isValid()) {
        return;
    }
    QDBusMessage set = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Accounts"), path.value().path(),
                                                      QStringLiteral("org.freedesktop.Accounts.User"), QStringLiteral("SetPasswordHint"));
    set << hint;
    bus.call(set, QDBus::Block, 10000);
}

// Folders under the account's home, made and owned by it; empty when one
// could not be made, or is not a plain folder.
QString accountFolder(const struct passwd *pw, const QStringList &parts)
{
    QString dir = QFile::decodeName(pw->pw_dir);
    for (const QString &part : parts) {
        dir += QLatin1Char('/') + part;
        const QByteArray path = QFile::encodeName(dir);
        if (::mkdir(path.constData(), 0700) != 0 && errno != EEXIST) {
            return {};
        }
        struct stat st;
        if (::lstat(path.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || ::chown(path.constData(), pw->pw_uid, pw->pw_gid) != 0) {
            return {};
        }
    }
    return dir;
}

// ~/.local/state/wildos-welcome/first-run.json, owned by the account.
bool writePlan(const struct passwd *pw, const QByteArray &plan)
{
    if (plan.size() > 1024 * 1024 || !QJsonDocument::fromJson(plan).isObject()) {
        return false;
    }
    const QString dir = accountFolder(pw, {QStringLiteral(".local"), QStringLiteral("state"), QStringLiteral("wildos-welcome")});
    return !dir.isEmpty() && writeAtomically(dir + QStringLiteral("/first-run.json"), plan, 0600, pw->pw_uid, pw->pw_gid);
}

// The locales Debian can make, from the locales package's list.
bool supportedLocale(const QString &name)
{
    static const QRegularExpression shape(QStringLiteral("^[a-z]{2,3}(_[A-Z]{2})?(@[a-z]+)?\\.UTF-8$"));
    if (!shape.match(name).hasMatch()) {
        return false;
    }
    const auto lines = readText(QStringLiteral("/usr/share/i18n/SUPPORTED")).split(QLatin1Char('\n'));
    return lines.contains(name + QStringLiteral(" UTF-8"));
}

bool setHostname(const QString &hostname)
{
    if (!writeAtomically(QStringLiteral("/etc/hostname"), hostname.toUtf8() + '\n', 0644)) {
        return false;
    }
    // Debian names the computer itself on 127.0.1.1; without the line, sudo
    // and anything else that looks the name up waits for DNS first.
    QStringList lines = readText(QStringLiteral("/etc/hosts")).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    const QString entry = QStringLiteral("127.0.1.1\t") + hostname;
    bool placed = false;
    for (QString &line : lines) {
        if (line.startsWith(QLatin1String("127.0.1.1")) && (line.size() == 9 || line.at(9).isSpace())) {
            line = entry;
            placed = true;
        }
    }
    if (!placed) {
        int at = 0;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).startsWith(QLatin1String("127.0.0.1"))) {
                at = i + 1;
            }
        }
        lines.insert(at, entry);
    }
    if (!writeAtomically(QStringLiteral("/etc/hosts"), lines.join(QLatin1Char('\n')).toUtf8() + '\n', 0644)) {
        return false;
    }
    const QByteArray name = hostname.toUtf8();
    return ::sethostname(name.constData(), name.size()) == 0;
}

QByteArray autoLoginFile(const QString &userName, const QByteArray &comment)
{
    return "# " + comment + "\n[Autologin]\nUser=" + userName.toUtf8() + "\nSession=wildos\nRelogin=false\n";
}

bool namesUser(const QString &path, const QString &userName)
{
    return readText(path).split(QLatin1Char('\n')).contains(QStringLiteral("User=") + userName);
}
}

SetupService::SetupService(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<QMap<QString, QString>>();
    // Activated on demand by the bus; nobody setting up for twenty minutes
    // has walked away, and the next call starts it again.
    m_idle.setInterval(20 * 60 * 1000);
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        // The wallet key exists only in this process, so it stays up until
        // Finish hands over to the account that needs it.
        if (!m_finishing && m_walletKey.isEmpty()) {
            QCoreApplication::quit();
        }
    });
    m_idle.start();
}

void SetupService::touch()
{
    m_idle.start();
}

bool SetupService::refuse(const QString &name, const QString &sentence)
{
    sendErrorReply(QStringLiteral("org.wildos.Setup1.Error.") + name, sentence);
    return false;
}

// Asks polkit about the process that sent this message, without any
// password prompt: the setup user has no password and there is no agent.
bool SetupService::authorised(const QString &action)
{
    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("system-bus-name") << QVariantMap{{QStringLiteral("name"), message().service()}};
    subject.endStructure();

    QDBusMessage check = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PolicyKit1"), QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
                                                        QStringLiteral("org.freedesktop.PolicyKit1.Authority"), QStringLiteral("CheckAuthorization"));
    check << QVariant::fromValue(subject) << action << QVariant::fromValue(QMap<QString, QString>()) << uint(0) << QString();
    const QDBusMessage reply = connection().call(check, QDBus::Block, 30000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return false;
    }
    const QDBusArgument result = reply.arguments().constFirst().value<QDBusArgument>();
    bool allowed = false;
    bool challenge = false;
    QMap<QString, QString> details;
    result.beginStructure();
    result >> allowed >> challenge >> details;
    result.endStructure();
    return allowed;
}

void SetupService::removeEarlierAccount()
{
    forgetWalletKey();
    const QString earlier = readText(stateFile).trimmed();
    if (earlier.isEmpty()) {
        return;
    }
    if (const struct passwd *pw = ::getpwnam(QFile::encodeName(earlier).constData())) {
        const qlonglong uid = pw->pw_uid;
        WalletKey::forget(pw->pw_uid);
        run(QStringLiteral("pkill"), {QStringLiteral("-KILL"), QStringLiteral("-u"), earlier});
        // Through AccountsService when it runs, so it forgets the account
        // instead of writing its user file back after userdel.
        QDBusMessage remove = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Accounts"), QStringLiteral("/org/freedesktop/Accounts"),
                                                             QStringLiteral("org.freedesktop.Accounts"), QStringLiteral("DeleteUser"));
        remove << uid << true;
        QDBusConnection::systemBus().call(remove, QDBus::Block, 30000);
        if (userExists(earlier)) {
            run(QStringLiteral("userdel"), {QStringLiteral("--remove"), earlier});
        }
        // The next account gets the same uid, and AccountsService keeps the
        // old one's object under it until it starts again.
        run(QStringLiteral("systemctl"), {QStringLiteral("try-restart"), QStringLiteral("accounts-daemon.service")});
    }
    QFile::remove(QStringLiteral("/var/lib/AccountsService/icons/") + earlier);
    QFile::remove(QStringLiteral("/var/lib/AccountsService/users/") + earlier);
    if (namesUser(autoLoginConf, earlier)) {
        removeDurably(autoLoginConf);
    }
    removeDurably(stateFile);
}

QString SetupService::CreateAccount(const QString &realName, const QString &password, const QString &hostname, bool autoLogin, const QString &hint,
                                    const QString &picture)
{
    touch();
    if (m_finishing || !QFile::exists(marker)) {
        refuse(QStringLiteral("Finished"), QStringLiteral("Setup has already finished."));
        return {};
    }
    if (!authorised(QStringLiteral("org.wildos.setup.create-account"))) {
        refuse(QStringLiteral("NotAllowed"), QStringLiteral("Only the setup screen can create the first account."));
        return {};
    }

    const QString name = realName.simplified();
    static const QRegularExpression badName(QStringLiteral("[:,=\\\\\\x00-\\x1f\\x7f]"));
    if (name.isEmpty() || name.size() > 100 || name.contains(badName)) {
        refuse(QStringLiteral("InvalidName"), QStringLiteral("Your name can't be empty or contain a colon, comma or equals sign."));
        return {};
    }
    static const QRegularExpression goodHost(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    if (!goodHost.match(hostname).hasMatch()) {
        refuse(QStringLiteral("InvalidHostname"), QStringLiteral("Use letters, numbers and hyphens only, up to 63, not starting or ending with a hyphen."));
        return {};
    }
    // Any length: the setup screen says when a password is weak and leaves
    // the choice to the person.
    if (password.isEmpty() || password.size() > 4096 || password.contains(QLatin1Char('\n')) || password.contains(QLatin1Char('\r'))) {
        refuse(QStringLiteral("InvalidPassword"), QStringLiteral("The password can't be empty or contain a line break."));
        return {};
    }
    if (hint.size() > 200 || hint.contains(QLatin1Char('\n')) || (!hint.isEmpty() && hint.contains(password))) {
        refuse(QStringLiteral("InvalidHint"), QStringLiteral("The hint can't contain the password itself."));
        return {};
    }
    const QString avatar = picture.isEmpty() ? defaultAvatar() : allowedPicture(picture);
    if (avatar.isEmpty() && !picture.isEmpty()) {
        refuse(QStringLiteral("InvalidPicture"), QStringLiteral("That picture can't be used."));
        return {};
    }
    if (!groupExists(QStringLiteral("console"))) {
        refuse(QStringLiteral("Broken"), QStringLiteral("Wild OS is missing the console group, so a new account could not sign in."));
        return {};
    }

    removeEarlierAccount();

    const QString userName = userNameFor(name);
    if (!writeAtomically(stateFile, userName.toUtf8() + '\n', 0600)) {
        refuse(QStringLiteral("Failed"), QStringLiteral("Wild OS could not save its progress. The disk may be full."));
        return {};
    }

    QStringList groups;
    for (const QString &group : baseGroups + adminGroups) {
        if (groupExists(group)) {
            groups << group;
        }
    }
    const Result made = run(QStringLiteral("useradd"),
                            {QStringLiteral("--create-home"), QStringLiteral("--shell"), QStringLiteral("/bin/bash"), QStringLiteral("--comment"), name,
                             QStringLiteral("--groups"), groups.join(QLatin1Char(',')), userName});
    if (made.code != 0) {
        qWarning("useradd failed: %s", qPrintable(made.error));
        removeDurably(stateFile);
        refuse(QStringLiteral("Failed"), QStringLiteral("The account could not be created."));
        return {};
    }

    const Result passworded = run(QStringLiteral("chpasswd"), {}, userName.toUtf8() + ':' + password.toUtf8() + '\n');
    if (passworded.code != 0) {
        qWarning("chpasswd failed: %s", qPrintable(passworded.error));
        removeEarlierAccount();
        refuse(QStringLiteral("PasswordRefused"), QStringLiteral("That password was turned down. Try a longer one."));
        return {};
    }

    const struct passwd *pw = ::getpwnam(QFile::encodeName(userName).constData());
    if (pw) {
        writeKeyboard(pw);
        prepareWallet(pw, password);
    }
    setAvatar(userName, avatar);
    if (!hint.isEmpty()) {
        setHint(userName, hint);
    }

    if (!setHostname(hostname)) {
        refuse(QStringLiteral("Failed"), QStringLiteral("The computer name could not be saved."));
        return {};
    }

    if (autoLogin) {
        if (!writeAtomically(autoLoginConf, autoLoginFile(userName, "Written by Wild OS setup. Settings > Accounts > Sign-in options turns it off."), 0644)) {
            refuse(QStringLiteral("Failed"), QStringLiteral("Automatic sign-in could not be turned on."));
            return {};
        }
        // Every later start signs in without a password too, so the wallet
        // needs its key from the system each time, not only from this
        // process in the first session.
        if (!m_walletKey.isEmpty() && !WalletKey::store(m_walletUid, m_walletKey)) {
            qWarning("could not keep the wallet key for automatic sign-in");
        }
    } else if (namesUser(autoLoginConf, userName)) {
        removeDurably(autoLoginConf);
    }
    ::sync();
    return userName;
}

// The salt pam_kwallet would otherwise make at the first sign-in with a
// password, and the key it would derive from that password with it. Only
// the key is kept, in this process, never on disk.
void SetupService::prepareWallet(const struct passwd *pw, const QString &password)
{
    forgetWalletKey();
    const QString dir = accountFolder(pw, {QStringLiteral(".local"), QStringLiteral("share"), QStringLiteral("kwalletd")});
    QByteArray salt(WalletKey::saltSize, '\0');
    if (dir.isEmpty() || ::getrandom(salt.data(), salt.size(), 0) != salt.size()
        || !writeAtomically(dir + QStringLiteral("/kdewallet.salt"), salt, 0600, pw->pw_uid, pw->pw_gid)) {
        // pam_kwallet makes its own salt at the first sign-in with the
        // password, so only the first session goes without a wallet.
        qWarning("could not prepare the wallet's salt");
        return;
    }
    m_walletKey = WalletKey::derive(password.toUtf8(), salt);
    m_walletUid = pw->pw_uid;
}

void SetupService::forgetWalletKey()
{
    m_walletKey.clear();
    m_walletUid = 0;
}

QByteArray SetupService::TakeWalletKey()
{
    const QDBusReply<uint> caller = connection().interface()->serviceUid(message().service());
    if (!m_finishing || m_walletKey.isEmpty() || !caller.isValid() || caller.value() != m_walletUid) {
        refuse(QStringLiteral("NoKey"), QStringLiteral("There is no wallet key for this account."));
        return {};
    }
    const QByteArray key = m_walletKey;
    forgetWalletKey();
    return key;
}

void SetupService::Finish(const QString &plan)
{
    touch();
    if (m_finishing) {
        return;
    }
    if (!QFile::exists(marker)) {
        refuse(QStringLiteral("Finished"), QStringLiteral("Setup has already finished."));
        return;
    }
    if (!authorised(QStringLiteral("org.wildos.setup.finish"))) {
        refuse(QStringLiteral("NotAllowed"), QStringLiteral("Only the setup screen can finish setup."));
        return;
    }
    const QString userName = readText(stateFile).trimmed();
    const struct passwd *pw = userName.isEmpty() ? nullptr : ::getpwnam(QFile::encodeName(userName).constData());
    if (!pw) {
        refuse(QStringLiteral("NoAccount"), QStringLiteral("Create your account first."));
        return;
    }

    // Steps 4 to 7 were chosen before the account could sign in; its own
    // welcome applies them, as itself, the first time it does.
    if (!plan.isEmpty() && !writePlan(pw, plan.toUtf8())) {
        refuse(QStringLiteral("Failed"), QStringLiteral("Wild OS could not save your choices. The disk may be full."));
        return;
    }

    // Signed straight in, once. When automatic sign-in was chosen its own
    // file already does that, every time.
    if (!namesUser(autoLoginConf, userName)
        && !writeAtomically(firstSignIn, autoLoginFile(userName, "Written by Wild OS setup for the first sign-in only. Removed once it has happened."), 0644)) {
        refuse(QStringLiteral("Failed"), QStringLiteral("Wild OS could not sign you in. Restart the computer to try again."));
        return;
    }
    removeDurably(setupConf);
    // Last: until the marker is gone a restart brings setup back.
    removeDurably(marker);
    removeDurably(stateFile);
    ::sync();

    m_finishing = true;
    const uint uid = pw->pw_uid;
    QTimer::singleShot(300, this, [this, userName, uid] {
        handOver(userName, uid);
    });
}

void SetupService::handOver(const QString &userName, uint uid)
{
    QDBusMessage restart = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("RestartUnit"));
    restart << QStringLiteral("display-manager.service") << QStringLiteral("replace");
    const QDBusMessage reply = QDBusConnection::systemBus().call(restart, QDBus::Block, 30000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning("could not restart the display manager: %s", qPrintable(reply.errorMessage()));
    }

    // SDDM reads its configuration when it starts, so the one-time file can
    // go as soon as the new account has a session. wildos-setup-boot also
    // removes it at the next start, whatever happens here.
    auto *poll = new QTimer(this);
    auto *waited = new int(0);
    connect(poll, &QTimer::timeout, this, [this, poll, waited, uid, userName] {
        ++*waited;
        if (!QFile::exists(QStringLiteral("/run/systemd/users/%1").arg(uid)) && *waited < 180) {
            return;
        }
        if (namesUser(firstSignIn, userName)) {
            removeDurably(firstSignIn);
        }
        // The session takes the wallet key in its first seconds; one that
        // has not after ten minutes is not going to.
        if (m_walletKey.isEmpty() || *waited >= 600) {
            poll->stop();
            forgetWalletKey();
            delete waited;
            QCoreApplication::quit();
        }
    });
    poll->start(1000);
}

bool SetupService::SetLocale(const QString &language, const QString &formats)
{
    touch();
    if (m_finishing || !QFile::exists(marker)) {
        return refuse(QStringLiteral("Finished"), QStringLiteral("Setup has already finished."));
    }
    if (!authorised(QStringLiteral("org.wildos.setup.create-account"))) {
        return refuse(QStringLiteral("NotAllowed"), QStringLiteral("Only the setup screen can change the language here."));
    }
    if (!supportedLocale(language) || !supportedLocale(formats)) {
        return refuse(QStringLiteral("InvalidLocale"), QStringLiteral("Wild OS doesn't have that language or region."));
    }

    // locale.gen lists every locale the computer can use; locale-gen makes
    // the ones not commented out.
    QStringList lines = readText(QStringLiteral("/etc/locale.gen")).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    for (const QString &wanted : {language, formats}) {
        const QString entry = wanted + QStringLiteral(" UTF-8");
        bool present = false;
        for (QString &line : lines) {
            QString bare = line;
            while (bare.startsWith(QLatin1Char('#')) || bare.startsWith(QLatin1Char(' '))) {
                bare.remove(0, 1);
            }
            if (bare == entry) {
                line = entry;
                present = true;
            }
        }
        if (!present) {
            lines << entry;
        }
    }
    if (!writeAtomically(QStringLiteral("/etc/locale.gen"), lines.join(QLatin1Char('\n')).toUtf8() + '\n', 0644)) {
        return refuse(QStringLiteral("Failed"), QStringLiteral("The language could not be saved."));
    }
    const Result made = run(QStringLiteral("locale-gen"), {});
    if (made.code != 0) {
        qWarning("locale-gen failed: %s", qPrintable(made.error));
        return refuse(QStringLiteral("Failed"), QStringLiteral("Wild OS could not prepare that language."));
    }

    QByteArray text = "LANG=" + language.toUtf8() + "\n";
    if (formats != language) {
        for (const char *key : {"LC_NUMERIC", "LC_TIME", "LC_MONETARY", "LC_PAPER", "LC_MEASUREMENT"}) {
            text += QByteArray(key) + '=' + formats.toUtf8() + '\n';
        }
    }
    if (!writeAtomically(QStringLiteral("/etc/default/locale"), text, 0644)) {
        return refuse(QStringLiteral("Failed"), QStringLiteral("The language could not be saved."));
    }
    ::sync();
    return true;
}

QString SetupService::FirmwareLicence()
{
    touch();
    if (m_finishing || !QFile::exists(marker)) {
        refuse(QStringLiteral("Finished"), QStringLiteral("Setup has already finished."));
        return {};
    }
    if (!authorised(QStringLiteral("org.wildos.setup.create-account"))) {
        refuse(QStringLiteral("NotAllowed"), QStringLiteral("Only the setup screen can check for a Windows licence."));
        return {};
    }
    // Computers sold with Windows 8 or later carry their licence key in the
    // ACPI MSDM table: a 56 byte header and fields, then the 29 character
    // key. Only its last five characters leave this service, to name it.
    QFile table(QStringLiteral("/sys/firmware/acpi/tables/MSDM"));
    if (!table.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = table.readAll();
    if (data.size() < 56 + 29 || !data.startsWith("MSDM")) {
        return {};
    }
    const QByteArray key = data.mid(56, 29);
    static const QRegularExpression shape(QStringLiteral("^([A-Z0-9]{5}-){4}[A-Z0-9]{5}$"));
    return shape.match(QString::fromLatin1(key)).hasMatch() ? QString::fromLatin1(key.right(5)) : QString();
}
