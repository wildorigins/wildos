#include "updates.h"
#include "securetime.h"

#include <algorithm>
#include <ctime>

#include <KService>

#include <NetworkManagerQt/Manager>
#include <PackageKit/Daemon>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

using PackageKit::Daemon;
using PackageKit::Transaction;

namespace
{
// How long a check or install waits for another install to let go of the
// package system, and how often it tries again meanwhile. Automatic
// updates of a big batch can take a quarter of an hour.
constexpr qint64 lockPatienceMs = 30 * 60 * 1000;
constexpr int lockRetryMs = 15 * 1000;

// A check that fails because the connection is not ready yet tries again,
// first after a few seconds and then less often, for about three minutes
// before it says why. Tor alone can take that long after start-up.
constexpr qint64 quietRetryMs = 3 * 60 * 1000;
constexpr int firstRetryMs = 5 * 1000;
constexpr int longestRetryMs = 60 * 1000;
// In the first minutes after start-up any failure is treated so: the
// network, Tor and the clock (sdwdate may move it) are all still settling.
constexpr qint64 settlingSeconds = 5 * 60;
// A clock that sdwdate has yet to set is waited for this long at most,
// looking every few seconds whether it has. Its first run after start-up
// took four minutes on the PC this was seen on, with two onion sites
// timing out.
constexpr qint64 clockWaitMs = 15 * 60 * 1000;
constexpr int clockPollMs = 2000;

// Where Settings remembers, until the PC restarts, that an update asked for
// a restart: the runtime folder is emptied at every boot.
QString restartMarker()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-restart-needed");
}

// What the update that asked for a restart brings, for the restart prompt
// (wildos-system-dialogs), gone with the restart like the marker above.
QString summaryMarker()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-update-summary");
}

// The last notes fetched, so Settings still has them offline.
QString notesCache()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos/update-notes.json");
}

// The build a package version was made in: 0.1.0+r261 is build 261. Local
// builds carry no +r and count as 0.
int buildOf(const QString &version)
{
    static const QRegularExpression revision(QStringLiteral("\\+r(\\d+)"));
    const auto match = revision.match(version);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

// The notes come from the update site, so they go the way apt goes there:
// through Tor when Wild OS's source is a tor+https one, through apt's proxy
// when it has one, and straight there otherwise.
QNetworkProxy aptProxy()
{
    QFile sources(QStringLiteral("/etc/apt/sources.list.d/wildos.sources"));
    if (sources.open(QIODevice::ReadOnly) && sources.readAll().contains("tor+http")) {
        // apt-transport-tor's own default.
        return QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), 9050);
    }
    QProcess config;
    config.start(QStringLiteral("apt-config"), {QStringLiteral("dump")});
    if (!config.waitForFinished(3000) || config.exitCode() != 0) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    // apt's settings by key: "https", "https::deb.wildos.io", "http", ...
    QHash<QString, QString> proxies;
    static const QRegularExpression line(QStringLiteral("^Acquire::(https?)::Proxy(::deb\\.wildos\\.io)? \"(.*)\";$"));
    const auto lines = QString::fromUtf8(config.readAllStandardOutput()).split(QLatin1Char('\n'));
    for (const QString &text : lines) {
        const auto match = line.match(text);
        if (match.hasMatch()) {
            proxies.insert(match.captured(1) + match.captured(2), match.captured(3));
        }
    }
    // A proxy for this one host wins over the one for every https site,
    // which wins over the http one apt also uses for https.
    QString chosen;
    for (const char *key : {"https::deb.wildos.io", "https", "http::deb.wildos.io", "http"}) {
        chosen = proxies.value(QString::fromLatin1(key));
        if (!chosen.isEmpty()) {
            break;
        }
    }
    const QUrl url(chosen);
    if (chosen.isEmpty() || chosen.compare(QLatin1String("DIRECT"), Qt::CaseInsensitive) == 0 || !url.isValid() || url.host().isEmpty()) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    const bool socks = url.scheme().startsWith(QLatin1String("socks"));
    return QNetworkProxy(socks ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy, url.host(), quint16(url.port(socks ? 1080 : 8080)),
                         url.userName(), url.password());
}

// Debian rarely says a restart is needed (no /run/reboot-required without
// update-notifier, and PackageKit only for a few packages), so Wild OS
// decides from what was installed: anything that only takes effect at
// start-up or sign-in. Apps pick their update up the next time they open.
bool needsRestart(const QStringList &packageIds)
{
    static const QStringList prefixes = {
        QStringLiteral("linux-image"), QStringLiteral("firmware-"), QStringLiteral("amd64-microcode"), QStringLiteral("intel-microcode"),
        QStringLiteral("systemd"), QStringLiteral("libc6"), QStringLiteral("dbus"), QStringLiteral("dracut"), QStringLiteral("grub"),
        QStringLiteral("plymouth"), QStringLiteral("sddm"), QStringLiteral("kwin"), QStringLiteral("plasma-workspace"),
        QStringLiteral("plasma-desktop"), QStringLiteral("libplasma"), QStringLiteral("libkf6"), QStringLiteral("libqt6"),
        QStringLiteral("qt6-base"), QStringLiteral("qt-style-kvantum"), QStringLiteral("qt6-style-kvantum"), QStringLiteral("mesa"),
        QStringLiteral("libgl"), QStringLiteral("libegl"), QStringLiteral("xwayland"), QStringLiteral("pipewire"),
        QStringLiteral("wireplumber"), QStringLiteral("security-misc"), QStringLiteral("apparmor"), QStringLiteral("wildos-base"),
        QStringLiteral("wildos-branding"), QStringLiteral("wildos-shell"), QStringLiteral("wildos-ui"), QStringLiteral("wildos-desktop"),
    };
    for (const QString &id : packageIds) {
        const QString name = Transaction::packageName(id);
        for (const QString &prefix : prefixes) {
            if (name.startsWith(prefix)) {
                return true;
            }
        }
    }
    return false;
}

QStringList hostsIn(const QString &path)
{
    QStringList hosts;
    QFile sources(path);
    if (sources.open(QIODevice::ReadOnly)) {
        static const QRegularExpression uri(QStringLiteral("https?://([^/\\s]+)"));
        auto it = uri.globalMatch(QString::fromUtf8(sources.readAll()));
        while (it.hasNext()) {
            const QString host = it.next().captured(1).toLower();
            if (!hosts.contains(host)) {
                hosts << host;
            }
        }
    }
    return hosts;
}

// The servers Wild OS's own packages come from.
QStringList wildosHosts()
{
    QStringList hosts{QStringLiteral("deb.wildos.io")};
    for (const QString &host : hostsIn(QStringLiteral("/etc/apt/sources.list.d/wildos.sources"))) {
        if (!hosts.contains(host)) {
            hosts << host;
        }
    }
    return hosts;
}

// Every server the system itself updates from: Wild OS's, and Debian's and
// Kicksecure's, which Kicksecure lists in debian.sources and the image in
// sources.list, mostly through Tor. Any other source was added by the
// person or by an app's installer (Steam, Chrome, VS Code).
QStringList ownHosts()
{
    QStringList hosts = wildosHosts();
    hosts << QStringLiteral("deb.debian.org") << QStringLiteral("security.debian.org") << QStringLiteral("fasttrack.debian.net")
          << QStringLiteral("deb.kicksecure.com");
    for (const QString &path : {QStringLiteral("/etc/apt/sources.list.d/debian.sources"), QStringLiteral("/etc/apt/sources.list")}) {
        for (const QString &host : hostsIn(path)) {
            if (!hosts.contains(host)) {
                hosts << host;
            }
        }
    }
    return hosts;
}

// apt's lines that name a source, without its notices about sources that
// are fine, such as one without 32-bit packages on a PC with 32-bit support
// turned on.
QStringList sourceLines(const QString &details)
{
    QStringList lines;
    const QStringList all = details.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : all) {
        if (!line.contains(QLatin1String("Skipping acquire")) && !line.contains(QLatin1String("support architecture"))) {
            lines << line;
        }
    }
    return lines;
}

bool aboutSigning(const QString &line)
{
    static const QStringList words = {
        QStringLiteral("signature"), QStringLiteral("not signed"), QStringLiteral("NO_PUBKEY"),   QStringLiteral("EXPKEYSIG"),
        QStringLiteral("Signing key"), QStringLiteral("OpenPGP"),  QStringLiteral("not valid yet"), QStringLiteral("is expired"),
    };
    return std::any_of(words.cbegin(), words.cend(), [&line](const QString &word) {
        return line.contains(word, Qt::CaseInsensitive);
    });
}

// A signature from after the time the clock shows. Right after start-up
// sdwdate can set the clock back a minute or two, and a source signed in
// that gap (Wild OS's, right after a release) fails until the clock passes
// the signing time again. Seen as sqv's "created after the --not-after
// date" and "Not live until <time>"; the second is also what a clock hours
// behind gives (a PC whose hardware clock kept local time for Windows).
bool signedLater(const QString &line)
{
    return line.contains(QLatin1String("not-after")) || line.contains(QLatin1String("created after"))
        || line.contains(QLatin1String("not valid yet"), Qt::CaseInsensitive) || line.contains(QLatin1String("not live until"), Qt::CaseInsensitive);
}

bool clockProblemIn(const QString &details)
{
    const QStringList lines = sourceLines(details);
    return std::any_of(lines.cbegin(), lines.cend(), signedLater);
}

// apt reaches Debian through Tor's SOCKS port; a Tor that is not running
// or has not finished connecting fails every one of those sources.
bool throughTor(const QString &line)
{
    return line.contains(QLatin1String("tor+http")) || line.contains(QLatin1String(":9050"));
}

bool offline()
{
    switch (NetworkManager::status()) {
    case NetworkManager::Asleep:
    case NetworkManager::Disconnected:
    case NetworkManager::Disconnecting:
    case NetworkManager::ConnectedLinkLocal:
        return true;
    default:
        return false;
    }
}

// Since start-up finished, not since the kernel started: the wait at the
// disk password comes before Tor, sdwdate and the network even start.
qint64 secondsSinceStart()
{
    auto get = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get << QStringLiteral("org.freedesktop.systemd1.Manager") << QStringLiteral("FinishTimestampMonotonic");
    const QDBusMessage reply = QDBusConnection::systemBus().call(get, QDBus::Block, 3000);
    const qulonglong finished = reply.type() == QDBusMessage::ReplyMessage ? reply.arguments().value(0).value<QDBusVariant>().variant().toULongLong() : 0;
    timespec now{};
    if (finished > 0 && clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        const qint64 nowUs = qint64(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
        return std::max<qint64>(0, (nowUs - qint64(finished)) / 1000000);
    }
    QFile uptime(QStringLiteral("/proc/uptime"));
    if (!uptime.open(QIODevice::ReadOnly)) {
        return -1;
    }
    return qint64(uptime.readAll().split(' ').value(0).toDouble());
}

// The check result Settings shares with the taskbar's Updates icon
// (/usr/libexec/wildos/update-status), which reads and writes the same
// files: "checking" names the process running a check (its pid, then its
// name, then "clock N" while it waits for sdwdate to set a clock at least N
// seconds behind), "failed" holds the last failed check (when the lists had
// last been refreshed as it failed, the reasons one per line, an empty line,
// then PackageKit's and apt's details). The failure stands until a refresh is
// newer than that, which holds however the clock moves: right after
// start-up sdwdate may set it back. In the runtime folder, so a failure is
// forgotten at the next start-up.
QString sharedDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-updates");
}

QString checkingFile()
{
    return sharedDir() + QStringLiteral("/checking");
}

QString failedFile()
{
    return sharedDir() + QStringLiteral("/failed");
}

// When the list was last refreshed by a check from Settings or the
// taskbar's icon, including one PackageKit does not record because it only
// missed sources added by other software.
QString checkedFile()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wildos/updates-checked");
}

QString processName(qint64 pid)
{
    QFile comm(pid > 0 ? QStringLiteral("/proc/%1/comm").arg(pid) : QStringLiteral("/proc/self/comm"));
    return comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.readAll()).trimmed() : QString();
}

QStringList fileLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
}

void writeLines(const QString &path, const QStringList &lines)
{
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
        file.commit();
    }
}

// Another process running a check now: the taskbar's icon. One that quit
// or crashed no longer counts.
bool otherChecking()
{
    const QStringList lines = fileLines(checkingFile());
    const qint64 pid = lines.value(0).toLongLong();
    return pid > 0 && pid != QCoreApplication::applicationPid() && !lines.value(1).isEmpty() && processName(pid) == lines.value(1);
}

// While a check waits for sdwdate to set the clock, its checking file has a
// third line, "clock" and how many seconds behind the clock was at least.
QStringList checkingLines(qint64 clockBehind)
{
    QStringList lines{QString::number(QCoreApplication::applicationPid()), processName(0)};
    if (clockBehind > 0) {
        lines << QStringLiteral("clock %1").arg(clockBehind);
    }
    return lines;
}

qint64 sharedClockWait()
{
    const QString line = fileLines(checkingFile()).value(2);
    return line.startsWith(QLatin1String("clock ")) ? line.mid(6).toLongLong() : 0;
}

struct SharedFailure {
    qint64 refreshedBefore = 0;
    QStringList reasons;
    QString details;
};

SharedFailure sharedFailure()
{
    SharedFailure failure;
    const QStringList lines = fileLines(failedFile());
    failure.refreshedBefore = lines.value(0).toLongLong();
    int i = 1;
    for (; i < lines.size() && !lines.at(i).isEmpty(); ++i) {
        failure.reasons << lines.at(i);
    }
    failure.details = lines.mid(i + 1).join(QLatin1Char('\n')).trimmed();
    return failure;
}

qint64 mtimeOf(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? info.lastModified().toSecsSinceEpoch() : 0;
}

// The last refresh by anyone, as the taskbar's icon reads it: apt's daily
// one leaves its stamp and the lists' times, a check from Settings or the
// icon leaves the checked file.
qint64 lastRefreshOnDisk()
{
    qint64 at = std::max(mtimeOf(QStringLiteral("/var/lib/apt/periodic/update-success-stamp")), fileLines(checkedFile()).value(0).toLongLong());
    const QFileInfoList lists = QDir(QStringLiteral("/var/lib/apt/lists")).entryInfoList({QStringLiteral("*Release")}, QDir::Files);
    for (const QFileInfo &list : lists) {
        at = std::max(at, list.lastModified().toSecsSinceEpoch());
    }
    return at;
}

// The sources a refresh could not use, read from apt's messages, which
// PackageKit passes on as the error's details: each host, and whether it
// failed on its signature rather than on the connection. apt checks every
// other source all the same.
QList<QPair<QString, bool>> failedSources(const QString &details)
{
    static const QRegularExpression address(QStringLiteral("(?:tor\\+)?https?://([^/\\s'\"]+)"));
    QList<QPair<QString, bool>> failed;
    const QStringList lines = sourceLines(details);
    for (const QString &line : lines) {
        const auto match = address.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString host = match.captured(1).toLower();
        const bool key = aboutSigning(line);
        auto existing = std::find_if(failed.begin(), failed.end(), [&host](const auto &entry) {
            return entry.first == host;
        });
        if (existing == failed.end()) {
            failed.append({host, key});
        } else if (key) {
            existing->second = true;
        }
    }
    return failed;
}

QString skippedText(const QList<QPair<QString, bool>> &failed)
{
    const auto why = [](bool key) {
        return key ? QStringLiteral("its signing key was not accepted") : QStringLiteral("it could not be reached");
    };
    if (failed.size() == 1) {
        return QStringLiteral("Everything else was checked, but not %1: %2. It was added by other software, so its updates wait until that is fixed or the source is removed.")
            .arg(failed.first().first, why(failed.first().second));
    }
    QStringList parts;
    for (const auto &entry : failed) {
        parts << QStringLiteral("%1 (%2)").arg(entry.first, why(entry.second));
    }
    const QString last = parts.takeLast();
    return QStringLiteral("Everything else was checked, but not %1 or %2. They were added by other software, so their updates wait until that is fixed or the sources are removed.")
        .arg(parts.join(QStringLiteral(", ")), last);
}
}


Updates::Updates(QObject *parent)
    : QObject(parent)
{
    // Lets polkit ask for the password. Without this hint PackageKit
    // marks every request as non-interactive and polkit refuses at once,
    // so installs failed with "needs the administrator password" and no
    // prompt ever appeared.
    PackageKit::Daemon::global()->setHints(QStringList{QStringLiteral("interactive=true")});
    m_restartNeeded = QFileInfo::exists(QStringLiteral("/run/reboot-required")) || QFileInfo::exists(restartMarker());
    m_network = new QNetworkAccessManager(this);
    readInstalledBuild();
    QFile cache(notesCache());
    if (cache.open(QIODevice::ReadOnly)) {
        readNotes(cache.readAll());
    }
    if (available()) {
        // A check started or finished on the taskbar's Updates icon shows
        // here at once, and one that stopped without a word (the icon's
        // shell restarted) is noticed within seconds.
        QDir().mkpath(sharedDir());
        m_sharedWatch = new QFileSystemWatcher({sharedDir()}, this);
        connect(m_sharedWatch, &QFileSystemWatcher::directoryChanged, this, &Updates::applyShared);
        m_followTimer = new QTimer(this);
        m_followTimer->setInterval(3000);
        connect(m_followTimer, &QTimer::timeout, this, &Updates::applyShared);
        readLastChecked();
        listUpdates();
        loadHistory();
        fetchNotes();
    }
}

bool Updates::available() const
{
    auto bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        return false;
    }
    return bus->isServiceRegistered(QStringLiteral("org.freedesktop.PackageKit"))
        || bus->activatableServiceNames().value().contains(QStringLiteral("org.freedesktop.PackageKit"));
}

QString Updates::clockBehind() const
{
    return m_clockBehind >= 60 ? SecureTime::durationText(m_clockBehind) : QString();
}

int Updates::securityCount() const
{
    int count = 0;
    for (const QVariant &update : m_updates) {
        if (update.toMap().value(QStringLiteral("security")).toBool()) {
            ++count;
        }
    }
    return count;
}

void Updates::setState(const QString &state, const QString &error, bool checkFailed)
{
    m_state = state;
    m_error = error;
    m_checkFailed = checkFailed;
    if (!checkFailed) {
        m_errorDetails.clear();
    }
    m_clockProblem = checkFailed && state == QLatin1String("error") && clockProblemIn(m_errorDetails);
    Q_EMIT stateChanged();
}

void Updates::readLastChecked()
{
    auto watcher = new QDBusPendingCallWatcher(Daemon::getTimeSinceAction(Transaction::RoleRefreshCache), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<uint> reply = *watcher;
        // PackageKit answers with the largest uint when it has never checked.
        if (!reply.isError() && reply.value() < 10u * 365 * 24 * 3600) {
            m_lastChecked = QDateTime::currentDateTime().addSecs(-qint64(reply.value()));
        } else {
            m_lastChecked = {};
        }
        // The same time the taskbar's icon shows, which also counts apt's
        // daily refresh and checks PackageKit did not record.
        const qint64 onDisk = lastRefreshOnDisk();
        if (onDisk > 0 && (!m_lastChecked.isValid() || onDisk > m_lastChecked.toSecsSinceEpoch())) {
            m_lastChecked = QDateTime::fromSecsSinceEpoch(onDisk);
        }
        applyShared();
        Q_EMIT lastCheckedChanged();
    });
}

void Updates::watchProgress(Transaction *transaction)
{
    m_progress = 0;
    m_detail.clear();
    Q_EMIT progressChanged();
    connect(transaction, &Transaction::percentageChanged, this, [this, transaction] {
        // 101 is PackageKit's "unknown".
        const uint pct = transaction->percentage();
        m_progress = pct > 100 ? 0 : int(pct);
        Q_EMIT progressChanged();
    });
    connect(transaction, &Transaction::statusChanged, this, [this, transaction] {
        setWaiting(transaction->status() == Transaction::StatusWaitingForLock);
    });
    connect(transaction, &Transaction::itemProgress, this, [this](const QString &itemID, Transaction::Status, uint) {
        m_detail = Transaction::packageName(itemID);
        Q_EMIT progressChanged();
    });
    connect(transaction, &Transaction::requireRestart, this, [this](Transaction::Restart type, const QString &) {
        if (type == Transaction::RestartSystem || type == Transaction::RestartSecuritySystem) {
            m_restartNeeded = true;
            Q_EMIT restartNeededChanged();
        }
    });
}


void Updates::setWaiting(bool waiting)
{
    if (m_waiting == waiting) {
        return;
    }
    m_waiting = waiting;
    if (waiting) {
        // What the last attempt reached means nothing while waiting.
        m_progress = 0;
        m_detail.clear();
    }
    Q_EMIT progressChanged();
}

// apt's lock is held for minutes at a time by the automatic security
// updates (unattended-upgrades) and apt's daily refresh, often in the first
// minutes after start-up, and PackageKit gives up on it after ten seconds.
// So a lock error is waited out here, as the person would otherwise have to
// by hand, until lockPatienceMs has passed.
bool Updates::lockHeld(Transaction::Error error)
{
    if (error != Transaction::ErrorCannotGetLock && error != Transaction::ErrorLockRequired) {
        return false;
    }
    if (!m_lockWait.isValid()) {
        m_lockWait.start();
    }
    if (m_lockWait.elapsed() > lockPatienceMs) {
        return false;
    }
    m_lockBlocked = true;
    return true;
}

void Updates::retryAfterLock(const std::function<void()> &retry)
{
    m_lockBlocked = false;
    setWaiting(true);
    QTimer::singleShot(lockRetryMs, this, retry);
}

void Updates::check()
{
    if (m_state == QLatin1String("checking") || m_state == QLatin1String("installing")) {
        return;
    }
    // One check at a time: when the taskbar's icon is already checking,
    // its result is this one's too.
    if (otherChecking()) {
        applyShared();
        return;
    }
    m_lockWait.invalidate();
    m_quietRetry.invalidate();
    m_clockWait.invalidate();
    m_retryDelayMs = firstRetryMs;
    startOwnCheck();
    setState(QStringLiteral("checking"));
    refreshForCheck();
}

void Updates::startOwnCheck()
{
    m_ownCheck = true;
    m_following = false;
    if (m_followTimer) {
        m_followTimer->stop();
    }
    writeLines(checkingFile(), checkingLines(0));
}

// The result goes where the taskbar's icon reads it before the check is
// marked as done, so it never shows the state from before the check.
void Updates::finishOwnCheck(const QStringList &reasons, const QString &details)
{
    if (reasons.isEmpty()) {
        QFile::remove(failedFile());
    } else {
        writeLines(failedFile(), QStringList{QString::number(lastRefreshOnDisk())} + reasons + QStringList{QString()} + details.split(QLatin1Char('\n')));
    }
    QFile::remove(checkingFile());
    m_ownCheck = false;
}

// Shows what the taskbar's icon found: its check while it runs, then its
// result, so Settings and the icon always say the same thing. Settings'
// own check and installs are left alone.
void Updates::applyShared()
{
    if (m_ownCheck || m_state == QLatin1String("installing")) {
        return;
    }
    if (otherChecking()) {
        m_following = true;
        if (m_state != QLatin1String("checking")) {
            m_skipped.clear();
            setState(QStringLiteral("checking"));
        }
        setClockBehind(sharedClockWait());
        if (m_followTimer) {
            m_followTimer->start();
        }
        return;
    }
    if (m_followTimer) {
        m_followTimer->stop();
    }
    setClockBehind(0);
    const bool followed = m_following;
    m_following = false;
    if (m_state == QLatin1String("installed")) {
        return;
    }
    const SharedFailure failure = sharedFailure();
    // Nothing has refreshed the lists since the check failed.
    if (!failure.reasons.isEmpty() && lastRefreshOnDisk() <= failure.refreshedBefore) {
        const QString reason = failure.reasons.join(QLatin1Char('\n'));
        if (m_state != QLatin1String("error") || !m_checkFailed || m_error != reason) {
            m_errorDetails = failure.details;
            setState(QStringLiteral("error"), reason, true);
        }
        return;
    }
    if (followed || (m_state == QLatin1String("error") && m_checkFailed)) {
        readLastChecked();
        listUpdates();
    }
}

// A source someone else added (Steam's, with a signing key Debian no
// longer accepts) fails the whole refresh in PackageKit, though apt
// refreshed every other source. That is no reason to say Wild OS could not
// check: the check counts, and the page names what was skipped. Only Wild
// OS's own server failing is an error.
bool Updates::onlyOthersFailed(const QString &details)
{
    const auto failed = failedSources(details);
    if (failed.isEmpty()) {
        return false;
    }
    const QStringList own = ownHosts();
    for (const auto &entry : failed) {
        if (own.contains(entry.first)) {
            return false;
        }
    }
    m_skipped = skippedText(failed);
    return true;
}

void Updates::refreshForCheck()
{
    // Someone asked to check: fetch the lists again even if they are recent.
    m_skipped.clear();
    m_partialRefresh = false;
    m_checkErrored = false;
    // With no connection at all a refresh waits minutes on Tor before it
    // fails, so it is not started.
    if (offline()) {
        checkFailedWith(Transaction::ErrorNoNetwork, QString());
        return;
    }
    Transaction *refresh = Daemon::refreshCache(true);
    watchProgress(refresh);
    connect(refresh, &Transaction::errorCode, this, [this](Transaction::Error error, const QString &details) {
        if (lockHeld(error)) {
            return;
        }
        if (onlyOthersFailed(details)) {
            m_partialRefresh = true;
            return;
        }
        m_checkErrored = true;
        m_checkError = error;
        m_checkDetails = details;
    });
    connect(refresh, &Transaction::finished, this, [this](Transaction::Exit status, uint) {
        if (m_lockBlocked) {
            retryAfterLock([this] {
                refreshForCheck();
            });
            return;
        }
        setWaiting(false);
        // PackageKit records only a refresh with no failures, so a partial
        // one is timed here.
        if (m_partialRefresh && !m_checkErrored) {
            m_lastChecked = QDateTime::currentDateTime();
            Q_EMIT lastCheckedChanged();
            status = Transaction::ExitSuccess;
        } else {
            readLastChecked();
        }
        if (status == Transaction::ExitSuccess && !m_checkErrored) {
            writeLines(checkedFile(), {QString::number(QDateTime::currentSecsSinceEpoch())});
            finishOwnCheck();
            listUpdates();
            fetchNotes();
        } else if (status == Transaction::ExitCancelled) {
            finishOwnCheck({QStringLiteral("The check was cancelled.")});
            setState(QStringLiteral("error"), QStringLiteral("The check was cancelled."), true);
        } else {
            checkFailedWith(m_checkErrored ? m_checkError : Transaction::ErrorUnknown, m_checkErrored ? m_checkDetails : QString());
        }
    });
}

void Updates::checkFailedWith(Transaction::Error error, const QString &details)
{
    const char *name = QMetaEnum::fromType<Transaction::Error>().valueToKey(error);
    const QString code = QStringLiteral("PackageKit %1 (%2)").arg(name ? QString::fromLatin1(name) : QStringLiteral("Error"), QString::number(int(error)));
    // In the journal too, since PackageKit's own lines only say "failed".
    qWarning().noquote() << "Update check failed:" << code << details.trimmed();
    if (retryQuietly(error, details)) {
        return;
    }
    m_errorDetails = details.trimmed().isEmpty() ? code : code + QLatin1Char('\n') + details.trimmed();
    const QString reason = whyCheckFailed(error, details);
    finishOwnCheck(reason.split(QLatin1Char('\n')), m_errorDetails);
    setState(QStringLiteral("error"), reason, true);
}

// Whether a failed check is worth trying again by itself: in the first
// minutes after start-up, or when the connection, Tor or the clock was not
// ready (the taskbar's icon decides the same in update-status), a
// minute later usually works, as it did on the PC this was seen on. A
// refusal, a full disk or a bad signature after that will not change.
bool Updates::retryQuietly(Transaction::Error error, const QString &details)
{
    if (error == Transaction::ErrorNotAuthorized || error == Transaction::ErrorTransactionCancelled || error == Transaction::ErrorNoSpaceOnDevice
        || error == Transaction::ErrorCannotGetLock || error == Transaction::ErrorLockRequired) {
        return false;
    }
    const QStringList lines = sourceLines(details);
    const bool settling = secondsSinceStart() >= 0 && secondsSinceStart() < settlingSeconds;
    // No connection at all long after start-up is said at once: waiting
    // will not bring one.
    if (offline() && !settling) {
        return false;
    }
    // apt's lines for a signature from the future include one that only
    // says verification failed, so one line naming the time is enough, and
    // PackageKit may still call it a signing error.
    const bool clockBehind = std::any_of(lines.cbegin(), lines.cend(), signedLater);
    // A clock hours behind stays so until sdwdate sets it through Tor, so
    // the check waits for that rather than for a fixed time.
    if (clockBehind && SecureTime::settingNow()) {
        if (!m_clockWait.isValid()) {
            m_clockWait.start();
        }
        if (m_clockWait.elapsed() < clockWaitMs) {
            waitForClock(SecureTime::secondsBehind(details));
            return true;
        }
    }
    const bool signing = !clockBehind
        && (error == Transaction::ErrorGpgFailure || error == Transaction::ErrorBadGpgSignature || error == Transaction::ErrorMissingGpgSignature
            || error == Transaction::ErrorCannotUpdateRepoUnsigned || std::any_of(lines.cbegin(), lines.cend(), aboutSigning));
    const bool connection = error == Transaction::ErrorNoNetwork || error == Transaction::ErrorNoCache || error == Transaction::ErrorRepoNotAvailable
        || error == Transaction::ErrorCannotFetchSources || error == Transaction::ErrorPackageDownloadFailed
        || error == Transaction::ErrorNoMoreMirrorsToTry || std::any_of(lines.cbegin(), lines.cend(), throughTor) || offline() || clockBehind;
    if (!settling && (signing || !connection)) {
        return false;
    }
    if (!m_quietRetry.isValid()) {
        m_quietRetry.start();
    }
    if (m_quietRetry.elapsed() + m_retryDelayMs > quietRetryMs) {
        return false;
    }
    QTimer::singleShot(m_retryDelayMs, this, [this] {
        if (m_state == QLatin1String("checking")) {
            refreshForCheck();
        }
    });
    m_retryDelayMs = std::min(m_retryDelayMs * 2, longestRetryMs);
    return true;
}

void Updates::setClockBehind(qint64 seconds)
{
    if (m_clockBehind == seconds) {
        return;
    }
    m_clockBehind = seconds;
    Q_EMIT progressChanged();
}

void Updates::waitForClock(qint64 behind)
{
    // At least a second, so the page says it is waiting even when every
    // signing time named has just passed.
    setClockBehind(std::max<qint64>(behind, 1));
    writeLines(checkingFile(), checkingLines(m_clockBehind));
    if (!m_clockTimer) {
        m_clockTimer = new QTimer(this);
        m_clockTimer->setInterval(clockPollMs);
        connect(m_clockTimer, &QTimer::timeout, this, [this] {
            if (m_state == QLatin1String("checking") && SecureTime::settingNow() && m_clockWait.elapsed() < clockWaitMs) {
                return;
            }
            m_clockTimer->stop();
            setClockBehind(0);
            if (m_state != QLatin1String("checking")) {
                return;
            }
            writeLines(checkingFile(), checkingLines(0));
            // The usual quiet retries start over: sdwdate's first setting
            // can leave the clock a moment before a source signed just now.
            m_quietRetry.invalidate();
            m_retryDelayMs = firstRetryMs;
            refreshForCheck();
        });
    }
    m_clockTimer->start();
}

// Why a check failed, one line for each likely cause, from PackageKit's
// error and the sources apt named.
QString Updates::whyCheckFailed(Transaction::Error error, const QString &details)
{
    switch (error) {
    case Transaction::ErrorNotAuthorized:
        return errorText(error, false);
    case Transaction::ErrorCannotGetLock:
    case Transaction::ErrorLockRequired:
        return QStringLiteral("Another install is still running, such as the automatic security updates. Try again when it has finished.");
    case Transaction::ErrorNoSpaceOnDevice:
        return QStringLiteral("There is not enough free space to check for updates.");
    default:
        break;
    }
    if (offline() || error == Transaction::ErrorNoNetwork) {
        return QStringLiteral("This computer is not connected to the internet.");
    }
    static const QRegularExpression address(QStringLiteral("(?:tor\\+)?https?://([^/\\s'\"]+)"));
    // A clock far behind fails every source, so it is the one reason given.
    const qint64 behind = clockProblemIn(details) ? SecureTime::secondsBehind(details) : 0;
    if (behind >= 60) {
        const QString clock = QStringLiteral("The clock on this computer is at least %1 behind, so the updates look as if they were signed in the future.")
                                  .arg(SecureTime::durationText(behind));
        return clock + QLatin1Char('\n')
            + (SecureTime::settingNow()
                   ? QStringLiteral("Wild OS sets the clock through Tor after start-up and has not managed to yet. Set the date and time in Settings > Date and time, then try again.")
                   : QStringLiteral("Set the date and time in Settings > Date and time, then try again."));
    }
    const QStringList wildos = wildosHosts();
    const QStringList own = ownHosts();
    bool wildosDown = false;
    bool debianDown = false;
    bool torDown = false;
    bool signing = error == Transaction::ErrorGpgFailure || error == Transaction::ErrorBadGpgSignature
        || error == Transaction::ErrorMissingGpgSignature || error == Transaction::ErrorCannotUpdateRepoUnsigned;
    const QStringList lines = sourceLines(details);
    // Every server reached directly failing to resolve is no internet at
    // all, though NetworkManager still has a connection (a router with no
    // line behind it).
    QSet<QString> unresolved;
    bool failedOtherwise = false;
    for (const QString &line : lines) {
        const auto match = address.match(line);
        if (!match.hasMatch() || throughTor(line)) {
            continue;
        }
        if (line.contains(QLatin1String("Temporary failure resolving")) || line.contains(QLatin1String("Could not resolve"))) {
            unresolved.insert(match.captured(1).toLower());
        } else {
            failedOtherwise = true;
        }
    }
    if (unresolved.size() >= 2 && !failedOtherwise) {
        return QStringLiteral("This computer is not connected to the internet.");
    }
    for (const QString &line : lines) {
        const auto match = address.match(line);
        const QString host = match.hasMatch() ? match.captured(1).toLower() : QString();
        if (aboutSigning(line)) {
            signing = signing || host.isEmpty() || own.contains(host);
        } else if (throughTor(line)) {
            torDown = true;
        } else if (wildos.contains(host)) {
            wildosDown = true;
        } else if (own.contains(host)) {
            debianDown = true;
        }
    }
    QStringList reasons;
    if (wildosDown) {
        reasons << QStringLiteral("Wild OS's update server could not be reached.");
    }
    if (torDown) {
        // Long after start-up, waiting for Tor is no longer the likely fix.
        const qint64 up = secondsSinceStart();
        reasons << (up >= 0 && up < 2 * settlingSeconds
                        ? QStringLiteral("Debian's updates could not be reached through Tor. Tor can take a few minutes to connect after start-up.")
                        : QStringLiteral("Debian's updates could not be reached through Tor."));
    } else if (debianDown) {
        reasons << QStringLiteral("Debian's update servers could not be reached.");
    }
    if (signing) {
        reasons << QStringLiteral("The updates' signatures could not be checked. This happens when the date and time on this computer are wrong.");
    }
    if (reasons.isEmpty()) {
        reasons << (error == Transaction::ErrorNoCache || error == Transaction::ErrorRepoNotAvailable || error == Transaction::ErrorCannotFetchSources
                            || error == Transaction::ErrorPackageDownloadFailed
                        ? QStringLiteral("The update servers could not be reached. Check your internet connection.")
                        : QStringLiteral("Something went wrong while checking. Try again in a few minutes."));
    }
    return reasons.join(QLatin1Char('\n'));
}

void Updates::listUpdates()
{
    readUpdates(false);
}

// quiet: only the list changes, so an error on show stays, unless nothing
// is left to install.
void Updates::readUpdates(bool quiet)
{
    auto collected = std::make_shared<QVariantList>();
    auto ids = std::make_shared<QStringList>();
    // An install that runs again after a wait keeps showing as installing.
    if (!quiet && m_state != QLatin1String("checking") && m_state != QLatin1String("installing")) {
        setState(QStringLiteral("checking"));
    }
    Transaction *list = Daemon::getUpdates();
    connect(list, &Transaction::package, this, [collected, ids](Transaction::Info info, const QString &packageID, const QString &summary) {
        // Blocked updates are held back on purpose and cannot be installed.
        if (info == Transaction::InfoBlocked) {
            return;
        }
        ids->append(packageID);
        collected->append(QVariantMap{
            {QStringLiteral("name"), Transaction::packageName(packageID)},
            {QStringLiteral("version"), Transaction::packageVersion(packageID)},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("security"), info == Transaction::InfoSecurity || info == Transaction::InfoImportant},
        });
    });
    connect(list, &Transaction::errorCode, this, [this, quiet](Transaction::Error error, const QString &) {
        if (!quiet) {
            setState(QStringLiteral("error"), errorText(error, false));
        }
    });
    connect(list, &Transaction::finished, this, [this, collected, ids, quiet](Transaction::Exit status, uint) {
        if (quiet && status != Transaction::ExitSuccess) {
            return;
        }
        if (status != Transaction::ExitSuccess) {
            m_installAfterList = false;
            if (m_state != QLatin1String("error")) {
                setState(QStringLiteral("error"), QStringLiteral("Wild OS could not read the list of updates."));
            }
            return;
        }
        // Security fixes first, then by name, so the reason to install now is
        // at the top.
        std::sort(collected->begin(), collected->end(), [](const QVariant &a, const QVariant &b) {
            const auto ma = a.toMap();
            const auto mb = b.toMap();
            if (ma.value(QStringLiteral("security")).toBool() != mb.value(QStringLiteral("security")).toBool()) {
                return ma.value(QStringLiteral("security")).toBool();
            }
            return ma.value(QStringLiteral("name")).toString() < mb.value(QStringLiteral("name")).toString();
        });
        m_updates = *collected;
        m_pending = *ids;
        m_pendingBuild = 0;
        for (const QVariant &update : std::as_const(m_updates)) {
            const auto map = update.toMap();
            if (map.value(QStringLiteral("name")).toString().startsWith(QLatin1String("wildos"))) {
                m_pendingBuild = std::max(m_pendingBuild, buildOf(map.value(QStringLiteral("version")).toString()));
            }
        }
        Q_EMIT updatesChanged();
        if (quiet) {
            if (m_updates.isEmpty() && m_state == QLatin1String("error")) {
                setState(QStringLiteral("uptodate"));
            }
            return;
        }
        if (m_installAfterList) {
            m_installAfterList = false;
            if (!m_pending.isEmpty()) {
                startInstall();
                return;
            }
        }
        setState(m_updates.isEmpty() ? QStringLiteral("uptodate") : QStringLiteral("available"));
        // A failed check from the taskbar's icon still stands over the list
        // read at start-up.
        applyShared();
    });
}

void Updates::install()
{
    if (m_pending.isEmpty() || m_state == QLatin1String("installing")) {
        return;
    }
    m_staleRetried = false;
    m_lockWait.invalidate();
    startInstall();
}

void Updates::startInstall()
{
    m_staleRetrying = false;
    setState(QStringLiteral("installing"));
    Transaction *update = Daemon::updatePackages(m_pending, Transaction::TransactionFlagOnlyTrusted);
    watchProgress(update);
    connect(update, &Transaction::errorCode, this, [this](Transaction::Error error, const QString &) {
        if (lockHeld(error)) {
            return;
        }
        // The list being installed came from the last check, and the update
        // server may have moved on since (a newer build replaces the files
        // it named). Checking again fixes that, so it happens once here
        // rather than as an error the person has to retry by hand.
        const bool stale = error == Transaction::ErrorPackageDownloadFailed || error == Transaction::ErrorPackageNotFound
            || error == Transaction::ErrorUpdateNotFound || error == Transaction::ErrorDepResolutionFailed;
        if (stale && !m_staleRetried) {
            m_staleRetrying = true;
            return;
        }
        setState(QStringLiteral("error"), errorText(error, true));
    });
    connect(update, &Transaction::finished, this, [this](Transaction::Exit status, uint) {
        loadHistory();
        if (m_lockBlocked) {
            // Whatever held the lock may have installed some of these
            // itself, so the list is read again before trying.
            retryAfterLock([this] {
                m_installAfterList = true;
                listUpdates();
            });
            return;
        }
        setWaiting(false);
        if (status != Transaction::ExitSuccess && m_staleRetrying) {
            m_staleRetrying = false;
            m_staleRetried = true;
            refreshThenInstall();
            return;
        }
        if (status == Transaction::ExitSuccess) {
            if (needsRestart(m_pending)) {
                QFile marker(restartMarker());
                if (marker.open(QIODevice::WriteOnly)) {
                    marker.close();
                }
                if (!m_restartNeeded) {
                    m_restartNeeded = true;
                    Q_EMIT restartNeededChanged();
                }
            }
            // The restart prompt says what the restart finishes installing.
            const QString brought = needsRestart(m_pending) && m_pendingBuild > m_installedBuild ? summary(m_installedBuild, m_pendingBuild) : QString();
            QSaveFile summaryFile(summaryMarker());
            if (!brought.isEmpty() && summaryFile.open(QIODevice::WriteOnly)) {
                summaryFile.write(brought.toUtf8());
                summaryFile.commit();
            }
            readInstalledBuild();
            m_updates.clear();
            m_pending.clear();
            m_pendingBuild = 0;
            Q_EMIT updatesChanged();
            if (QFileInfo::exists(QStringLiteral("/run/reboot-required")) && !m_restartNeeded) {
                m_restartNeeded = true;
                Q_EMIT restartNeededChanged();
            }
            setState(QStringLiteral("installed"));
        } else {
            if (m_state != QLatin1String("error")) {
                setState(QStringLiteral("error"), status == Transaction::ExitCancelled ? QStringLiteral("Installing was cancelled.")
                                                                                        : QStringLiteral("The updates could not be installed."));
            }
            // Part of the list may have gone in before the failure; what
            // did should not still be listed as waiting.
            readUpdates(true);
        }
    });
}

void Updates::refreshThenInstall()
{
    m_partialRefresh = false;
    Transaction *refresh = Daemon::refreshCache(true);
    watchProgress(refresh);
    connect(refresh, &Transaction::errorCode, this, [this](Transaction::Error error, const QString &details) {
        if (onlyOthersFailed(details)) {
            m_partialRefresh = true;
            return;
        }
        setState(QStringLiteral("error"), errorText(error, true));
    });
    connect(refresh, &Transaction::finished, this, [this](Transaction::Exit status, uint) {
        readLastChecked();
        if (m_partialRefresh && m_state != QLatin1String("error")) {
            status = Transaction::ExitSuccess;
        }
        if (status == Transaction::ExitSuccess) {
            m_installAfterList = true;
            listUpdates();
        } else if (m_state != QLatin1String("error")) {
            setState(QStringLiteral("error"), QStringLiteral("The updates could not be installed."));
        }
    });
}

void Updates::loadHistory()
{
    // PackageKit-Qt listens for the daemon's Transaction signal with a
    // different argument list than PackageKit 1.3 sends, so its history call
    // never reports anything. The history is read over D-Bus directly.
    auto watcher = new QDBusPendingCallWatcher(Daemon::createTransaction(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            return;
        }
        const QString path = reply.value().path();
        QDBusConnection bus = QDBusConnection::systemBus();
        m_historyPath = path;
        m_collected.clear();
        bus.connect(QStringLiteral("org.freedesktop.PackageKit"), path, QStringLiteral("org.freedesktop.PackageKit.Transaction"),
                    QStringLiteral("Transaction"), this,
                    SLOT(onOldTransaction(QDBusObjectPath, QString, bool, uint, uint, QString, uint, QString)));
        bus.connect(QStringLiteral("org.freedesktop.PackageKit"), path, QStringLiteral("org.freedesktop.PackageKit.Transaction"),
                    QStringLiteral("Finished"), this, SLOT(onHistoryFinished(uint, uint)));
        auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PackageKit"), path,
                                                   QStringLiteral("org.freedesktop.PackageKit.Transaction"), QStringLiteral("GetOldTransactions"));
        call << uint(50);
        bus.asyncCall(call);
    });
}

void Updates::onOldTransaction(const QDBusObjectPath &, const QString &timespec, bool succeeded, uint role, uint, const QString &data, uint,
                               const QString &)
{
    const auto r = Transaction::Role(role);
    if (r != Transaction::RoleUpdatePackages && r != Transaction::RoleInstallPackages && r != Transaction::RoleRemovePackages) {
        return;
    }
    // data is one "info<TAB>package-id" line per package touched.
    QStringList names;
    const auto lines = data.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString id = line.section(QLatin1Char('\t'), 1, 1);
        if (!id.isEmpty()) {
            names << Transaction::packageName(id);
        }
    }
    m_collected.append(QVariantMap{
        {QStringLiteral("when"), QDateTime::fromString(timespec, Qt::ISODateWithMs).toLocalTime()},
        {QStringLiteral("kind"), r == Transaction::RoleUpdatePackages ? QStringLiteral("update")
                                 : r == Transaction::RoleInstallPackages ? QStringLiteral("install")
                                                                         : QStringLiteral("remove")},
        {QStringLiteral("succeeded"), succeeded},
        {QStringLiteral("packages"), names},
    });
}

void Updates::onHistoryFinished(uint, uint)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.disconnect(QStringLiteral("org.freedesktop.PackageKit"), m_historyPath, QStringLiteral("org.freedesktop.PackageKit.Transaction"),
                   QStringLiteral("Transaction"), this, SLOT(onOldTransaction(QDBusObjectPath, QString, bool, uint, uint, QString, uint, QString)));
    bus.disconnect(QStringLiteral("org.freedesktop.PackageKit"), m_historyPath, QStringLiteral("org.freedesktop.PackageKit.Transaction"),
                   QStringLiteral("Finished"), this, SLOT(onHistoryFinished(uint, uint)));
    std::sort(m_collected.begin(), m_collected.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("when")).toDateTime() > b.toMap().value(QStringLiteral("when")).toDateTime();
    });
    m_history = m_collected;
    m_collected.clear();
    Q_EMIT historyChanged();
}

void Updates::restart()
{
    // Plasma's own restart path, so open apps get the chance to save.
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                                                  QStringLiteral("logoutAndReboot"));
    QDBusConnection::sessionBus().asyncCall(message);
}

QString Updates::errorText(Transaction::Error error, bool installing)
{
    switch (error) {
    case Transaction::ErrorNoNetwork:
    case Transaction::ErrorNoCache:
    case Transaction::ErrorRepoNotAvailable:
    case Transaction::ErrorCannotFetchSources:
        return QStringLiteral("Wild OS could not reach the update servers. Check your internet connection and try again.");
    case Transaction::ErrorNotAuthorized:
        return installing ? QStringLiteral("Installing needs the administrator password, and it was not given.")
                          : QStringLiteral("Checking needs the administrator password, and it was not given.");
    case Transaction::ErrorPackageDownloadFailed:
        return QStringLiteral("Some updates could not be downloaded. Check your internet connection and try again.");
    case Transaction::ErrorNoSpaceOnDevice:
        return QStringLiteral("There is not enough free space to install the updates.");
    case Transaction::ErrorLockRequired:
    case Transaction::ErrorCannotGetLock:
        return QStringLiteral("Another app is installing software. Try again when it has finished.");
    case Transaction::ErrorTransactionCancelled:
        return QStringLiteral("The update was cancelled.");
    default:
        return QStringLiteral("Something went wrong while updating. Try again in a few minutes.");
    }
}

namespace
{
// Desktop entries by the names a package is likely to carry: the entry's
// own name, its last reverse-DNS part and the program it runs.
const QHash<QString, QString> &appNames()
{
    static const QHash<QString, QString> names = [] {
        QHash<QString, QString> map;
        const auto services = KService::allServices();
        for (const KService::Ptr &service : services) {
            if (service->noDisplay() || service->name().isEmpty()) {
                continue;
            }
            const QString entry = service->desktopEntryName().toLower();
            map.insert(entry, service->name());
            map.insert(entry.section(QLatin1Char('.'), -1), service->name());
            const QString program = service->exec().section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
            if (!program.isEmpty() && !map.contains(program)) {
                map.insert(program, service->name());
            }
        }
        return map;
    }();
    return names;
}

bool startsWithAny(const QString &name, std::initializer_list<const char *> prefixes)
{
    for (const char *prefix : prefixes) {
        if (name.startsWith(QLatin1String(prefix))) {
            return true;
        }
    }
    return false;
}
}

QVariantMap Updates::describe(const QString &package) const
{
    const QString name = package.toLower();
    auto result = [](const char *group, const QString &title = {}) {
        return QVariantMap{{QStringLiteral("group"), QString::fromLatin1(group)}, {QStringLiteral("title"), title}};
    };
    if (name.startsWith(QLatin1String("wildos"))) {
        return result("wildos");
    }
    if (startsWithAny(name, {"brave-browser", "firefox", "chromium", "google-chrome"})) {
        return result("browser");
    }
    if (startsWithAny(name, {"linux-", "firmware-", "intel-microcode", "amd64-microcode", "grub", "shim", "systemd", "udev", "libc6", "libc-bin",
                             "base-files", "dpkg", "apt", "cryptsetup", "sudo", "openssl", "libssl", "initramfs", "dracut", "plymouth", "polkit",
                             "libpam", "openssh", "dbus", "util-linux", "coreutils", "bash", "tzdata", "ca-certificates", "apparmor", "usbguard",
                             "nftables", "iptables", "efibootmgr", "mokutil"})) {
        return result("core");
    }
    const auto &apps = appNames();
    const auto app = apps.constFind(name);
    if (app != apps.constEnd()) {
        return result("app", app.value());
    }
    if (startsWithAny(name, {"plasma", "kwin", "kde", "libkf6", "kf6", "qml6", "qt6", "libqt6", "sddm", "xdg-desktop-portal", "breeze", "kscreen",
                             "powerdevil", "pipewire", "wireplumber", "libpipewire", "pulseaudio", "mesa", "libgl", "libegl", "libgbm", "libdrm",
                             "libvulkan", "xwayland", "libwayland", "fonts-", "kvantum", "libplasma", "libkscreen", "bluedevil", "bluez"})) {
        return result("desktop");
    }
    if (name.startsWith(QLatin1String("lib"))) {
        return result("library");
    }
    return result("other");
}

void Updates::readInstalledBuild()
{
    // A build stamps its number only on the packages it changed, so the
    // highest number among them is the build this computer has.
    QProcess query;
    query.start(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${Status}\t${Version}\n"), QStringLiteral("wildos-*")});
    if (!query.waitForFinished(3000)) {
        return;
    }
    int build = 0;
    const auto lines = QString::fromUtf8(query.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.section(QLatin1Char('\t'), 0, 0).endsWith(QLatin1String(" installed"))) {
            build = std::max(build, buildOf(line.section(QLatin1Char('\t'), 1)));
        }
    }
    if (build != m_installedBuild) {
        m_installedBuild = build;
        Q_EMIT installedBuildChanged();
    }
}

void Updates::fetchNotes()
{
    QNetworkRequest request{QUrl(QStringLiteral("https://deb.wildos.io/notes.json"))};
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    m_network->setProxy(aptProxy());
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        // A failure keeps the notes Settings already has, or none, and the
        // page then reads as it did before there were notes.
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        const QByteArray json = reply->readAll();
        if (readNotes(json)) {
            QDir().mkpath(QFileInfo(notesCache()).path());
            QSaveFile cache(notesCache());
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(json);
                cache.commit();
            }
        }
    });
}

bool Updates::readNotes(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || !doc.object().value(QStringLiteral("builds")).isArray()) {
        return false;
    }
    static const QStringList categories = {QStringLiteral("new"), QStringLiteral("improved"), QStringLiteral("fixed")};
    QVariantList notes;
    const QJsonArray builds = doc.object().value(QStringLiteral("builds")).toArray();
    for (const QJsonValue &value : builds) {
        const QJsonObject build = value.toObject();
        const int number = build.value(QStringLiteral("build")).toInt();
        if (number <= 0) {
            continue;
        }
        QVariantList items;
        const QJsonArray entries = build.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &entry : entries) {
            const QJsonObject item = entry.toObject();
            const QString category = item.value(QStringLiteral("category")).toString();
            const QString text = item.value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty() && categories.contains(category)) {
                items.append(QVariantMap{{QStringLiteral("category"), category}, {QStringLiteral("text"), text}});
            }
        }
        notes.append(QVariantMap{
            {QStringLiteral("build"), number},
            {QStringLiteral("date"), QDateTime::fromString(build.value(QStringLiteral("date")).toString(), Qt::ISODate).toLocalTime()},
            {QStringLiteral("items"), items},
        });
    }
    std::sort(notes.begin(), notes.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("build")).toInt() > b.toMap().value(QStringLiteral("build")).toInt();
    });
    m_notes = notes;
    Q_EMIT notesChanged();
    return true;
}

QVariantList Updates::notesBetween(int after, int upTo) const
{
    QVariantList result;
    for (const QVariant &build : m_notes) {
        const int number = build.toMap().value(QStringLiteral("build")).toInt();
        if (number > after && number <= upTo) {
            result.append(build);
        }
    }
    return result;
}

QString Updates::summary(int after, int upTo) const
{
    QHash<QString, int> counts;
    QSet<QString> seen;
    const QVariantList builds = notesBetween(after, upTo);
    for (const QVariant &build : builds) {
        const QVariantList items = build.toMap().value(QStringLiteral("items")).toList();
        for (const QVariant &entry : items) {
            const auto item = entry.toMap();
            const QString text = item.value(QStringLiteral("text")).toString();
            if (!seen.contains(text)) {
                seen.insert(text);
                counts[item.value(QStringLiteral("category")).toString()] += 1;
            }
        }
    }
    auto count = [](int n, const char *one, const char *many) {
        return QString::number(n) + QLatin1Char(' ') + QString::fromLatin1(n == 1 ? one : many);
    };
    QStringList parts;
    if (const int n = counts.value(QStringLiteral("new"))) {
        parts << count(n, "new feature", "new features");
    }
    if (const int n = counts.value(QStringLiteral("fixed"))) {
        parts << count(n, "fix", "fixes");
    }
    if (const int n = counts.value(QStringLiteral("improved"))) {
        parts << count(n, "improvement", "improvements");
    }
    if (parts.size() <= 1) {
        return parts.join(QString());
    }
    const QString last = parts.takeLast();
    return parts.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
}
