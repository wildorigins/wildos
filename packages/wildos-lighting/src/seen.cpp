#include "seen.h"

#include <QDir>
#include <QFile>

namespace Seen
{
namespace
{
QString readLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLatin1(file.readLine(256)).trimmed();
}

struct Card {
    const char *vendor;
    const char *device;
    const char *subVendor;
    const char *subDevice;
    const char *name;
    // The lights, by whether each can only do white.
    QList<bool> white;
};

// Founders Edition cards are told apart by NVIDIA's own subsystem IDs.
const QList<Card> &cards()
{
    static const QList<Card> list = {
        {"0x10de", "0x2204", "0x10de", "0x147d", "GeForce RTX 3090", {false, true}},
    };
    return list;
}
}

QList<Part> graphicsCards(const QString &root)
{
    QList<Part> parts;
    const QDir devices(root + QStringLiteral("/bus/pci/devices"));
    const QStringList entries = devices.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString dir = devices.filePath(entry);
        // Display controllers only: class 0x03xxxx.
        if (!readLine(dir + QStringLiteral("/class")).startsWith(QLatin1String("0x03"))) {
            continue;
        }
        const QString vendor = readLine(dir + QStringLiteral("/vendor"));
        const QString device = readLine(dir + QStringLiteral("/device"));
        const QString subVendor = readLine(dir + QStringLiteral("/subsystem_vendor"));
        const QString subDevice = readLine(dir + QStringLiteral("/subsystem_device"));
        for (const Card &card : cards()) {
            if (vendor != QLatin1String(card.vendor) || device != QLatin1String(card.device) || subVendor != QLatin1String(card.subVendor)
                || subDevice != QLatin1String(card.subDevice)) {
                continue;
            }
            Part part;
            part.key = QStringLiteral("pci ") + entry;
            part.name = QString::fromLatin1(card.name);
            for (const bool white : card.white) {
                part.lights.append({QString(), white});
            }
            parts.append(part);
        }
    }
    return parts;
}

double processorTemperature(const QString &root)
{
    static const QStringList drivers = {QStringLiteral("coretemp"), QStringLiteral("k10temp"), QStringLiteral("zenpower"), QStringLiteral("cpu_thermal")};
    const QDir monitors(root + QStringLiteral("/class/hwmon"));
    const QStringList entries = monitors.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString dir = monitors.filePath(entry);
        if (!drivers.contains(readLine(dir + QStringLiteral("/name")))) {
            continue;
        }
        bool ok = false;
        const int milli = readLine(dir + QStringLiteral("/temp1_input")).toInt(&ok);
        if (ok && milli > 0 && milli < 150000) {
            return milli / 1000.0;
        }
    }
    return -1;
}
}
