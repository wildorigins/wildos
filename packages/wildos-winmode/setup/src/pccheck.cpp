#include "pccheck.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPair>
#include <QSet>

#include <cerrno>
#include <cstring>
#include <cpuid.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace PcCheck
{
Virtualisation virtualisation()
{
    Virtualisation result;
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    char vendor[13] = {};
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        memcpy(vendor, &ebx, 4);
        memcpy(vendor + 4, &edx, 4);
        memcpy(vendor + 8, &ecx, 4);
    }
    bool capable = false;
    if (qstrcmp(vendor, "GenuineIntel") == 0 && __get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        result.technology = QStringLiteral("Intel VT-x");
        capable = ecx & (1u << 5);
    } else if (qstrcmp(vendor, "AuthenticAMD") == 0 && __get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx)) {
        result.technology = QStringLiteral("AMD-V");
        capable = ecx & (1u << 2);
    }

    const int kvm = ::open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm < 0) {
        if (errno == EACCES || errno == EPERM) {
            result.state = QStringLiteral("noaccess");
        } else {
            // Firmware with VT-x switched off still reports the feature to
            // CPUID on most PCs; KVM then refuses to load, so there is no
            // /dev/kvm. A processor with no feature at all is a different
            // problem with a different answer.
            result.state = capable || result.technology.isEmpty() ? QStringLiteral("off") : QStringLiteral("unsupported");
        }
        return result;
    }
    const bool apiOk = ::ioctl(kvm, KVM_GET_API_VERSION, 0) == KVM_API_VERSION;
    const int vm = apiOk ? ::ioctl(kvm, KVM_CREATE_VM, 0) : -1;
    if (vm >= 0) {
        ::close(vm);
        result.state = QStringLiteral("ok");
    } else {
        result.state = QStringLiteral("off");
    }
    ::close(kvm);
    return result;
}

int processorCores()
{
    // A core is a package and core id pair. The second thread of a core
    // that Wild OS keeps offline has no topology folder, so is skipped.
    QSet<QPair<QByteArray, QByteArray>> cores;
    QDirIterator it(QStringLiteral("/sys/devices/system/cpu"), {QStringLiteral("cpu[0-9]*")}, QDir::Dirs);
    while (it.hasNext()) {
        const QString topology = it.next() + QStringLiteral("/topology/");
        QFile package(topology + QStringLiteral("physical_package_id"));
        QFile core(topology + QStringLiteral("core_id"));
        if (package.open(QIODevice::ReadOnly) && core.open(QIODevice::ReadOnly)) {
            cores.insert({package.readAll().trimmed(), core.readAll().trimmed()});
        }
    }
    return int(cores.size());
}

qint64 memoryBytes()
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    // /proc files report a size of 0, so read them whole rather than by
    // line until atEnd().
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (line.startsWith("MemTotal:")) {
            return line.mid(9).trimmed().split(' ').value(0).toLongLong() * 1024;
        }
    }
    return 0;
}

QString memoryLabel(qint64 bytes)
{
    const double gib = double(bytes) / (1024.0 * 1024 * 1024);
    // Firmware and graphics keep part of the memory, so the kernel sees a
    // little less than was fitted: 15.5 GB of a 16 GB PC.
    static const int sizes[] = {2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 48, 64, 96, 128, 192, 256};
    int fitted = 0;
    for (int size : sizes) {
        if (gib >= size * 0.9) {
            fitted = size;
        }
    }
    return fitted > 0 ? QCoreApplication::translate("PcCheck", "%1 GB").arg(fitted) : sizeLabel(bytes);
}

qint64 freeBytes(const QString &path)
{
    QString existing = path;
    while (!existing.isEmpty() && !QFileInfo::exists(existing)) {
        existing = QFileInfo(existing).path();
    }
    struct statvfs info {};
    if (::statvfs(QFile::encodeName(existing).constData(), &info) != 0) {
        return -1;
    }
    return qint64(info.f_bavail) * qint64(info.f_frsize);
}

QString sizeLabel(qint64 bytes)
{
    const QLocale locale = QLocale::system();
    const double gb = double(bytes) / 1e9;
    if (gb >= 10) {
        return QCoreApplication::translate("PcCheck", "%1 GB").arg(qRound64(gb));
    }
    if (gb >= 1) {
        return QCoreApplication::translate("PcCheck", "%1 GB").arg(locale.toString(gb, 'f', 1));
    }
    return QCoreApplication::translate("PcCheck", "%1 MB").arg(qMax<qint64>(1, qRound64(double(bytes) / 1e6)));
}

qint64 folderBytes(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (!info.isSymLink()) {
            total += info.size();
        }
    }
    return total;
}

qint64 neededBytes(bool office)
{
    // Windows takes about 20 GB once set up, Microsoft 365 another 4, and
    // the download needs 6 more until it is deleted after setup.
    return office ? qint64(30) * 1000 * 1000 * 1000 : qint64(26) * 1000 * 1000 * 1000;
}

Firmware parseFirmware(const QByteArray &output)
{
    Firmware result;
    bool sawMsdm = false;
    for (const QByteArray &line : output.split('\n')) {
        const int eq = line.indexOf('=');
        if (eq <= 0) {
            continue;
        }
        const QByteArray key = line.left(eq);
        const QByteArray value = line.mid(eq + 1).trimmed();
        if (key == "msdm") {
            sawMsdm = true;
            result.msdm = QByteArray::fromBase64(value);
        } else if (key == "windows") {
            result.windowsInstalled = value == "yes";
        } else if (key == "edition") {
            result.editionId = QString::fromUtf8(value);
        } else if (key == "description") {
            result.description = QString::fromUtf8(value);
        }
    }
    result.ok = sawMsdm;
    if (!sawMsdm) {
        result.error = QStringLiteral("The licence check did not answer.");
    }
    return result;
}
}
