#include "sensors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace Cooler
{
namespace
{
// Without a limit from the sensor: where current desktop processors start
// to slow themselves down.
constexpr double usualLimit = 95;
// The NVIDIA driver is asked less often than the kernel's sensors: a
// graphics card's temperature moves slowly and each question goes to the
// card.
constexpr int nvidiaEverySeconds = 5;
constexpr int historySeconds = 60;

void remember(QList<double> &history, double value)
{
    if (value < 0) {
        return;
    }
    history << value;
    if (history.size() > historySeconds) {
        history.removeFirst();
    }
}

QByteArray firstLine(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    return in.readLine(256).trimmed();
}

// hwmon gives thousandths of a degree.
double degrees(const QString &file)
{
    bool ok = false;
    const double value = firstLine(file).toDouble(&ok);
    return ok ? value / 1000.0 : -1;
}
}

Sensors::Sensors(QObject *parent)
    : QObject(parent)
    , m_root(qEnvironmentVariable("WILDOS_COOLER_SYSROOT"))
{
    m_timer.setInterval(1000);
    // Readings a moment late are fine; waking with other timers is cheaper.
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Sensors::read);

    m_highestFile = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/wildos-cooler/cpu-highest");
    const QList<QByteArray> kept = firstLine(m_highestFile).split(' ');
    m_highestDay = QDate::currentDate();
    if (kept.size() == 2 && QDate::fromString(QString::fromLatin1(kept.at(0)), Qt::ISODate) == m_highestDay) {
        m_highest = kept.at(1).toDouble();
    }
}

Sensors::~Sensors()
{
    stop();
}

void Sensors::start()
{
    if (m_timer.isActive()) {
        return;
    }
    if (!m_found) {
        find();
    }
    startNvidia();
    // A minute that ended before a pause is not the last minute.
    m_cpuHistory.clear();
    m_gpuHistory.clear();
    // The first load figure needs two readings to compare.
    readLoad();
    read();
    m_timer.start();
}

void Sensors::stop()
{
    m_timer.stop();
    stopNvidia();
}

// The processor's own sensor is coretemp on Intel (the whole package is the
// one labelled "Package id 0") and k10temp or zenpower on AMD (temp1, the
// control temperature the fans are meant to follow). The graphics card's is
// the driver's hwmon where it has one; NVIDIA's own driver has none and is
// asked through its management library, NVML.
void Sensors::find()
{
    m_found = true;
    const QDir hwmon(m_root + QStringLiteral("/sys/class/hwmon"));
    const QStringList entries = hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString dir = hwmon.filePath(entry);
        const QByteArray name = firstLine(dir + QStringLiteral("/name"));
        if (m_cpuFile.isEmpty() && (name == "coretemp" || name == "k10temp" || name == "zenpower")) {
            QString sensor = QStringLiteral("temp1");
            const QStringList labels = QDir(dir).entryList({QStringLiteral("temp*_label")}, QDir::Files, QDir::Name);
            for (const QString &label : labels) {
                if (firstLine(dir + QLatin1Char('/') + label).startsWith("Package id")) {
                    sensor = label.left(label.indexOf(QLatin1Char('_')));
                    break;
                }
            }
            if (QFileInfo::exists(dir + QLatin1Char('/') + sensor + QStringLiteral("_input"))) {
                m_cpuFile = dir + QLatin1Char('/') + sensor + QStringLiteral("_input");
                const double limit = degrees(dir + QLatin1Char('/') + sensor + QStringLiteral("_crit"));
                m_cpuLimit = limit > 0 ? limit : usualLimit;
            }
        } else if (m_gpuFile.isEmpty() && (name == "amdgpu" || name == "nouveau" || name == "radeon")
                   && QFileInfo::exists(dir + QStringLiteral("/temp1_input"))) {
            m_gpuFile = dir + QStringLiteral("/temp1_input");
        }
    }
}

// NVML is what nvidia-smi itself reads from, and it comes with the driver
// (libnvidia-ml1) however the driver was packaged: NVIDIA's own repository
// leaves nvidia-smi out, so the program cannot be relied on. The library
// is opened at start() and let go at stop().
void Sensors::startNvidia()
{
    if (!m_gpuFile.isEmpty() || m_nvml || !m_root.isEmpty()) {
        return;
    }
    auto *library = new QLibrary(QStringLiteral("nvidia-ml"), 1);
    const auto init = reinterpret_cast<int (*)()>(library->resolve("nvmlInit_v2"));
    const auto handle = reinterpret_cast<int (*)(unsigned int, void **)>(library->resolve("nvmlDeviceGetHandleByIndex_v2"));
    m_nvmlTemperature = reinterpret_cast<int (*)(void *, int, unsigned int *)>(library->resolve("nvmlDeviceGetTemperature"));
    m_nvmlShutdown = reinterpret_cast<int (*)()>(library->resolve("nvmlShutdown"));
    if (!init || !handle || !m_nvmlTemperature || !m_nvmlShutdown || init() != 0) {
        library->unload();
        delete library;
        return;
    }
    m_nvml = library;
    if (handle(0, &m_nvmlDevice) != 0) {
        stopNvidia();
        return;
    }
    m_nvidiaWait = 0;
}

void Sensors::stopNvidia()
{
    if (!m_nvml) {
        return;
    }
    m_nvmlShutdown();
    m_nvml->unload();
    delete m_nvml;
    m_nvml = nullptr;
    m_nvmlDevice = nullptr;
    m_gpuTemperature = -1;
}

void Sensors::readNvidia()
{
    if (!m_nvmlDevice || m_nvidiaWait-- > 0) {
        return;
    }
    m_nvidiaWait = nvidiaEverySeconds - 1;
    // NVML_TEMPERATURE_GPU is 0. Where the card is passed through without
    // its sensor (WSL, some virtual machines) NVML answers 0 degrees, which
    // a working card never is.
    unsigned int value = 0;
    m_gpuTemperature = m_nvmlTemperature(m_nvmlDevice, 0, &value) == 0 && value > 0 ? double(value) : -1;
}

void Sensors::read()
{
    if (!m_cpuFile.isEmpty()) {
        m_cpuTemperature = degrees(m_cpuFile);
        noteHighest();
    }
    if (!m_gpuFile.isEmpty()) {
        m_gpuTemperature = degrees(m_gpuFile);
    }
    readNvidia();
    remember(m_cpuHistory, m_cpuTemperature);
    remember(m_gpuHistory, m_gpuTemperature);
    readLoad();
    Q_EMIT changed();
}

void Sensors::readLoad()
{
    QFile stat(m_root + QStringLiteral("/proc/stat"));
    if (!stat.open(QIODevice::ReadOnly)) {
        return;
    }
    QList<Ticks> cores;
    Ticks all;
    while (true) {
        const QByteArray line = stat.readLine(512);
        if (!line.startsWith("cpu")) {
            break;
        }
        const QList<QByteArray> fields = line.simplified().split(' ');
        Ticks ticks;
        // user nice system idle iowait irq softirq steal; the two idle ones
        // are the fourth and fifth.
        for (int i = 1; i < fields.size() && i <= 8; ++i) {
            const quint64 value = fields.at(i).toULongLong();
            ticks.total += value;
            if (i != 4 && i != 5) {
                ticks.busy += value;
            }
        }
        if (fields.at(0) == "cpu") {
            all = ticks;
        } else {
            cores << ticks;
        }
    }
    const auto load = [](const Ticks &now, const Ticks &before) {
        const quint64 total = now.total - before.total;
        return total > 0 && now.total >= before.total ? 100.0 * double(now.busy - before.busy) / double(total) : 0.0;
    };
    if (m_lastAll.total > 0) {
        m_cpuLoad = load(all, m_lastAll);
    }
    if (m_lastCores.size() == cores.size()) {
        QList<double> loads;
        loads.reserve(cores.size());
        for (qsizetype i = 0; i < cores.size(); ++i) {
            loads << load(cores.at(i), m_lastCores.at(i));
        }
        m_coreLoads = loads;
    }
    m_lastAll = all;
    m_lastCores = cores;
}

// Written only when a whole degree higher than before, so a day costs a few
// dozen small writes at most.
void Sensors::noteHighest()
{
    const QDate today = QDate::currentDate();
    const bool newDay = today != m_highestDay;
    if (!newDay && std::floor(m_cpuTemperature) <= std::floor(m_highest)) {
        m_highest = std::max(m_highest, m_cpuTemperature);
        return;
    }
    m_highestDay = today;
    m_highest = m_cpuTemperature;
    QDir().mkpath(QFileInfo(m_highestFile).path());
    QSaveFile out(m_highestFile);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(today.toString(Qt::ISODate).toLatin1() + ' ' + QByteArray::number(m_highest, 'f', 1) + '\n');
        out.commit();
    }
}
}
