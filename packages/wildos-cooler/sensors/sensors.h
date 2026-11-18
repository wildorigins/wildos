#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QLibrary;

// The readings that do not come from the cooler: the processor's
// temperature and load and the graphics card's temperature, read on this PC
// from the kernel (hwmon in /sys/class/hwmon, /proc/stat) and, for NVIDIA
// cards, from the driver's NVML library. Shared by the Cooler window and the
// program that draws the faces. Nothing is read until
// start(), and nothing after stop(), so a program that is showing nothing
// costs nothing.
//
// WILDOS_COOLER_SYSROOT puts a folder in front of /sys and /proc, for tests.
namespace Cooler
{
class Sensors : public QObject
{
    Q_OBJECT

public:
    explicit Sensors(QObject *parent = nullptr);
    ~Sensors() override;

    // Reads once a second from now on; changed() follows each reading.
    void start();
    void stop();
    bool running() const { return m_timer.isActive(); }

    // Degrees Celsius; negative when this PC has no sensor Cooler knows.
    double cpuTemperature() const { return m_cpuTemperature; }
    // The temperature at which the processor slows itself down.
    double cpuLimit() const { return m_cpuLimit; }
    // The highest since midnight, kept across restarts of either program.
    double cpuHighestToday() const { return m_highest; }
    // Per cent busy since the last reading, overall and for each core.
    double cpuLoad() const { return m_cpuLoad; }
    QList<double> coreLoads() const { return m_coreLoads; }
    // Negative without a graphics card that reports one.
    double gpuTemperature() const { return m_gpuTemperature; }
    // The last minute of each temperature since start(), a value a second,
    // oldest first. Empty where there is no sensor.
    QList<double> cpuHistory() const { return m_cpuHistory; }
    QList<double> gpuHistory() const { return m_gpuHistory; }

Q_SIGNALS:
    void changed();

private:
    struct Ticks {
        quint64 busy = 0;
        quint64 total = 0;
    };

    void find();
    void read();
    void readLoad();
    void noteHighest();
    void startNvidia();
    void stopNvidia();
    void readNvidia();

    QTimer m_timer;
    QString m_root;
    bool m_found = false;
    QString m_cpuFile;
    QString m_gpuFile;
    QLibrary *m_nvml = nullptr;
    void *m_nvmlDevice = nullptr;
    int (*m_nvmlTemperature)(void *, int, unsigned int *) = nullptr;
    int (*m_nvmlShutdown)() = nullptr;
    int m_nvidiaWait = 0;

    double m_cpuTemperature = -1;
    double m_cpuLimit = 95;
    double m_cpuLoad = 0;
    QList<double> m_coreLoads;
    double m_gpuTemperature = -1;
    QList<double> m_cpuHistory;
    QList<double> m_gpuHistory;
    Ticks m_lastAll;
    QList<Ticks> m_lastCores;

    QString m_highestFile;
    QDate m_highestDay;
    double m_highest = 0;
};
}
