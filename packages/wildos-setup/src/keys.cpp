#include "keys.h"
#include "walletkey.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QProcess>
#include <QRegularExpression>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
// /run, so the record of who has taken their key this boot goes with the
// boot. The unit's RuntimeDirectory keeps it across this service's restarts.
const char givenDir[] = "/run/wildos-wallet-keys";

struct Conversation {
    QByteArray password;
    bool answered = false;
};

void freeReplies(struct pam_response *replies, int count)
{
    for (int i = 0; i < count; ++i) {
        if (replies[i].resp) {
            ::explicit_bzero(replies[i].resp, ::strlen(replies[i].resp));
            ::free(replies[i].resp);
        }
    }
    ::free(replies);
}

// Answers the one password prompt and nothing else: a second prompt (a
// new password, a one-time code) means this is not a plain password check.
int converse(int count, const struct pam_message **messages, struct pam_response **responses, void *data)
{
    auto *conversation = static_cast<Conversation *>(data);
    if (count <= 0 || count > PAM_MAX_NUM_MSG) {
        return PAM_CONV_ERR;
    }
    auto *replies = static_cast<struct pam_response *>(::calloc(size_t(count), sizeof(struct pam_response)));
    if (!replies) {
        return PAM_BUF_ERR;
    }
    for (int i = 0; i < count; ++i) {
        switch (messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
            if (conversation->answered) {
                freeReplies(replies, count);
                return PAM_CONV_ERR;
            }
            replies[i].resp = ::strndup(conversation->password.constData(), size_t(conversation->password.size()));
            conversation->answered = true;
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            break;
        default:
            freeReplies(replies, count);
            return PAM_CONV_ERR;
        }
    }
    *responses = replies;
    return PAM_SUCCESS;
}

struct Account {
    uid_t uid = 0;
    QByteArray name;
};

// Accounts for people only, as Settings counts them.
bool account(uid_t uid, Account *out)
{
    const struct passwd *pw = ::getpwuid(uid);
    if (!pw || uid < 1000 || uid >= 60000) {
        return false;
    }
    out->uid = uid;
    out->name = pw->pw_name;
    return true;
}

// Checks the password and keeps the key made from it. PAM looks accounts up
// itself, which overwrites getpwuid's buffer, so the entry is fetched again
// after the check.
enum class StoreResult { Stored, WrongPassword, Failed };
StoreResult storeFor(const Account &who, const QByteArray &password)
{
    if (password.isEmpty() || password.contains('\0') || !checkPassword(who.name, password)) {
        return StoreResult::WrongPassword;
    }
    const struct passwd *pw = ::getpwuid(who.uid);
    if (!pw) {
        return StoreResult::Failed;
    }
    const QByteArray salt = WalletKey::accountSalt(pw);
    if (salt.size() != WalletKey::saltSize) {
        return StoreResult::Failed;
    }
    QByteArray key = WalletKey::derive(password, salt);
    const bool stored = WalletKey::store(who.uid, key);
    key.fill('\0');
    return stored ? StoreResult::Stored : StoreResult::Failed;
}

QByteArray givenMarker(uid_t uid)
{
    return QByteArray(givenDir) + '/' + QByteArray::number(uint(uid));
}

QByteArray readLine()
{
    char line[4097];
    if (!std::fgets(line, sizeof line, stdin)) {
        return {};
    }
    const size_t size = std::strlen(line);
    QByteArray text(line, qsizetype(size > 0 && line[size - 1] == '\n' ? size - 1 : size));
    ::explicit_bzero(line, sizeof line);
    return text;
}

struct ChangeConversation {
    QByteArray current;
    QByteArray next;
    int currentAsked = 0;
    int nextAsked = 0;
    QStringList errors;
};

// The passwd stack's prompts, answered by what they ask for: the current
// password, or the new one (asked twice). A third new-password prompt
// means the first was turned down and PAM is asking again after saying
// why, so the conversation stops there.
int converseChange(int count, const struct pam_message **messages, struct pam_response **responses, void *data)
{
    auto *conversation = static_cast<ChangeConversation *>(data);
    if (count <= 0 || count > PAM_MAX_NUM_MSG) {
        return PAM_CONV_ERR;
    }
    auto *replies = static_cast<struct pam_response *>(::calloc(size_t(count), sizeof(struct pam_response)));
    if (!replies) {
        return PAM_BUF_ERR;
    }
    for (int i = 0; i < count; ++i) {
        const QByteArray text = messages[i]->msg ? QByteArray(messages[i]->msg).trimmed() : QByteArray();
        switch (messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF: {
            const QByteArray prompt = text.toLower();
            const bool asksNew = prompt.contains("new") || prompt.contains("retype") || prompt.contains("again");
            if (asksNew ? ++conversation->nextAsked > 2 : ++conversation->currentAsked > 1) {
                freeReplies(replies, count);
                return PAM_CONV_ERR;
            }
            const QByteArray &answer = asksNew ? conversation->next : conversation->current;
            replies[i].resp = ::strndup(answer.constData(), size_t(answer.size()));
            break;
        }
        case PAM_ERROR_MSG:
            if (!text.isEmpty()) {
                conversation->errors << QString::fromUtf8(text);
            }
            break;
        case PAM_TEXT_INFO:
            break;
        default:
            freeReplies(replies, count);
            return PAM_CONV_ERR;
        }
    }
    *responses = replies;
    return PAM_SUCCESS;
}

QString sentence(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) {
        return text;
    }
    text[0] = text[0].toUpper();
    if (!text.endsWith(QLatin1Char('.')) && !text.endsWith(QLatin1Char('!'))) {
        text += QLatin1Char('.');
    }
    return text;
}

// What PAM said when it turned the new password down, as one sentence for
// Settings to show: pwquality's reason, or pam_unix's and pwhistory's own.
QString refusal(const QStringList &errors)
{
    if (errors.isEmpty()) {
        return QStringLiteral("The password was not changed.");
    }
    for (auto it = errors.crbegin(); it != errors.crend(); ++it) {
        static const QRegularExpression bad(QStringLiteral("^BAD PASSWORD:\\s*(.+)$"));
        const auto match = bad.match(*it);
        if (match.hasMatch()) {
            return QStringLiteral("Wild OS turned down the new password. ") + sentence(match.captured(1));
        }
    }
    const QString last = errors.constLast();
    if (last.contains(QLatin1String("unchanged"), Qt::CaseInsensitive)) {
        return QStringLiteral("The new password is the same as the current one.");
    }
    if (last.contains(QLatin1String("already used"), Qt::CaseInsensitive)) {
        return QStringLiteral("You have used that password for this account before. Choose another.");
    }
    return sentence(last);
}

// Run by ChangePassword as a child, as root: change-password UID, with the
// current and new passwords on standard input, one per line. The sentence
// on failure goes to standard error; 3 means the current password is wrong.
int changePassword(const QStringList &arguments)
{
    bool numeric = false;
    const uint uid = arguments.value(2).toUInt(&numeric);
    Account who;
    if (!numeric || !account(uid, &who)) {
        std::fputs("There is no such account.\n", stderr);
        return 2;
    }
    QByteArray current = readLine();
    QByteArray next = readLine();
    const auto forget = [&] {
        current.fill('\0');
        next.fill('\0');
    };
    if (current.isEmpty() || !checkPassword(who.name, current)) {
        forget();
        std::fputs("The current password isn't right.\n", stderr);
        return 3;
    }
    if (next == current) {
        forget();
        std::fputs("The new password is the same as the current one.\n", stderr);
        return 4;
    }
    // As root, with the current password checked above: pam_unix turns
    // down a short or simple password when an account changes its own, and
    // Settings says when a password is weak and leaves the choice to the
    // person.
    ChangeConversation conversation{current, next};
    forget();
    const struct pam_conv conv = {converseChange, &conversation};
    pam_handle_t *handle = nullptr;
    int result = ::pam_start("passwd", who.name.constData(), &conv, &handle);
    if (result == PAM_SUCCESS) {
        result = ::pam_chauthtok(handle, 0);
        ::pam_end(handle, result);
    }
    conversation.current.fill('\0');
    conversation.next.fill('\0');
    if (result == PAM_SUCCESS) {
        return 0;
    }
    std::fputs((refusal(conversation.errors) + QLatin1Char('\n')).toUtf8().constData(), stderr);
    return 4;
}

// settings-users runs these as root, through pkexec. The password comes on
// standard input, never on the command line.
int commandLine(const QStringList &arguments)
{
    if (::getuid() != 0 || ::geteuid() != 0) {
        std::fputs("Only Wild OS Settings can do this.\n", stderr);
        return 2;
    }
    const QString command = arguments.value(1);
    if (command == QLatin1String("change-password") && arguments.size() == 3) {
        return changePassword(arguments);
    }
    if (command == QLatin1String("tidy") && arguments.size() == 2) {
        WalletKey::forgetOthers();
        return 0;
    }
    if (command == QLatin1String("store") && arguments.size() == 3) {
        const struct passwd *pw = ::getpwnam(arguments.at(2).toUtf8().constData());
        Account who;
        if (!pw || !account(pw->pw_uid, &who)) {
            std::fputs("There is no such account.\n", stderr);
            return 2;
        }
        char line[4097];
        QByteArray password;
        if (std::fgets(line, sizeof line, stdin)) {
            password = QByteArray(line).chopped(line[0] && line[std::strlen(line) - 1] == '\n' ? 1 : 0);
            ::explicit_bzero(line, sizeof line);
        }
        const StoreResult result = storeFor(who, password);
        password.fill('\0');
        if (result == StoreResult::WrongPassword) {
            std::fputs("That password isn't right.\n", stderr);
            return 3;
        }
        if (result == StoreResult::Failed) {
            std::fputs("Wild OS could not keep the key that opens your saved passwords.\n", stderr);
            return 1;
        }
        return 0;
    }
    std::fputs("usage: wildos-wallet-keys store NAME | tidy | change-password UID\n", stderr);
    return 2;
}
}

bool checkPassword(const QByteArray &user, const QByteArray &password)
{
    Conversation conversation{password};
    const struct pam_conv conv = {converse, &conversation};
    pam_handle_t *handle = nullptr;
    if (::pam_start("wildos-wallet-keys", user.constData(), &conv, &handle) != PAM_SUCCESS) {
        return false;
    }
    const int result = ::pam_authenticate(handle, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    ::pam_end(handle, result);
    conversation.password.fill('\0');
    return result == PAM_SUCCESS;
}

WalletKeys::WalletKeys(QObject *parent)
    : QObject(parent)
{
    // Activated by the bus for a moment at sign-in or in Settings.
    m_idle.setInterval(60 * 1000);
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
    m_idle.start();
}

bool WalletKeys::refuse(const QString &name, const QString &sentence)
{
    sendErrorReply(QStringLiteral("org.wildos.WalletKeys1.Error.") + name, sentence);
    return false;
}

bool WalletKeys::callerUid(uint *uid)
{
    m_idle.start();
    const QDBusReply<uint> caller = connection().interface()->serviceUid(message().service());
    if (!caller.isValid()) {
        return refuse(QStringLiteral("NotAllowed"), QStringLiteral("Wild OS could not tell which account is asking."));
    }
    *uid = caller.value();
    return true;
}

void WalletKeys::Store(const QString &password)
{
    uint uid = 0;
    Account who;
    if (!callerUid(&uid)) {
        return;
    }
    if (!account(uid, &who) || (WalletKey::autoLoginUser().toUtf8() != who.name && !WalletKey::hasPin(who.name))) {
        refuse(QStringLiteral("NotAutoSignIn"), QStringLiteral("This account does not sign in automatically or with a PIN."));
        return;
    }
    QByteArray secret = password.toUtf8();
    const StoreResult result = storeFor(who, secret);
    secret.fill('\0');
    if (result == StoreResult::WrongPassword) {
        refuse(QStringLiteral("WrongPassword"), QStringLiteral("That password isn't right."));
    } else if (result == StoreResult::Failed) {
        refuse(QStringLiteral("Failed"), QStringLiteral("Wild OS could not keep the key that opens your saved passwords."));
    } else {
        WalletKey::forgetOthers();
    }
}

QByteArray WalletKeys::Key()
{
    uint uid = 0;
    Account who;
    if (!callerUid(&uid)) {
        return {};
    }
    WalletKey::forgetOthers();
    if (!account(uid, &who)) {
        refuse(QStringLiteral("NotAutoSignIn"), QStringLiteral("This account does not sign in automatically or with a PIN."));
        return {};
    }
    // The note goes whether or not a key is kept, so it never outlasts the
    // sign-in that left it.
    if (WalletKey::hasPin(who.name) && WalletKey::takePinSignIn(uid)) {
        const QByteArray key = WalletKey::load(uid);
        if (key.isEmpty()) {
            refuse(QStringLiteral("NoKey"), QStringLiteral("There is no key for this account."));
        }
        return key;
    }
    if (WalletKey::autoLoginUser().toUtf8() != who.name) {
        refuse(QStringLiteral("NotAutoSignIn"), QStringLiteral("This account did not sign in automatically or with its PIN."));
        return {};
    }
    const QByteArray marker = givenMarker(uid);
    if (::access(marker.constData(), F_OK) == 0) {
        refuse(QStringLiteral("AlreadyTaken"), QStringLiteral("The key has already been used since this computer started."));
        return {};
    }
    const QByteArray key = WalletKey::load(uid);
    if (key.isEmpty()) {
        refuse(QStringLiteral("NoKey"), QStringLiteral("There is no key for this account."));
        return {};
    }
    // Recorded before the key leaves, so a failure here never hands it out
    // twice.
    ::mkdir(givenDir, 0700);
    const int fd = ::open(marker.constData(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        refuse(QStringLiteral("Failed"), QStringLiteral("Wild OS could not hand over the key."));
        return {};
    }
    ::close(fd);
    return key;
}

void WalletKeys::Forget()
{
    uint uid = 0;
    if (callerUid(&uid)) {
        WalletKey::forget(uid);
    }
}

void WalletKeys::Tidy()
{
    m_idle.start();
    WalletKey::forgetOthers();
}

void WalletKeys::CheckPassword(const QString &password)
{
    uint uid = 0;
    Account who;
    if (!callerUid(&uid)) {
        return;
    }
    QByteArray secret = password.toUtf8();
    const bool right = account(uid, &who) && !secret.isEmpty() && !secret.contains('\0') && checkPassword(who.name, secret);
    secret.fill('\0');
    if (!right) {
        refuse(QStringLiteral("WrongPassword"), QStringLiteral("That password isn't right."));
    }
}

void WalletKeys::ChangePassword(const QString &current, const QString &next)
{
    uint uid = 0;
    Account who;
    if (!callerUid(&uid)) {
        return;
    }
    if (!account(uid, &who)) {
        refuse(QStringLiteral("NotAllowed"), QStringLiteral("This account's password can't be changed here."));
        return;
    }
    for (const QString &password : {current, next}) {
        if (password.isEmpty() || password.size() > 4096 || password.contains(QLatin1Char('\n')) || password.contains(QLatin1Char('\r'))
            || password.contains(QChar(0))) {
            refuse(QStringLiteral("Refused"), QStringLiteral("A password can't be empty or contain a line break."));
            return;
        }
    }

    // A child does the work (PAM's rules can take seconds, and the child
    // changes its real user), so this replies when it is done.
    setDelayedReply(true);
    const QDBusMessage request = message();
    m_idle.stop();
    auto *process = new QProcess(this);
    QProcessEnvironment env;
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process->setProcessEnvironment(env);
    auto *limit = new QTimer(process);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [this, process, request](int code, QProcess::ExitStatus status) {
        const QString said = QString::fromUtf8(process->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
        process->deleteLater();
        m_idle.start();
        QDBusMessage reply;
        if (status == QProcess::NormalExit && code == 0) {
            reply = request.createReply();
        } else if (status == QProcess::NormalExit && code == 3) {
            reply = request.createErrorReply(QStringLiteral("org.wildos.WalletKeys1.Error.WrongPassword"), said);
        } else {
            reply = request.createErrorReply(QStringLiteral("org.wildos.WalletKeys1.Error.Refused"),
                                             said.isEmpty() ? QStringLiteral("The password was not changed.") : said);
        }
        QDBusConnection::systemBus().send(reply);
    });
    process->start(QStringLiteral("/usr/libexec/wildos/wildos-wallet-keys"), {QStringLiteral("change-password"), QString::number(uid)});
    QByteArray input = current.toUtf8() + '\n' + next.toUtf8() + '\n';
    process->write(input);
    input.fill('\0');
    process->closeWriteChannel();
    limit->start(60 * 1000);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-wallet-keys"));
    ::umask(077);

    if (app.arguments().size() > 1) {
        return commandLine(app.arguments());
    }
    if (::geteuid() != 0) {
        qCritical("wildos-wallet-keys must run as root, started by the system bus");
        return 1;
    }
    QDBusConnection bus = QDBusConnection::systemBus();
    WalletKeys service;
    if (!bus.registerObject(QStringLiteral("/org/wildos/WalletKeys1"), &service, QDBusConnection::ExportAllSlots)
        || !bus.registerService(QStringLiteral("org.wildos.WalletKeys1"))) {
        qCritical("could not take org.wildos.WalletKeys1 on the system bus: %s", qPrintable(bus.lastError().message()));
        return 1;
    }
    return app.exec();
}
