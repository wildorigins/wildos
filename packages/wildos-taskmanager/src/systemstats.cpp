#include "systemstats.h"
#include "processes.h"
#include "winmode.h"

#include <KConfig>
#include <KConfigGroup>
#include <sensors/Sensor.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QThread>

#include <cmath>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace
{
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString readLine(const QString &path)
{
    return QString::fromUtf8(readFile(path)).trimmed();
}

// One decimal under 10 and none above, as Windows Task Manager writes them.
QString number(double value, int decimals = 1)
{
    return QLocale::system().toString(value, 'f', decimals);
}

QString bytes(double value)
{
    if (value < 0) {
        return QString();
    }
    const double gb = value / (1024.0 * 1024 * 1024);
    if (gb >= 1) {
        return QCoreApplication::translate("SystemStats", "%1 GB").arg(number(gb, gb >= 100 ? 0 : 1));
    }
    const double mb = value / (1024.0 * 1024);
    return QCoreApplication::translate("SystemStats", "%1 MB").arg(number(mb, 0));
}

QString byteRate(double value)
{
    if (value >= 1024 * 1024) {
        return QCoreApplication::translate("SystemStats", "%1 MB/s").arg(number(value / (1024 * 1024), 1));
    }
    return QCoreApplication::translate("SystemStats", "%1 KB/s").arg(number(value / 1024, 0));
}

QString bitRate(double bytesPerSecond)
{
    const double bits = bytesPerSecond * 8;
    if (bits >= 1000 * 1000 * 1000) {
        return QCoreApplication::translate("SystemStats", "%1 Gbps").arg(number(bits / 1e9, 1));
    }
    if (bits >= 1000 * 1000) {
        return QCoreApplication::translate("SystemStats", "%1 Mbps").arg(number(bits / 1e6, 1));
    }
    return QCoreApplication::translate("SystemStats", "%1 Kbps").arg(number(bits / 1e3, 0));
}

QString percent(double value)
{
    return number(value, 0) + QLatin1Char('%');
}

QString upTime()
{
    const qint64 seconds = qint64(readLine(QStringLiteral("/proc/uptime")).section(QLatin1Char(' '), 0, 0).toDouble());
    const qint64 days = seconds / 86400;
    const QString clock = QStringLiteral("%1:%2:%3")
                              .arg((seconds / 3600) % 24)
                              .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                              .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return days > 0 ? QStringLiteral("%1:%2").arg(days).arg(clock.rightJustified(8, QLatin1Char('0'))) : clock;
}

QVariantList toList(const QList<double> &values)
{
    QVariantList out;
    out.reserve(values.size());
    for (double v : values) {
        out << v;
    }
    return out;
}

QVariantMap pair(const QString &label, const QString &value)
{
    return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}

// udev's database entry for a block device, for its file system and
// model without needing root.
QHash<QString, QString> udevProperties(const QString &sysName)
{
    QHash<QString, QString> props;
    const QString dev = readLine(QStringLiteral("/sys/class/block/") + sysName + QStringLiteral("/dev"));
    for (const QByteArray &line : readFile(QStringLiteral("/run/udev/data/b") + dev).split('\n')) {
        if (line.startsWith("E:")) {
            const int eq = line.indexOf('=');
            props.insert(QString::fromUtf8(line.mid(2, eq - 2)), QString::fromUtf8(line.mid(eq + 1)));
        }
    }
    return props;
}

QList<int> cpuList(const QString &text)
{
    QList<int> cpus;
    for (const QString &part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList range = part.split(QLatin1Char('-'));
        const int from = range.value(0).toInt();
        const int to = range.size() > 1 ? range.value(1).toInt() : from;
        for (int i = from; i <= to; ++i) {
            cpus << i;
        }
    }
    return cpus;
}

QString cleanCpuName(QString name)
{
    name.remove(QStringLiteral("(R)"));
    name.remove(QStringLiteral("(TM)"));
    name.remove(QRegularExpression(QStringLiteral("^\\d+(st|nd|rd|th) Gen ")));
    name.remove(QRegularExpression(QStringLiteral("\\s+(CPU\\s+)?@.*$")));
    name.remove(QRegularExpression(QStringLiteral("\\s+\\d+-Core Processor$")));
    name.replace(QStringLiteral(" Processor"), QString());
    return name.simplified();
}
}

SystemStats *SystemStats::instance()
{
    static SystemStats *self = new SystemStats;
    return self;
}

SystemStats *SystemStats::create(QQmlEngine *, QJSEngine *engine)
{
    SystemStats *self = instance();
    engine->setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

SystemStats::SystemStats(QObject *parent)
    : QObject(parent)
{
    const int cpus = qMax(1, QThread::idealThreadCount());
    m_cores.resize(cpus);
    for (const QString &id : {QStringLiteral("cpu/all/usage"), QStringLiteral("cpu/all/averageFrequency"), QStringLiteral("cpu/all/averageTemperature"),
                              QStringLiteral("cpu/all/name"), QStringLiteral("memory/physical/used"), QStringLiteral("memory/physical/total"),
                              QStringLiteral("gpu/gpu0/name"), QStringLiteral("gpu/gpu0/usage"), QStringLiteral("gpu/gpu0/usedVram"),
                              QStringLiteral("gpu/gpu0/totalVram"), QStringLiteral("gpu/gpu0/temperature"), QStringLiteral("gpu/gpu0/coreFrequency"),
                              QStringLiteral("gpu/gpu0/power"), QStringLiteral("pressure/memory/some10Sec")}) {
        sensor(id);
    }
    for (int i = 0; i < cpus; ++i) {
        sensor(QStringLiteral("cpu/cpu%1/usage").arg(i));
    }
    readHardware();
    findDisks();
    findNetwork();
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, &SystemStats::sample);
    m_timer.setInterval(1000);
    m_timer.start();
    sample();
}

KSysGuard::Sensor *SystemStats::sensor(const QString &id)
{
    if (auto existing = m_sensors.value(id)) {
        return existing;
    }
    auto s = new KSysGuard::Sensor(id, this);
    m_sensors.insert(id, s);
    return s;
}

QVariant SystemStats::sensorValue(const QString &id) const
{
    const KSysGuard::Sensor *s = m_sensors.value(id);
    if (!s || s->status() != KSysGuard::Sensor::Status::Ready) {
        return {};
    }
    return s->value();
}

void SystemStats::setInterval(int ms)
{
    ms = qBound(250, ms, 10000);
    if (ms == m_timer.interval()) {
        return;
    }
    m_timer.setInterval(ms);
    Q_EMIT intervalChanged();
}

void SystemStats::setPaused(bool paused)
{
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (paused) {
        m_timer.stop();
    } else {
        m_timer.start();
    }
    Q_EMIT pausedChanged();
}

void SystemStats::push(QList<double> &history, double value) const
{
    // Sixty seconds at the current update speed.
    const int keep = qMax(2, int(std::ceil(60000.0 / m_timer.interval())) + 1);
    history << value;
    while (history.size() > keep) {
        history.removeFirst();
    }
}

void SystemStats::readHardware()
{
    const QString cpuinfo = QString::fromUtf8(readFile(QStringLiteral("/proc/cpuinfo")));
    QString modelLine;
    QString flags;
    for (const QString &line : cpuinfo.split(QLatin1Char('\n'))) {
        if (modelLine.isEmpty() && line.startsWith(QLatin1String("model name"))) {
            modelLine = line.section(QLatin1Char(':'), 1).trimmed();
        } else if (flags.isEmpty() && line.startsWith(QLatin1String("flags"))) {
            flags = line.section(QLatin1Char(':'), 1);
        }
    }
    m_cpuModel = cleanCpuName(modelLine);

    // Intel reports the base clock itself; otherwise the model name often
    // carries it ("@ 2.10GHz").
    const qint64 baseKhz = readLine(QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency")).toLongLong();
    if (baseKhz > 0) {
        m_baseSpeed = QCoreApplication::translate("SystemStats", "%1 GHz").arg(number(baseKhz / 1e6, 2));
    } else if (const auto match = QRegularExpression(QStringLiteral("@\\s*([0-9.]+)\\s*GHz")).match(modelLine); match.hasMatch()) {
        m_baseSpeed = QCoreApplication::translate("SystemStats", "%1 GHz").arg(number(match.captured(1).toDouble(), 2));
    }

    const int logical = m_cores.size();
    QSet<QString> cores;
    for (int i = 0; i < logical; ++i) {
        const QString base = QStringLiteral("/sys/devices/system/cpu/cpu%1/topology/").arg(i);
        cores.insert(readLine(base + QStringLiteral("physical_package_id")) + QLatin1Char(':') + readLine(base + QStringLiteral("core_cpus_list")));
    }
    // Intel's hybrid chips list their performance and efficient cores apart.
    const QList<int> pCpus = cpuList(readLine(QStringLiteral("/sys/devices/cpu_core/cpus")));
    const QList<int> eCpus = cpuList(readLine(QStringLiteral("/sys/devices/cpu_atom/cpus")));
    if (!pCpus.isEmpty() && !eCpus.isEmpty()) {
        QSet<QString> pCores;
        for (int cpu : pCpus) {
            pCores.insert(readLine(QStringLiteral("/sys/devices/system/cpu/cpu%1/topology/core_cpus_list").arg(cpu)));
        }
        m_coreText = QCoreApplication::translate("SystemStats", "%1 (%2 performance, %3 efficient)").arg(pCores.size() + eCpus.size()).arg(pCores.size()).arg(eCpus.size());
    } else {
        m_coreText = QString::number(cores.size());
    }

    if (flags.contains(QLatin1String(" vmx")) || flags.contains(QLatin1String(" svm"))) {
        m_virtualisation = QFileInfo::exists(QStringLiteral("/dev/kvm")) ? QCoreApplication::translate("SystemStats", "On") : QCoreApplication::translate("SystemStats", "Off");
    } else {
        m_virtualisation = QCoreApplication::translate("SystemStats", "Unknown");
    }

    // Each cache once, however many processors share it.
    QHash<int, qint64> perLevel;
    QSet<QString> seen;
    for (int i = 0; i < logical; ++i) {
        const QString base = QStringLiteral("/sys/devices/system/cpu/cpu%1/cache/").arg(i);
        const auto indexes = QDir(base).entryList({QStringLiteral("index*")}, QDir::Dirs);
        for (const QString &index : indexes) {
            const int level = readLine(base + index + QStringLiteral("/level")).toInt();
            const QString type = readLine(base + index + QStringLiteral("/type"));
            const QString key = QStringLiteral("%1-%2-%3").arg(level).arg(type, readLine(base + index + QStringLiteral("/shared_cpu_list")));
            if (level < 2 || seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            QString size = readLine(base + index + QStringLiteral("/size"));
            qint64 kb = size.left(size.size() - 1).toLongLong();
            if (size.endsWith(QLatin1Char('M'))) {
                kb *= 1024;
            }
            perLevel[level] += kb;
        }
    }
    QStringList cache;
    for (int level : {2, 3}) {
        if (perLevel.value(level) > 0) {
            cache << QCoreApplication::translate("SystemStats", "L%1 %2 MB").arg(level).arg(number(perLevel.value(level) / 1024.0, 1));
        }
    }
    m_cache = cache.join(QStringLiteral(", "));

    // The first graphics card the kernel's DRM layer has.
    const auto cards = QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card?")}, QDir::Dirs | QDir::System);
    for (const QString &card : cards) {
        const QString device = QStringLiteral("/sys/class/drm/") + card + QStringLiteral("/device");
        if (!QFileInfo::exists(device + QStringLiteral("/vendor"))) {
            continue;
        }
        m_hasGpu = true;
        m_gpuCard = card;
        m_gpuDriver = QFileInfo(device + QStringLiteral("/driver")).symLinkTarget().section(QLatin1Char('/'), -1);
        const QString slot = QFileInfo(device).symLinkTarget().section(QLatin1Char('/'), -1);
        for (const QByteArray &line : readFile(QStringLiteral("/run/udev/data/+pci:") + slot).split('\n')) {
            if (line.startsWith("E:ID_MODEL_FROM_DATABASE=")) {
                m_gpuName = QString::fromUtf8(line.mid(25));
            }
        }
        break;
    }
}

void SystemStats::findDisks()
{
    // The Wild OS disk is the partition / is on; Windows' C: is its NTFS
    // partition (the biggest, if there are several), mounted or not.
    QString rootDevice;
    struct stat st;
    if (::stat("/", &st) == 0) {
        rootDevice = QStringLiteral("%1:%2").arg(major(st.st_dev)).arg(minor(st.st_dev));
    }
    // On Btrfs, which Wild OS installs on, / reports a device number of its
    // own that no block device has, so the disk was never found and its
    // activity never read. The device the root is mounted from names it
    // instead (/dev/mapper/... resolves to its dm-N).
    const QString rootFrom = QString::fromUtf8(QStorageInfo::root().device());
    const QString rootSource = rootFrom.startsWith(QLatin1String("/dev/")) ? QFileInfo(rootFrom).canonicalFilePath().section(QLatin1Char('/'), -1) : QString();
    QHash<QString, QString> mounts;
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        mounts.insert(QString::fromUtf8(volume.device()), volume.rootPath());
    }
    Disk system;
    Disk windows;
    const auto blocks = QDir(QStringLiteral("/sys/class/block")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &name : blocks) {
        const QString sys = QStringLiteral("/sys/class/block/") + name;
        if (name.startsWith(QLatin1String("loop")) || name.startsWith(QLatin1String("ram")) || name.startsWith(QLatin1String("zram"))) {
            continue;
        }
        const QHash<QString, QString> props = udevProperties(name);
        Disk d;
        d.device = name;
        d.parent = QFileInfo::exists(sys + QStringLiteral("/partition")) ? QFileInfo(sys).canonicalFilePath().section(QLatin1Char('/'), -2, -2) : name;
        d.uuid = props.value(QStringLiteral("ID_FS_UUID"));
        d.fsType = props.value(QStringLiteral("ID_FS_TYPE"));
        d.model = props.value(QStringLiteral("ID_MODEL")).replace(QLatin1Char('_'), QLatin1Char(' '));
        d.size = readLine(sys + QStringLiteral("/size")).toLongLong() * 512;
        d.ssd = readLine(QStringLiteral("/sys/block/") + d.parent + QStringLiteral("/queue/rotational")) != QLatin1String("1");
        d.mount = mounts.value(QStringLiteral("/dev/") + name);
        const QString dev = readLine(sys + QStringLiteral("/dev"));
        // A root on LVM or encryption sits on a dm device; its slave is the
        // partition whose activity counts.
        if ((dev == rootDevice || (!rootSource.isEmpty() && name == rootSource)) && system.device.isEmpty()) {
            d.system = true;
            d.mount = QStringLiteral("/");
            const auto slaves = QDir(sys + QStringLiteral("/slaves")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
            if (!slaves.isEmpty()) {
                const QString inner = slaves.constFirst();
                d.device = inner;
                d.parent = QFileInfo(QStringLiteral("/sys/class/block/") + inner).canonicalFilePath().section(QLatin1Char('/'), -2, -2);
                d.ssd = readLine(QStringLiteral("/sys/block/") + d.parent + QStringLiteral("/queue/rotational")) != QLatin1String("1");
                if (d.model.isEmpty()) {
                    d.model = udevProperties(inner).value(QStringLiteral("ID_MODEL")).replace(QLatin1Char('_'), QLatin1Char(' '));
                }
            }
            system = d;
        } else if (d.fsType == QLatin1String("ntfs") && d.size > windows.size && d.size > qint64(20) * 1024 * 1024 * 1024) {
            windows = d;
        }
    }
    if (system.device.isEmpty()) {
        // No block device behind / (a container or a live session on an
        // overlay): the disk is still shown, from the file system alone.
        system.system = true;
        system.mount = QStringLiteral("/");
        system.fsType = QStorageInfo::root().fileSystemType();
    }
    if (system.model.isEmpty()) {
        system.model = udevProperties(system.parent).value(QStringLiteral("ID_MODEL")).replace(QLatin1Char('_'), QLatin1Char(' '));
    }
    system.id = QStringLiteral("disk-system");
    system.title = QCoreApplication::translate("SystemStats", "Disk (Wild OS)");
    m_disks << system;
    if (!windows.device.isEmpty()) {
        windows.id = QStringLiteral("disk-windows");
        windows.title = QCoreApplication::translate("SystemStats", "Disk (Windows C:)");
        if (windows.model.isEmpty()) {
            windows.model = udevProperties(windows.parent).value(QStringLiteral("ID_MODEL")).replace(QLatin1Char('_'), QLatin1Char(' '));
        }
        m_disks << windows;
    }
}

void SystemStats::findNetwork()
{
    // The connection the default route goes through.
    const QList<QByteArray> lines = readFile(QStringLiteral("/proc/net/route")).split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> cols = line.simplified().split(' ');
        if (cols.size() > 2 && cols.at(1) == "00000000") {
            m_iface = QString::fromUtf8(cols.at(0));
            break;
        }
    }
    if (m_iface.isEmpty()) {
        const auto ifaces = QDir(QStringLiteral("/sys/class/net")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &iface : ifaces) {
            if (iface != QLatin1String("lo") && readLine(QStringLiteral("/sys/class/net/") + iface + QStringLiteral("/operstate")) == QLatin1String("up")) {
                m_iface = iface;
                break;
            }
        }
    }
    m_wireless = !m_iface.isEmpty() && QFileInfo::exists(QStringLiteral("/sys/class/net/") + m_iface + QStringLiteral("/wireless"));
    if (!m_iface.isEmpty()) {
        for (const QString &leaf : {QStringLiteral("download"), QStringLiteral("upload"), QStringLiteral("network"), QStringLiteral("signal")}) {
            sensor(QStringLiteral("network/%1/%2").arg(m_iface, leaf));
        }
    }
}

void SystemStats::sample()
{
    const double seconds = qMax(0.05, m_clock.restart() / 1000.0);
    sampleCpu(seconds);
    sampleMemory();
    sampleDisks(seconds);
    sampleNetwork(seconds);
    sampleGpu(seconds);
    const Processes *p = Processes::instance();
    push(m_winHistory, p->winModeCpu());
    m_winInstalled = WinMode::installed().size();
    QStringList ids;
    for (const QVariant &r : resources()) {
        ids << r.toMap().value(QStringLiteral("id")).toString();
    }
    if (ids != m_resourceIds) {
        m_resourceIds = ids;
        Q_EMIT resourceIdsChanged();
    }
    ++m_revision;
    Q_EMIT updated();
}

void SystemStats::sampleCpu(double)
{
    // /proc/stat backs ksystemstats up until its sensors are ready.
    const QList<QByteArray> lines = readFile(QStringLiteral("/proc/stat")).split('\n');
    QList<double> perCore(m_cores.size(), 0.0);
    double total = 0;
    QList<QPair<quint64, quint64>> ticks;
    for (const QByteArray &line : lines) {
        if (!line.startsWith("cpu")) {
            break;
        }
        const QList<QByteArray> cols = line.simplified().split(' ');
        quint64 all = 0;
        for (int i = 1; i < cols.size(); ++i) {
            all += cols.at(i).toULongLong();
        }
        const quint64 idle = cols.value(4).toULongLong() + cols.value(5).toULongLong();
        ticks << qMakePair(all, idle);
    }
    for (int i = 0; i < ticks.size(); ++i) {
        double usage = 0;
        if (i < m_cpuTicks.size()) {
            const quint64 all = ticks.at(i).first - m_cpuTicks.at(i).first;
            const quint64 idle = ticks.at(i).second - m_cpuTicks.at(i).second;
            usage = all > 0 ? 100.0 * (all - idle) / all : 0;
        }
        if (i == 0) {
            total = usage;
        } else if (i - 1 < perCore.size()) {
            perCore[i - 1] = usage;
        }
    }
    m_cpuTicks = ticks;

    const QVariant all = sensorValue(QStringLiteral("cpu/all/usage"));
    m_cpu = all.isValid() ? all.toDouble() : total;
    push(m_cpuHistory, m_cpu);
    for (int i = 0; i < m_cores.size(); ++i) {
        const QVariant core = sensorValue(QStringLiteral("cpu/cpu%1/usage").arg(i));
        push(m_cores[i], core.isValid() ? core.toDouble() : perCore.value(i));
    }

    const QVariant speed = sensorValue(QStringLiteral("cpu/all/averageFrequency"));
    if (speed.isValid() && speed.toDouble() > 0) {
        m_speedMhz = speed.toDouble();
    } else {
        double sum = 0;
        int n = 0;
        for (int i = 0; i < m_cores.size(); ++i) {
            const double khz = readLine(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(i)).toDouble();
            if (khz > 0) {
                sum += khz / 1000;
                ++n;
            }
        }
        m_speedMhz = n > 0 ? sum / n : 0;
    }
    m_temperature = sensorValue(QStringLiteral("cpu/all/averageTemperature")).toDouble();
    if (m_cpuModel.isEmpty()) {
        m_cpuModel = cleanCpuName(sensorValue(QStringLiteral("cpu/all/name")).toString());
    }
}

void SystemStats::sampleMemory()
{
    QHash<QByteArray, qint64> info;
    for (const QByteArray &line : readFile(QStringLiteral("/proc/meminfo")).split('\n')) {
        const int colon = line.indexOf(':');
        if (colon > 0) {
            info.insert(line.left(colon), line.mid(colon + 1).simplified().split(' ').value(0).toLongLong() * 1024);
        }
    }
    m_memTotal = info.value("MemTotal");
    m_memAvailable = info.value("MemAvailable");
    m_memFree = info.value("MemFree");
    m_memCached = info.value("Cached") + info.value("SReclaimable");
    m_swapTotal = info.value("SwapTotal");
    m_swapUsed = m_swapTotal - info.value("SwapFree");
    const QVariant used = sensorValue(QStringLiteral("memory/physical/used"));
    m_memUsed = used.isValid() ? used.toLongLong() : m_memTotal - m_memAvailable;
    push(m_memHistory, m_memTotal > 0 ? 100.0 * m_memUsed / m_memTotal : 0);
    if (m_swapKind.isEmpty()) {
        const QString swaps = QString::fromUtf8(readFile(QStringLiteral("/proc/swaps")));
        m_swapKind = swaps.contains(QLatin1String("zram")) ? QCoreApplication::translate("SystemStats", "Compressed in memory")
            : swaps.contains(QLatin1String("partition"))     ? QCoreApplication::translate("SystemStats", "Swap partition")
            : swaps.contains(QLatin1String("file"))          ? QCoreApplication::translate("SystemStats", "Swap file")
                                                             : QCoreApplication::translate("SystemStats", "None");
    }
}

void SystemStats::sampleDisks(double seconds)
{
    for (Disk &d : m_disks) {
        if (!d.device.isEmpty()) {
            // Fields 3 and 7 are sectors read and written, 10 the time the
            // device was busy in ms: busy time over elapsed time is the
            // "active time" Windows graphs.
            const QList<QByteArray> cols = readFile(QStringLiteral("/sys/class/block/") + d.device + QStringLiteral("/stat")).simplified().split(' ');
            const quint64 read = cols.value(2).toULongLong();
            const quint64 write = cols.value(6).toULongLong();
            const quint64 ticks = cols.value(9).toULongLong();
            if (d.sampled) {
                d.active = qBound(0.0, (ticks - d.ticks) / (seconds * 10.0), 100.0);
                d.readRate = (read - d.readSectors) * 512.0 / seconds;
                d.writeRate = (write - d.writeSectors) * 512.0 / seconds;
            }
            d.ticks = ticks;
            d.readSectors = read;
            d.writeSectors = write;
            d.sampled = true;
        }
        const QString mount = d.mount.isEmpty() ? QString() : d.mount;
        if (!mount.isEmpty()) {
            const QStorageInfo volume(mount);
            if (volume.isValid()) {
                d.size = volume.bytesTotal();
                d.free = volume.bytesAvailable();
                d.used = d.size - volume.bytesFree();
                if (d.fsType.isEmpty()) {
                    d.fsType = QString::fromUtf8(volume.fileSystemType());
                }
            }
        }
        push(d.history, d.active);
    }
}

void SystemStats::sampleNetwork(double seconds)
{
    if (m_iface.isEmpty()) {
        findNetwork();
        if (m_iface.isEmpty()) {
            push(m_rxHistory, 0);
            push(m_txHistory, 0);
            return;
        }
    }
    const QString base = QStringLiteral("/sys/class/net/") + m_iface + QStringLiteral("/statistics/");
    const quint64 rx = readLine(base + QStringLiteral("rx_bytes")).toULongLong();
    const quint64 tx = readLine(base + QStringLiteral("tx_bytes")).toULongLong();
    double rxRate = m_rxBytes > 0 && rx >= m_rxBytes ? (rx - m_rxBytes) / seconds : 0;
    double txRate = m_txBytes > 0 && tx >= m_txBytes ? (tx - m_txBytes) / seconds : 0;
    m_rxBytes = rx;
    m_txBytes = tx;
    const QVariant down = sensorValue(QStringLiteral("network/%1/download").arg(m_iface));
    const QVariant up = sensorValue(QStringLiteral("network/%1/upload").arg(m_iface));
    if (down.isValid() && up.isValid()) {
        rxRate = down.toDouble();
        txRate = up.toDouble();
    }
    m_rx = rxRate;
    m_tx = txRate;
    push(m_rxHistory, m_rx);
    push(m_txHistory, m_tx);
    double peak = 0;
    for (int i = 0; i < m_rxHistory.size(); ++i) {
        peak = qMax(peak, qMax(m_rxHistory.at(i), m_txHistory.value(i)));
    }
    m_netMax = peak;
    // NetworkManager is asked for the link speed now and then, not every
    // sample.
    if (m_linkTick++ % 30 == 0) {
        m_link = linkSpeed();
    }
}

void SystemStats::sampleGpu(double seconds)
{
    if (!m_hasGpu) {
        return;
    }
    // Per-process use from each DRM client's fdinfo: nanoseconds each
    // engine spent on it. Every other tick, as walking every fd costs more
    // than the rest of a sample.
    if ((m_gpuTick++ % 2) == 0) {
        QHash<QString, QHash<QString, quint64>> clients;
        QHash<qint64, double> perPid;
        double busiest = 0;
        QHash<QString, double> engineTotals;
        const auto pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &pidText : pids) {
            bool ok = false;
            const qint64 pid = pidText.toLongLong(&ok);
            if (!ok) {
                continue;
            }
            const QString fdDir = QStringLiteral("/proc/") + pidText + QStringLiteral("/fd");
            const auto fds = QDir(fdDir).entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot);
            double pidUse = 0;
            for (const QString &fd : fds) {
                if (!QFileInfo(fdDir + QLatin1Char('/') + fd).symLinkTarget().startsWith(QLatin1String("/dev/dri/"))) {
                    continue;
                }
                QString client;
                QHash<QString, quint64> engines;
                for (const QByteArray &line : readFile(QStringLiteral("/proc/") + pidText + QStringLiteral("/fdinfo/") + fd).split('\n')) {
                    if (line.startsWith("drm-client-id:")) {
                        client = QString::fromUtf8(line.mid(14).trimmed());
                    } else if (line.startsWith("drm-engine-") && !line.startsWith("drm-engine-capacity")) {
                        const int colon = line.indexOf(':');
                        engines.insert(QString::fromUtf8(line.mid(11, colon - 11)), line.mid(colon + 1).trimmed().split(' ').value(0).toULongLong());
                    }
                }
                if (client.isEmpty() || clients.contains(client)) {
                    continue;
                }
                clients.insert(client, engines);
                const auto previous = m_gpuClients.value(client);
                for (auto it = engines.cbegin(); it != engines.cend(); ++it) {
                    if (!previous.contains(it.key()) || it.value() < previous.value(it.key())) {
                        continue;
                    }
                    const double use = 100.0 * (it.value() - previous.value(it.key())) / (seconds * 2 * 1e9);
                    pidUse = qMax(pidUse, use);
                    engineTotals[it.key()] += use;
                }
            }
            if (pidUse > 0) {
                perPid.insert(pid, qMin(100.0, pidUse));
            }
        }
        for (double total : std::as_const(engineTotals)) {
            busiest = qMax(busiest, total);
        }
        m_gpuClients = clients;
        Processes::instance()->setGpuUsage(perPid);
        const QVariant usage = sensorValue(QStringLiteral("gpu/gpu0/usage"));
        m_gpu = usage.isValid() ? usage.toDouble() : qMin(100.0, busiest);
    }
    push(m_gpuHistory, m_gpu);
    if (const QString name = sensorValue(QStringLiteral("gpu/gpu0/name")).toString(); !name.isEmpty()) {
        m_gpuName = name;
    }
}

qint64 SystemStats::linkSpeed() const
{
    if (m_iface.isEmpty()) {
        return 0;
    }
    // Ethernet reports its speed in Mbps; Wi-Fi's changes all the time and
    // only NetworkManager knows it (kbit/s).
    if (!m_wireless) {
        const qint64 mbps = readLine(QStringLiteral("/sys/class/net/") + m_iface + QStringLiteral("/speed")).toLongLong();
        return mbps > 0 ? mbps * 1000 * 1000 : 0;
    }
    QDBusInterface nm(QStringLiteral("org.freedesktop.NetworkManager"), QStringLiteral("/org/freedesktop/NetworkManager"),
                      QStringLiteral("org.freedesktop.NetworkManager"), QDBusConnection::systemBus());
    const QDBusReply<QDBusObjectPath> device = nm.call(QStringLiteral("GetDeviceByIpIface"), m_iface);
    if (!device.isValid()) {
        return 0;
    }
    QDBusInterface wifi(QStringLiteral("org.freedesktop.NetworkManager"), device.value().path(),
                        QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"), QDBusConnection::systemBus());
    return qint64(wifi.property("Bitrate").toUInt()) * 1000;
}

double SystemStats::diskPercent() const
{
    return m_disks.isEmpty() ? 0 : m_disks.constFirst().active;
}

double SystemStats::networkPercent() const
{
    const qint64 link = m_link > 0 ? m_link : qint64(100) * 1000 * 1000;
    return qMin(100.0, 100.0 * (m_rx + m_tx) * 8 / link);
}

QVariantList SystemStats::coreHistories() const
{
    QVariantList out;
    for (const auto &core : m_cores) {
        out << QVariant(toList(core));
    }
    return out;
}

QVariantList SystemStats::resources() const
{
    QVariantList out;
    const Processes *p = Processes::instance();
    out << QVariantMap{
        {QStringLiteral("id"), QStringLiteral("cpu")},
        {QStringLiteral("title"), QCoreApplication::translate("SystemStats", "CPU")},
        {QStringLiteral("subtitle"), percent(m_cpu) + QStringLiteral("  ") + QCoreApplication::translate("SystemStats", "%1 GHz").arg(number(m_speedMhz / 1000, 1))},
        {QStringLiteral("colour"), QStringLiteral("cpu")},
        {QStringLiteral("history"), toList(m_cpuHistory)},
        {QStringLiteral("max"), 100},
    };
    out << QVariantMap{
        {QStringLiteral("id"), QStringLiteral("memory")},
        {QStringLiteral("title"), QCoreApplication::translate("SystemStats", "Memory")},
        {QStringLiteral("subtitle"), QCoreApplication::translate("SystemStats", "%1/%2 GB (%3)")
                                         .arg(number(m_memUsed / 1073741824.0, 1), number(m_memTotal / 1073741824.0, 0), percent(memoryPercent()))},
        {QStringLiteral("colour"), QStringLiteral("memory")},
        {QStringLiteral("history"), toList(m_memHistory)},
        {QStringLiteral("max"), 100},
    };
    const auto diskTile = [this](const Disk &d) {
        return QVariantMap{
            {QStringLiteral("id"), d.id},
            {QStringLiteral("title"), d.title},
            {QStringLiteral("subtitle"), (d.ssd ? QStringLiteral("SSD  ") : QStringLiteral("HDD  ")) + percent(d.active)},
            {QStringLiteral("colour"), QStringLiteral("disk")},
            {QStringLiteral("history"), toList(d.history)},
            {QStringLiteral("max"), 100},
        };
    };
    if (!m_disks.isEmpty()) {
        out << diskTile(m_disks.constFirst());
    }
    if (!m_iface.isEmpty()) {
        QList<double> sum;
        for (int i = 0; i < m_rxHistory.size(); ++i) {
            sum << m_rxHistory.at(i) + m_txHistory.value(i);
        }
        double peak = 0;
        for (double v : std::as_const(sum)) {
            peak = qMax(peak, v);
        }
        out << QVariantMap{
            {QStringLiteral("id"), QStringLiteral("network")},
            {QStringLiteral("title"), m_wireless ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet")},
            {QStringLiteral("subtitle"), QStringLiteral("↑ %1  ↓ %2").arg(bitRate(m_tx).section(QLatin1Char(' '), 0, 0), bitRate(m_rx))},
            {QStringLiteral("colour"), QStringLiteral("network")},
            {QStringLiteral("history"), toList(sum)},
            {QStringLiteral("max"), qMax(peak * 1.25, 125000.0)},
        };
    }
    for (int i = 1; i < m_disks.size(); ++i) {
        out << diskTile(m_disks.at(i));
    }
    if (m_hasGpu) {
        out << QVariantMap{
            {QStringLiteral("id"), QStringLiteral("gpu")},
            {QStringLiteral("title"), QCoreApplication::translate("SystemStats", "Graphics")},
            {QStringLiteral("subtitle"), (m_gpuName.isEmpty() ? QString() : m_gpuName + QStringLiteral("  ")) + percent(m_gpu)},
            {QStringLiteral("colour"), QStringLiteral("gpu")},
            {QStringLiteral("history"), toList(m_gpuHistory)},
            {QStringLiteral("max"), 100},
        };
    }
    if (m_winInstalled > 0 || !p->winModeStatus().vm.isEmpty()) {
        out << QVariantMap{
            {QStringLiteral("id"), QStringLiteral("winmode")},
            {QStringLiteral("title"), QCoreApplication::translate("SystemStats", "Windows mode")},
            {QStringLiteral("subtitle"), bytes(p->winModeMemory()) + QStringLiteral("  ") + QCoreApplication::translate("SystemStats", "%1 CPU").arg(percent(p->winModeCpu()))},
            {QStringLiteral("colour"), QStringLiteral("winmode")},
            {QStringLiteral("history"), toList(m_winHistory)},
            {QStringLiteral("max"), 100},
        };
    }
    return out;
}

QVariantMap SystemStats::resource(const QString &id) const
{
    for (const QVariant &r : resources()) {
        if (r.toMap().value(QStringLiteral("id")).toString() == id) {
            return r.toMap();
        }
    }
    return {};
}

QVariantMap SystemStats::details(const QString &id) const
{
    const Processes *p = Processes::instance();
    QVariantMap out{{QStringLiteral("id"), id}};
    if (id == QLatin1String("cpu")) {
        out[QStringLiteral("title")] = QCoreApplication::translate("SystemStats", "CPU");
        out[QStringLiteral("subtitle")] = m_cpuModel;
        out[QStringLiteral("caption")] = QCoreApplication::translate("SystemStats", "% utilisation over 60 seconds");
        out[QStringLiteral("scale")] = QStringLiteral("100%");
        out[QStringLiteral("history")] = toList(m_cpuHistory);
        out[QStringLiteral("max")] = 100;
        out[QStringLiteral("colour")] = QStringLiteral("cpu");
        out[QStringLiteral("stats")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "Utilisation"), percent(m_cpu)),
            pair(QCoreApplication::translate("SystemStats", "Speed"), m_speedMhz > 0 ? QCoreApplication::translate("SystemStats", "%1 GHz").arg(number(m_speedMhz / 1000, 1)) : QCoreApplication::translate("SystemStats", "Unknown")),
            pair(QCoreApplication::translate("SystemStats", "Temperature"), m_temperature > 0 ? QCoreApplication::translate("SystemStats", "%1°C").arg(number(m_temperature, 0)) : QCoreApplication::translate("SystemStats", "Unknown")),
            pair(QCoreApplication::translate("SystemStats", "Processes"), QLocale::system().toString(p->processCount())),
            pair(QCoreApplication::translate("SystemStats", "Threads"), QLocale::system().toString(p->threadCount())),
            pair(QCoreApplication::translate("SystemStats", "Up time"), upTime()),
        };
        QVariantList specs;
        if (!m_baseSpeed.isEmpty()) {
            specs << pair(QCoreApplication::translate("SystemStats", "Base speed"), m_baseSpeed);
        }
        specs << pair(QCoreApplication::translate("SystemStats", "Cores"), m_coreText);
        specs << pair(QCoreApplication::translate("SystemStats", "Logical processors"), QString::number(m_cores.size()));
        specs << pair(QCoreApplication::translate("SystemStats", "Virtualisation"), m_virtualisation);
        if (!m_cache.isEmpty()) {
            specs << pair(QCoreApplication::translate("SystemStats", "Cache"), m_cache);
        }
        out[QStringLiteral("specs")] = specs;
        out[QStringLiteral("topTitle")] = QCoreApplication::translate("SystemStats", "Using the most CPU");
        out[QStringLiteral("topMetric")] = QStringLiteral("cpu");
        out[QStringLiteral("topTotal")] = qMax(m_cpu, 1.0);
        return out;
    }
    if (id == QLatin1String("memory")) {
        const double totalGb = m_memTotal / 1073741824.0;
        out[QStringLiteral("title")] = QCoreApplication::translate("SystemStats", "Memory");
        out[QStringLiteral("subtitle")] = QCoreApplication::translate("SystemStats", "%1 GB").arg(number(totalGb, 1));
        out[QStringLiteral("caption")] = QCoreApplication::translate("SystemStats", "Memory in use over 60 seconds");
        out[QStringLiteral("scale")] = QCoreApplication::translate("SystemStats", "%1 GB").arg(number(totalGb, 1));
        out[QStringLiteral("history")] = toList(m_memHistory);
        out[QStringLiteral("max")] = 100;
        out[QStringLiteral("colour")] = QStringLiteral("memory");
        out[QStringLiteral("stats")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "In use"), bytes(m_memUsed)),
            pair(QCoreApplication::translate("SystemStats", "Available"), bytes(m_memAvailable)),
            pair(QCoreApplication::translate("SystemStats", "Cached"), bytes(m_memCached)),
            pair(QCoreApplication::translate("SystemStats", "Free"), bytes(m_memFree)),
            pair(QCoreApplication::translate("SystemStats", "Swap in use"), bytes(m_swapUsed)),
            pair(QCoreApplication::translate("SystemStats", "Swap size"), bytes(m_swapTotal)),
        };
        const double pressure = sensorValue(QStringLiteral("pressure/memory/some10Sec")).toDouble();
        out[QStringLiteral("specs")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "Usable"), bytes(m_memTotal)),
            pair(QCoreApplication::translate("SystemStats", "Swap"), m_swapKind),
            pair(QCoreApplication::translate("SystemStats", "Memory pressure"), pressure > 10 ? QCoreApplication::translate("SystemStats", "High") : pressure > 1 ? QCoreApplication::translate("SystemStats", "Some") : QCoreApplication::translate("SystemStats", "Low")),
        };
        out[QStringLiteral("topTitle")] = QCoreApplication::translate("SystemStats", "Using the most memory");
        out[QStringLiteral("topMetric")] = QStringLiteral("memory");
        out[QStringLiteral("topTotal")] = double(qMax<qint64>(m_memUsed, 1));
        return out;
    }
    for (const Disk &d : m_disks) {
        if (d.id != id) {
            continue;
        }
        out[QStringLiteral("title")] = d.title;
        out[QStringLiteral("subtitle")] = d.model;
        out[QStringLiteral("caption")] = QCoreApplication::translate("SystemStats", "Active time over 60 seconds");
        out[QStringLiteral("scale")] = QStringLiteral("100%");
        out[QStringLiteral("history")] = toList(d.history);
        out[QStringLiteral("max")] = 100;
        out[QStringLiteral("colour")] = QStringLiteral("disk");
        QVariantList stats{
            pair(QCoreApplication::translate("SystemStats", "Active time"), percent(d.active)),
            pair(QCoreApplication::translate("SystemStats", "Read speed"), byteRate(d.readRate)),
            pair(QCoreApplication::translate("SystemStats", "Write speed"), byteRate(d.writeRate)),
            pair(QCoreApplication::translate("SystemStats", "Capacity"), bytes(d.size)),
        };
        if (d.used >= 0) {
            stats << pair(QCoreApplication::translate("SystemStats", "Used"), bytes(d.used)) << pair(QCoreApplication::translate("SystemStats", "Free"), bytes(d.free));
        } else {
            stats << pair(QCoreApplication::translate("SystemStats", "Used"), QCoreApplication::translate("SystemStats", "Not open")) << pair(QCoreApplication::translate("SystemStats", "Free"), QCoreApplication::translate("SystemStats", "Not open"));
        }
        out[QStringLiteral("stats")] = stats;
        const QString fs = d.fsType == QLatin1String("ntfs") ? QStringLiteral("NTFS") : d.fsType == QLatin1String("vfat") ? QStringLiteral("FAT32") : d.fsType;
        out[QStringLiteral("specs")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "Type"), d.ssd ? QStringLiteral("SSD") : QCoreApplication::translate("SystemStats", "Hard drive")),
            pair(QCoreApplication::translate("SystemStats", "File system"), fs.isEmpty() ? QCoreApplication::translate("SystemStats", "Unknown") : fs),
            pair(QCoreApplication::translate("SystemStats", "System disk"), d.system ? QCoreApplication::translate("SystemStats", "Yes") : QCoreApplication::translate("SystemStats", "No")),
            pair(QCoreApplication::translate("SystemStats", "Open in Wild OS"), d.mount.isEmpty() ? QCoreApplication::translate("SystemStats", "No") : QCoreApplication::translate("SystemStats", "Yes")),
        };
        // Which program reads which disk is not known per disk, so the list
        // only goes with the disk Wild OS runs from.
        if (d.system) {
            out[QStringLiteral("topTitle")] = QCoreApplication::translate("SystemStats", "Using the disk most");
            out[QStringLiteral("topMetric")] = QStringLiteral("disk");
            out[QStringLiteral("topTotal")] = qMax(d.readRate + d.writeRate, 1.0);
        }
        return out;
    }
    if (id == QLatin1String("network")) {
        QString ipv4;
        QString ipv6;
        const QNetworkInterface iface = QNetworkInterface::interfaceFromName(m_iface);
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && ipv4.isEmpty()) {
                ipv4 = entry.ip().toString();
            } else if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol && ipv6.isEmpty() && !entry.ip().isLinkLocal()) {
                ipv6 = entry.ip().toString();
            }
        }
        const QString ssid = sensorValue(QStringLiteral("network/%1/network").arg(m_iface)).toString();
        const QVariant signal = sensorValue(QStringLiteral("network/%1/signal").arg(m_iface));
        const qint64 link = m_link;
        out[QStringLiteral("title")] = m_wireless ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet");
        out[QStringLiteral("subtitle")] = ssid.isEmpty() ? m_iface : ssid;
        out[QStringLiteral("caption")] = QCoreApplication::translate("SystemStats", "Throughput over 60 seconds");
        const double scale = qMax(m_netMax * 1.25, 125000.0);
        out[QStringLiteral("scale")] = bitRate(scale);
        out[QStringLiteral("history")] = toList(m_rxHistory);
        out[QStringLiteral("second")] = toList(m_txHistory);
        out[QStringLiteral("max")] = scale;
        out[QStringLiteral("colour")] = QStringLiteral("network");
        QVariantList stats{
            pair(QCoreApplication::translate("SystemStats", "Send"), bitRate(m_tx)),
            pair(QCoreApplication::translate("SystemStats", "Receive"), bitRate(m_rx)),
            pair(QCoreApplication::translate("SystemStats", "Sent"), bytes(m_txBytes)),
            pair(QCoreApplication::translate("SystemStats", "Received"), bytes(m_rxBytes)),
            pair(QCoreApplication::translate("SystemStats", "Link speed"), link > 0 ? bitRate(link / 8.0) : QCoreApplication::translate("SystemStats", "Unknown")),
        };
        if (m_wireless) {
            stats << pair(QCoreApplication::translate("SystemStats", "Signal"), signal.isValid() ? percent(signal.toDouble()) : QCoreApplication::translate("SystemStats", "Unknown"));
        } else {
            stats << pair(QCoreApplication::translate("SystemStats", "Processes"), QString::number(p->top(QStringLiteral("network"), 1000).size()));
        }
        out[QStringLiteral("stats")] = stats;
        QVariantList specs{
            pair(QCoreApplication::translate("SystemStats", "Adapter"), m_iface),
            pair(QCoreApplication::translate("SystemStats", "Connection type"), m_wireless ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet")),
        };
        if (!ssid.isEmpty()) {
            specs << pair(QCoreApplication::translate("SystemStats", "Network name"), ssid);
        }
        specs << pair(QCoreApplication::translate("SystemStats", "IPv4 address"), ipv4.isEmpty() ? QCoreApplication::translate("SystemStats", "None") : ipv4);
        specs << pair(QCoreApplication::translate("SystemStats", "IPv6 address"), ipv6.isEmpty() ? QCoreApplication::translate("SystemStats", "None") : ipv6);
        out[QStringLiteral("specs")] = specs;
        out[QStringLiteral("topTitle")] = QCoreApplication::translate("SystemStats", "Using the network most");
        out[QStringLiteral("topMetric")] = QStringLiteral("network");
        out[QStringLiteral("topTotal")] = qMax(m_rx + m_tx, 1.0);
        return out;
    }
    if (id == QLatin1String("gpu")) {
        const qint64 usedVram = sensorValue(QStringLiteral("gpu/gpu0/usedVram")).toLongLong();
        const qint64 totalVram = sensorValue(QStringLiteral("gpu/gpu0/totalVram")).toLongLong();
        const double temperature = sensorValue(QStringLiteral("gpu/gpu0/temperature")).toDouble();
        const double clock = sensorValue(QStringLiteral("gpu/gpu0/coreFrequency")).toDouble();
        const double power = sensorValue(QStringLiteral("gpu/gpu0/power")).toDouble();
        out[QStringLiteral("title")] = QCoreApplication::translate("SystemStats", "Graphics");
        out[QStringLiteral("subtitle")] = m_gpuName;
        out[QStringLiteral("caption")] = QCoreApplication::translate("SystemStats", "% utilisation over 60 seconds");
        out[QStringLiteral("scale")] = QStringLiteral("100%");
        out[QStringLiteral("history")] = toList(m_gpuHistory);
        out[QStringLiteral("max")] = 100;
        out[QStringLiteral("colour")] = QStringLiteral("gpu");
        const QString na = QCoreApplication::translate("SystemStats", "Unknown");
        out[QStringLiteral("stats")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "Utilisation"), percent(m_gpu)),
            pair(QCoreApplication::translate("SystemStats", "Graphics memory"), totalVram > 0 ? QCoreApplication::translate("SystemStats", "%1 of %2").arg(bytes(usedVram), bytes(totalVram)) : QCoreApplication::translate("SystemStats", "Shared")),
            pair(QCoreApplication::translate("SystemStats", "Temperature"), temperature > 0 ? QCoreApplication::translate("SystemStats", "%1°C").arg(number(temperature, 0)) : na),
            pair(QCoreApplication::translate("SystemStats", "Clock speed"), clock > 0 ? QCoreApplication::translate("SystemStats", "%1 MHz").arg(number(clock, 0)) : na),
            pair(QCoreApplication::translate("SystemStats", "Power"), power > 0 ? QCoreApplication::translate("SystemStats", "%1 W").arg(number(power, 1)) : na),
            pair(QCoreApplication::translate("SystemStats", "Apps using it"), QString::number(p->top(QStringLiteral("gpu"), 1000).size())),
        };
        out[QStringLiteral("specs")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "Driver"), m_gpuDriver.isEmpty() ? QCoreApplication::translate("SystemStats", "Unknown") : m_gpuDriver),
            pair(QCoreApplication::translate("SystemStats", "Memory"), totalVram > 0 ? QCoreApplication::translate("SystemStats", "%1 dedicated").arg(bytes(totalVram)) : QCoreApplication::translate("SystemStats", "Shared with the system")),
            pair(QCoreApplication::translate("SystemStats", "Device"), m_gpuCard),
        };
        out[QStringLiteral("topTitle")] = QCoreApplication::translate("SystemStats", "Using the most graphics");
        out[QStringLiteral("topMetric")] = QStringLiteral("gpu");
        out[QStringLiteral("topTotal")] = qMax(m_gpu, 1.0);
        return out;
    }
    if (id == QLatin1String("winmode")) {
        KConfig config(QStringLiteral("wildos-appbridgerc"));
        const bool enabled = config.group(QStringLiteral("General")).readEntry("Enabled", true);
        const WinMode::Status &status = p->winModeStatus();
        const QList<WinMode::App> apps = WinMode::installed();
        QStringList open;
        for (const QString &program : status.openPrograms) {
            const QString app = WinMode::appFor(apps, program);
            const QString key = app.isEmpty() ? program.toLower() : app;
            if (!open.contains(key)) {
                open << key;
            }
        }
        out[QStringLiteral("title")] = QCoreApplication::translate("SystemStats", "Windows mode");
        out[QStringLiteral("subtitle")] = QCoreApplication::translate("SystemStats", "Windows apps, in Windows running in the background");
        out[QStringLiteral("caption")] = QCoreApplication::translate("SystemStats", "% CPU used over 60 seconds");
        out[QStringLiteral("scale")] = QStringLiteral("100%");
        out[QStringLiteral("history")] = toList(m_winHistory);
        out[QStringLiteral("max")] = 100;
        out[QStringLiteral("colour")] = QStringLiteral("winmode");
        out[QStringLiteral("stats")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "CPU"), percent(p->winModeCpu())),
            pair(QCoreApplication::translate("SystemStats", "Memory"), bytes(p->winModeMemory())),
            pair(QCoreApplication::translate("SystemStats", "State"), WinMode::stateText(status.vm)),
            pair(QCoreApplication::translate("SystemStats", "Apps open"), QString::number(open.size())),
            pair(QCoreApplication::translate("SystemStats", "Apps installed"), QString::number(m_winInstalled)),
        };
        out[QStringLiteral("specs")] = QVariantList{
            pair(QCoreApplication::translate("SystemStats", "Windows apps"), enabled ? QCoreApplication::translate("SystemStats", "On") : QCoreApplication::translate("SystemStats", "Off")),
            pair(QCoreApplication::translate("SystemStats", "When none are open"), WinMode::idleText(status.whenIdle, status.idleSeconds)),
        };
        out[QStringLiteral("topTitle")] = QCoreApplication::translate("SystemStats", "Using the most memory");
        out[QStringLiteral("topMetric")] = QStringLiteral("winmode");
        out[QStringLiteral("topTotal")] = double(qMax<qint64>(p->winModeMemory(), 1));
        return out;
    }
    return out;
}
