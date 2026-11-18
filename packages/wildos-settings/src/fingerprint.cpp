#include "fingerprint.h"

#include <PolkitQt1/Agent/Listener>
#include <PolkitQt1/Agent/Session>
#include <PolkitQt1/Identity>
#include <PolkitQt1/Subject>

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSocketNotifier>

#include <libudev.h>
#include <unistd.h>

namespace
{
const QString service = QStringLiteral("net.reactivated.Fprint");
const QString managerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const QString managerInterface = QStringLiteral("net.reactivated.Fprint.Manager");
const QString deviceInterface = QStringLiteral("net.reactivated.Fprint.Device");
const QString errorPrefix = QStringLiteral("net.reactivated.Fprint.Error.");
const QString useFile = QStringLiteral("/etc/wildos/fingerprint.conf");
const QString useHelper = QStringLiteral("/usr/libexec/wildos/settings-fingerprint");
const QString walletKeys = QStringLiteral("org.wildos.WalletKeys1");

// The order fingers are set up in: the ones most people use first. The
// dialog doesn't ask which finger; fprintd only needs a name to file it
// under, and Manage lists them as Fingerprint 1, 2 and so on.
const QStringList fingerOrder = {
    QStringLiteral("right-index-finger"),  QStringLiteral("left-index-finger"),  QStringLiteral("right-thumb"),
    QStringLiteral("left-thumb"),          QStringLiteral("right-middle-finger"), QStringLiteral("left-middle-finger"),
    QStringLiteral("right-ring-finger"),   QStringLiteral("left-ring-finger"),   QStringLiteral("right-little-finger"),
    QStringLiteral("left-little-finger"),
};

struct Readers {
    // A reader libfprint lists as one it can't drive is plugged in (many
    // need a maker's closed driver). Worth saying, rather than hiding the
    // row as if there were no reader at all.
    bool unsupported = false;
    QString place;
};

// libfprint's udev hwdb names every reader it knows, supported ones first
// and then, under an "unsupported" comment, the ones it can't drive. The
// USB devices plugged in are matched against both. fprintd doesn't say
// which USB device its reader is, so the place comes from the one
// supported reader found here.
Readers lookAtReaders()
{
    Readers readers;
    QFile hwdb(QStringLiteral("/usr/lib/udev/hwdb.d/60-autosuspend-libfprint-2.hwdb"));
    if (!hwdb.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return readers;
    }
    QSet<QString> supported;
    QSet<QString> unsupported;
    bool unsupportedSection = false;
    while (!hwdb.atEnd()) {
        const QString line = QString::fromLatin1(hwdb.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            unsupportedSection = unsupportedSection || line.contains(QLatin1String("unsupported"), Qt::CaseInsensitive);
        } else if (line.startsWith(QLatin1String("usb:v")) && line.size() >= 14) {
            // usb:v27C6p533C*
            const QString id = (line.mid(5, 4) + QLatin1Char(':') + line.mid(10, 4)).toLower();
            (unsupportedSection ? unsupported : supported).insert(id);
        }
    }
    QFile chassis(QStringLiteral("/sys/class/dmi/id/chassis_type"));
    const int chassisType = chassis.open(QIODevice::ReadOnly) ? chassis.readAll().trimmed().toInt() : 0;
    // SMBIOS: portable, laptop, notebook, sub notebook, convertible,
    // detachable. A reader that isn't removable on one of those is in it.
    const bool portable = QSet<int>{8, 9, 10, 14, 31, 32}.contains(chassisType);

    const QDir usb(QStringLiteral("/sys/bus/usb/devices"));
    const QStringList entries = usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QFile vendor(usb.filePath(entry + QStringLiteral("/idVendor")));
        QFile product(usb.filePath(entry + QStringLiteral("/idProduct")));
        if (!vendor.open(QIODevice::ReadOnly) || !product.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString id = QString::fromLatin1(vendor.readAll().trimmed() + ':' + product.readAll().trimmed()).toLower();
        if (unsupported.contains(id)) {
            readers.unsupported = true;
        } else if (supported.contains(id) && readers.place.isEmpty()) {
            QFile removable(usb.filePath(entry + QStringLiteral("/removable")));
            const QByteArray how = removable.open(QIODevice::ReadOnly) ? removable.readAll().trimmed() : QByteArray();
            if (how == "removable") {
                readers.place = QCoreApplication::translate("Fingerprint", "plugged in over USB");
            } else if (how == "fixed" || portable) {
                readers.place = QCoreApplication::translate("Fingerprint", "built into this PC");
            }
        }
    }
    return readers;
}

QVariantMap deviceProperties(const QString &path)
{
    auto message = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << deviceInterface;
    const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 5000);
    QVariantMap map;
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        reply.arguments().constFirst().value<QDBusArgument>() >> map;
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (it.value().canConvert<QDBusVariant>()) {
                it.value() = it.value().value<QDBusVariant>().variant();
            }
        }
    }
    return map;
}

// The last "key=value" line for key; anything but "off" is on, and so is a
// missing file, which is how fingerprint-turn reads it too.
bool useOn(const QByteArray &text, const QByteArray &key)
{
    bool on = true;
    for (const QByteArray &raw : text.split('\n')) {
        const QByteArray line = raw.trimmed();
        const qsizetype equals = line.indexOf('=');
        if (equals > 0 && line.left(equals).trimmed() == key) {
            on = line.mid(equals + 1).trimmed() != "off";
        }
    }
    return on;
}
}

// fprintd asks polkit before it enrols a finger. Registered for this
// process only, this answers that one question with the password the setup
// dialog just checked, so the system permission prompt doesn't ask for it a
// second time. It answers only for this account; anything else is turned
// down and fprintd says no.
class FingerprintAgent : public PolkitQt1::Agent::Listener
{
    Q_OBJECT

public:
    FingerprintAgent(const QString &password, QObject *parent)
        : PolkitQt1::Agent::Listener(parent)
        , m_password(password)
    {
    }

    ~FingerprintAgent() override
    {
        m_password.fill(QLatin1Char('\0'));
    }

    bool start()
    {
        return registerListener(PolkitQt1::UnixProcessSubject(QCoreApplication::applicationPid()),
                                QStringLiteral("/org/wildos/Settings/FingerprintAgent"));
    }

    // Set when the password was put to polkit and turned down.
    bool refused() const { return m_refused; }

public Q_SLOTS:
    void initiateAuthentication(const QString &, const QString &, const QString &, const PolkitQt1::Details &, const QString &cookie,
                                const PolkitQt1::Identity::List &identities, PolkitQt1::Agent::AsyncResult *result) override
    {
        const PolkitQt1::UnixUserIdentity me(::getuid());
        bool mine = false;
        for (const PolkitQt1::Identity &identity : identities) {
            mine = mine || identity.toString() == me.toString();
        }
        if (!mine || m_password.isEmpty() || m_session) {
            result->setCompleted();
            return;
        }
        m_result = result;
        m_session = new PolkitQt1::Agent::Session(me, cookie, result, this);
        connect(m_session, &PolkitQt1::Agent::Session::request, this, [this](const QString &, bool echo) {
            // One hidden answer, the password. A second question means PAM
            // wants something this can't give.
            if (echo || m_answered) {
                m_session->cancel();
                return;
            }
            m_answered = true;
            m_session->setResponse(m_password);
            m_password.fill(QLatin1Char('\0'));
            m_password.clear();
        });
        connect(m_session, &PolkitQt1::Agent::Session::completed, this, [this](bool gained) {
            m_refused = !gained;
            if (m_result) {
                m_result->setCompleted();
                m_result = nullptr;
            }
            m_session->deleteLater();
        });
        m_session->initiate();
    }

    bool initiateAuthenticationFinish() override
    {
        return true;
    }

    void cancelAuthentication() override
    {
        if (m_session) {
            m_session->cancel();
        }
    }

private:
    QString m_password;
    QPointer<PolkitQt1::Agent::Session> m_session;
    PolkitQt1::Agent::AsyncResult *m_result = nullptr;
    bool m_answered = false;
    bool m_refused = false;
};

// Nothing is asked until Sign-in options opens, so Settings does not
// start fprintd every time it starts.
Fingerprint::Fingerprint(QObject *parent)
    : QObject(parent)
{
    // A reader takes a moment after it appears before fprintd has it, and
    // a dock brings several USB devices at once.
    m_settle.setSingleShot(true);
    m_settle.setInterval(1500);
    connect(&m_settle, &QTimer::timeout, this, &Fingerprint::reload);
}

Fingerprint::~Fingerprint()
{
    if (m_monitor) {
        udev_monitor_unref(m_monitor);
    }
    if (m_udev) {
        udev_unref(m_udev);
    }
}

// Readers come and go: USB readers are plugged in and out, and fprintd
// starts again when it is updated. A USB device arriving or leaving, or
// fprintd starting, reads the reader again. fprintd also quits on its own
// when idle, which changes nothing, so its leaving is not watched.
void Fingerprint::watch()
{
    if (m_watching) {
        return;
    }
    m_watching = true;
    auto fprintd = new QDBusServiceWatcher(service, QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForRegistration, this);
    connect(fprintd, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        m_settle.start();
    });

    m_udev = udev_new();
    m_monitor = m_udev ? udev_monitor_new_from_netlink(m_udev, "udev") : nullptr;
    if (!m_monitor) {
        return;
    }
    udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "usb", "usb_device");
    if (udev_monitor_enable_receiving(m_monitor) < 0) {
        return;
    }
    m_udevNotifier = new QSocketNotifier(udev_monitor_get_fd(m_monitor), QSocketNotifier::Read, this);
    connect(m_udevNotifier, &QSocketNotifier::activated, this, &Fingerprint::onUdev);
}

void Fingerprint::onUdev()
{
    struct udev_device *device = udev_monitor_receive_device(m_monitor);
    if (!device) {
        return;
    }
    const QByteArray action = udev_device_get_action(device);
    udev_device_unref(device);
    if (action == "add" || action == "remove") {
        m_settle.start();
    }
}

QDBusMessage Fingerprint::deviceCall(const QString &method, const QVariantList &arguments) const
{
    auto message = QDBusMessage::createMethodCall(service, m_device, deviceInterface, method);
    message.setArguments(arguments);
    return message;
}

void Fingerprint::readUse()
{
    QFile file(useFile);
    const QByteArray text = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    const bool signIn = useOn(text, "signin");
    const bool admin = useOn(text, "admin");
    if (signIn != m_signIn || admin != m_admin) {
        m_signIn = signIn;
        m_admin = admin;
        Q_EMIT useChanged();
    }
}

void Fingerprint::readFingers()
{
    m_fingers.clear();
    if (m_device.isEmpty()) {
        return;
    }
    // An empty name is the caller's own account. No prints is an error
    // (NoEnrolledPrints), which is just an empty list here.
    const QDBusMessage listed = QDBusConnection::systemBus().call(deviceCall(QStringLiteral("ListEnrolledFingers"), {QString()}), QDBus::Block, 5000);
    if (listed.type() == QDBusMessage::ReplyMessage && !listed.arguments().isEmpty()) {
        const QStringList enrolled = listed.arguments().constFirst().toStringList();
        for (const QString &finger : fingerOrder) {
            if (enrolled.contains(finger)) {
                m_fingers.append(finger);
            }
        }
    }
}

void Fingerprint::reload()
{
    watch();
    readUse();
    // fprintd starts on demand and looks for readers as it does, so the
    // first call can take a moment; the page shows what it had meanwhile.
    auto message = QDBusMessage::createMethodCall(service, managerPath, managerInterface, QStringLiteral("GetDefaultDevice"));
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 15000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (!m_busy.isEmpty() || m_claimed) {
            return;
        }
        const QString device = reply.isError() ? QString() : reply.value().path();
        if (device != m_device) {
            m_windowsKey = false;
        }
        m_device = device;
        const Readers readers = lookAtReaders();
        m_unsupported = m_device.isEmpty() && readers.unsupported;
        m_place = readers.place;
        if (!m_device.isEmpty()) {
            const QVariantMap properties = deviceProperties(m_device);
            m_stages = properties.value(QStringLiteral("num-enroll-stages"), 5).toInt();
            m_scanType = properties.value(QStringLiteral("scan-type"), QStringLiteral("press")).toString();
        }
        readFingers();
        Q_EMIT changed();
    });
}

QString Fingerprint::nextFinger() const
{
    for (const QString &finger : fingerOrder) {
        if (!m_fingers.contains(finger)) {
            return finger;
        }
    }
    return {};
}

void Fingerprint::setBusy(const QString &busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

QString Fingerprint::interrupted(const QString &what, const QString &errorName) const
{
    if (errorName == errorPrefix + QLatin1String("AlreadyInUse")) {
        return tr("Something else is using the fingerprint reader. Try again in a moment.");
    }
    if (errorName == errorPrefix + QLatin1String("PermissionDenied") || errorName == QLatin1String("org.freedesktop.DBus.Error.AccessDenied")) {
        return what == QLatin1String("remove") ? tr("Wild OS needs your password to remove a fingerprint.")
                                               : tr("Wild OS needs your password to add a fingerprint.");
    }
    if (errorName == errorPrefix + QLatin1String("NoSuchDevice") || errorName == errorPrefix + QLatin1String("Internal")) {
        return tr("The fingerprint reader stopped answering. Try again, or restart the PC if it keeps happening.");
    }
    return what == QLatin1String("remove") ? tr("The fingerprint couldn't be removed. Try again.")
                                           : tr("The fingerprint reader couldn't be started. Try again.");
}

void Fingerprint::dropAgent()
{
    if (m_agent) {
        delete m_agent;
    }
}

// The reader belongs to one account at a time.
void Fingerprint::claim(const std::function<void()> &next, const QString &what)
{
    QDBusMessage message = deviceCall(QStringLiteral("Claim"), {QString()});
    message.setInteractiveAuthorizationAllowed(true);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 180000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, next, what, attempt = m_attempt] {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        if (!reply.isError()) {
            m_claimed = true;
            if (attempt != m_attempt) {
                release();
                return;
            }
            next();
            return;
        }
        if (attempt != m_attempt) {
            return;
        }
        dropAgent();
        setBusy(QString());
        // The Goodix 533c driver refuses a reader that Windows Hello has
        // given its own key, rather than change the key (it can't be put
        // back if that goes wrong). fprintd passes its message through.
        const QString message = reply.error().message();
        if (message.contains(QLatin1String("PSK does not match")) || message.contains(QLatin1String("in its bootloader"))) {
            m_windowsKey = true;
            Q_EMIT changed();
            Q_EMIT failed(what, windowsKeyMessage());
        } else {
            Q_EMIT failed(what, interrupted(what, reply.error().name()));
        }
        reload();
    });
}

void Fingerprint::release()
{
    if (m_claimed) {
        m_claimed = false;
        QDBusConnection::systemBus().asyncCall(deviceCall(QStringLiteral("Release")));
    }
}

void Fingerprint::begin(const QString &password)
{
    if (!m_busy.isEmpty() || m_device.isEmpty() || password.isEmpty()) {
        return;
    }
    if (nextFinger().isEmpty()) {
        Q_EMIT failed(QStringLiteral("setup"), tr("Every finger is set up already."));
        return;
    }
    ++m_attempt;
    setBusy(QStringLiteral("auth"));
    // Checked by PAM first, so a mistyped password says so plainly rather
    // than as a refusal from the reader.
    QDBusMessage check = QDBusMessage::createMethodCall(walletKeys, QStringLiteral("/org/wildos/WalletKeys1"), walletKeys, QStringLiteral("CheckPassword"));
    check << password;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(check, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, password, attempt = m_attempt] {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        if (attempt != m_attempt) {
            return;
        }
        if (reply.isError()) {
            setBusy(QString());
            Q_EMIT failed(QStringLiteral("password"), reply.error().name().endsWith(QLatin1String("WrongPassword"))
                                                          ? tr("That password isn't right.")
                                                          : tr("Wild OS could not check your password. Try again."));
            return;
        }
        dropAgent();
        m_agent = new FingerprintAgent(password, this);
        if (!m_agent->start()) {
            // The system permission prompt asks instead.
            dropAgent();
        }
        m_passed = 0;
        m_retry.clear();
        Q_EMIT enrollChanged();
        claim([this] { startEnroll(); }, QStringLiteral("setup"));
    });
}

void Fingerprint::startEnroll()
{
    m_finger = nextFinger();
    m_passed = 0;
    m_retry.clear();
    Q_EMIT enrollChanged();
    if (m_finger.isEmpty()) {
        dropAgent();
        setBusy(QString());
        Q_EMIT failed(QStringLiteral("enroll"), tr("Every finger is set up already."));
        return;
    }
    setBusy(QStringLiteral("auth"));
    QDBusConnection::systemBus().connect(service, m_device, deviceInterface, QStringLiteral("EnrollStatus"), this, SLOT(onEnrollStatus(QString, bool)));
    QDBusMessage start = deviceCall(QStringLiteral("EnrollStart"), {m_finger});
    start.setInteractiveAuthorizationAllowed(true);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(start, 180000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, attempt = m_attempt] {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        if (attempt != m_attempt) {
            // The dialog closed while polkit was asking.
            if (!reply.isError()) {
                QDBusConnection::systemBus().asyncCall(deviceCall(QStringLiteral("EnrollStop")));
            }
            release();
            return;
        }
        const bool refused = m_agent && m_agent->refused();
        dropAgent();
        if (reply.isError()) {
            QDBusConnection::systemBus().disconnect(service, m_device, deviceInterface, QStringLiteral("EnrollStatus"), this,
                                                    SLOT(onEnrollStatus(QString, bool)));
            release();
            setBusy(QString());
            if (refused) {
                Q_EMIT failed(QStringLiteral("password"), tr("That password isn't right."));
            } else {
                Q_EMIT failed(QStringLiteral("setup"), interrupted(QStringLiteral("enroll"), reply.error().name()));
            }
            reload();
            return;
        }
        m_enrolling = true;
        setBusy(QStringLiteral("enroll"));
        Q_EMIT started();
    });
}

// fprintd's EnrollStatus results. Nothing about the print itself comes
// through here, only how the touch went.
void Fingerprint::onEnrollStatus(const QString &result, bool done)
{
    if (!m_enrolling) {
        return;
    }
    if (result == QLatin1String("enroll-stage-passed")) {
        // Some readers pass more stages than they announce; the ring stays
        // short of full until the finger is saved.
        m_passed = qMin(m_passed + 1, qMax(m_stages - 1, 0));
        m_retry.clear();
    } else if (result == QLatin1String("enroll-retry-scan") || result == QLatin1String("enroll-remove-and-retry")) {
        m_retry = tr("Clean the sensor and try again");
    } else if (result == QLatin1String("enroll-swipe-too-short")) {
        m_retry = tr("Too quick, hold it a moment longer");
    } else if (result == QLatin1String("enroll-finger-not-centered")) {
        m_retry = tr("Only part of your finger touched");
    }
    if (!done) {
        Q_EMIT enrollChanged();
        return;
    }

    QString failure;
    if (result == QLatin1String("enroll-completed")) {
        m_passed = m_stages;
        m_retry.clear();
    } else if (result == QLatin1String("enroll-duplicate")) {
        failure = tr("That finger is already set up on this PC. Try a different one.");
    } else if (result == QLatin1String("enroll-data-full")) {
        failure = tr("The reader has no room for another finger. Remove one first.");
    } else if (result == QLatin1String("enroll-disconnected")) {
        failure = tr("The reader was disconnected. Plug it back in and try again.");
    } else {
        failure = tr("The reader couldn't save that finger. Try again.");
    }
    Q_EMIT enrollChanged();
    const bool gone = result == QLatin1String("enroll-disconnected");
    stopEnroll([this, failure, gone, finger = m_finger] {
        readFingers();
        Q_EMIT changed();
        if (gone) {
            release();
            reload();
        }
        if (failure.isEmpty()) {
            Q_EMIT enrolled(finger);
        } else {
            Q_EMIT failed(QStringLiteral("enroll"), failure);
        }
    });
}

// EnrollStop before anything else: fprintd refuses to let go of a reader,
// or start on another finger, while it is still enrolling.
void Fingerprint::stopEnroll(const std::function<void()> &next)
{
    QDBusConnection::systemBus().disconnect(service, m_device, deviceInterface, QStringLiteral("EnrollStatus"), this, SLOT(onEnrollStatus(QString, bool)));
    if (!m_enrolling) {
        setBusy(QString());
        next();
        return;
    }
    m_enrolling = false;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(deviceCall(QStringLiteral("EnrollStop"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, next] {
        watcher->deleteLater();
        setBusy(QString());
        next();
    });
}

void Fingerprint::again()
{
    if (!m_busy.isEmpty()) {
        return;
    }
    if (m_claimed) {
        startEnroll();
    } else if (!m_device.isEmpty()) {
        ++m_attempt;
        setBusy(QStringLiteral("auth"));
        claim([this] { startEnroll(); }, QStringLiteral("enroll"));
    }
}

void Fingerprint::finish()
{
    ++m_attempt;
    dropAgent();
    if (m_busy == QLatin1String("auth")) {
        // A claim or EnrollStart still on its way finishes and is let go
        // when it lands; the reader is released below either way.
        setBusy(QString());
    }
    stopEnroll([this] {
        release();
        m_passed = 0;
        m_retry.clear();
        Q_EMIT enrollChanged();
        reload();
    });
}

void Fingerprint::remove(const QString &finger)
{
    if (!m_busy.isEmpty() || m_claimed || m_device.isEmpty() || !m_fingers.contains(finger)) {
        return;
    }
    ++m_attempt;
    setBusy(finger);
    claim(
        [this, finger] {
            QDBusMessage message = deviceCall(QStringLiteral("DeleteEnrolledFinger"), {finger});
            message.setInteractiveAuthorizationAllowed(true);
            auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 180000), this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
                watcher->deleteLater();
                const QDBusPendingReply<> reply = *watcher;
                release();
                setBusy(QString());
                if (reply.isError()) {
                    Q_EMIT failed(QStringLiteral("remove"), interrupted(QStringLiteral("remove"), reply.error().name()));
                }
                reload();
            });
        },
        QStringLiteral("remove"));
}

void Fingerprint::setUse(const QString &what, bool on)
{
    if (!m_switching.isEmpty() || (what != QLatin1String("signin") && what != QLatin1String("admin"))) {
        return;
    }
    m_switching = what;
    Q_EMIT useChanged();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
        process->deleteLater();
        m_switching.clear();
        // Read back either way, so a switch the password prompt turned down
        // goes back to where it was.
        readUse();
        Q_EMIT useChanged();
        if (status != QProcess::NormalExit || code != 0) {
            Q_EMIT failed(QStringLiteral("use"), code == 126 || code == 127 ? tr("An administrator's password is needed to change this.")
                                                  : error.isEmpty()            ? tr("Wild OS could not make the change.")
                                                                               : error);
        }
    });
    process->start(QStringLiteral("pkexec"), {useHelper, what, on ? QStringLiteral("on") : QStringLiteral("off")});
}

QString Fingerprint::print() const
{
    QFile svg(QStringLiteral(":/fluent/fingerprint_48_regular.svg"));
    if (!svg.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QRegularExpressionMatch match = QRegularExpression(QStringLiteral(" d=\"([^\"]+)\"")).match(QString::fromUtf8(svg.readAll()));
    return match.hasMatch() ? match.captured(1) : QString();
}

// Wild OS never changes the reader's key: a wrong write can't be undone and
// leaves the reader dead. The reader works here once it is back on its
// factory key, which takes Dell's own driver, or once Windows Hello's key is
// copied over from the Windows that set it.
QString Fingerprint::windowsKeyMessage() const
{
    return tr("This reader still holds a key from Windows Hello, so Wild OS can't use it. Wild OS never changes that key, "
               "because a mistake would break the reader for good. If Windows is still on this PC, its key can be brought across: "
               "see help.wildos.io/security/fingerprint-dell-xps/. Until then, sign in with your password.");
}

#include "fingerprint.moc"
