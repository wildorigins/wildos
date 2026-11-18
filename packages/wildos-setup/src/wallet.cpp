#include "walletkey.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

#include <pwd.h>
#include <unistd.h>

/* wildos-setup-wallet: opens the KWallet of an account that signed in
 * without typing a password (wildos-setup-wallet.service), and tells the
 * person when it could not (--notice, wildos-wallet-notice.service).
 *
 * pam_kwallet opens the wallet with a key it makes from the password typed
 * at sign-in. Three sign-ins type none, and without this the first app to
 * ask for a secret gets KWallet's own password prompt:
 *
 * - The first session after first boot setup, which signs the new account
 *   in once. Setup's helper made the key from the password the person chose
 *   and hands it over once (TakeWalletKey). No wallet exists yet; kwalletd
 *   creates it with that key, so every later sign-in with the password
 *   opens it.
 * - Every start of the account that signs in automatically. The system
 *   keeps that account's key (wildos-wallet-keys) and hands it to the
 *   account once per boot.
 * - A sign-in with the PIN. The system keeps the key of an account with a
 *   PIN too, and hands it over once after each sign-in with the right PIN.
 *
 * A wallet already open (the password was typed after all) is left alone.
 *
 * Two cases still end with KWallet's prompt, and the notice offers the way
 * out, in Settings, at most once per boot:
 *
 * - An account that signs in automatically with no key kept: it turned
 *   automatic sign-in on before Wild OS kept keys, and nobody would guess
 *   that turning it off and on again fixes it. The same for a PIN saved
 *   before the sign-in screen took PINs.
 * - A wallet still closed after the desktop started. Closed alone proves
 *   little, so the notice says why only when it knows:
 *   - kwalletd was started by pam_kwallet, so it was handed the key made
 *     from the password typed at sign-in, and that key failed: the
 *     wallet's password is not the account's (made in KWallet's own
 *     dialog, or left behind by a password change before Settings moved
 *     wallets along).
 *   - kwalletd was started some other way (an app asked for it before
 *     pam_kwallet's could), so it never had a key. The password may well
 *     be right; it just never reached the wallet.
 *
 * A wallet made in KWallet's own dialog with the password left empty is
 * not one with a different password either. Any app opens it without a
 * prompt, so the notice opens it too and offers to protect it with the
 * account password instead.
 */
namespace
{
QDBusMessage kwallet(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.kwalletd6"), QStringLiteral("/modules/kwalletd6"), QStringLiteral("org.kde.KWallet"),
                                          method);
}

QByteArray systemKey(const QString &service, const QString &path, const QString &method, QString *errorName = nullptr)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(service, path, service, method);
    const QDBusReply<QByteArray> key = QDBusConnection::systemBus().call(call, QDBus::Block, 30000);
    if (!key.isValid()) {
        qWarning("no wallet key from %s: %s", qPrintable(service), qPrintable(key.error().message()));
        if (errorName) {
            *errorName = key.error().name();
        }
        return {};
    }
    return key.value();
}

QString walletName(QDBusConnection &session)
{
    const QDBusReply<QString> local = session.call(kwallet(QStringLiteral("localWallet")), QDBus::Block, 30000);
    return local.isValid() && !local.value().isEmpty() ? local.value() : QStringLiteral("kdewallet");
}

bool walletOpen(QDBusConnection &session, const QString &wallet)
{
    QDBusMessage isOpen = kwallet(QStringLiteral("isOpen"));
    isOpen << wallet;
    const QDBusReply<bool> open = session.call(isOpen, QDBus::Block, 30000);
    return open.isValid() && open.value();
}

bool startedByPam(QDBusConnection &session)
{
    const QDBusReply<uint> pid = session.interface()->servicePid(QStringLiteral("org.kde.kwalletd6"));
    if (!pid.isValid()) {
        return false;
    }
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid.value()));
    if (!cmdline.open(QIODevice::ReadOnly)) {
        return false;
    }
    return cmdline.readAll().split('\0').contains("--pam-login");
}

// KWallet's create dialog accepts an empty password, and people press
// Enter on it without ever thinking they chose one. Such a wallet opens
// with the key made from no password at all.
bool openWithoutPassword(QDBusConnection &session, const QString &wallet)
{
    QFile saltFile(QDir::homePath() + QStringLiteral("/.local/share/kwalletd/kdewallet.salt"));
    const QByteArray salt = saltFile.open(QIODevice::ReadOnly) ? saltFile.read(WalletKey::saltSize + 1) : QByteArray();
    if (salt.size() != WalletKey::saltSize) {
        return false;
    }
    QByteArray key = WalletKey::derive(QByteArray(""), salt);
    if (key.isEmpty()) {
        return false;
    }
    // pamOpen never replies; isOpen, sent after it, answers once it has run.
    QDBusMessage open = kwallet(QStringLiteral("pamOpen"));
    open << wallet << key << 0;
    key.fill('\0');
    return session.send(open) && walletOpen(session, wallet);
}

// Left by the sign-in half for the notice half, in the session's runtime
// folder, so it goes with the session.
QString needsKeyMarker()
{
    return qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/wildos-wallet-needs-key");
}

QString walletDir()
{
    return QDir::homePath() + QStringLiteral("/.local/share/kwalletd");
}

int openAtSignIn()
{
    // Setup leaves the salt and no wallet.
    const QString dir = walletDir();
    const bool firstSession = QFileInfo::exists(dir + QStringLiteral("/kdewallet.salt")) && !QFileInfo::exists(dir + QStringLiteral("/kdewallet.kwl"));
    const struct passwd *pw = ::getpwuid(::getuid());
    const bool automatic = pw && WalletKey::autoLoginUser() == QString::fromUtf8(pw->pw_name);
    const bool pin = pw && WalletKey::hasPin(pw->pw_name);
    QFile::remove(needsKeyMarker());
    if (!firstSession && !automatic && !pin) {
        return 0;
    }
    // Set only when a password was typed: pam_kwallet has started kwalletd
    // with the key made from it. Asking kwalletd anything before that one
    // is on the bus would start a second one through the bus, without the
    // key.
    if (qEnvironmentVariableIsSet("PAM_KWALLET5_LOGIN")) {
        return 0;
    }

    QDBusConnection session = QDBusConnection::sessionBus();
    const QString wallet = walletName(session);
    if (walletOpen(session, wallet)) {
        return 0;
    }

    QByteArray key;
    if (firstSession) {
        key = systemKey(QStringLiteral("org.wildos.Setup1"), QStringLiteral("/org/wildos/Setup1"), QStringLiteral("TakeWalletKey"));
    }
    if (key.isEmpty() && (automatic || pin)) {
        QString error;
        key = systemKey(QStringLiteral("org.wildos.WalletKeys1"), QStringLiteral("/org/wildos/WalletKeys1"), QStringLiteral("Key"), &error);
        // No key kept is expected on a computer that turned automatic
        // sign-in on before keys were kept, or saved a PIN before the
        // sign-in screen took PINs; the notice offers the fix.
        if (error == QLatin1String("org.wildos.WalletKeys1.Error.NoKey")) {
            QFile marker(needsKeyMarker());
            if (marker.open(QIODevice::WriteOnly)) {
                marker.write(automatic ? "automatic\n" : "pin\n");
            }
            return 0;
        }
        // An account with a PIN that signed in some other way without its
        // password (a fingerprint): the key stays where it is.
        if (!automatic && error == QLatin1String("org.wildos.WalletKeys1.Error.NotAutoSignIn")) {
            return 0;
        }
    }
    if (key.isEmpty()) {
        return 1;
    }

    // pamOpen never replies. kwalletd handles one connection's calls in
    // order, so isOpen, sent after it, answers once it has run.
    QDBusMessage open = kwallet(QStringLiteral("pamOpen"));
    open << wallet << key << 0;
    key.fill('\0');
    if (!session.send(open)) {
        qWarning("could not reach kwalletd");
        return 1;
    }
    if (!walletOpen(session, wallet)) {
        qWarning("kwalletd did not open %s with the key it was given", qPrintable(wallet));
        return 1;
    }
    return 0;
}

// The notification's answer: the button, or the notification closing.
class Answer : public QObject
{
    Q_OBJECT

public:
    uint id = 0;
    bool accepted = false;

Q_SIGNALS:
    void finished();

public Q_SLOTS:
    void actionInvoked(uint notification, const QString &action)
    {
        if (id != 0 && notification == id) {
            accepted = action == QLatin1String("password") || action == QLatin1String("default");
            Q_EMIT finished();
        }
    }
    void closed(uint notification, uint reason)
    {
        if (id != 0 && notification == id) {
            qInfo("saved passwords notice closed (reason %u)", reason);
            Q_EMIT finished();
        }
    }
};

int notice()
{
    const bool needsKey = QFileInfo::exists(needsKeyMarker());
    if (!needsKey && !QFileInfo::exists(walletDir() + QStringLiteral("/kdewallet.kwl"))) {
        return 0;
    }
    // At most once per boot: the boot's id is kept once it has been shown.
    QFile bootIdFile(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    const QByteArray bootId = bootIdFile.open(QIODevice::ReadOnly) ? bootIdFile.readAll().trimmed() : QByteArray();
    const QString stateDir = QDir::homePath() + QStringLiteral("/.local/state/wildos");
    const QString shownFile = stateDir + QStringLiteral("/wallet-notice-boot");
    QFile shown(shownFile);
    if (!bootId.isEmpty() && shown.open(QIODevice::ReadOnly) && shown.readAll().trimmed() == bootId) {
        return 0;
    }
    shown.close();

    QDBusConnection session = QDBusConnection::sessionBus();
    // Signed in with a fingerprint: no password reached pam_kwallet, so it
    // started no kwalletd and the wallet stays shut until the password is
    // typed. An app may already have asked for it and been given it.
    const struct passwd *pw = ::getpwuid(::getuid());
    const bool automatic = pw && WalletKey::autoLoginUser() == QString::fromUtf8(pw->pw_name);
    const bool noPasswordTyped = !needsKey && !automatic && !qEnvironmentVariableIsSet("PAM_KWALLET5_LOGIN");
    if (noPasswordTyped) {
        QEventLoop wait;
        QTimer::singleShot(5000, &wait, &QEventLoop::quit);
        wait.exec();
        if (walletOpen(session, walletName(session))) {
            return 0;
        }
    } else if (!needsKey) {
        // pam_kwallet's opener starts kwalletd alongside the desktop and
        // hands it the key; give it time before deciding the wallet did not
        // open. kwalletd is only asked once it is running: started here by
        // the bus first, it would never get pam_kwallet's key. Without
        // pam_kwallet starting it there is nothing to say.
        const QString kwalletd = QStringLiteral("org.kde.kwalletd6");
        bool running = false;
        for (int i = 0; i < 20; ++i) {
            running = session.interface()->isServiceRegistered(kwalletd);
            if (running && walletOpen(session, walletName(session))) {
                return 0;
            }
            QEventLoop wait;
            QTimer::singleShot(1000, &wait, &QEventLoop::quit);
            wait.exec();
        }
        if (!running || walletOpen(session, walletName(session))) {
            return 0;
        }
    }

    enum class Reason { NeedsKey, NeedsKeyPin, NoPassword, OtherPassword, NoKey, Fingerprint, NoPasswordTyped };
    Reason reason = Reason::NeedsKey;
    if (needsKey) {
        QFile marker(needsKeyMarker());
        if (marker.open(QIODevice::ReadOnly) && marker.readAll().trimmed() == "pin") {
            reason = Reason::NeedsKeyPin;
        }
    } else if (noPasswordTyped) {
        // With a PIN there is no telling which of the two signed in.
        reason = pw && WalletKey::hasPin(pw->pw_name) ? Reason::NoPasswordTyped : Reason::Fingerprint;
    } else {
        const QString wallet = walletName(session);
        reason = openWithoutPassword(session, wallet) ? Reason::NoPassword : startedByPam(session) ? Reason::OtherPassword : Reason::NoKey;
    }

    QString summary;
    QString body;
    QString button;
    const char *why = "";
    switch (reason) {
    case Reason::NeedsKey:
        summary = QCoreApplication::translate("Wallet", "Saved passwords need your password");
        body = QCoreApplication::translate("Wallet", "You sign in automatically, so your saved passwords can't open by themselves yet. Enter your password once and they will. "
                              "Until then, apps may ask for it.");
        button = QCoreApplication::translate("Wallet", "Enter password");
        why = "no key kept";
        break;
    case Reason::NeedsKeyPin:
        summary = QCoreApplication::translate("Wallet", "Saved passwords need your password");
        body = QCoreApplication::translate("Wallet", "You signed in with your PIN, and your saved passwords can't open with it yet. Enter your password once and they will. "
                              "Until then, apps may ask for it.");
        button = QCoreApplication::translate("Wallet", "Enter password");
        why = "no key kept for the PIN";
        break;
    case Reason::NoPasswordTyped:
        summary = QCoreApplication::translate("Wallet", "Saved passwords are locked");
        body = QCoreApplication::translate("Wallet", "They didn't open when you signed in. Enter your password once and they stay open until you sign out. "
                              "Until then, apps may ask for it.");
        button = QCoreApplication::translate("Wallet", "Enter password");
        why = "signed in without a password";
        break;
    case Reason::NoPassword:
        summary = QCoreApplication::translate("Wallet", "Saved passwords aren't protected");
        body = QCoreApplication::translate("Wallet", "They were set up without a password, so they don't open with your account. Protect them with your account password and "
                              "they open when you sign in.");
        button = QCoreApplication::translate("Wallet", "Protect them");
        why = "wallet has no password";
        break;
    case Reason::OtherPassword:
        summary = QCoreApplication::translate("Wallet", "Saved passwords didn't open");
        body = QCoreApplication::translate("Wallet", "They use a different password from your account, so apps may ask for it. Move them to your account password to fix it.");
        button = QCoreApplication::translate("Wallet", "Fix it");
        why = "wallet password differs";
        break;
    case Reason::Fingerprint:
        summary = QCoreApplication::translate("Wallet", "Saved passwords are locked");
        body = QCoreApplication::translate("Wallet", "Signing in with your fingerprint doesn't open them. Enter your password once and they stay open until you sign out. "
                              "Until then, apps may ask for it.");
        button = QCoreApplication::translate("Wallet", "Enter password");
        why = "signed in without a password";
        break;
    case Reason::NoKey:
        summary = QCoreApplication::translate("Wallet", "Saved passwords didn't open");
        body = QCoreApplication::translate("Wallet", "They didn't open by themselves this time, so apps may ask for your password. Enter it once to open them.");
        button = QCoreApplication::translate("Wallet", "Enter password");
        why = "kwalletd had no key";
        break;
    }
    QDBusMessage notify = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                         QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    // Critical, so it stays until answered or closed: shortly after the
    // desktop starts Plasma lets ordinary notifications expire with their
    // popup, and the button would go with it. Once per boot at most.
    const QVariantMap hints = {{QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.settings")},
                               {QStringLiteral("urgency"), QVariant::fromValue<uchar>(2)}};
    notify << QCoreApplication::translate("Wallet", "Settings") << uint(0) << QStringLiteral("dialog-password") << summary << body
           << QStringList{QStringLiteral("password"), button} << hints << int(0);

    const auto pause = [](int seconds) {
        QEventLoop wait;
        QTimer::singleShot(seconds * 1000, &wait, &QEventLoop::quit);
        wait.exec();
    };
    const QString service = QStringLiteral("org.freedesktop.Notifications");
    const QString path = QStringLiteral("/org/freedesktop/Notifications");
    // Plasma takes the notification name before its popups are ready and
    // closes what arrives in between at once. So this waits for the name
    // (without asking for it, which would make the bus start a server),
    // then a moment more, and tries once more if the first is closed
    // straight away.
    for (int i = 0; i < 60 && !session.interface()->isServiceRegistered(service); ++i) {
        pause(1);
    }
    pause(10);

    Answer answer;
    QEventLoop loop;
    QObject::connect(&answer, &Answer::finished, &loop, &QEventLoop::quit);
    session.connect(service, path, service, QStringLiteral("ActionInvoked"), &answer, SLOT(actionInvoked(uint, QString)));
    session.connect(service, path, service, QStringLiteral("NotificationClosed"), &answer, SLOT(closed(uint, uint)));
    for (int attempt = 0; attempt < 2; ++attempt) {
        const QDBusReply<uint> id = session.call(notify, QDBus::Block, 30000);
        if (!id.isValid()) {
            qWarning("could not show the saved passwords notice: %s", qPrintable(id.error().message()));
            return 1;
        }
        answer.id = id.value();
        qInfo("saved passwords notice shown (%s)", why);
        QElapsedTimer shownFor;
        shownFor.start();
        // The notification stays in the history; this waits for it a while.
        QTimer::singleShot(30 * 60 * 1000, &loop, &QEventLoop::quit);
        loop.exec();
        if (answer.accepted || shownFor.elapsed() > 5000) {
            break;
        }
        pause(10);
    }
    QDir().mkpath(stateDir);
    if (shown.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        shown.write(bootId + '\n');
        shown.close();
    }
    if (!answer.accepted) {
        return 0;
    }
    // Started as its own app unit, so it outlives this one and the
    // permission prompt names it as Settings. Waited for: systemd stops
    // whatever is left in this unit when it ends, systemd-run included.
    return QProcess::execute(QStringLiteral("systemd-run"),
                             {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"),
                              QStringLiteral("--unit=app-org.wildos.settings@wallet%1.service").arg(QCoreApplication::applicationPid()),
                              QStringLiteral("/usr/bin/wildos-settings"), QStringLiteral("--page"), QStringLiteral("signin-wallet")})
            == 0
        ? 0
        : 1;
}
}

// Running from the USB stick, as wildos-setup-boot tells it. The live
// session signs in by itself and keeps no saved passwords, so there is
// nothing to open and nothing to warn about.
bool liveSession()
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (cmdline.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> words = cmdline.readAll().simplified().split(' ');
        if (words.contains("boot=live") || words.contains("rd.live.image")) {
            return true;
        }
    }
    return QFileInfo::exists(QStringLiteral("/run/live/medium"));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-setup-wallet"));
    // No QML here, so WildOS.UI never loads the translations; the notice
    // loads its own, for the display language chosen in Settings.
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("wildos-setup"), QStringLiteral("_"), QStringLiteral("/usr/share/wildos/translations"))) {
        app.installTranslator(&translator);
    }
    if (liveSession()) {
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--notice"))) {
        return notice();
    }
    return openAtSignIn();
}

#include "wallet.moc"
