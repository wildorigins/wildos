#include "processes.h"
#include "winmode.h"

#include <KService>
#include <KShell>
#include <processcore/process_controller.h>
#include <processcore/process_data_model.h>

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <csignal>
#include <unistd.h>

namespace
{
const QStringList attributes = {
    QStringLiteral("pid"), QStringLiteral("parentPid"), QStringLiteral("uid"), QStringLiteral("name"), QStringLiteral("command"),
    QStringLiteral("usage"), QStringLiteral("memory"), QStringLiteral("ioCharactersActuallyReadRate"), QStringLiteral("ioCharactersActuallyWrittenRate"),
    QStringLiteral("netInbound"), QStringLiteral("netOutbound"), QStringLiteral("numThreads"), QStringLiteral("status"), QStringLiteral("niceLevel"),
    QStringLiteral("startTime"),
};
enum Column { Pid, ParentPid, Uid, Name, Command, Usage, Memory, ReadRate, WriteRate, NetIn, NetOut, Threads, Status, Nice, StartTime };

// Plain names for the parts of the desktop and the system a person sees in
// the list, by process name. Anything not here shows its process name, as
// Windows shows a file name when a program has no description.
struct Known {
    const char *name;
    // Shown under Background processes even though the system runs it.
    bool background = false;
    // Ending it ends the session, so End task is not offered.
    bool critical = false;
    // Offered as "Restart" instead of "End task", as Windows Explorer is.
    bool restart = false;
    const char *status = "";
};

const QHash<QString, Known> &known()
{
    static const QHash<QString, Known> table = {
        {QStringLiteral("plasmashell"), {"Wild OS desktop", false, false, true}},
        {QStringLiteral("kwin_wayland"), {"Window manager", false, true}},
        {QStringLiteral("kwin_wayland_wrapper"), {"Window manager", false, true}},
        {QStringLiteral("ksmserver"), {"Session manager", false, true}},
        {QStringLiteral("plasma_session"), {"Session manager", false, true}},
        {QStringLiteral("startplasma-wayland"), {"Session manager", false, true}},
        {QStringLiteral("Xwayland"), {"Display for older apps", false, true}},
        {QStringLiteral("kded6"), {"Desktop services"}},
        {QStringLiteral("kglobalacceld"), {"Keyboard shortcuts"}},
        {QStringLiteral("kactivitymanagerd"), {"Recent files and activities"}},
        {QStringLiteral("kwalletd6"), {"Password storage"}},
        {QStringLiteral("ksecretd"), {"Password storage"}},
        {QStringLiteral("baloo_file"), {"File search"}},
        {QStringLiteral("baloo_file_extractor"), {"File search"}},
        {QStringLiteral("polkit-kde-authentication-agent-1"), {"Permission prompts"}},
        {QStringLiteral("org_kde_powerdevil"), {"Power management"}},
        {QStringLiteral("ksystemstats"), {"System monitor service"}},
        {QStringLiteral("kscreen_backend_launcher"), {"Display settings service"}},
        {QStringLiteral("xdg-desktop-portal"), {"App permissions portal"}},
        {QStringLiteral("xdg-desktop-portal-kde"), {"App permissions portal"}},
        {QStringLiteral("xdg-document-portal"), {"File sharing portal"}},
        {QStringLiteral("xdg-permission-store"), {"App permissions store"}},
        {QStringLiteral("pipewire"), {"Sound and video"}},
        {QStringLiteral("pipewire-pulse"), {"Sound for older apps"}},
        {QStringLiteral("wireplumber"), {"Sound devices"}},
        {QStringLiteral("dbus-daemon"), {"Message bus", false, true}},
        {QStringLiteral("dbus-broker"), {"Message bus", false, true}},
        {QStringLiteral("dbus-broker-launch"), {"Message bus", false, true}},
        {QStringLiteral("gmenudbusmenuproxy"), {"App menu helper"}},
        {QStringLiteral("xembedsniproxy"), {"Tray icon helper"}},
        {QStringLiteral("kaccess"), {"Accessibility"}},
        {QStringLiteral("at-spi-bus-launcher"), {"Accessibility"}},
        {QStringLiteral("at-spi2-registryd"), {"Accessibility"}},
        {QStringLiteral("wildos-launchd"), {"App start helper"}},
        {QStringLiteral("DiscoverNotifier"), {"Update notifier"}},
        {QStringLiteral("ksgrd_network_helper"), {"Network use monitor"}},
        {QStringLiteral("systemd"), {"Wild OS", false, true}},
        {QStringLiteral("unattended-upgr"), {"Security updates", true, false, false, "Working"}},
        {QStringLiteral("unattended-upgrade"), {"Security updates", true, false, false, "Working"}},
        {QStringLiteral("apt-get"), {"Security updates", true, false, false, "Working"}},
        {QStringLiteral("apt.systemd.dai"), {"Security updates", true, false, false, "Working"}},
        {QStringLiteral("packagekitd"), {"Software updates", true}},
        {QStringLiteral("firewalld"), {"Firewall", true}},
        {QStringLiteral("ufw"), {"Firewall", true}},
        {QStringLiteral("sdwdate"), {"Secure time", true}},
        {QStringLiteral("NetworkManager"), {"Network"}},
        {QStringLiteral("wpa_supplicant"), {"Wi-Fi security"}},
        {QStringLiteral("ModemManager"), {"Mobile broadband"}},
        {QStringLiteral("bluetoothd"), {"Bluetooth"}},
        {QStringLiteral("cupsd"), {"Printing"}},
        {QStringLiteral("cups-browsed"), {"Printer discovery"}},
        {QStringLiteral("avahi-daemon"), {"Network discovery"}},
        {QStringLiteral("upowerd"), {"Battery and power"}},
        {QStringLiteral("power-profiles-daemon"), {"Power modes"}},
        {QStringLiteral("udisksd"), {"Drives"}},
        {QStringLiteral("accounts-daemon"), {"Accounts"}},
        {QStringLiteral("polkitd"), {"Permissions"}},
        {QStringLiteral("rtkit-daemon"), {"Sound timing"}},
        {QStringLiteral("colord"), {"Colour profiles"}},
        {QStringLiteral("thermald"), {"Temperature control"}},
        {QStringLiteral("fwupd"), {"Firmware updates"}},
        {QStringLiteral("cron"), {"Scheduled tasks"}},
        {QStringLiteral("sddm"), {"Sign-in screen", false, true}},
        {QStringLiteral("sddm-helper"), {"Sign-in screen", false, true}},
        {QStringLiteral("systemd-journald"), {"System log", false, true}},
        {QStringLiteral("systemd-logind"), {"Sign-in manager", false, true}},
        {QStringLiteral("systemd-udevd"), {"Device manager", false, true}},
        {QStringLiteral("systemd-resolved"), {"Name lookup"}},
        {QStringLiteral("systemd-timesyncd"), {"Clock sync"}},
        {QStringLiteral("systemd-oomd"), {"Low memory guard"}},
        {QStringLiteral("tor"), {"Tor"}},
        {QStringLiteral("flatpak-session-helper"), {"Sandboxed app helper"}},
        {QStringLiteral("flatpak-portal"), {"Sandboxed app helper"}},
        {QStringLiteral("winmoded"), {"Windows mode"}},
        {QStringLiteral("wildos-rdp"), {"Windows mode"}},
        {QStringLiteral("(sd-pam)"), {"Session helper", false, true}},
        {QStringLiteral("dconf-service"), {"App settings storage"}},
        {QStringLiteral("drkonqi-coredump-processor"), {"Crash reporter"}},
        {QStringLiteral("drkonqi-coredump-launcher"), {"Crash reporter"}},
        {QStringLiteral("kioworker"), {"File access helper"}},
        {QStringLiteral("fm-shim"), {"Open in Files helper"}},
        {QStringLiteral("filter-chain"), {"Sound effects"}},
        {QStringLiteral("obexd"), {"Bluetooth file transfer"}},
        {QStringLiteral("mpris-proxy"), {"Bluetooth media controls"}},
        {QStringLiteral("usbguard-notifier"), {"USB protection alerts"}},
        {QStringLiteral("usbguard-daemon"), {"USB protection", false, true}},
        {QStringLiteral("usbguard-dbus"), {"USB protection", false, true}},
        {QStringLiteral("privleapd"), {"Permission helper", false, true}},
        {QStringLiteral("haveged"), {"Randomness for encryption", false, true}},
        {QStringLiteral("jitterentropy-r"), {"Randomness for encryption", false, true}},
        {QStringLiteral("canary-daemon"), {"System check", false, true}},
        {QStringLiteral("iscsiuio"), {"Network storage"}},
        {QStringLiteral("blkmapd"), {"Network storage"}},
        {QStringLiteral("acpid"), {"Power button"}},
        {QStringLiteral("qemu-ga"), {"Virtual machine helper"}},
        // The kernel cuts process names to 15 characters, so the short
        // forms are listed alongside the full ones.
        {QStringLiteral("drkonqi-coredum"), {"Crash reporter"}},
        {QStringLiteral("usbguard-notifi"), {"USB protection alerts"}},
        {QStringLiteral("fm-shim-backend"), {"Open in Files helper"}},
        {QStringLiteral("kio-fuse"), {"File access helper"}},
        {QStringLiteral("ksmserver"), {"Session manager", false, true}},
        {QStringLiteral("Xwayland"), {"Older app support"}},
        {QStringLiteral("xsettingsd"), {"Settings for older apps"}},
        {QStringLiteral("ssh-agent"), {"SSH keys"}},
        {QStringLiteral("switcheroo-cont"), {"Graphics switching"}},
        {QStringLiteral("switcheroo-control"), {"Graphics switching"}},
        {QStringLiteral("emerg-shutdown"), {"Emergency shutdown", false, true}},
        {QStringLiteral("memlockd"), {"Memory lock service", false, true}},
        {QStringLiteral("watchdogd"), {"System watchdog", false, true}},
        {QStringLiteral("psimon"), {"System pressure monitor", false, true}},
        {QStringLiteral("msgdispatcher"), {"System messages", false, true}},
        {QStringLiteral("acpi_fakekeyd"), {"Power keys", false, true}},
        {QStringLiteral("sdwdate-start-a"), {"Secure time", true}},
        {QStringLiteral("wildos-clipboar"), {"Clipboard history"}},
        {QStringLiteral("wildos-clipboard"), {"Clipboard history"}},
        {QStringLiteral("wildos-dialogs"), {"System dialogs"}},
        {QStringLiteral("wildos-portal"), {"File dialogs"}},
    };
    return table;
}

QString lastUnit(const QString &cgroup)
{
    const QStringList parts = cgroup.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (auto it = parts.crbegin(); it != parts.crend(); ++it) {
        if (it->endsWith(QLatin1String(".service")) || it->endsWith(QLatin1String(".scope"))) {
            return *it;
        }
    }
    return {};
}

QString unescape(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (int i = 0; i < name.size(); ++i) {
        if (name.at(i) == QLatin1Char('\\') && i + 3 < name.size() && name.at(i + 1) == QLatin1Char('x')) {
            bool ok = false;
            const int code = name.mid(i + 2, 2).toInt(&ok, 16);
            if (ok) {
                out += QChar(code);
                i += 3;
                continue;
            }
        }
        out += name.at(i);
    }
    return out;
}

bool userScope(const QString &cgroup)
{
    return cgroup.contains(QLatin1String("/user@")) || cgroup.contains(QLatin1String("/session-"));
}

QDBusMessage systemdUser(const QString &method, const QVariantList &args)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"), method);
    message.setArguments(args);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
}

// A process's start time from /proc, which tells a process apart from a
// later one that was given the same id.
QByteArray startedAt(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray stat = file.readAll();
    // The name in brackets may hold spaces; the fields after it do not.
    const QList<QByteArray> fields = stat.mid(stat.lastIndexOf(')') + 2).split(' ');
    return fields.size() > 19 ? fields.at(19) : QByteArray();
}

QString resultText(KSysGuard::ProcessController::Result result)
{
    using R = KSysGuard::ProcessController::Result;
    switch (result) {
    case R::Success:
    case R::NoSuchProcess:
        return {};
    case R::InsufficientPermissions:
        return QStringLiteral("Task Manager was not allowed to do that.");
    case R::UserCancelled:
        return QStringLiteral("Nothing was changed.");
    case R::Unsupported:
    case R::Unknown:
    case R::Error:
        break;
    }
    return QStringLiteral("That did not work. Try again in a moment.");
}
}

Processes *Processes::instance()
{
    static Processes *self = new Processes;
    return self;
}

Processes *Processes::create(QQmlEngine *, QJSEngine *engine)
{
    Processes *self = instance();
    engine->setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

Processes::Processes(QObject *parent)
    : QAbstractListModel(parent)
    , m_source(new KSysGuard::ProcessDataModel(this))
    , m_controller(new KSysGuard::ProcessController(this))
    , m_uid(::getuid())
{
    m_cpuCount = qMax(1, QThread::idealThreadCount());
    m_source->setEnabledAttributes(attributes);
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(120);
    connect(&m_coalesce, &QTimer::timeout, this, &Processes::refresh);
    const auto poke = [this] {
        if (!m_coalesce.isActive()) {
            m_coalesce.start();
        }
    };
    connect(m_source, &QAbstractItemModel::dataChanged, this, poke);
    connect(m_source, &QAbstractItemModel::rowsInserted, this, poke);
    connect(m_source, &QAbstractItemModel::rowsRemoved, this, poke);
    connect(m_source, &QAbstractItemModel::modelReset, this, poke);
    m_expanded.insert(QStringLiteral("winmode"));
}

void Processes::setSearch(const QString &search)
{
    if (m_search == search) {
        return;
    }
    m_search = search;
    Q_EMIT searchChanged();
    rebuild();
}

void Processes::setSortColumn(const QString &column)
{
    if (m_sortColumn == column) {
        return;
    }
    m_sortColumn = column;
    Q_EMIT sortChanged();
    rebuild();
}

void Processes::setSortDescending(bool descending)
{
    if (m_sortDescending == descending) {
        return;
    }
    m_sortDescending = descending;
    Q_EMIT sortChanged();
    rebuild();
}

void Processes::sortBy(const QString &column)
{
    if (m_sortColumn == column) {
        m_sortDescending = !m_sortDescending;
    } else {
        m_sortColumn = column;
        m_sortDescending = column != QLatin1String("name") && column != QLatin1String("status");
    }
    Q_EMIT sortChanged();
    rebuild();
}

void Processes::setPaused(bool paused)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    m_source->setEnabled(!paused);
    Q_EMIT pausedChanged();
}

void Processes::setShowSystem(bool show)
{
    if (m_showSystem == show) {
        return;
    }
    m_showSystem = show;
    Q_EMIT showSystemChanged();
    rebuild();
}

QString Processes::cgroupOf(qint64 pid, qint64 startTime)
{
    const auto cached = m_cgroups.constFind(pid);
    if (cached != m_cgroups.constEnd() && cached->first == startTime) {
        return cached->second;
    }
    QFile file(QStringLiteral("/proc/%1/cgroup").arg(pid));
    QString path;
    if (file.open(QIODevice::ReadOnly)) {
        // cgroup v2 has a single "0::/path" line.
        for (const QByteArray &line : file.readAll().split('\n')) {
            if (line.startsWith("0::")) {
                path = QString::fromUtf8(line.mid(3)).trimmed();
                break;
            }
        }
    }
    m_cgroups.insert(pid, {startTime, path});
    return path;
}

Processes::AppInfo Processes::appForUnit(const QString &unit)
{
    const auto cached = m_apps.constFind(unit);
    if (cached != m_apps.constEnd()) {
        return *cached;
    }
    AppInfo info;
    QString base = unit;
    const bool scope = base.endsWith(QLatin1String(".scope"));
    base.chop(scope ? 6 : 8);
    if (base.startsWith(QLatin1String("app-"))) {
        base = unescape(base.mid(4));
        if (const int at = base.indexOf(QLatin1Char('@')); at >= 0) {
            base = base.left(at);
        } else if (scope) {
            // Scopes end in a number or a random string after the last dash;
            // desktop ids never have a dot there.
            const int dash = base.lastIndexOf(QLatin1Char('-'));
            if (dash > 0 && !base.mid(dash + 1).contains(QLatin1Char('.'))) {
                base = base.left(dash);
            }
        }
        if (base.startsWith(QLatin1String("flatpak-"))) {
            base = base.mid(8);
            info.flatpak = true;
        }
        if (!base.startsWith(QLatin1String("dbus-"))) {
            // KService's cache first; the launcher's file itself when the
            // cache has not caught up with a newly installed app.
            const auto lookup = [](const QString &id) -> KService::Ptr {
                KService::Ptr found = KService::serviceByDesktopName(id);
                if (!found) {
                    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id + QStringLiteral(".desktop"));
                    if (!path.isEmpty()) {
                        found = KService::Ptr(new KService(path));
                    }
                }
                return found && found->isValid() ? found : KService::Ptr();
            };
            KService::Ptr service = lookup(base);
            if (!service && base.contains(QLatin1Char('-'))) {
                // A launcher name in front ("app-gnome-<id>").
                service = lookup(base.mid(base.indexOf(QLatin1Char('-')) + 1));
            }
            if (service && service->isApplication()) {
                info.id = service->desktopEntryName();
                info.name = service->name();
                info.icon = service->icon();
                // Autostarted helpers with no launcher of their own are
                // background processes, as on Windows.
                info.app = !service->noDisplay() || info.flatpak;
            } else if (info.flatpak) {
                info.id = base;
                info.name = base.section(QLatin1Char('.'), -1);
                info.app = true;
            }
        }
    }
    m_apps.insert(unit, info);
    return info;
}

Processes::AppInfo Processes::appForProgram(const Proc &p)
{
    // Launchers come and go with installs; the folders are read again at
    // most once a minute.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now - m_programsRead > 60) {
        m_programsRead = now;
        m_programs.clear();
        const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        // Earlier folders win, as they do for the launchers themselves.
        for (auto dir = dirs.crbegin(); dir != dirs.crend(); ++dir) {
            QDirIterator it(*dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                KService service(it.next());
                if (!service.isValid() || !service.isApplication() || service.noDisplay()) {
                    continue;
                }
                const QStringList args = KShell::splitArgs(service.exec());
                if (args.isEmpty()) {
                    continue;
                }
                const QString program = QFileInfo(args.constFirst()).fileName();
                // Launchers that go through a helper or an interpreter
                // (env, flatpak, kstart, a shell, Python) are found by their
                // unit name instead; matching the helper would claim every
                // process that runs it.
                static const QStringList helpers = {
                    QStringLiteral("env"), QStringLiteral("flatpak"), QStringLiteral("kstart"), QStringLiteral("kstart6"), QStringLiteral("sh"),
                    QStringLiteral("bash"), QStringLiteral("python3"), QStringLiteral("python"), QStringLiteral("java"), QStringLiteral("perl"),
                    QStringLiteral("wildos-winmode"), QStringLiteral("wildos-appbridge"),
                };
                if (helpers.contains(program)) {
                    continue;
                }
                AppInfo info;
                info.id = service.desktopEntryName();
                info.name = service.name();
                info.icon = service.icon();
                info.app = true;
                m_programs.insert(program, info);
                // The kernel keeps only the first 15 characters of a name.
                m_programs.insert(program.left(15), info);
            }
        }
    }
    const QString program = QFileInfo(p.command.section(QLatin1Char(' '), 0, 0)).fileName();
    return m_programs.value(program, m_programs.value(p.name));
}

void Processes::refresh()
{
    const int rows = m_source->rowCount();
    QList<Proc> procs;
    procs.reserve(rows);
    QSet<qint64> alive;
    int threads = 0;
    const auto value = [this](int row, int column) {
        return m_source->data(m_source->index(row, column), KSysGuard::ProcessDataModel::Value);
    };
    for (int r = 0; r < rows; ++r) {
        Proc p;
        p.pid = value(r, Pid).toLongLong();
        p.ppid = value(r, ParentPid).toLongLong();
        // Kernel threads are not programs.
        if (p.pid <= 0 || p.pid == 2 || p.ppid == 2) {
            continue;
        }
        p.uid = value(r, Uid).toLongLong();
        p.name = value(r, Name).toString();
        p.command = value(r, Command).toString();
        p.cpu = value(r, Usage).toDouble() / m_cpuCount;
        p.memory = value(r, Memory).toLongLong() * 1024;
        p.disk = value(r, ReadRate).toLongLong() + value(r, WriteRate).toLongLong();
        p.network = value(r, NetIn).toLongLong() + value(r, NetOut).toLongLong();
        p.threads = value(r, Threads).toInt();
        p.status = value(r, Status).toString();
        p.nice = value(r, Nice).toInt();
        p.cgroup = cgroupOf(p.pid, value(r, StartTime).toLongLong());
        threads += qMax(1, p.threads);
        alive.insert(p.pid);
        procs.append(p);
    }
    for (auto it = m_cgroups.begin(); it != m_cgroups.end();) {
        it = alive.contains(it.key()) ? std::next(it) : m_cgroups.erase(it);
    }
    m_procs = procs;
    m_processCount = procs.size();
    m_threadCount = threads;

    QHash<QString, Item> apps;
    QHash<QString, Item> background;
    QHash<QString, Item> system;
    // Windows mode runs in one process of the system's; when this account
    // can see it, it is counted once, in the Windows mode row.
    m_winStatus = WinMode::status();
    const qint64 vmPid = m_winStatus.pid;
    QSet<QString> named;
    const auto add = [](Item &item, const Proc &p, double gpu) {
        item.pids << p.pid;
        item.cpu += p.cpu;
        item.memory += p.memory;
        item.disk += p.disk;
        item.network += p.network;
        item.gpu += gpu;
    };

    for (int i = 0; i < m_procs.size(); ++i) {
        const Proc &p = m_procs.at(i);
        const double gpu = m_gpu.value(p.pid);
        if (vmPid > 0 && p.pid == vmPid) {
            continue;
        }
        const QString unit = lastUnit(p.cgroup);
        const bool mine = p.uid == m_uid && userScope(p.cgroup);
        if (mine && unit.startsWith(QLatin1String("app-"))) {
            const AppInfo info = appForUnit(unit);
            if (info.app) {
                const QString key = QStringLiteral("app:") + info.id;
                Item &item = apps[key];
                if (item.key.isEmpty()) {
                    item.key = key;
                    item.name = info.name;
                    item.icon = info.icon;
                    item.group = 0;
                    item.status = info.flatpak ? QStringLiteral("Sandboxed") : QString();
                }
                add(item, p, gpu);
                continue;
            }
        }
        // A program with a launcher of its own is that app wherever it was
        // started from, as long as it runs as this account.
        if (p.uid == m_uid) {
            const AppInfo info = appForProgram(p);
            if (info.app) {
                const QString key = QStringLiteral("app:") + info.id;
                Item &item = apps[key];
                if (item.key.isEmpty()) {
                    item.key = key;
                    item.name = info.name;
                    item.icon = info.icon;
                    item.group = 0;
                }
                add(item, p, gpu);
                continue;
            }
        }
        const Known k = known().value(p.name);
        // A service or helper scope is one entry with its processes inside;
        // processes of a sign-in session scope are each their own entry.
        const bool grouped = !unit.isEmpty() && !unit.startsWith(QLatin1String("session-")) && unit != QLatin1String("init.scope");
        const bool backgroundGroup = mine || k.background;
        QHash<QString, Item> &target = backgroundGroup ? background : system;
        const QString key = (backgroundGroup ? QStringLiteral("bg:") : QStringLiteral("sys:")) + (grouped ? unit : QString::number(p.pid));
        Item &item = target[key];
        const bool first = item.key.isEmpty();
        if (first) {
            item.key = key;
            item.group = backgroundGroup ? 1 : 2;
        }
        // The entry is named after the first of its processes that has a
        // plain name, else after its first process.
        if (k.name && !named.contains(key)) {
            named.insert(key);
            item.name = QString::fromUtf8(k.name);
            item.status = QString::fromUtf8(k.status);
            item.canEnd = !k.critical;
            item.restart = k.restart;
        } else if (first) {
            // A helper started from a launcher of its own (an autostarted
            // reminder service) keeps that launcher's name and icon.
            const AppInfo info = mine && unit.startsWith(QLatin1String("app-")) ? appForUnit(unit) : AppInfo();
            item.name = info.name.isEmpty() ? p.name : info.name;
            item.icon = info.icon;
        }
        add(item, p, gpu);
    }

    // Each entry's processes, named by process.
    const auto fillChildren = [this](Item &item) {
        item.children.clear();
        for (qint64 pid : std::as_const(item.pids)) {
            const auto proc = std::find_if(m_procs.cbegin(), m_procs.cend(), [pid](const Proc &p) {
                return p.pid == pid;
            });
            if (proc == m_procs.cend()) {
                continue;
            }
            Item child;
            child.key = item.key + QLatin1Char('/') + QString::number(pid);
            const Known k = known().value(proc->name);
            child.name = proc->name;
            child.icon = item.icon;
            child.group = item.group;
            child.pids = {pid};
            child.cpu = proc->cpu;
            child.memory = proc->memory;
            child.disk = proc->disk;
            child.network = proc->network;
            child.gpu = m_gpu.value(pid);
            child.canEnd = !k.critical;
            child.status = proc->status == QLatin1String("stopped") ? QStringLiteral("Suspended") : QString();
            item.children << child;
        }
        // Everything stopped reads as suspended, as a frozen app does.
        const bool allStopped = !item.children.isEmpty() && std::all_of(item.children.cbegin(), item.children.cend(), [](const Item &c) {
            return c.status == QLatin1String("Suspended");
        });
        if (allStopped) {
            item.status = QStringLiteral("Suspended");
        }
        if (!item.canEnd) {
            for (Item &child : item.children) {
                child.canEnd = false;
            }
        }
    };

    QList<Item> items;
    for (auto *table : {&apps, &background, &system}) {
        for (Item &item : *table) {
            fillChildren(item);
            item.endLabel = item.restart ? QStringLiteral("Restart") : QStringLiteral("End task");
            items << item;
        }
    }

    // Windows mode: one row, the Windows that every Windows app runs in.
    // Its apps are closed from their own windows, never ended here, where
    // their unsaved work would be lost.
    m_winCpu = 0;
    m_winMemory = 0;
    const QString vm = m_winStatus.vm;
    if (vm == QLatin1String("running") || vm == QLatin1String("paused")) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_winCpuSeconds >= 0 && now > m_winSampled && m_winStatus.cpuSeconds >= m_winCpuSeconds) {
            m_winCpu = (m_winStatus.cpuSeconds - m_winCpuSeconds) * 1000.0 / double(now - m_winSampled) * 100.0 / qMax(1, m_cpuCount);
        }
        m_winCpuSeconds = m_winStatus.cpuSeconds;
        m_winSampled = now;
        m_winMemory = m_winStatus.memory;
        Item mode;
        mode.key = QStringLiteral("winmode");
        mode.name = QStringLiteral("Windows mode");
        mode.icon = QStringLiteral("wildos-windows-mode");
        mode.chip = QStringLiteral("Windows");
        mode.group = 0;
        mode.winMode = true;
        mode.canEnd = false;
        mode.endLabel = QStringLiteral("End task");
        mode.status = vm == QLatin1String("paused") ? QStringLiteral("Paused") : QString();
        mode.cpu = m_winCpu;
        mode.memory = m_winMemory;
        if (m_winStatus.pid > 0) {
            mode.pids << m_winStatus.pid;
        }
        items << mode;
    } else {
        m_winCpuSeconds = -1;
    }

    m_items = items;
    m_ready = true;
    rebuild();
    Q_EMIT updated();
}

void Processes::rebuild()
{
    const QString needle = m_search.trimmed();
    const auto matches = [&needle](const Item &item) {
        if (needle.isEmpty() || item.name.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
        return std::any_of(item.children.cbegin(), item.children.cend(), [&needle](const Item &c) {
            return c.name.contains(needle, Qt::CaseInsensitive);
        });
    };
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const QString column = m_sortColumn;
    const bool descending = m_sortDescending;
    const auto metric = [&column](const Item &item) -> double {
        if (column == QLatin1String("cpu")) {
            return item.cpu;
        }
        if (column == QLatin1String("memory")) {
            return double(item.memory);
        }
        if (column == QLatin1String("disk")) {
            return double(item.disk);
        }
        if (column == QLatin1String("network")) {
            return double(item.network);
        }
        return 0;
    };
    const auto less = [&](const Item &a, const Item &b) {
        if (column == QLatin1String("name") || column == QLatin1String("status")) {
            const int c = column == QLatin1String("name") ? collator.compare(a.name, b.name) : collator.compare(a.status, b.status);
            if (c != 0) {
                return descending ? c > 0 : c < 0;
            }
            return collator.compare(a.name, b.name) < 0;
        }
        const double x = metric(a);
        const double y = metric(b);
        if (x != y) {
            return descending ? x > y : x < y;
        }
        return collator.compare(a.name, b.name) < 0;
    };

    static const char *titles[] = {"Apps", "Background processes", "Wild OS processes"};
    QList<Row> rows;
    for (int group = 0; group < (m_showSystem ? 3 : 2); ++group) {
        QList<const Item *> members;
        for (const Item &item : std::as_const(m_items)) {
            if (item.group == group && matches(item)) {
                members << &item;
            }
        }
        if (members.isEmpty()) {
            continue;
        }
        std::sort(members.begin(), members.end(), [&less](const Item *a, const Item *b) {
            return less(*a, *b);
        });
        Row header;
        header.key = QStringLiteral("group:%1").arg(group);
        header.kind = Header;
        header.group = group;
        header.name = QString::fromLatin1(titles[group]);
        header.count = members.size();
        rows << header;
        for (const Item *item : std::as_const(members)) {
            Row row;
            row.key = item->key;
            row.kind = Entry;
            row.group = group;
            row.name = item->name;
            row.icon = item->icon;
            row.status = item->status;
            row.chip = item->chip;
            row.cpu = item->cpu;
            row.memory = item->memory;
            row.disk = item->disk;
            row.network = item->network;
            // As in Windows, an entry opens only when there is more than
            // one thing inside; Windows mode always shows its apps.
            row.expandable = item->winMode ? !item->children.isEmpty() : item->children.size() > 1;
            // Searching for a process opens the entry it belongs to.
            row.expanded = row.expandable && (m_expanded.contains(item->key) || (!needle.isEmpty() && !item->name.contains(needle, Qt::CaseInsensitive)));
            row.canEnd = item->canEnd;
            row.endLabel = item->endLabel;
            rows << row;
            if (!row.expanded) {
                continue;
            }
            QList<const Item *> children;
            for (const Item &child : item->children) {
                children << &child;
            }
            std::sort(children.begin(), children.end(), [&less](const Item *a, const Item *b) {
                return less(*a, *b);
            });
            for (const Item *child : std::as_const(children)) {
                Row c;
                c.key = child->key;
                c.kind = Child;
                c.depth = 1;
                c.group = group;
                c.name = child->name;
                c.icon = child->icon;
                c.status = child->status;
                c.cpu = child->cpu;
                c.memory = child->memory;
                c.disk = child->disk;
                c.network = child->network;
                c.canEnd = child->canEnd;
                c.endLabel = child->endLabel.isEmpty() ? QStringLiteral("End task") : child->endLabel;
                rows << c;
            }
        }
    }
    applyRows(rows);
}

void Processes::applyRows(const QList<Row> &next)
{
    QHash<QString, int> position;
    for (int i = 0; i < next.size(); ++i) {
        position.insert(next.at(i).key, i);
    }
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (!position.contains(m_rows.at(i).key)) {
            int first = i;
            while (first > 0 && !position.contains(m_rows.at(first - 1).key)) {
                --first;
            }
            beginRemoveRows({}, first, i);
            m_rows.remove(first, i - first + 1);
            endRemoveRows();
            i = first;
        }
    }
    const bool ordered = std::is_sorted(m_rows.cbegin(), m_rows.cend(), [&position](const Row &a, const Row &b) {
        return position.value(a.key) < position.value(b.key);
    });
    if (!ordered) {
        Q_EMIT layoutAboutToBeChanged();
        QStringList oldKeys;
        for (const Row &row : std::as_const(m_rows)) {
            oldKeys << row.key;
        }
        std::stable_sort(m_rows.begin(), m_rows.end(), [&position](const Row &a, const Row &b) {
            return position.value(a.key) < position.value(b.key);
        });
        QHash<QString, int> newRow;
        for (int r = 0; r < m_rows.size(); ++r) {
            newRow.insert(m_rows.at(r).key, r);
        }
        const QModelIndexList before = persistentIndexList();
        for (const QModelIndex &index : before) {
            changePersistentIndex(index, createIndex(newRow.value(oldKeys.value(index.row())), 0));
        }
        Q_EMIT layoutChanged();
    }
    // What is left is now in the new order, so each run of new rows goes in
    // just before the next row that was already there.
    for (int i = 0; i < next.size(); ++i) {
        if (i < m_rows.size() && m_rows.at(i).key == next.at(i).key) {
            m_rows[i] = next.at(i);
            continue;
        }
        const QString stop = i < m_rows.size() ? m_rows.at(i).key : QString();
        int last = i;
        while (last + 1 < next.size() && (stop.isNull() || next.at(last + 1).key != stop)) {
            ++last;
        }
        beginInsertRows({}, i, last);
        for (int j = i; j <= last; ++j) {
            m_rows.insert(j, next.at(j));
        }
        endInsertRows();
        i = last;
    }
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(m_rows.size() - 1));
    }
}

int Processes::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant Processes::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case KeyRole:
        return row.key;
    case KindRole:
        return row.kind;
    case DepthRole:
        return row.depth;
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case IconRole:
        return row.icon;
    case StatusRole:
        return row.status;
    case CpuRole:
        return row.cpu;
    case MemoryRole:
        return double(row.memory);
    case DiskRole:
        return double(row.disk);
    case NetworkRole:
        return double(row.network);
    case ExpandableRole:
        return row.expandable;
    case ExpandedRole:
        return row.expanded;
    case CountRole:
        return row.count;
    case ChipRole:
        return row.chip;
    case CanEndRole:
        return row.canEnd;
    case EndLabelRole:
        return row.endLabel;
    case GroupRole:
        return row.group;
    }
    return {};
}

QHash<int, QByteArray> Processes::roleNames() const
{
    return {
        {KeyRole, "key"},
        {KindRole, "kind"},
        {DepthRole, "depth"},
        {NameRole, "name"},
        {IconRole, "iconName"},
        {StatusRole, "status"},
        {CpuRole, "cpu"},
        {MemoryRole, "memory"},
        {DiskRole, "disk"},
        {NetworkRole, "network"},
        {ExpandableRole, "expandable"},
        {ExpandedRole, "expanded"},
        {CountRole, "count"},
        {ChipRole, "chip"},
        {CanEndRole, "canEnd"},
        {EndLabelRole, "endLabel"},
        {GroupRole, "group"},
    };
}

void Processes::toggle(const QString &key)
{
    setExpanded(key, !m_expanded.contains(key));
}

void Processes::setExpanded(const QString &key, bool expanded)
{
    if (expanded == m_expanded.contains(key)) {
        return;
    }
    if (expanded) {
        m_expanded.insert(key);
    } else {
        m_expanded.remove(key);
    }
    rebuild();
}

int Processes::indexOf(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).key == key) {
            return i;
        }
    }
    return -1;
}

const Processes::Item *Processes::find(const QString &key) const
{
    for (const Item &item : m_items) {
        if (item.key == key) {
            return &item;
        }
        for (const Item &child : item.children) {
            if (child.key == key) {
                return &child;
            }
        }
    }
    return nullptr;
}

QVariantMap Processes::entry(const QString &key) const
{
    const Item *item = find(key);
    if (!item) {
        return {};
    }
    const bool child = key.contains(QLatin1Char('/'));
    return {
        {QStringLiteral("key"), item->key},
        {QStringLiteral("name"), item->name},
        {QStringLiteral("child"), child},
        {QStringLiteral("winMode"), item->winMode},
        {QStringLiteral("winId"), item->winId},
        {QStringLiteral("canEnd"), item->canEnd},
        {QStringLiteral("endLabel"), item->endLabel.isEmpty() ? QStringLiteral("End task") : item->endLabel},
        {QStringLiteral("group"), item->group},
        {QStringLiteral("expandable"), item->winMode ? !item->children.isEmpty() : item->children.size() > 1},
        {QStringLiteral("expanded"), m_expanded.contains(item->key)},
        {QStringLiteral("processes"), item->pids.size()},
        {QStringLiteral("single"), item->pids.size() == 1},
    };
}

QList<qint64> Processes::treeOf(const QList<qint64> &pids) const
{
    QList<qint64> out = pids;
    QSet<qint64> seen(pids.cbegin(), pids.cend());
    bool grew = true;
    while (grew) {
        grew = false;
        for (const Proc &p : m_procs) {
            if (!seen.contains(p.pid) && seen.contains(p.ppid)) {
                seen.insert(p.pid);
                out << p.pid;
                grew = true;
            }
        }
    }
    return out;
}

QString Processes::signal(const QList<qint64> &pids, int sig)
{
    if (pids.isEmpty()) {
        return {};
    }
    QList<long long> list;
    for (qint64 pid : pids) {
        list << pid;
    }
    return resultText(m_controller->sendSignal(list, sig));
}

// End task as on Windows: the app is asked to quit, and whatever is still
// running three seconds later is killed, so an app that has stopped
// responding still goes. A suspended process only acts on the request once
// it runs again, so it is woken too.
QString Processes::end(const QList<qint64> &pids)
{
    QList<QPair<qint64, QByteArray>> started;
    for (qint64 pid : pids) {
        started.append({pid, startedAt(pid)});
    }
    const QString error = signal(pids, SIGTERM);
    if (!error.isEmpty()) {
        return error;
    }
    signal(pids, SIGCONT);
    QTimer::singleShot(3000, this, [this, started] {
        QList<qint64> left;
        for (const auto &[pid, at] : started) {
            if (!at.isEmpty() && startedAt(pid) == at) {
                left << pid;
            }
        }
        signal(left, SIGKILL);
    });
    return {};
}

QString Processes::endTask(const QString &key)
{
    const Item *item = find(key);
    if (!item || !item->canEnd) {
        return {};
    }
    if (item->restart) {
        // The desktop runs as a user service; restarting the unit brings it
        // straight back, as Windows does with Explorer.
        const QDBusMessage reply = systemdUser(QStringLiteral("RestartUnit"), {QStringLiteral("plasma-plasmashell.service"), QStringLiteral("replace")});
        if (reply.type() != QDBusMessage::ErrorMessage) {
            return {};
        }
    }
    return end(item->pids);
}

QString Processes::endTree(const QString &key)
{
    const Item *item = find(key);
    if (!item || !item->canEnd || item->winMode) {
        return endTask(key);
    }
    return end(treeOf(item->pids));
}

QString Processes::setPriority(const QString &key, int nice)
{
    const Item *item = find(key);
    if (!item || item->pids.isEmpty()) {
        return {};
    }
    QList<long long> list;
    for (qint64 pid : item->pids) {
        list << pid;
    }
    return resultText(m_controller->setPriority(list, nice));
}

void Processes::openFileLocation(const QString &key)
{
    const Item *item = find(key);
    if (!item || item->pids.isEmpty()) {
        return;
    }
    const QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(item->pids.constFirst())).symLinkTarget();
    if (exe.isEmpty()) {
        return;
    }
    QProcess::startDetached(QStringLiteral("wildos-files"), {QStringLiteral("--select"), exe});
}

QVariantList Processes::top(const QString &metric, int count) const
{
    QList<const Item *> items;
    for (const Item &item : m_items) {
        items << &item;
    }
    const auto valueOf = [&metric](const Item *item) -> double {
        if (metric == QLatin1String("cpu")) {
            return item->cpu;
        }
        if (metric == QLatin1String("memory")) {
            return double(item->memory);
        }
        if (metric == QLatin1String("disk")) {
            return double(item->disk);
        }
        if (metric == QLatin1String("network")) {
            return double(item->network);
        }
        if (metric == QLatin1String("gpu")) {
            return item->gpu;
        }
        if (metric == QLatin1String("winmode")) {
            return item->winMode ? double(item->memory) : -1;
        }
        return 0;
    };
    // Windows apps are ranked one by one on the Windows mode resource.
    if (metric == QLatin1String("winmode")) {
        items.clear();
        for (const Item &item : m_items) {
            if (item.winMode) {
                for (const Item &child : item.children) {
                    items << &child;
                }
            }
        }
    }
    std::sort(items.begin(), items.end(), [&valueOf](const Item *a, const Item *b) {
        return valueOf(a) > valueOf(b);
    });
    QVariantList out;
    for (const Item *item : std::as_const(items)) {
        if (out.size() >= count || valueOf(item) <= 0) {
            break;
        }
        out << QVariantMap{
            {QStringLiteral("key"), item->key},
            {QStringLiteral("name"), item->name},
            {QStringLiteral("icon"), item->icon},
            {QStringLiteral("value"), valueOf(item)},
        };
    }
    return out;
}
