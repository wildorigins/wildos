#include "recovery.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace
{
const QString tool = QStringLiteral("/usr/libexec/wildos/restore-points");
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-recovery");
const QString btrfsIndex = QStringLiteral("/.snapshots/index.json");
const QString ext4Index = QStringLiteral("/var/lib/wildos/restore-points/index.json");
const QString resetState = QStringLiteral("/var/lib/wildos/reset/state.json");
const QString recoveryKernel = QStringLiteral("/boot/wildos-recovery/vmlinuz");
const QString backupDesktop = QStringLiteral("org.wildos.backup.desktop");
}

Recovery::Recovery(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Recovery::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Recovery::refresh);
    refresh();
}

QString Recovery::indexPath() const
{
    if (QFileInfo::exists(btrfsIndex)) {
        return btrfsIndex;
    }
    return ext4Index;
}

void Recovery::refresh()
{
    const QString path = indexPath();
    QFile file(path);
    QJsonObject index;
    if (file.open(QIODevice::ReadOnly)) {
        index = QJsonDocument::fromJson(file.readAll()).object();
    }
    m_available = QFileInfo::exists(tool) && (!index.isEmpty() || QFileInfo::exists(QStringLiteral("/usr/libexec/wildos/settings-recovery")));
    m_backend = index.value(QStringLiteral("backend")).toString();
    m_enabled = index.value(QStringLiteral("enabled")).toBool(true);
    m_keep = index.value(QStringLiteral("keep")).toInt(5);
    m_maxGb = index.value(QStringLiteral("max_gb")).toInt(20);
    m_usedBytes = index.value(QStringLiteral("used_bytes")).toDouble();
    m_restartPending = !index.value(QStringLiteral("pending")).isNull() && !index.value(QStringLiteral("pending")).isUndefined();

    m_points.clear();
    const QJsonArray points = index.value(QStringLiteral("points")).toArray();
    for (const QJsonValue &v : points) {
        const QJsonObject p = v.toObject();
        QVariantMap m;
        m[QStringLiteral("id")] = p.value(QStringLiteral("id")).toInt();
        m[QStringLiteral("label")] = p.value(QStringLiteral("label")).toString();
        m[QStringLiteral("when")] = QDateTime::fromSecsSinceEpoch(qint64(p.value(QStringLiteral("time")).toDouble()));
        m[QStringLiteral("summary")] = p.value(QStringLiteral("summary")).toString();
        m[QStringLiteral("apps")] = p.value(QStringLiteral("apps")).toArray().toVariantList();
        m[QStringLiteral("system")] = p.value(QStringLiteral("system")).toBool();
        m[QStringLiteral("driver")] = p.value(QStringLiteral("driver")).toBool();
        m[QStringLiteral("build")] = p.value(QStringLiteral("build")).toInt();
        m[QStringLiteral("kind")] = p.value(QStringLiteral("kind")).toString();
        m[QStringLiteral("holdable")] = !p.value(QStringLiteral("holdable")).toArray().isEmpty();
        m_points.append(m);
    }

    QFile reset(resetState);
    QJsonObject rs;
    if (reset.open(QIODevice::ReadOnly)) {
        rs = QJsonDocument::fromJson(reset.readAll()).object();
    }
    m_resetReady = rs.value(QStringLiteral("verified")).toBool();
    m_resetWhy = rs.isEmpty() ? QStringLiteral("missing") : rs.value(QStringLiteral("why")).toString();
    // /boot is root-only (mode 700), so Settings cannot see the recovery
    // kernel there. A verified copy is what puts it on /boot (reset-copy
    // boot-files), so that stands in for it.
    m_startupRecovery = m_resetReady || QFileInfo::exists(recoveryKernel);

    for (const QString &watched : m_watcher.files()) {
        m_watcher.removePath(watched);
    }
    if (QFileInfo::exists(path)) {
        m_watcher.addPath(path);
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (QFileInfo::exists(dir) && !m_watcher.directories().contains(dir)) {
        m_watcher.addPath(dir);
    }
    Q_EMIT changed();
}

bool Recovery::wipeFits() const
{
    // MemTotal of a PC with 4 GB fitted, less what the firmware and kernel
    // keep, is about 3.7 GiB; the image alone takes 2.2.
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QList<QByteArray> fields = meminfo.readLine().simplified().split(' ');
    return fields.size() >= 2 && fields.at(0) == "MemTotal:" && fields.at(1).toLongLong() >= 3500LL * 1024;
}

bool Recovery::backupInstalled() const
{
    return !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, backupDesktop).isEmpty();
}

QDateTime Recovery::lastBackup() const
{
    // Backup (wildos-backup) notes each drive's last backup in its settings
    // file: drives/list, a JSON array of drives with "lastBackup".
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/wildos/backup.conf"),
                       QSettings::IniFormat);
    const QJsonArray drives = QJsonDocument::fromJson(settings.value(QStringLiteral("drives/list")).toString().toUtf8()).array();
    QDateTime newest;
    for (const QJsonValue &d : drives) {
        const QDateTime when = QDateTime::fromString(d.toObject().value(QStringLiteral("lastBackup")).toString(), Qt::ISODate);
        if (when.isValid() && (!newest.isValid() || when > newest)) {
            newest = when;
        }
    }
    return newest;
}

void Recovery::setBusy(bool busy, const QString &text)
{
    m_busy = busy;
    m_busyText = text;
    Q_EMIT busyChanged();
}

void Recovery::run(const QStringList &args, const QString &text, const QString &what)
{
    if (m_busy) {
        return;
    }
    m_error.clear();
    setBusy(true, text);
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, [this, what](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(m_process->readAll()).trimmed();
        m_process->deleteLater();
        m_process = nullptr;
        // 126 and 127: the password prompt was dismissed.
        if (status != QProcess::NormalExit || (code != 0 && code != 126 && code != 127)) {
            m_error = output.section(QLatin1Char('\n'), -1).isEmpty() ? QStringLiteral("Something went wrong. Nothing was changed.")
                                                                       : output.section(QLatin1Char('\n'), -1);
        }
        setBusy(false);
        refresh();
        if (code == 0 && status == QProcess::NormalExit) {
            Q_EMIT finished(what);
        }
    });
    m_process->start(QStringLiteral("pkexec"), QStringList{helper} + args);
}

void Recovery::create()
{
    run({QStringLiteral("create"), QStringLiteral("Restore point you made")}, QStringLiteral("Making a restore point"), QStringLiteral("create"));
}

void Recovery::goBack(int id, bool hold)
{
    QStringList args{QStringLiteral("go-back"), QString::number(id)};
    if (hold) {
        args << QStringLiteral("hold");
    }
    run(args, QStringLiteral("Getting the earlier version ready"), QStringLiteral("go-back"));
}

void Recovery::remove(int id)
{
    run({QStringLiteral("delete"), QString::number(id)}, QStringLiteral("Deleting the restore point"), QStringLiteral("delete"));
}

void Recovery::setLimits(int keep, int gb, bool on)
{
    run({QStringLiteral("limits"), QString::number(keep), QString::number(gb), on ? QStringLiteral("on") : QStringLiteral("off")},
        QStringLiteral("Saving"), QStringLiteral("limits"));
}

void Recovery::restartToRecovery()
{
    run({QStringLiteral("restart-recovery")}, QStringLiteral("Getting start-up options ready"), QStringLiteral("startup"));
}

void Recovery::reset(const QString &how)
{
    if (how != QLatin1String("keep") && how != QLatin1String("everything")) {
        return;
    }
    run({QStringLiteral("reset"), how}, QStringLiteral("Getting the reset ready"), QStringLiteral("reset"));
}

void Recovery::openBackup()
{
    // By name, as Devices does: gtk-launch isn't in the image.
    const QString program = QStandardPaths::findExecutable(QStringLiteral("wildos-backup"));
    if (!program.isEmpty()) {
        QProcess::startDetached(program, {});
    }
}

void Recovery::restart()
{
    // The session manager's reboot, so open apps can save their work.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                                       QStringLiteral("org.kde.Shutdown"), QStringLiteral("logoutAndReboot"));
    QDBusConnection::sessionBus().asyncCall(call);
}

int Recovery::pointForBuild(int build) const
{
    if (build <= 0) {
        return -1;
    }
    for (const QVariant &v : m_points) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("build")).toInt() == build) {
            return p.value(QStringLiteral("id")).toInt();
        }
    }
    return -1;
}

QVariantMap Recovery::point(int id) const
{
    for (const QVariant &v : m_points) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("id")).toInt() == id) {
            return p;
        }
    }
    return {};
}
