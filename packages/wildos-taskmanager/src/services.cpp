#include "services.h"

#include <QCoreApplication>
#include <QCollator>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QHash>

#include <algorithm>

namespace
{
struct UnitStatus {
    QString name;
    QString description;
    QString loadState;
    QString activeState;
    QString subState;
    QString following;
    QDBusObjectPath path;
    quint32 jobId = 0;
    QString jobType;
    QDBusObjectPath jobPath;
};

struct UnitFile {
    QString path;
    QString state;
};

const QDBusArgument &operator>>(const QDBusArgument &arg, UnitStatus &u)
{
    arg.beginStructure();
    arg >> u.name >> u.description >> u.loadState >> u.activeState >> u.subState >> u.following >> u.path >> u.jobId >> u.jobType >> u.jobPath;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const UnitStatus &u)
{
    arg.beginStructure();
    arg << u.name << u.description << u.loadState << u.activeState << u.subState << u.following << u.path << u.jobId << u.jobType << u.jobPath;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, UnitFile &u)
{
    arg.beginStructure();
    arg >> u.path >> u.state;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const UnitFile &u)
{
    arg.beginStructure();
    arg << u.path << u.state;
    arg.endStructure();
    return arg;
}
}

Q_DECLARE_METATYPE(UnitStatus)
Q_DECLARE_METATYPE(UnitFile)

namespace
{
QDBusConnection bus(bool user)
{
    return user ? QDBusConnection::sessionBus() : QDBusConnection::systemBus();
}

QDBusMessage manager(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                          QStringLiteral("org.freedesktop.systemd1.Manager"), method);
}

// Plain names for the services people meet, by unit name.
const QHash<QString, QString> &friendly()
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("NetworkManager"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Network"))},
        {QStringLiteral("NetworkManager-wait-online"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Wait for the network at start-up"))},
        {QStringLiteral("wpa_supplicant"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Wi-Fi security"))},
        {QStringLiteral("ModemManager"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Mobile broadband"))},
        {QStringLiteral("bluetooth"), QStringLiteral("Bluetooth")},
        {QStringLiteral("cups"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Printing"))},
        {QStringLiteral("cups-browsed"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Printer discovery"))},
        {QStringLiteral("avahi-daemon"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Network discovery"))},
        {QStringLiteral("ssh"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Remote sign-in (SSH)"))},
        {QStringLiteral("tor"), QStringLiteral("Tor")},
        {QStringLiteral("tor@default"), QStringLiteral("Tor")},
        {QStringLiteral("unattended-upgrades"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Security updates"))},
        {QStringLiteral("apt-daily"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Update check"))},
        {QStringLiteral("apt-daily-upgrade"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Install updates"))},
        {QStringLiteral("packagekit"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Software updates"))},
        {QStringLiteral("firewalld"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Firewall"))},
        {QStringLiteral("ufw"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Firewall"))},
        {QStringLiteral("sdwdate"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Secure time"))},
        {QStringLiteral("apparmor"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "App protection"))},
        {QStringLiteral("accounts-daemon"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Accounts"))},
        {QStringLiteral("upower"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Battery and power"))},
        {QStringLiteral("power-profiles-daemon"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Power modes"))},
        {QStringLiteral("udisks2"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Drives"))},
        {QStringLiteral("fwupd"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Firmware updates"))},
        {QStringLiteral("fwupd-refresh"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Firmware update check"))},
        {QStringLiteral("cron"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Scheduled tasks"))},
        {QStringLiteral("anacron"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Scheduled tasks catch-up"))},
        {QStringLiteral("sddm"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Sign-in screen"))},
        {QStringLiteral("thermald"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Temperature control"))},
        {QStringLiteral("colord"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Colour profiles"))},
        {QStringLiteral("rtkit-daemon"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Sound timing"))},
        {QStringLiteral("switcheroo-control"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Graphics switching"))},
        {QStringLiteral("smartmontools"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Disk health"))},
        {QStringLiteral("flatpak-system-helper"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Sandboxed app installs"))},
        {QStringLiteral("wildos-launch-preload"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Faster app starts"))},
        {QStringLiteral("pipewire"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Sound and video"))},
        {QStringLiteral("pipewire-pulse"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Sound for older apps"))},
        {QStringLiteral("wireplumber"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Sound devices"))},
        {QStringLiteral("plasma-ksystemstats"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "System monitor service"))},
        {QStringLiteral("plasma-plasmashell"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Wild OS desktop"))},
        {QStringLiteral("plasma-kwin_wayland"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Window manager"))},
        {QStringLiteral("plasma-powerdevil"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Power management"))},
        {QStringLiteral("plasma-kglobalaccel"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Keyboard shortcuts"))},
        {QStringLiteral("plasma-polkit-agent"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "Permission prompts"))},
        {QStringLiteral("plasma-xdg-desktop-portal-kde"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "App permissions portal"))},
        {QStringLiteral("xdg-desktop-portal"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "App permissions portal"))},
        {QStringLiteral("xdg-document-portal"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "File sharing portal"))},
        {QStringLiteral("xdg-permission-store"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "App permissions store"))},
        {QStringLiteral("baloo"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "File search"))},
        {QStringLiteral("kde-baloo"), QStringLiteral(QT_TRANSLATE_NOOP("Services", "File search"))},
    };
    return names;
}

// Parts of the system nobody needs to start or stop by hand. They stay
// listed under "Show all".
bool plumbing(const QString &unit, const QString &fileState)
{
    static const QStringList prefixes = {
        QStringLiteral("systemd-"), QStringLiteral("user@"), QStringLiteral("user-runtime-dir@"), QStringLiteral("getty@"),
        QStringLiteral("serial-getty@"), QStringLiteral("autovt@"), QStringLiteral("container-getty@"), QStringLiteral("console-"),
        QStringLiteral("dbus"), QStringLiteral("polkit"), QStringLiteral("plymouth"), QStringLiteral("initrd-"), QStringLiteral("emergency"),
        QStringLiteral("rescue"), QStringLiteral("kmod"), QStringLiteral("modprobe@"), QStringLiteral("lvm2-"), QStringLiteral("dm-event"),
        QStringLiteral("keyboard-setup"), QStringLiteral("setvtrgb"), QStringLiteral("ifupdown"), QStringLiteral("e2scrub"),
        QStringLiteral("ldconfig"), QStringLiteral("quotaon"), QStringLiteral("rc-local"), QStringLiteral("dracut"), QStringLiteral("app-"),
        QStringLiteral("wildos-winmode"), QStringLiteral("drkonqi"), QStringLiteral("dirmngr"), QStringLiteral("gpg-agent"),
        QStringLiteral("at-spi"), QStringLiteral("plasma-"), QStringLiteral("xdg-"), QStringLiteral("kde-"), QStringLiteral("kded"),
        QStringLiteral("ssh-agent"), QStringLiteral("keyboxd"), QStringLiteral("filter-chain"), QStringLiteral("grub-"),
        QStringLiteral("networking"), QStringLiteral("hwclock"), QStringLiteral("procps"), QStringLiteral("x11-common"),
        QStringLiteral("alsa-"), QStringLiteral("cryptdisks"), QStringLiteral("live-"), QStringLiteral("fstrim"),
        QStringLiteral("man-db"), QStringLiteral("logrotate"), QStringLiteral("dpkg-db-backup"), QStringLiteral("apt-listchanges"),
        QStringLiteral("pam_namespace"), QStringLiteral("sudo"), QStringLiteral("nftables"), QStringLiteral("wtmpdb"),
    };
    // Only what has an on and off switch of its own: enabled or disabled
    // units. Static ones are started by something else when needed.
    if (fileState != QLatin1String("enabled") && fileState != QLatin1String("disabled") && fileState != QLatin1String("enabled-runtime")) {
        return !friendly().contains(unit);
    }
    for (const QString &prefix : prefixes) {
        if (unit.startsWith(prefix) && !friendly().contains(unit)) {
            return true;
        }
    }
    return false;
}

QString descriptionFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith("Description=")) {
            return QString::fromUtf8(line.mid(12));
        }
    }
    return {};
}
}

Services::Services(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<UnitStatus>();
    qDBusRegisterMetaType<QList<UnitStatus>>();
    qDBusRegisterMetaType<UnitFile>();
    qDBusRegisterMetaType<QList<UnitFile>>();
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &Services::reload);
}

void Services::setActive(bool active)
{
    if (active == m_timer.isActive()) {
        return;
    }
    if (active) {
        reload();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    Q_EMIT activeChanged();
}

void Services::setShowAll(bool all)
{
    if (all == m_showAll) {
        return;
    }
    m_showAll = all;
    Q_EMIT showAllChanged();
    filter();
}

void Services::setSearch(const QString &search)
{
    if (search == m_search) {
        return;
    }
    m_search = search;
    Q_EMIT searchChanged();
    filter();
}

QVariantList Services::list(bool user) const
{
    QDBusConnection connection = bus(user);
    QHash<QString, QVariantMap> units;

    const QDBusMessage files = connection.call(manager(QStringLiteral("ListUnitFilesByPatterns")) << QStringList() << QStringList{QStringLiteral("*.service")},
                                               QDBus::Block, 4000);
    QHash<QString, QString> fileState;
    QHash<QString, QString> filePath;
    if (files.type() == QDBusMessage::ReplyMessage && !files.arguments().isEmpty()) {
        const auto list = qdbus_cast<QList<UnitFile>>(files.arguments().constFirst());
        for (const UnitFile &f : list) {
            const QString name = f.path.section(QLatin1Char('/'), -1);
            // Templates are not services until something starts an instance.
            if (name.contains(QLatin1String("@."))) {
                continue;
            }
            fileState.insert(name, f.state);
            filePath.insert(name, f.path);
        }
    }

    const QDBusMessage loaded = connection.call(manager(QStringLiteral("ListUnitsByPatterns")) << QStringList() << QStringList{QStringLiteral("*.service")},
                                                QDBus::Block, 4000);
    QHash<QString, UnitStatus> status;
    if (loaded.type() == QDBusMessage::ReplyMessage && !loaded.arguments().isEmpty()) {
        const auto list = qdbus_cast<QList<UnitStatus>>(loaded.arguments().constFirst());
        for (const UnitStatus &u : list) {
            if (u.loadState == QLatin1String("not-found")) {
                continue;
            }
            status.insert(u.name, u);
        }
    }

    QSet<QString> names(fileState.keyBegin(), fileState.keyEnd());
    for (auto it = status.cbegin(); it != status.cend(); ++it) {
        names.insert(it.key());
    }
    QVariantList out;
    for (const QString &name : std::as_const(names)) {
        const UnitStatus u = status.value(name);
        QString base = name;
        base.chop(8);
        QString state = fileState.value(name);
        if (state.isEmpty()) {
            state = u.loadState == QLatin1String("loaded") ? QStringLiteral("static") : u.loadState;
        }
        QString description = u.description;
        if (description.isEmpty() || description == name) {
            description = descriptionFromFile(filePath.value(name));
        }
        // The plain names are English in the table and translated here.
        const QString plain = friendly().contains(base) ? QCoreApplication::translate("Services", friendly().value(base).toUtf8().constData())
                                                        : description.isEmpty() ? base : description;
        const QString active = u.activeState.isEmpty() ? QStringLiteral("inactive") : u.activeState;
        const QString shown = active == QLatin1String("active") ? QStringLiteral("Running")
            : active == QLatin1String("activating")             ? QStringLiteral("Starting")
            : active == QLatin1String("deactivating")           ? QStringLiteral("Stopping")
            : active == QLatin1String("failed")                 ? QStringLiteral("Failed")
                                                                : QStringLiteral("Stopped");
        const QString starts = state.startsWith(QLatin1String("enabled")) ? QCoreApplication::translate("Services", "Automatically")
            : state == QLatin1String("masked")                            ? QCoreApplication::translate("Services", "Never")
            : state == QLatin1String("disabled")                          ? QCoreApplication::translate("Services", "Manually")
                                                                          : QCoreApplication::translate("Services", "When needed");
        out << QVariantMap{
            {QStringLiteral("key"), (user ? QStringLiteral("user:") : QStringLiteral("system:")) + name},
            {QStringLiteral("unit"), name},
            {QStringLiteral("name"), plain},
            {QStringLiteral("description"), description},
            {QStringLiteral("status"), shown},
            {QStringLiteral("running"), active == QLatin1String("active") || active == QLatin1String("activating")},
            {QStringLiteral("starts"), starts},
            {QStringLiteral("user"), user},
            {QStringLiteral("owner"), user ? QCoreApplication::translate("Services", "You") : QCoreApplication::translate("Services", "System")},
            {QStringLiteral("masked"), state == QLatin1String("masked")},
            {QStringLiteral("plumbing"), plumbing(base, state) || state == QLatin1String("masked")},
            {QStringLiteral("known"), friendly().contains(base)},
        };
    }
    return out;
}

void Services::reload()
{
    QVariantList all = list(false) + list(true);
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(all.begin(), all.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(), b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_all = all;
    filter();
}

void Services::filter()
{
    QVariantList visible;
    const QString needle = m_search.trimmed();
    for (const QVariant &v : std::as_const(m_all)) {
        const QVariantMap s = v.toMap();
        if (!m_showAll && s.value(QStringLiteral("plumbing")).toBool()) {
            continue;
        }
        if (!needle.isEmpty() && !s.value(QStringLiteral("name")).toString().contains(needle, Qt::CaseInsensitive)
            && !s.value(QStringLiteral("unit")).toString().contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        visible << s;
    }
    if (visible != m_visible) {
        m_visible = visible;
        Q_EMIT servicesChanged();
    }
}

void Services::act(const QString &unit, bool user, const QString &action)
{
    const QString method = action == QLatin1String("start") ? QStringLiteral("StartUnit")
        : action == QLatin1String("stop")                   ? QStringLiteral("StopUnit")
                                                            : QStringLiteral("RestartUnit");
    QDBusMessage message = manager(method);
    message << unit << QStringLiteral("replace");
    // systemd asks polkit, which asks the person for a password through the
    // desktop's agent, only when the caller allows it to.
    message.setInteractiveAuthorizationAllowed(true);
    auto watcher = new QDBusPendingCallWatcher(bus(user).asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher->isError()) {
            const QString name = watcher->error().name();
            if (name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied") || name.endsWith(QLatin1String("InteractiveAuthorizationRequired"))) {
                Q_EMIT failed(QCoreApplication::translate("Services", "Wild OS needs an administrator's permission to change this service."));
            } else {
                Q_EMIT failed(QCoreApplication::translate("Services", "The service could not be changed. %1").arg(watcher->error().message()));
            }
        }
        QTimer::singleShot(400, this, &Services::reload);
    });
}
