#pragma once

#include <QByteArray>
#include <QString>

// The checks of board 9.1, each against the real PC.
namespace PcCheck
{
struct Virtualisation {
    // ok, off (the processor has it but the firmware switched it off),
    // unsupported (the processor has none), or noaccess (switched on, but
    // this account cannot use it, which means it is not at the PC's own
    // screen)
    QString state;
    // "Intel VT-x", "AMD-V" or empty
    QString technology;
    // Wild OS itself runs in a virtual machine (CPUID's hypervisor bit), so
    // Windows would be a virtual PC inside a virtual PC: it works where the
    // outer one passes virtualisation on, many times slower.
    bool nested = false;
};

// CPUID says whether the processor has VT-x or AMD-V; creating a VM through
// /dev/kvm says whether it is switched on and usable. /proc/cpuinfo is not
// read, as Kicksecure can hide it.
Virtualisation virtualisation();

// The processor's cores, counted once each however many threads they run,
// as board 9.1 says "8 cores". Wild OS runs one thread per core unless
// someone turns hyper-threading on, and a second thread is not a second
// core for Windows' sake. 0 when it can't be read.
int processorCores();
// Windows mode runs 64-bit Windows 11, which wants 4 cores at least.
constexpr int neededCores = 4;

qint64 memoryBytes();
// What the PC was sold with, "16 GB", from the slightly smaller total the
// kernel reports.
QString memoryLabel(qint64 bytes);
qint64 freeBytes(const QString &path);
// "412 GB", "18 GB", "9.4 GB", "850 MB"
QString sizeLabel(qint64 bytes);
// Total size of the files under a folder, without following links.
qint64 folderBytes(const QString &path);

// Space setup needs on the disk holding the home folder.
qint64 neededBytes(bool office);

// The output of winmode-firmware (see data/winmode-firmware).
struct Firmware {
    bool ok = false;
    QString error;
    QByteArray msdm;
    bool windowsInstalled = false;
    QString editionId;
    QString description;
};
Firmware parseFirmware(const QByteArray &output);
}
