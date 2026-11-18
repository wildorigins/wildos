#include "prompts.h"

#include "clockformat.h"

#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <csignal>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
const QString upower = QStringLiteral("org.freedesktop.UPower");
const QString displayDevice = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
const QString upowerDevice = QStringLiteral("org.freedesktop.UPower.Device");
const QString profileService = QStringLiteral("org.kde.Solid.PowerManagement");
const QString profilePath = QStringLiteral("/org/kde/Solid/PowerManagement/Actions/PowerProfile");
const QString profileInterface = QStringLiteral("org.kde.Solid.PowerManagement.Actions.PowerProfile");
const QString rebootRequired = QStringLiteral("/run/reboot-required");
const QString preparedUpdate = QStringLiteral("/var/lib/PackageKit/prepared-update");
constexpr int snoozeHours = 4;
constexpr int warningSeconds = 60;
// A restart picked for a time the computer slept through is not made
// later without asking again.
constexpr int lateMinutes = 30;
// After the 32-bit prompt closes, further requests are dropped for this
// long, so a launcher that retries in a loop raises it once.
constexpr int compat32QuietMinutes = 5;
const QString compat32Helper = QStringLiteral("/usr/libexec/wildos/settings-32bit");
const QString compat32Choice = QStringLiteral("/etc/wildos/32bit");

// UPower's states: 2 discharging, 6 pending discharge.
bool discharging(uint state)
{
    return state == 2 || state == 6;
}

QString profileCall(const QString &method, const QVariantList &args = {})
{
    auto message = QDBusMessage::createMethodCall(profileService, profilePath, profileInterface, method);
    message.setArguments(args);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    const QVariant value = reply.arguments().constFirst();
    return value.canConvert<QStringList>() && value.typeId() == QMetaType::QStringList ? value.toStringList().join(QLatin1Char(',')) : value.toString();
}

QDateTime bootTime()
{
    QFile uptime(QStringLiteral("/proc/uptime"));
    if (!uptime.open(QIODevice::ReadOnly)) {
        return {};
    }
    const double seconds = QString::fromLatin1(uptime.readAll()).section(QLatin1Char(' '), 0, 0).toDouble();
    return QDateTime::currentDateTime().addMSecs(-qint64(seconds * 1000));
}

KConfigGroup restartConfig()
{
    return KSharedConfig::openConfig(Prompts::configFile())->group(QStringLiteral("Restart"));
}

// "Tonight at 23:00", "Tomorrow at 12:00", in the regional format chosen in
// Settings, on the 12 or 24-hour clock if one is chosen there.
QString describe(const QDateTime &when, const QDateTime &now)
{
    const QString time = ClockFormat::time(when.time());
    const int days = now.date().daysTo(when.date());
    const bool night = when.time().hour() >= 18 || when.time().hour() < 5;
    if (days == 0) {
        return night ? QCoreApplication::translate("Prompts", "Tonight at %1").arg(time) : QCoreApplication::translate("Prompts", "Today at %1").arg(time);
    }
    if (days == 1 && when.time().hour() < 5) {
        return QCoreApplication::translate("Prompts", "Tonight at %1").arg(time);
    }
    if (days == 1) {
        return QCoreApplication::translate("Prompts", "Tomorrow at %1").arg(time);
    }
    // The day's name in the display language chosen in Settings, which is
    // the first of uiLanguages (English when none is chosen).
    const QString language = QLocale::system().uiLanguages().value(0);
    const QLocale names = language.isEmpty() || language == QLatin1String("C") || language.startsWith(QLatin1String("en"))
        ? QLocale(QLocale::English, QLocale::UnitedKingdom)
        : QLocale(language);
    return QCoreApplication::translate("Prompts", "%1 at %2").arg(names.toString(when.date(), QStringLiteral("dddd")), time);
}

// Plasma's restart path first, so open apps get the chance to save. It is
// started by D-Bus on demand, so it is called rather than looked for, and
// its reply (which only comes once every app has closed) is not waited on.
// logind restarts the computer only if Plasma's path answers with an error.
void rebootGracefully()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                                                  QStringLiteral("logoutAndReboot"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 600000), qApp);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, qApp, [watcher] {
        watcher->deleteLater();
        const QDBusError::ErrorType error = watcher->error().type();
        if (!watcher->isError() || (error != QDBusError::ServiceUnknown && error != QDBusError::UnknownObject
                                    && error != QDBusError::UnknownMethod && error != QDBusError::UnknownInterface)) {
            return;
        }
        auto fallback = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                                                       QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("Reboot"));
        fallback << true;
        QDBusConnection::systemBus().asyncCall(fallback);
    });
}
}

Prompts::Prompts(QObject *parent)
    : QObject(parent)
{
    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0) {
            m_countdownTimer.stop();
            m_countdown = 0;
            restartNow();
        }
        Q_EMIT countdownChanged();
    });
    // Connected once here: Qt 6 refuses Qt::UniqueConnection for a lambda,
    // which left the minute before Windows apps' work is kept never ending.
    m_shutdownTimer.setInterval(1000);
    connect(&m_shutdownTimer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0) {
            answerWindowsShutdown(QStringLiteral("keep"));
        }
        Q_EMIT countdownChanged();
    });
}

void Prompts::show(const QString &kind)
{
    m_showing = kind;
    Q_EMIT infoChanged();
    Q_EMIT showingChanged();
}

void Prompts::dismiss()
{
    if (m_showing == QLatin1String("restart")) {
        notNow();
        return;
    }
    if (m_showing == QLatin1String("compat32")) {
        m_compat32QuietUntil = QDateTime::currentDateTime().addSecs(compat32QuietMinutes * 60);
    }
    m_showing.clear();
    Q_EMIT showingChanged();
    if (m_oneShot) {
        QTimer::singleShot(150, qApp, &QCoreApplication::quit);
    }
}

void Prompts::watch()
{
    QDBusConnection::systemBus().connect(upower, displayDevice, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                                         SLOT(upowerChanged(QString, QVariantMap, QStringList)));
    readBattery();

    // A restart set before signing out still stands after signing in; one
    // set before the computer last started has happened.
    const QDateTime setAt = QDateTime::fromString(restartConfig().readEntry("SetAt", QString()), Qt::ISODate);
    if (scheduled().isValid() && (!setAt.isValid() || setAt < bootTime())) {
        setScheduled({});
    }
    m_snoozedUntil = QDateTime::currentDateTime().addSecs(120);
    m_restartCheck.setInterval(30 * 1000);
    connect(&m_restartCheck, &QTimer::timeout, this, &Prompts::checkRestart);
    m_restartCheck.start();
    listen32bit();
}

// One datagram per attempt from 32bit-blocked: "program\napp id". The
// socket's name carries this user's uid, and the kernel's record of who
// sent each message is checked as well, so another account cannot raise a
// prompt here.
void Prompts::listen32bit()
{
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return;
    }
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const QByteArray name = "wildos-32bit-" + QByteArray::number(::getuid());
    memcpy(addr.sun_path + 1, name.constData(), size_t(name.size()));
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), socklen_t(offsetof(sockaddr_un, sun_path) + 1 + name.size())) != 0) {
        ::close(fd);
        return;
    }
    m_compat32Socket = fd;
    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &Prompts::read32bit);
}

void Prompts::read32bit()
{
    for (;;) {
        char buffer[1024];
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(ucred))];
        iovec iov{buffer, sizeof buffer};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof control;
        const ssize_t n = ::recvmsg(m_compat32Socket, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (n < 0) {
            return;
        }
        bool mine = false;
        for (cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS) {
                ucred cred;
                memcpy(&cred, CMSG_DATA(c), sizeof cred);
                mine = cred.uid == ::getuid();
            }
        }
        if (!mine) {
            continue;
        }
        const QString text = QString::fromUtf8(buffer, int(n));
        ask32bit(text.section(QLatin1Char('\n'), 0, 0), text.section(QLatin1Char('\n'), 1, 1));
    }
}

QString Prompts::state32bit() const
{
    QFile level(QStringLiteral("/etc/wildos/protection-level"));
    const QByteArray value = level.open(QIODevice::ReadOnly) ? level.readAll().trimmed() : QByteArray();
    if (value == "strict" || value == "maximum") {
        return QStringLiteral("locked");
    }
    return QFileInfo::exists(compat32Choice) ? QStringLiteral("pending") : QStringLiteral("off");
}

void Prompts::ask32bit(const QString &program, const QString &appId)
{
    // Another prompt already on screen matters more, and one 32-bit prompt
    // is enough for a while.
    if (!m_showing.isEmpty() || (m_compat32QuietUntil.isValid() && QDateTime::currentDateTime() < m_compat32QuietUntil)) {
        return;
    }
    // Inside a Flatpak the program is one of the app's files; the app's own
    // name is the one people know.
    QString name = QFileInfo(program).fileName();
    QString icon;
    if (!appId.isEmpty()) {
        if (const KService::Ptr service = KService::serviceByDesktopName(appId)) {
            name = service->name();
            icon = service->icon();
        }
    }
    m_info = {
        {QStringLiteral("appName"), name.left(80)},
        {QStringLiteral("iconName"), icon},
        {QStringLiteral("mode"), state32bit()},
    };
    show(QStringLiteral("compat32"));
}

void Prompts::turnOn32bit()
{
    const QString mode = m_info.value(QStringLiteral("mode")).toString();
    if (m_demo) {
        dismiss();
        return;
    }
    if (mode == QLatin1String("locked")) {
        QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), QStringLiteral("privacy")});
        dismiss();
        return;
    }
    if (mode == QLatin1String("pending")) {
        m_compat32QuietUntil = QDateTime::currentDateTime().addSecs(compat32QuietMinutes * 60);
        restartNow();
        return;
    }
    if (m_info.value(QStringLiteral("busy")).toBool()) {
        return;
    }
    m_info.insert(QStringLiteral("busy"), true);
    m_info.remove(QStringLiteral("error"));
    Q_EMIT infoChanged();
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        if (m_showing != QLatin1String("compat32")) {
            return;
        }
        m_info.insert(QStringLiteral("busy"), false);
        if (code == 0) {
            m_compat32QuietUntil = QDateTime::currentDateTime().addSecs(compat32QuietMinutes * 60);
            restartNow();
            return;
        }
        // A cancelled password changes nothing; the card stays so it can be
        // tried again or put away. Code 3: the protection level was raised
        // meanwhile, which the card now says.
        m_info.insert(QStringLiteral("mode"), state32bit());
        if (code != 3 && code != 126 && code != 127) {
            m_info.insert(QStringLiteral("error"), tr("32-bit apps could not be turned on."));
        }
        Q_EMIT infoChanged();
    });
    process->start(QStringLiteral("pkexec"), {compat32Helper, QStringLiteral("on")});
}

void Prompts::readBattery()
{
    auto message = QDBusMessage::createMethodCall(upower, displayDevice, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << upowerDevice;
    message.setAutoStartService(false);
    QDBusReply<QVariantMap> reply = QDBusConnection::systemBus().call(message, QDBus::Block, 2000);
    if (!reply.isValid()) {
        return;
    }
    upowerChanged(upowerDevice, reply.value(), {});
}

void Prompts::upowerChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != upowerDevice) {
        return;
    }
    if (changed.contains(QStringLiteral("Percentage"))) {
        m_percent = changed.value(QStringLiteral("Percentage")).toDouble();
    }
    if (changed.contains(QStringLiteral("State"))) {
        m_state = changed.value(QStringLiteral("State")).toUInt();
    }
    if (changed.contains(QStringLiteral("TimeToEmpty"))) {
        m_timeToEmpty = changed.value(QStringLiteral("TimeToEmpty")).toLongLong();
    }
    if (changed.contains(QStringLiteral("Type")) || changed.contains(QStringLiteral("IsPresent"))) {
        // Type 2 is a battery; a desktop's display device is a line power.
        m_isBattery = changed.value(QStringLiteral("Type"), 2).toUInt() == 2 && changed.value(QStringLiteral("IsPresent"), true).toBool();
    }
    onBatteryChanged();
}

void Prompts::onBatteryChanged()
{
    const int low = KSharedConfig::openConfig(QStringLiteral("powerdevilrc"))->group(QStringLiteral("BatteryManagement")).readEntry("BatteryLowLevel", 10);
    const bool isLow = m_isBattery && discharging(m_state) && m_percent <= low;
    if (!isLow) {
        // Plugged in or charged above the line: the next drop warns again.
        m_batteryWarned = false;
        if (m_showing == QLatin1String("battery") && !discharging(m_state)) {
            m_showing.clear();
            Q_EMIT showingChanged();
        }
        return;
    }
    m_info.insert(QStringLiteral("percent"), qRound(m_percent));
    m_info.insert(QStringLiteral("minutes"), m_timeToEmpty > 0 ? int(m_timeToEmpty / 60) : 0);
    if (m_showing == QLatin1String("battery")) {
        Q_EMIT infoChanged();
        return;
    }
    if (m_batteryWarned) {
        return;
    }
    m_batteryWarned = true;
    const QStringList choices = profileCall(QStringLiteral("profileChoices")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QString current = profileCall(QStringLiteral("currentProfile"));
    m_info.insert(QStringLiteral("saver"), choices.contains(QLatin1String("power-saver")));
    m_info.insert(QStringLiteral("saverOn"), current == QLatin1String("power-saver"));
    show(QStringLiteral("battery"));
}

void Prompts::batterySaver()
{
    if (!m_demo) {
        profileCall(QStringLiteral("setProfile"), {QStringLiteral("power-saver")});
    }
    m_showing.clear();
    Q_EMIT showingChanged();
}

bool Prompts::restartPending() const
{
    return QFileInfo::exists(rebootRequired) || QFileInfo::exists(preparedUpdate);
}

QDateTime Prompts::scheduled() const
{
    return QDateTime::fromString(restartConfig().readEntry("At", QString()), Qt::ISODate);
}

void Prompts::setScheduled(const QDateTime &when)
{
    KConfigGroup group = restartConfig();
    if (when.isValid()) {
        group.writeEntry("At", when.toString(Qt::ISODate), KConfig::Notify);
        group.writeEntry("Label", describe(when, QDateTime::currentDateTime()), KConfig::Notify);
        group.writeEntry("SetAt", QDateTime::currentDateTime().toString(Qt::ISODate), KConfig::Notify);
    } else {
        group.deleteEntry("At", KConfig::Notify);
        group.deleteEntry("Label", KConfig::Notify);
        group.deleteEntry("SetAt", KConfig::Notify);
    }
    group.sync();
}

void Prompts::checkRestart()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime at = scheduled();
    // Settings may ask for a restart PackageKit reported without leaving
    // a file behind, so a time the person picked counts as pending too.
    if (!restartPending() && !at.isValid()) {
        m_restartAsked = false;
        return;
    }
    if (at.isValid()) {
        if (now < at || m_countdown > 0) {
            return;
        }
        if (at.secsTo(now) > lateMinutes * 60) {
            setScheduled({});
            showRestart();
            return;
        }
        // The time the person picked: a minute's warning, then restart.
        m_info.insert(QStringLiteral("mode"), QStringLiteral("countdown"));
        m_countdown = warningSeconds;
        m_countdownTimer.start();
        Q_EMIT countdownChanged();
        show(QStringLiteral("restart"));
        return;
    }
    if (m_showing.isEmpty() && (!m_snoozedUntil.isValid() || now >= m_snoozedUntil)) {
        showRestart();
    }
}

void Prompts::buildTimes()
{
    const QDateTime now = QDateTime::currentDateTime();
    QList<QDateTime> times;
    const QDate today = now.date();
    if (now.time() < QTime(22, 30)) {
        times << QDateTime(today, QTime(23, 0));
    }
    times << QDateTime(now.time() < QTime(2, 30) ? today : today.addDays(1), QTime(3, 0));
    times << QDateTime(today.addDays(1), QTime(12, 0));
    times << QDateTime(today.addDays(1), QTime(18, 0));
    m_times.clear();
    for (const QDateTime &when : std::as_const(times)) {
        m_times.append(QVariantMap{{QStringLiteral("text"), describe(when, now)}, {QStringLiteral("value"), when.toString(Qt::ISODate)}});
    }
}

void Prompts::showRestart()
{
    buildTimes();
    bool security = false;
    QFile pkgs(rebootRequired + QStringLiteral(".pkgs"));
    if (!m_demo && pkgs.open(QIODevice::ReadOnly)) {
        const QStringList names = QString::fromUtf8(pkgs.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!names.isEmpty()) {
            QProcess apt;
            apt.start(QStringLiteral("apt-cache"), QStringList{QStringLiteral("policy")} + names);
            if (apt.waitForFinished(3000)) {
                security = QString::fromUtf8(apt.readAllStandardOutput()).contains(QLatin1String("-security"));
            }
        }
    }
    if (m_demo) {
        security = true;
    }
    // What the Wild OS update waiting on this restart brings, in a few
    // words, left by Settings > Updates when it installed it.
    QString summary;
    QFile summaryFile(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-update-summary"));
    if (!m_demo && summaryFile.open(QIODevice::ReadOnly)) {
        summary = QString::fromUtf8(summaryFile.readAll()).trimmed();
    } else if (m_demo) {
        summary = QStringLiteral("3 fixes and 2 improvements");
    }
    const QDateTime at = scheduled();
    m_info.insert(QStringLiteral("security"), security);
    m_info.insert(QStringLiteral("summary"), summary);
    m_info.insert(QStringLiteral("mode"), QStringLiteral("ask"));
    m_info.insert(QStringLiteral("scheduled"), at.isValid() ? describe(at, QDateTime::currentDateTime()) : QString());
    m_restartAsked = true;
    show(QStringLiteral("restart"));
}

void Prompts::restartNow()
{
    m_countdownTimer.stop();
    m_countdown = 0;
    m_showing.clear();
    Q_EMIT showingChanged();
    if (m_demo) {
        return;
    }
    setScheduled({});
    // An update PackageKit prepared to install while the computer starts
    // needs telling to go ahead.
    if (QFileInfo::exists(preparedUpdate) && !QFileInfo::exists(QStringLiteral("/system-update"))) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PackageKit"), QStringLiteral("/org/freedesktop/PackageKit"),
                                                      QStringLiteral("org.freedesktop.PackageKit.Offline"), QStringLiteral("Trigger"));
        message << QStringLiteral("reboot");
        QDBusConnection::systemBus().call(message, QDBus::Block, 10000);
    }
    rebootGracefully();
    if (m_oneShot) {
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    }
}

void Prompts::schedule(const QString &when)
{
    const QDateTime at = QDateTime::fromString(when, Qt::ISODate);
    if (!at.isValid()) {
        return;
    }
    if (!m_demo) {
        setScheduled(at);
    }
    m_showing.clear();
    Q_EMIT showingChanged();
    if (m_oneShot) {
        QTimer::singleShot(150, qApp, &QCoreApplication::quit);
    }
}

void Prompts::notNow()
{
    if (m_countdown > 0) {
        // Stopping the warning also drops the time; the prompt comes back
        // later to pick another.
        m_countdownTimer.stop();
        m_countdown = 0;
        Q_EMIT countdownChanged();
        setScheduled({});
    }
    m_snoozedUntil = QDateTime::currentDateTime().addSecs(snoozeHours * 3600);
    m_showing.clear();
    Q_EMIT showingChanged();
    if (m_oneShot) {
        QTimer::singleShot(150, qApp, &QCoreApplication::quit);
    }
}

void Prompts::notResponding(qint64 pid, const QString &appId, const QString &caption, const QString &handle, bool local)
{
    m_oneShot = true;
    m_pid = pid;
    // Without a desktop file the window title is the best name there is.
    QString name = caption.isEmpty() ? appId : caption;
    QString icon;
    if (const KService::Ptr service = KService::serviceByDesktopName(appId)) {
        name = service->name();
        icon = service->icon();
    }
    m_info = {
        {QStringLiteral("appName"), name},
        {QStringLiteral("iconName"), icon},
        {QStringLiteral("caption"), caption},
        {QStringLiteral("handle"), handle},
        {QStringLiteral("local"), local},
    };
    show(QStringLiteral("notresponding"));
}

// The app's windows are frozen, so asking it to close would go unanswered.
void Prompts::endTask()
{
    if (!m_demo && m_pid > 0 && m_info.value(QStringLiteral("local")).toBool()) {
        ::kill(pid_t(m_pid), SIGKILL);
    }
    m_showing.clear();
    Q_EMIT showingChanged();
    if (m_oneShot) {
        QTimer::singleShot(150, qApp, &QCoreApplication::quit);
    }
}

void Prompts::windowsMode(const QString &id, const QString &name, const QString &icon, const QString &target)
{
    m_oneShot = true;
    m_windowsId = id;
    m_windowsTarget = target;
    m_info = {
        {QStringLiteral("appName"), name.isEmpty() ? tr("This app") : name},
        {QStringLiteral("iconName"), icon},
    };
    show(QStringLiteral("windowsmode"));
}

void Prompts::useWindowsMode()
{
    if (!m_demo) {
        // The switch Settings > Windows apps shows as "Windows app support".
        KConfigGroup group = KSharedConfig::openConfig(QStringLiteral("wildos-appbridgerc"))->group(QStringLiteral("General"));
        group.writeEntry("Enabled", true, KConfig::Notify);
        group.sync();
        if (!m_windowsId.isEmpty()) {
            QStringList args{QStringLiteral("--run"), m_windowsId};
            if (!m_windowsTarget.isEmpty()) {
                args << QStringLiteral("--target") << m_windowsTarget;
            }
            QProcess::startDetached(QStringLiteral("wildos-appbridge"), args);
        }
    }
    m_showing.clear();
    Q_EMIT showingChanged();
    if (m_oneShot) {
        QTimer::singleShot(150, qApp, &QCoreApplication::quit);
    }
}

void Prompts::askWindowsShutdown(const QString &action, const QString &appsJson, const QDBusMessage &reply)
{
    // A second ask while one is showing (two sign-outs at once) settles the
    // first the safe way.
    if (m_shutdownReply.type() == QDBusMessage::MethodCallMessage) {
        QDBusConnection::sessionBus().send(m_shutdownReply.createReply(safeShutdownAnswer()));
    }
    m_shutdownReply = reply;
    m_shutdownAction = action;
    QVariantList apps;
    QStringList unsavedDocs;
    QString unsavedApp;
    bool oneApp = true;
    for (const QJsonValue &v : QJsonDocument::fromJson(appsJson.toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        const QString app = o.value(QStringLiteral("app")).toString().left(80);
        QString document = o.value(QStringLiteral("document")).toString().left(120);
        const QJsonValue unsaved = o.value(QStringLiteral("unsaved"));
        QString id = app.toLower();
        id.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        apps << QVariantMap{
            {QStringLiteral("app"), app},
            {QStringLiteral("document"), document.isEmpty() ? app : document},
            {QStringLiteral("state"), unsaved.isBool() ? (unsaved.toBool() ? QStringLiteral("unsaved") : QStringLiteral("saved")) : QStringLiteral("open")},
            {QStringLiteral("icon"), QStringLiteral("wildos-winmode-") + id},
        };
        if (unsaved.toBool()) {
            // "Budget 2027.xlsx" reads as "Budget 2027" in a sentence.
            const QString stem = document.section(QLatin1Char('.'), 0, -2);
            unsavedDocs << (stem.isEmpty() ? document : stem);
            oneApp = oneApp && (unsavedApp.isEmpty() || unsavedApp == app);
            unsavedApp = app;
        }
    }
    QString reopen;
    if (!unsavedDocs.isEmpty() && oneApp) {
        const QString list = unsavedDocs.size() == 1
            ? unsavedDocs.first()
            : tr("%1 and %2").arg(unsavedDocs.mid(0, unsavedDocs.size() - 1).join(tr(", ")), unsavedDocs.last());
        reopen = tr("%1 opens with %2 just as you left them.").arg(unsavedApp, list);
    }
    // Empty: the card says "your Windows apps open just as you left them"
    // in its own sentence.
    m_info = {
        {QStringLiteral("action"), action},
        {QStringLiteral("apps"), apps},
        {QStringLiteral("reopen"), reopen},
    };
    // Wild OS shutting down can't wait for ever, so after a minute the work
    // is kept. Restarting or shutting down Windows alone can wait: nothing
    // happens until the person says.
    const bool windowsOnly = action.startsWith(QLatin1String("windows-"));
    m_countdown = windowsOnly ? 0 : 60;
    Q_EMIT countdownChanged();
    m_shutdownTimer.stop();
    if (!windowsOnly) {
        m_shutdownTimer.start();
    }
    show(QStringLiteral("windowsshutdown"));
}

QString Prompts::safeShutdownAnswer() const
{
    // Wild OS ending the session keeps the work; Windows' own restart or
    // shutdown simply doesn't happen.
    return m_shutdownAction.startsWith(QLatin1String("windows-")) ? QStringLiteral("back") : QStringLiteral("keep");
}

void Prompts::answerWindowsShutdown(const QString &choice)
{
    m_shutdownTimer.stop();
    m_countdown = 0;
    Q_EMIT countdownChanged();
    if (m_shutdownReply.type() == QDBusMessage::MethodCallMessage) {
        QDBusConnection::sessionBus().send(m_shutdownReply.createReply(choice));
    }
    m_shutdownReply = QDBusMessage();
    m_showing.clear();
    Q_EMIT showingChanged();
}

void Prompts::askWindowsDevices(const QString &appName, const QString &devicesJson, const QDBusMessage &reply)
{
    if (m_devicesReply.type() == QDBusMessage::MethodCallMessage) {
        QDBusConnection::sessionBus().send(m_devicesReply.createReply(QStringLiteral("{\"choice\":\"deny\"}")));
    }
    m_devicesReply = reply;
    const QJsonObject devices = QJsonDocument::fromJson(devicesJson.toUtf8()).object();
    QVariantList cameras;
    for (const QJsonValue &v : devices.value(QStringLiteral("cameras")).toArray()) {
        cameras << QVariantMap{{QStringLiteral("value"), v.toString()}, {QStringLiteral("text"), v.toString()}};
    }
    QVariantList microphones;
    for (const QJsonValue &v : devices.value(QStringLiteral("microphones")).toArray()) {
        const QJsonObject m = v.toObject();
        microphones << QVariantMap{{QStringLiteral("value"), m.value(QStringLiteral("id")).toString()}, {QStringLiteral("text"), m.value(QStringLiteral("name")).toString()}};
    }
    m_info = {
        {QStringLiteral("appName"), appName.left(80)},
        {QStringLiteral("cameras"), cameras},
        {QStringLiteral("microphones"), microphones},
    };
    show(QStringLiteral("windowsdevices"));
}

void Prompts::answerWindowsDevices(bool allow, const QString &camera, const QString &microphone)
{
    QJsonObject answer{{QStringLiteral("choice"), allow ? QStringLiteral("allow") : QStringLiteral("deny")}};
    if (allow) {
        QJsonObject chosen;
        if (!camera.isEmpty()) {
            chosen.insert(QStringLiteral("camera"), camera);
        }
        if (!microphone.isEmpty()) {
            chosen.insert(QStringLiteral("microphone"), microphone);
        }
        answer.insert(QStringLiteral("devices"), chosen);
    }
    if (m_devicesReply.type() == QDBusMessage::MethodCallMessage) {
        QDBusConnection::sessionBus().send(m_devicesReply.createReply(QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact))));
    }
    m_devicesReply = QDBusMessage();
    m_showing.clear();
    Q_EMIT showingChanged();
}

void Prompts::windowsStarting(const QString &appName, const QString &iconName, int seconds, bool cold)
{
    // A question already on screen matters more than a progress card.
    if (!m_showing.isEmpty() && m_showing != QLatin1String("windowsstarting")) {
        return;
    }
    m_info = {
        {QStringLiteral("appName"), appName.left(80)},
        {QStringLiteral("iconName"), iconName},
        {QStringLiteral("seconds"), qBound(1, seconds, 600)},
        {QStringLiteral("cold"), cold},
    };
    show(QStringLiteral("windowsstarting"));
}

void Prompts::windowsStartingDone()
{
    if (m_showing == QLatin1String("windowsstarting")) {
        dismiss();
    }
}

void Prompts::demo(const QString &kind)
{
    m_demo = true;
    if (kind == QLatin1String("battery")) {
        m_info = {{QStringLiteral("percent"), 10}, {QStringLiteral("minutes"), 25}, {QStringLiteral("saver"), true}, {QStringLiteral("saverOn"), false}};
        show(kind);
    } else if (kind == QLatin1String("restart")) {
        showRestart();
    } else if (kind == QLatin1String("countdown")) {
        m_info = {{QStringLiteral("mode"), QStringLiteral("countdown")}, {QStringLiteral("security"), true}};
        m_countdown = 48;
        show(QStringLiteral("restart"));
    } else if (kind == QLatin1String("notresponding")) {
        m_info = {{QStringLiteral("appName"), QStringLiteral("Invoice Maker")}, {QStringLiteral("local"), true}};
        show(kind);
    } else if (kind == QLatin1String("windowsmode")) {
        m_info = {{QStringLiteral("appName"), QStringLiteral("Invoice Maker")}};
        show(kind);
    } else if (kind == QLatin1String("windowsshutdown")) {
        askWindowsShutdown(QStringLiteral("shutdown"),
                           QStringLiteral("[{\"app\":\"Excel\",\"document\":\"Budget 2027.xlsx\",\"unsaved\":true},"
                                          "{\"app\":\"Excel\",\"document\":\"Book1\",\"unsaved\":true},"
                                          "{\"app\":\"Word\",\"document\":\"Quote for Harbour Café.docx\",\"unsaved\":false}]"),
                           QDBusMessage());
    } else if (kind == QLatin1String("windowsrestart") || kind == QLatin1String("windowspoweroff")) {
        // The taskbar panel's Restart and Shut down for Windows alone.
        askWindowsShutdown(kind == QLatin1String("windowsrestart") ? QStringLiteral("windows-restart") : QStringLiteral("windows-shutdown"),
                           QStringLiteral("[{\"app\":\"Excel\",\"document\":\"Budget 2027.xlsx\",\"unsaved\":true},"
                                          "{\"app\":\"Invoice Maker\",\"document\":\"March invoices\",\"unsaved\":null},"
                                          "{\"app\":\"Notepad\",\"document\":\"notes.txt\",\"unsaved\":false}]"),
                           QDBusMessage());
    } else if (kind == QLatin1String("windowsdevices")) {
        askWindowsDevices(QStringLiteral("Zoom"),
                          QStringLiteral("{\"cameras\":[\"Integrated webcam\"],\"microphones\":[{\"id\":\"alsa_input.pci\",\"name\":\"Laptop microphone\"}]}"),
                          QDBusMessage());
    } else if (kind == QLatin1String("windowsstarting")) {
        windowsStarting(QStringLiteral("Word"), QString(), 60, true);
    } else if (kind.startsWith(QLatin1String("compat32"))) {
        // compat32, compat32-locked or compat32-pending
        const QString mode = kind.section(QLatin1Char('-'), 1);
        m_info = {{QStringLiteral("appName"), QStringLiteral("Steam")}, {QStringLiteral("iconName"), QStringLiteral("steam")},
                  {QStringLiteral("mode"), mode.isEmpty() ? QStringLiteral("off") : mode}};
        show(QStringLiteral("compat32"));
    }
}
