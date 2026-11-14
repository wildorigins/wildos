#include "devices.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <libudev.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>

#include <algorithm>
#include <functional>

using ManagedObjects = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
Q_DECLARE_METATYPE(ManagedObjects)

namespace
{
const QString socketPath = QStringLiteral("/run/wildos-devices.sock");
// What the shield's "Allow once" runs for a device USB protection blocked.
const QString allowHelper = QStringLiteral("/usr/libexec/wildos/usb-devices");

// The order the page lists devices in: what people look for first.
const QStringList kindOrder = {
    QStringLiteral("graphics"),   QStringLiteral("wifi"),       QStringLiteral("ethernet"),   QStringLiteral("bluetooth"),
    QStringLiteral("sound"),      QStringLiteral("camera"),     QStringLiteral("fingerprint"), QStringLiteral("storage"),
    QStringLiteral("touchpad"),   QStringLiteral("keyboard"),   QStringLiteral("keyboardmouse"), QStringLiteral("mouse"),
    QStringLiteral("touchscreen"), QStringLiteral("pen"),       QStringLiteral("controller"), QStringLiteral("battery"),
    QStringLiteral("printer"),    QStringLiteral("scanner"),    QStringLiteral("smartcard"),  QStringLiteral("usbdrive"),
    QStringLiteral("card"),       QStringLiteral("hdmisound"),  QStringLiteral("cardreader"), QStringLiteral("usb"),
    QStringLiteral("usbctl"),     QStringLiteral("storagectl"), QStringLiteral("firewire"),
};

struct Dev {
    QString key;
    QString kind;
    QString name;
    QString icon;
    QString line;
    QString state = QStringLiteral("ok");
    QString badge;
    QString problem;
    QString action;
    QString actionLabel;
    QString model;
    QString maker;
    QString driver;
    QString connection;
    QString hardwareId;
    QVariantList extra;
    // How it is attached: "pci", "usb", "builtin" (serio, i2c, platform),
    // "bluetooth".
    QString bus;
    QString vendorId;
    QString productId;
    // The device, or the USB interface, that wants a driver and has none.
    QString unboundPath;
    bool virtualHardware = false;
    // Ahead of others of its kind: the drive Wild OS runs from.
    bool first = false;
    bool builtIn = false;
    // Interfaces of a USB device that should have a driver and have none.
    bool unboundInterface = false;
    // Its job's output (a network adapter, a sound card...) is missing.
    bool functionMissing = false;
    // Every bound driver name, for a USB device with several interfaces.
    QStringList drivers;
    QString netInterface;
    QString wireless;
    QString hci;
    QString blockName;
    QString port;

    QVariantMap toMap() const
    {
        return {
            {QStringLiteral("key"), key},           {QStringLiteral("kind"), kind},
            {QStringLiteral("name"), name},         {QStringLiteral("icon"), icon},
            {QStringLiteral("line"), line},         {QStringLiteral("state"), state},
            {QStringLiteral("badge"), badge},       {QStringLiteral("problem"), problem},
            {QStringLiteral("action"), action},     {QStringLiteral("actionLabel"), actionLabel},
            {QStringLiteral("model"), model},       {QStringLiteral("maker"), maker},
            {QStringLiteral("driver"), driver},     {QStringLiteral("connection"), connection},
            {QStringLiteral("hardwareId"), hardwareId}, {QStringLiteral("extra"), extra},
            {QStringLiteral("port"), port},
        };
    }
};

QString text(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

QString real(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

QString linkName(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? QFileInfo(info.symLinkTarget()).fileName() : QString();
}

bool under(const QString &path, const QString &ancestor)
{
    return !ancestor.isEmpty() && (path == ancestor || path.startsWith(ancestor + QLatin1Char('/')));
}

// The USB device a sysfs path belongs to (the folder with idVendor), for
// interfaces, HID devices, network adapters and disks below it.
QString usbDeviceOf(QString path)
{
    while (path.size() > 1 && path.contains(QLatin1String("/usb"))) {
        if (QFile::exists(path + QStringLiteral("/idVendor")) && !QFileInfo(path).fileName().startsWith(QLatin1String("usb"))) {
            return path;
        }
        path = path.section(QLatin1Char('/'), 0, -2);
    }
    return {};
}

QString pciAddressIn(const QString &path)
{
    static const QRegularExpression address(QStringLiteral("^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7]$"));
    const QStringList parts = path.split(QLatin1Char('/'));
    for (int i = parts.size() - 1; i >= 0; --i) {
        if (address.match(parts.at(i)).hasMatch()) {
            return parts.at(i);
        }
    }
    return {};
}

// "Intel Corporation" -> "Intel", "Advanced Micro Devices, Inc. [AMD/ATI]"
// -> "AMD", "Realtek Semiconductor Co., Ltd." -> "Realtek".
QString shortMaker(QString maker)
{
    static const QRegularExpression bracket(QStringLiteral("\\[([^\\]/]+)"));
    const QRegularExpressionMatch m = bracket.match(maker);
    if (m.hasMatch()) {
        return m.captured(1).trimmed();
    }
    static const QRegularExpression suffix(QStringLiteral(
        "[,.]?\\s+(Corporation|Corp\\.?|Inc\\.?|Incorporated|Co\\.?,?|Ltd\\.?|Limited|GmbH|AG|S\\.A\\.|Semiconductor|Technology|Technologies|"
        "Electronics|International|Microelectronics|Systems)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    QString before;
    while (before != maker) {
        before = maker;
        maker.remove(suffix);
        maker = maker.trimmed();
    }
    return maker;
}

// "Alder Lake-P GT2 [Iris Xe Graphics]" -> "Iris Xe Graphics".
QString shortModel(const QString &model)
{
    static const QRegularExpression bracket(QStringLiteral("\\[([^\\]]+)\\]\\s*$"));
    const QRegularExpressionMatch m = bracket.match(model);
    return m.hasMatch() ? m.captured(1).trimmed() : model.trimmed();
}

// Units of 1024 labelled GB, as System::formatBytes shows them, so a drive
// reads the same here as in Storage and Files.
QString sizeText(double bytes)
{
    const QLocale locale = QLocale::system();
    const double gb = 1024.0 * 1024 * 1024;
    const double tb = gb * 1024;
    if (bytes >= tb) {
        return QStringLiteral("%1 TB").arg(locale.toString(bytes / tb, 'f', 2));
    }
    return QStringLiteral("%1 GB").arg(locale.toString(bytes / gb, 'f', bytes >= 100 * gb ? 0 : 1));
}

bool isVirtualVendor(const QString &vendor)
{
    static const QSet<QString> vendors = {QStringLiteral("1af4"), QStringLiteral("1b36"), QStringLiteral("1234"), QStringLiteral("15ad"),
                                          QStringLiteral("80ee"), QStringLiteral("0627"), QStringLiteral("46f4"), QStringLiteral("0e0f")};
    return vendors.contains(vendor);
}

// Interface classes from a USB device's raw descriptors. A device USB
// protection has blocked has no interfaces in sysfs yet, but the kernel has
// read these already.
QSet<QString> descriptorClasses(const QString &path)
{
    QSet<QString> classes;
    QFile file(path + QStringLiteral("/descriptors"));
    if (!file.open(QIODevice::ReadOnly)) {
        return classes;
    }
    const QByteArray b = file.read(65536);
    for (int i = 0; i + 1 < b.size() && quint8(b.at(i)) >= 2; i += quint8(b.at(i))) {
        if (quint8(b.at(i + 1)) == 4 && i + 5 < b.size()) {
            QString cls = QStringLiteral("%1").arg(quint8(b.at(i + 5)), 2, 16, QLatin1Char('0'));
            classes.insert(cls == QLatin1String("0a") ? QStringLiteral("02") : cls);
        }
    }
    return classes;
}

// Names a USB device gives itself can hold anything; keep what a product
// name needs.
QString cleanName(QString name)
{
    static const QRegularExpression unwanted(QStringLiteral("[^\\p{L}\\p{N} ._,()+/&'-]"));
    name.remove(unwanted);
    return name.simplified().left(60);
}

struct Udev {
    struct udev *handle = udev_new();
    struct udev_hwdb *hwdb = handle ? udev_hwdb_new(handle) : nullptr;
    ~Udev()
    {
        if (hwdb) {
            udev_hwdb_unref(hwdb);
        }
        if (handle) {
            udev_unref(handle);
        }
    }
    // udev's database entry loses the hwdb names when a driver is unbound,
    // so they are looked up again by the device's modalias when missing.
    QHash<QString, QString> withNames(const QString &syspath) const
    {
        QHash<QString, QString> out = properties(syspath);
        if (out.contains(QStringLiteral("ID_MODEL_FROM_DATABASE")) || !hwdb) {
            return out;
        }
        const QByteArray alias = text(syspath + QStringLiteral("/modalias")).toUtf8();
        if (alias.isEmpty()) {
            return out;
        }
        for (struct udev_list_entry *e = udev_hwdb_get_properties_list_entry(hwdb, alias.constData(), 0); e; e = udev_list_entry_get_next(e)) {
            const QString key = QString::fromUtf8(udev_list_entry_get_name(e));
            if (!out.contains(key)) {
                out.insert(key, QString::fromUtf8(udev_list_entry_get_value(e)));
            }
        }
        return out;
    }
    QHash<QString, QString> properties(const QString &syspath) const
    {
        QHash<QString, QString> out;
        if (!handle) {
            return out;
        }
        struct udev_device *device = udev_device_new_from_syspath(handle, syspath.toUtf8().constData());
        if (!device) {
            return out;
        }
        for (struct udev_list_entry *e = udev_device_get_properties_list_entry(device); e; e = udev_list_entry_get_next(e)) {
            out.insert(QString::fromUtf8(udev_list_entry_get_name(e)), QString::fromUtf8(udev_list_entry_get_value(e)));
        }
        udev_device_unref(device);
        return out;
    }
};

QVariant dbusProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call << interface << name;
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

// What NetworkManager says about one network adapter, in a few words.
QString networkLine(const QString &interface, bool wifi, bool cable)
{
    const QString nm = QStringLiteral("org.freedesktop.NetworkManager");
    QDBusMessage call = QDBusMessage::createMethodCall(nm, QStringLiteral("/org/freedesktop/NetworkManager"), nm, QStringLiteral("GetDeviceByIpIface"));
    call << interface;
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    const QString device = reply.arguments().constFirst().value<QDBusObjectPath>().path();
    const QString active = dbusProperty(nm, device, nm + QStringLiteral(".Device"), QStringLiteral("ActiveConnection")).value<QDBusObjectPath>().path();
    if (active.isEmpty() || active == QLatin1String("/")) {
        if (wifi && !dbusProperty(nm, QStringLiteral("/org/freedesktop/NetworkManager"), nm, QStringLiteral("WirelessEnabled")).toBool()) {
            return QStringLiteral("Turned off");
        }
        if (!wifi && !cable) {
            return QStringLiteral("No cable plugged in");
        }
        return QStringLiteral("Not connected");
    }
    const QString id = dbusProperty(nm, active, nm + QStringLiteral(".Connection.Active"), QStringLiteral("Id")).toString();
    const uint state = dbusProperty(nm, active, nm + QStringLiteral(".Connection.Active"), QStringLiteral("State")).toUInt();
    if (!wifi) {
        return state == 2 ? QStringLiteral("Connected") : QStringLiteral("Connecting");
    }
    return (state == 2 ? QStringLiteral("Connected to %1") : QStringLiteral("Connecting to %1")).arg(id);
}

// BlueZ's adapters: hciN -> {powered, paired}.
QHash<QString, QPair<bool, int>> bluetoothAdapters()
{
    QHash<QString, QPair<bool, int>> out;
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                       QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return out;
    }
    const ManagedObjects objects = qdbus_cast<ManagedObjects>(reply.arguments().constFirst());
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QString path = it.key().path();
        if (it.value().contains(QStringLiteral("org.bluez.Adapter1"))) {
            out[path.section(QLatin1Char('/'), -1)].first = it.value().value(QStringLiteral("org.bluez.Adapter1")).value(QStringLiteral("Powered")).toBool();
        }
    }
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap device = it.value().value(QStringLiteral("org.bluez.Device1"));
        if (!device.isEmpty() && device.value(QStringLiteral("Paired")).toBool()) {
            out[it.key().path().section(QLatin1Char('/'), 3, 3)].second += 1;
        }
    }
    return out;
}

// settings-devices' answer: kernel firmware failures and drive health.
QJsonObject askRoot(bool *answered)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    *answered = false;
    if (!socket.waitForConnected(2000)) {
        return {};
    }
    QByteArray data;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 20000) {
        if (socket.bytesAvailable() || socket.waitForReadyRead(500)) {
            data += socket.readAll();
            if (data.endsWith('\n')) {
                break;
            }
        } else if (socket.state() != QLocalSocket::ConnectedState) {
            break;
        }
    }
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    *answered = doc.isObject();
    return doc.object();
}

// Space used on each disk by what is mounted from it, through LUKS and LVM.
QHash<QString, QPair<double, double>> spaceByDisk(QSet<QString> *systemDisks)
{
    QHash<QString, QPair<double, double>> out;
    QFile mounts(QStringLiteral("/proc/self/mountinfo"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    QSet<dev_t> seen;
    const std::function<QStringList(const QString &)> disksOf = [&disksOf](const QString &name) -> QStringList {
        const QString path = real(QStringLiteral("/sys/class/block/") + name);
        if (path.isEmpty()) {
            return {};
        }
        if (QFile::exists(path + QStringLiteral("/partition"))) {
            return {QFileInfo(path.section(QLatin1Char('/'), 0, -2)).fileName()};
        }
        const QStringList slaves = QDir(path + QStringLiteral("/slaves")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (slaves.isEmpty()) {
            return {name};
        }
        QStringList all;
        for (const QString &slave : slaves) {
            all << disksOf(slave);
        }
        return all;
    };
    // procfs files have no size, so QFile::atEnd() is true before reading.
    const QStringList lines = QString::fromUtf8(mounts.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList halves = line.split(QStringLiteral(" - "));
        if (halves.size() < 2) {
            continue;
        }
        const QStringList left = halves.at(0).split(QLatin1Char(' '));
        const QStringList right = halves.at(1).split(QLatin1Char(' '));
        if (left.size() < 5 || right.size() < 2 || !right.at(1).startsWith(QLatin1String("/dev/"))) {
            continue;
        }
        struct stat st;
        if (::stat(right.at(1).toUtf8().constData(), &st) != 0 || !S_ISBLK(st.st_mode) || seen.contains(st.st_rdev)) {
            continue;
        }
        seen.insert(st.st_rdev);
        const QString mountPoint = QString(left.at(4)).replace(QStringLiteral("\\040"), QStringLiteral(" "));
        struct statvfs vfs;
        if (::statvfs(mountPoint.toUtf8().constData(), &vfs) != 0 || vfs.f_blocks == 0) {
            continue;
        }
        const double total = double(vfs.f_blocks) * vfs.f_frsize;
        const double used = total - double(vfs.f_bfree) * vfs.f_frsize;
        const QString name = linkName(QStringLiteral("/sys/dev/block/%1:%2").arg(major(st.st_rdev)).arg(minor(st.st_rdev)));
        for (const QString &disk : disksOf(name)) {
            if (mountPoint == QLatin1String("/")) {
                systemDisks->insert(disk);
            }
            out[disk].first += used;
            out[disk].second += total;
        }
    }
    return out;
}

struct LibfprintReaders {
    QSet<QString> supported;
    QSet<QString> unsupported;
};

// libfprint's hwdb lists the readers it drives, then under an "unsupported"
// comment the ones it knows of but can't (as Fingerprint reads it).
LibfprintReaders libfprintReaders()
{
    LibfprintReaders readers;
    QFile hwdb(QStringLiteral("/usr/lib/udev/hwdb.d/60-autosuspend-libfprint-2.hwdb"));
    if (!hwdb.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return readers;
    }
    bool unsupportedSection = false;
    while (!hwdb.atEnd()) {
        const QString line = QString::fromLatin1(hwdb.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            unsupportedSection = unsupportedSection || line.contains(QLatin1String("unsupported"), Qt::CaseInsensitive);
        } else if (line.startsWith(QLatin1String("usb:v")) && line.size() >= 14) {
            const QString id = (line.mid(5, 4) + QLatin1Char(':') + line.mid(10, 4)).toLower();
            (unsupportedSection ? readers.unsupported : readers.supported).insert(id);
        }
    }
    return readers;
}

QString usbConnection(const QString &path)
{
    const QString port = QFileInfo(path).fileName();
    return text(path + QStringLiteral("/removable")) == QLatin1String("fixed") ? QStringLiteral("Built in, on USB port %1").arg(port)
                                                                              : QStringLiteral("USB port %1").arg(port);
}

QString pciConnection(const QString &address)
{
    return QStringLiteral("Inside this PC, in PCI slot %1").arg(address);
}

// NVIDIA cards Debian's nvidia-driver drives: Maxwell (GeForce GTX 900,
// device IDs from 0x1340) up to Ada (RTX 40). Older ones need legacy
// drivers Debian no longer ships and RTX 50 cards (0x2b00 on) need a newer
// driver, so the open driver is the best those can have here.
bool nvidiaSupported(const QString &product)
{
    bool ok = false;
    const uint id = product.toUInt(&ok, 16);
    return ok && id >= 0x1340 && id < 0x2b00;
}

QVariantList scanAll(const QJsonObject &root, bool rootAnswered)
{
    Udev udev;
    const LibfprintReaders fprint = libfprintReaders();
    QList<Dev> list;
    const auto find = [&list](const QString &key) -> Dev * {
        for (Dev &d : list) {
            if (d.key == key) {
                return &d;
            }
        }
        return nullptr;
    };

    // What each kind of device produces when its driver works, by the real
    // path of the device it belongs to.
    QStringList drmCards;
    for (const QString &card : QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::System)) {
        if (!card.contains(QLatin1Char('-'))) {
            drmCards << real(QStringLiteral("/sys/class/drm/%1/device").arg(card));
        }
    }
    QStringList soundCards;
    for (const QString &card : QDir(QStringLiteral("/sys/class/sound")).entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::System)) {
        soundCards << real(QStringLiteral("/sys/class/sound/%1/device").arg(card));
    }
    QStringList videoDevices;
    for (const QString &node : QDir(QStringLiteral("/sys/class/video4linux")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        videoDevices << real(QStringLiteral("/sys/class/video4linux/%1/device").arg(node));
    }
    QStringList netDevices;
    for (const QString &net : QDir(QStringLiteral("/sys/class/net")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        netDevices << real(QStringLiteral("/sys/class/net/%1/device").arg(net));
    }
    QStringList hciDevices;
    for (const QString &hci : QDir(QStringLiteral("/sys/class/bluetooth")).entryList({QStringLiteral("hci*")}, QDir::Dirs | QDir::System)) {
        if (!hci.contains(QLatin1Char(':'))) {
            hciDevices << real(QStringLiteral("/sys/class/bluetooth/%1/device").arg(hci));
        }
    }
    QStringList blockDevices;
    for (const QString &block : QDir(QStringLiteral("/sys/block")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        blockDevices << real(QStringLiteral("/sys/block/") + block);
    }
    const auto anyUnder = [](const QStringList &paths, const QString &ancestor) {
        return std::any_of(paths.cbegin(), paths.cend(), [&ancestor](const QString &p) {
            return under(p, ancestor);
        });
    };

    // PCI: the parts inside the PC.
    for (const QString &address : QDir(QStringLiteral("/sys/bus/pci/devices")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        const QString path = real(QStringLiteral("/sys/bus/pci/devices/") + address);
        bool ok = false;
        const uint cls = text(path + QStringLiteral("/class")).toUInt(&ok, 16);
        if (!ok) {
            continue;
        }
        const uint base = cls >> 16;
        const uint sub = (cls >> 8) & 0xff;
        const uint progIf = cls & 0xff;
        const auto props = udev.withNames(path);
        const QString vendor = text(path + QStringLiteral("/vendor")).mid(2);
        const QString product = text(path + QStringLiteral("/device")).mid(2);
        const QString dbModel = props.value(QStringLiteral("ID_MODEL_FROM_DATABASE"));

        QString kind;
        if (base == 0x03) {
            kind = QStringLiteral("graphics");
        } else if (base == 0x02) {
            kind = sub == 0x80 ? QStringLiteral("wifi") : QStringLiteral("ethernet");
        } else if (base == 0x0d) {
            kind = sub == 0x11 ? QStringLiteral("bluetooth") : QStringLiteral("wifi");
        } else if (base == 0x04 && (sub == 0x01 || sub == 0x03)) {
            const bool onGraphics = (vendor == QLatin1String("10de") || vendor == QLatin1String("1002")) && !address.endsWith(QLatin1String(".0"));
            kind = onGraphics ? QStringLiteral("hdmisound") : QStringLiteral("sound");
        } else if (base == 0x04 && dbModel.contains(QRegularExpression(QStringLiteral("IPU|Imaging|Camera"), QRegularExpression::CaseInsensitiveOption))) {
            kind = QStringLiteral("camera");
        } else if (base == 0x01) {
            kind = QStringLiteral("storagectl");
        } else if (base == 0x0c && sub == 0x03) {
            kind = QStringLiteral("usbctl");
        } else if (base == 0x0c && sub == 0x00) {
            kind = QStringLiteral("firewire");
        } else if ((base == 0x08 && sub == 0x05) || (base == 0xff && dbModel.contains(QLatin1String("Card Reader"), Qt::CaseInsensitive))) {
            kind = QStringLiteral("cardreader");
        } else {
            continue;
        }
        Dev d;
        d.key = path;
        d.kind = kind;
        d.bus = QStringLiteral("pci");
        d.vendorId = vendor;
        d.productId = product;
        d.unboundPath = path;
        d.maker = shortMaker(props.value(QStringLiteral("ID_VENDOR_FROM_DATABASE")));
        d.model = dbModel.isEmpty() ? props.value(QStringLiteral("ID_PCI_SUBCLASS_FROM_DATABASE")) : shortModel(dbModel);
        d.hardwareId = QStringLiteral("PCI %1:%2").arg(vendor, product);
        d.connection = pciConnection(address);
        d.virtualHardware = isVirtualVendor(vendor);
        d.builtIn = true;
        d.driver = linkName(path + QStringLiteral("/driver"));
        // virtio devices: virtio-pci only carries the bus; the driver that
        // does the work sits on the virtioN device below it.
        if (d.driver == QLatin1String("virtio-pci")) {
            for (const QString &child : QDir(path).entryList({QStringLiteral("virtio*")}, QDir::Dirs)) {
                const QString inner = linkName(path + QLatin1Char('/') + child + QStringLiteral("/driver"));
                if (!inner.isEmpty()) {
                    d.driver = inner;
                }
            }
        }
        if (kind == QLatin1String("storagectl")) {
            // An NVMe drive or a virtual disk is its own controller: the
            // drive's row covers it, unless it has no driver.
            if ((sub == 0x08 || d.virtualHardware) && !d.driver.isEmpty()) {
                continue;
            }
        }
        if (kind == QLatin1String("usbctl") && progIf == 0x40) {
            d.name = QStringLiteral("USB4 controller");
        }
        list << d;
    }

    // USB: what is plugged in, and what is built in over USB (cameras,
    // fingerprint readers, Bluetooth).
    for (const QString &entry : QDir(QStringLiteral("/sys/bus/usb/devices")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        if (entry.contains(QLatin1Char(':')) || entry.startsWith(QLatin1String("usb"))) {
            continue;
        }
        const QString path = real(QStringLiteral("/sys/bus/usb/devices/") + entry);
        if (text(path + QStringLiteral("/bDeviceClass")) == QLatin1String("09")) {
            continue;
        }
        const auto props = udev.withNames(path);
        Dev d;
        d.key = path;
        d.bus = QStringLiteral("usb");
        d.vendorId = text(path + QStringLiteral("/idVendor"));
        d.productId = text(path + QStringLiteral("/idProduct"));
        d.hardwareId = QStringLiteral("USB %1:%2").arg(d.vendorId, d.productId);
        d.virtualHardware = isVirtualVendor(d.vendorId);
        d.connection = usbConnection(path);
        d.builtIn = text(path + QStringLiteral("/removable")) == QLatin1String("fixed");
        const QString manufacturer = cleanName(text(path + QStringLiteral("/manufacturer")));
        const QString productName = cleanName(text(path + QStringLiteral("/product")));
        d.maker = shortMaker(props.value(QStringLiteral("ID_VENDOR_FROM_DATABASE"), manufacturer));
        if (d.maker.isEmpty()) {
            d.maker = manufacturer;
        }
        d.model = props.value(QStringLiteral("ID_MODEL_FROM_DATABASE"), productName);
        if (d.model.isEmpty()) {
            d.model = productName;
        }
        d.model = shortModel(d.model);

        // By interface class: whether any interface of it has a driver. A
        // device with several audio or video interfaces needs only one of
        // them claimed, and CDC data goes with its control interface.
        QSet<QString> classes;
        QHash<QString, bool> classBound;
        QHash<QString, QString> unboundByClass;
        for (const QString &iface : QDir(path).entryList({entry + QStringLiteral(":*")}, QDir::Dirs)) {
            const QString ipath = path + QLatin1Char('/') + iface;
            QString cls = text(ipath + QStringLiteral("/bInterfaceClass"));
            if (cls == QLatin1String("0a")) {
                cls = QStringLiteral("02");
            }
            classes.insert(cls);
            const QString driver = linkName(ipath + QStringLiteral("/driver"));
            classBound[cls] = classBound.value(cls) || !driver.isEmpty();
            if (!driver.isEmpty() && !d.drivers.contains(driver)) {
                d.drivers << driver;
            }
            if (driver.isEmpty() && !unboundByClass.contains(cls)) {
                unboundByClass.insert(cls, real(ipath));
            }
        }
        const bool blocked = text(path + QStringLiteral("/authorized")) == QLatin1String("0");
        if (blocked) {
            classes = descriptorClasses(path);
            d.port = entry;
            d.state = QStringLiteral("attention");
            d.problem = QStringLiteral("Blocked by USB protection until you allow it.");
            d.badge = QStringLiteral("Blocked");
            d.action = QStringLiteral("allow");
            d.actionLabel = QStringLiteral("Allow");
        }
        QString unboundIface;
        for (const QString &cls : {QStringLiteral("01"), QStringLiteral("02"), QStringLiteral("03"), QStringLiteral("05"), QStringLiteral("07"),
                                   QStringLiteral("08"), QStringLiteral("0e"), QStringLiteral("0f"), QStringLiteral("10"), QStringLiteral("e0")}) {
            if (!blocked && classes.contains(cls) && !classBound.value(cls)) {
                d.unboundInterface = true;
                unboundIface = unboundByClass.value(cls);
                break;
            }
        }
        if (unboundIface.isEmpty()) {
            for (auto it = unboundByClass.cbegin(); it != unboundByClass.cend(); ++it) {
                if (!classBound.value(it.key())) {
                    unboundIface = it.value();
                    break;
                }
            }
        }
        d.driver = d.drivers.join(QStringLiteral(", "));
        d.unboundPath = unboundIface.isEmpty() ? path : unboundIface;
        const QString id = d.vendorId + QLatin1Char(':') + d.productId;

        if (fprint.supported.contains(id) || fprint.unsupported.contains(id)) {
            d.kind = QStringLiteral("fingerprint");
            // libfprint drives it from user space; no kernel driver to want.
            d.unboundInterface = false;
            d.driver = QStringLiteral("libfprint");
            if (fprint.unsupported.contains(id)) {
                d.state = QStringLiteral("attention");
                d.problem = QStringLiteral("Wild OS has no driver for this reader yet.");
                d.badge = QStringLiteral("Needs a driver");
                d.action = QStringLiteral("report");
                d.actionLabel = QStringLiteral("Report it");
                d.driver.clear();
            }
        } else if (classes.contains(QStringLiteral("0e"))) {
            d.kind = QStringLiteral("camera");
        } else if (classes.contains(QStringLiteral("07"))) {
            d.kind = QStringLiteral("printer");
        } else if (classes.contains(QStringLiteral("e0"))) {
            d.kind = QStringLiteral("bluetooth");
        } else if (classes.contains(QStringLiteral("01"))) {
            d.kind = QStringLiteral("sound");
        } else if (props.value(QStringLiteral("libsane_matched")) == QLatin1String("yes")) {
            d.kind = QStringLiteral("scanner");
            d.driver = QStringLiteral("SANE");
            d.unboundInterface = false;
        } else if (classes.contains(QStringLiteral("0b"))) {
            d.kind = QStringLiteral("smartcard");
            d.driver = d.driver.isEmpty() ? QStringLiteral("pcscd") : d.driver;
        } else if (classes.contains(QStringLiteral("08"))) {
            d.kind = QStringLiteral("usbdrive");
        } else if (classes == QSet<QString>{QStringLiteral("03")}) {
            d.kind = QStringLiteral("hid");
        } else if (props.value(QStringLiteral("ID_MTP_DEVICE")) == QLatin1String("1") || classes.contains(QStringLiteral("06"))) {
            d.kind = QStringLiteral("phone");
            d.unboundInterface = false;
        } else {
            d.kind = QStringLiteral("usb");
        }
        list << d;
    }

    // Network adapters: Wi-Fi or Ethernet, and the interface name for
    // NetworkManager.
    for (const QString &net : QDir(QStringLiteral("/sys/class/net")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        QString device = real(QStringLiteral("/sys/class/net/%1/device").arg(net));
        if (device.isEmpty()) {
            continue;
        }
        const QString usb = usbDeviceOf(device);
        if (!usb.isEmpty()) {
            device = usb;
        } else if (QFileInfo(device).fileName().startsWith(QLatin1String("virtio"))) {
            device = device.section(QLatin1Char('/'), 0, -2);
        }
        Dev *d = find(device);
        if (!d) {
            continue;
        }
        const bool wifi = QFile::exists(QStringLiteral("/sys/class/net/%1/wireless").arg(net)) || QFile::exists(QStringLiteral("/sys/class/net/%1/phy80211").arg(net));
        d->kind = wifi ? QStringLiteral("wifi") : QStringLiteral("ethernet");
        d->netInterface = net;
    }

    // Drives.
    QSet<QString> systemDisks;
    const QHash<QString, QPair<double, double>> space = spaceByDisk(&systemDisks);
    const QJsonObject health = root.value(QStringLiteral("disks")).toObject();
    for (const QString &block : QDir(QStringLiteral("/sys/block")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        static const QRegularExpression skip(QStringLiteral("^(loop|ram|zram|dm-|md|sr|fd|nbd)"));
        if (skip.match(block).hasMatch()) {
            continue;
        }
        const QString path = real(QStringLiteral("/sys/block/") + block);
        const double bytes = text(path + QStringLiteral("/size")).toDouble() * 512;
        if (bytes <= 0) {
            continue;
        }
        const QString usb = usbDeviceOf(path);
        if (!usb.isEmpty()) {
            Dev *d = find(usb);
            if (d) {
                d->kind = QStringLiteral("usbdrive");
                d->blockName = block;
                d->line = QStringLiteral("Plugged in, %1").arg(sizeText(bytes));
            }
            continue;
        }
        Dev d;
        d.key = path;
        d.blockName = block;
        d.builtIn = true;
        const QString pciAddress = pciAddressIn(path);
        const QString pciPath = pciAddress.isEmpty() ? QString() : real(QStringLiteral("/sys/bus/pci/devices/") + pciAddress);
        const auto pciProps = pciPath.isEmpty() ? QHash<QString, QString>() : udev.properties(pciPath);
        d.virtualHardware = isVirtualVendor(text(pciPath + QStringLiteral("/vendor")).mid(2));
        const bool removable = text(path + QStringLiteral("/removable")) == QLatin1String("1");
        const bool solid = text(path + QStringLiteral("/queue/rotational")) == QLatin1String("0") || block.startsWith(QLatin1String("nvme"));
        static const QRegularExpression ata(QStringLiteral("/ata(\\d+)/"));
        const QRegularExpressionMatch port = ata.match(path);
        d.model = cleanName(text(path + QStringLiteral("/device/model")));
        if (d.model.isEmpty()) {
            d.model = shortModel(pciProps.value(QStringLiteral("ID_MODEL_FROM_DATABASE")));
        }
        // A SATA drive's vendor field says only "ATA" and its controller is
        // someone else's, so its maker is left to the model name.
        d.maker = text(path + QStringLiteral("/device/vendor"));
        if (d.maker == QLatin1String("ATA")) {
            d.maker.clear();
        } else if (d.maker.isEmpty() && !port.hasMatch()) {
            d.maker = shortMaker(pciProps.value(QStringLiteral("ID_VENDOR_FROM_DATABASE")));
        }
        d.driver = linkName(path + QStringLiteral("/device/driver"));
        if (d.driver.isEmpty() && block.startsWith(QLatin1String("nvme"))) {
            d.driver = QStringLiteral("nvme");
        }
        if (d.driver.isEmpty() || d.driver == QLatin1String("sd")) {
            d.driver = d.driver.isEmpty() ? linkName(pciPath + QStringLiteral("/driver")) : QStringLiteral("sd (%1)").arg(linkName(pciPath + QStringLiteral("/driver")));
        }
        d.connection =port.hasMatch() ? QStringLiteral("Inside this PC, on SATA port %1").arg(port.captured(1))
                     : !pciAddress.isEmpty() ? pciConnection(pciAddress)
                                             : QStringLiteral("Built in");
        d.hardwareId = pciPath.isEmpty() ? QString() : QStringLiteral("PCI %1:%2").arg(text(pciPath + QStringLiteral("/vendor")).mid(2), text(pciPath + QStringLiteral("/device")).mid(2));
        if (removable || block.startsWith(QLatin1String("mmcblk"))) {
            d.kind = QStringLiteral("card");
            d.name = QStringLiteral("Memory card, %1").arg(sizeText(bytes));
            d.line = QStringLiteral("In the card reader");
            list << d;
            continue;
        }
        d.kind = QStringLiteral("storage");
        d.first = systemDisks.contains(block);
        d.name = (d.virtualHardware ? QStringLiteral("Virtual drive, %1") : solid ? QStringLiteral("Internal SSD, %1") : QStringLiteral("Internal hard drive, %1")).arg(sizeText(bytes));
        d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Size")}, {QStringLiteral("value"), sizeText(bytes)}};

        QString used;
        if (space.contains(block) && space.value(block).second > 0) {
            const auto s = space.value(block);
            used = QStringLiteral("%1% used").arg(qRound(100 * s.first / s.second));
            d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Space used")},
                                   {QStringLiteral("value"), QStringLiteral("%1% (%2 of %3)").arg(qRound(100 * s.first / s.second)).arg(sizeText(s.first), sizeText(s.second))}};
        }
        const QJsonObject h = health.value(block).toObject();
        QString healthWords;
        if (!rootAnswered) {
            healthWords = QStringLiteral("Couldn't be checked just now.");
            d.line = used.isEmpty() ? QStringLiteral("Built in") : used.left(1).toUpper() + used.mid(1);
        } else if (!h.value(QStringLiteral("reported")).toBool()) {
            healthWords = d.virtualHardware ? QStringLiteral("A virtual drive doesn't report its health.") : QStringLiteral("This drive doesn't report its health.");
            d.line = used.isEmpty() ? QStringLiteral("Built in") : used.left(1).toUpper() + used.mid(1);
        } else {
            const bool passed = h.value(QStringLiteral("passed")).toBool();
            const int warning = h.value(QStringLiteral("criticalWarning")).toInt();
            const int wear = h.value(QStringLiteral("percentUsed")).toInt(-1);
            if (!passed || warning != 0) {
                healthWords = QStringLiteral("Failing. Its own health check says it may stop working.");
                d.state = QStringLiteral("attention");
                d.problem = QStringLiteral("Its health check says it's failing. Back up your files now.");
                d.badge = QStringLiteral("Failing");
                d.action = QStringLiteral("backup");
                d.actionLabel = QStringLiteral("Back up now");
            } else if (wear >= 100) {
                healthWords = QStringLiteral("Worn out. It has used all of its rated life.");
                d.state = QStringLiteral("attention");
                d.problem = QStringLiteral("It has used all of its rated life. Back up your files now.");
                d.badge = QStringLiteral("Wearing out");
                d.action = QStringLiteral("backup");
                d.actionLabel = QStringLiteral("Back up now");
            } else {
                healthWords = QStringLiteral("Healthy. Its own health check passed.");
            }
            d.line = QStringLiteral("Healthy") + (used.isEmpty() ? QString() : QStringLiteral(". ") + used);
            if (wear >= 0) {
                d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Wear")}, {QStringLiteral("value"), QStringLiteral("%1% of its rated life used").arg(wear)}};
            }
            if (h.contains(QStringLiteral("temperature"))) {
                d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Temperature")}, {QStringLiteral("value"), QStringLiteral("%1 °C").arg(h.value(QStringLiteral("temperature")).toInt())}};
            }
            if (h.value(QStringLiteral("hours")).toInteger() > 0) {
                d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Switched on for")},
                                       {QStringLiteral("value"), QStringLiteral("%1 hours in all").arg(QLocale(QLocale::English).toString(h.value(QStringLiteral("hours")).toInteger()))}};
            }
            const qint64 bad = h.value(QStringLiteral("reallocated")).toInteger() + h.value(QStringLiteral("pending")).toInteger();
            if (bad > 0) {
                d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Damaged areas")},
                                       {QStringLiteral("value"), QStringLiteral("%1, set aside so nothing is stored there").arg(bad)}};
            }
        }
        d.extra.insert(0, QVariantMap{{QStringLiteral("label"), QStringLiteral("Health")}, {QStringLiteral("value"), healthWords}});
        list << d;
    }

    // Keyboards, mice, touchpads, touchscreens, pens and game controllers,
    // one row per physical device however many input nodes it has.
    struct Input {
        QString key;
        QString source;
        QString bus;
        QString name;
        QString driver;
        int bits = 0;
    };
    enum { Keyboard = 1, Mouse = 2, Touchpad = 4, Touchscreen = 8, Pen = 16, Joystick = 32 };
    QList<Input> inputs;
    static const QRegularExpression notKeyboard(
        QStringLiteral("Video Bus|Power Button|Sleep Button|Lid Switch|HID events|WMI|hotkeys|Consumer Control|System Control|Wireless Radio|Speaker|Headphone|Mic|HDMI|Jack"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString &node : QDir(QStringLiteral("/sys/class/input")).entryList({QStringLiteral("input*")}, QDir::Dirs | QDir::System)) {
        const QString path = real(QStringLiteral("/sys/class/input/") + node);
        const auto props = udev.properties(path);
        const QString name = text(path + QStringLiteral("/name"));
        const QString idBus = props.value(QStringLiteral("ID_BUS"));
        if (path.contains(QLatin1String("/virtual/")) && idBus != QLatin1String("bluetooth")) {
            continue;
        }
        int bits = 0;
        if (props.value(QStringLiteral("ID_INPUT_KEYBOARD")) == QLatin1String("1") && !notKeyboard.match(name).hasMatch()) {
            bits |= Keyboard;
        }
        if (props.value(QStringLiteral("ID_INPUT_MOUSE")) == QLatin1String("1")) {
            bits |= Mouse;
        }
        if (props.value(QStringLiteral("ID_INPUT_TOUCHPAD")) == QLatin1String("1")) {
            bits |= Touchpad;
        }
        if (props.value(QStringLiteral("ID_INPUT_TOUCHSCREEN")) == QLatin1String("1")) {
            bits |= Touchscreen;
        }
        if (props.value(QStringLiteral("ID_INPUT_TABLET")) == QLatin1String("1") && !(bits & Mouse)) {
            bits |= Pen;
        }
        if (props.value(QStringLiteral("ID_INPUT_JOYSTICK")) == QLatin1String("1")) {
            bits |= Joystick;
        }
        if (bits == 0) {
            continue;
        }
        Input in;
        in.name = cleanName(name);
        in.bits = bits;
        const QString parent = path.section(QLatin1Char('/'), 0, -2);
        in.driver = linkName(parent + QStringLiteral("/driver"));
        if (idBus == QLatin1String("bluetooth")) {
            in.key = QStringLiteral("bluetooth:") + text(path + QStringLiteral("/uniq"));
            in.bus = QStringLiteral("bluetooth");
        } else if (const QString usb = usbDeviceOf(path); !usb.isEmpty()) {
            in.key = usb;
            in.bus = QStringLiteral("usb");
        } else {
            static const QRegularExpression physical(QStringLiteral("^(.*/(serio\\d+|i2c-[^/]*:[^/]*|[^/]*:[^/]*\\.\\d+))(/|$)"));
            const QRegularExpressionMatch m = physical.match(parent);
            in.key = m.hasMatch() ? m.captured(1) : parent;
            in.bus = QStringLiteral("builtin");
        }
        in.source = path;
        bool merged = false;
        for (Input &existing : inputs) {
            if (existing.key == in.key) {
                existing.bits |= in.bits;
                if (existing.driver.isEmpty()) {
                    existing.driver = in.driver;
                }
                merged = true;
            }
        }
        if (!merged) {
            inputs << in;
        }
    }
    for (const Input &in : std::as_const(inputs)) {
        Dev *existing = find(in.key);
        Dev d = existing ? *existing : Dev();
        d.key = in.key;
        d.bus = in.bus;
        if (in.bits & Touchpad) {
            d.kind = QStringLiteral("touchpad");
        } else if (in.bits & Touchscreen) {
            d.kind = QStringLiteral("touchscreen");
        } else if (in.bits & Pen) {
            d.kind = QStringLiteral("pen");
        } else if (in.bits & Joystick) {
            d.kind = QStringLiteral("controller");
        } else if ((in.bits & Keyboard) && (in.bits & Mouse)) {
            d.kind = QStringLiteral("keyboardmouse");
        } else if (in.bits & Keyboard) {
            d.kind = QStringLiteral("keyboard");
        } else {
            d.kind = QStringLiteral("mouse");
        }
        if (in.bus == QLatin1String("bluetooth")) {
            d.connection = QStringLiteral("Bluetooth");
            d.model = in.name;
            d.driver = in.driver;
        } else if (in.bus == QLatin1String("builtin")) {
            d.connection = QStringLiteral("Built in");
            d.builtIn = true;
            d.model = in.name;
            d.driver = in.driver;
        } else if (d.driver.isEmpty()) {
            d.driver = in.driver;
        }
        if (existing) {
            *existing = d;
        } else {
            list << d;
        }
    }

    // Batteries in the PC (not a mouse's: those say scope Device).
    for (const QString &supply : QDir(QStringLiteral("/sys/class/power_supply")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        const QString path = real(QStringLiteral("/sys/class/power_supply/") + supply);
        if (text(path + QStringLiteral("/type")) != QLatin1String("Battery") || text(path + QStringLiteral("/scope")) == QLatin1String("Device")) {
            continue;
        }
        Dev d;
        d.key = path;
        d.kind = QStringLiteral("battery");
        d.builtIn = true;
        d.connection = QStringLiteral("Built in");
        d.maker = text(path + QStringLiteral("/manufacturer"));
        d.model = text(path + QStringLiteral("/model_name"));
        d.driver = linkName(path + QStringLiteral("/device/driver"));
        double full = text(path + QStringLiteral("/energy_full")).toDouble();
        double design = text(path + QStringLiteral("/energy_full_design")).toDouble();
        if (design <= 0) {
            full = text(path + QStringLiteral("/charge_full")).toDouble();
            design = text(path + QStringLiteral("/charge_full_design")).toDouble();
        }
        if (design > 0 && full > 0) {
            const int percent = qMin(100, qRound(100 * full / design));
            d.line = QStringLiteral("Holds %1% of its charge when new").arg(percent);
            d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Health")}, {QStringLiteral("value"), d.line}};
        } else {
            d.line = QStringLiteral("Built in");
        }
        list << d;
    }

    // Kernel lines about firmware that failed to load, by the device each
    // is about.
    QList<QPair<QString, QString>> firmware;
    for (const QJsonValue &v : root.value(QStringLiteral("firmware")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString device = o.value(QStringLiteral("device")).toString();
        const int colon = device.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        const QString path = real(QStringLiteral("/sys/bus/%1/devices/%2").arg(device.left(colon), device.mid(colon + 1)));
        if (!path.isEmpty()) {
            firmware << qMakePair(path, o.value(QStringLiteral("file")).toString());
        }
    }
    const QJsonObject unbound = root.value(QStringLiteral("unbound")).toObject();
    const QHash<QString, QPair<bool, int>> adapters = bluetoothAdapters();
    QList<QPair<QString, QString>> rfkills;
    for (const QString &r : QDir(QStringLiteral("/sys/class/rfkill")).entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        const QString path = real(QStringLiteral("/sys/class/rfkill/") + r);
        const QString how = text(path + QStringLiteral("/hard")) == QLatin1String("1") ? QStringLiteral("hard")
                          : text(path + QStringLiteral("/soft")) == QLatin1String("1") ? QStringLiteral("soft")
                                                                                         : QString();
        rfkills << qMakePair(path, how);
    }

    // Names, status lines and whatever needs attention.
    QVariantList out;
    QList<Dev> done;
    for (Dev d : std::as_const(list)) {
        if (d.kind == QLatin1String("hid") && !d.unboundInterface && d.state != QLatin1String("attention")) {
            continue;
        }
        if (d.kind == QLatin1String("hid")) {
            d.kind = QStringLiteral("usb");
        }
        if (d.kind == QLatin1String("usbdrive") && d.blockName.isEmpty() && !d.unboundInterface) {
            // Mass storage with no disk yet (an empty card reader).
            d.kind = QStringLiteral("cardreader");
        }

        const QString rfkill = [&]() {
            for (const auto &r : std::as_const(rfkills)) {
                if (under(r.first, d.key) && !r.second.isEmpty()) {
                    return r.second;
                }
            }
            return QString();
        }();
        const bool bound = !d.driver.isEmpty();

        // Plain names and icons.
        static const QHash<QString, QPair<QString, QString>> plain = {
            {QStringLiteral("graphics"), {QStringLiteral("Graphics"), QStringLiteral("monitor-round")}},
            {QStringLiteral("wifi"), {QStringLiteral("Wi-Fi"), QStringLiteral("wifi-round")}},
            {QStringLiteral("ethernet"), {QStringLiteral("Ethernet"), QStringLiteral("ethernet")}},
            {QStringLiteral("bluetooth"), {QStringLiteral("Bluetooth"), QStringLiteral("bluetooth")}},
            {QStringLiteral("sound"), {QStringLiteral("Speakers and microphone"), QStringLiteral("volume-round")}},
            {QStringLiteral("hdmisound"), {QStringLiteral("Sound over HDMI"), QStringLiteral("volume-round")}},
            {QStringLiteral("camera"), {QStringLiteral("Camera"), QStringLiteral("camera-round")}},
            {QStringLiteral("fingerprint"), {QStringLiteral("Fingerprint reader"), QStringLiteral("fingerprint")}},
            {QStringLiteral("storage"), {QStringLiteral("Storage"), QStringLiteral("hard-drive")}},
            {QStringLiteral("touchpad"), {QStringLiteral("Touchpad"), QStringLiteral("touchpad")}},
            {QStringLiteral("keyboard"), {QStringLiteral("Keyboard"), QStringLiteral("keyboard")}},
            {QStringLiteral("keyboardmouse"), {QStringLiteral("Keyboard and mouse"), QStringLiteral("keyboard")}},
            {QStringLiteral("mouse"), {QStringLiteral("Mouse"), QStringLiteral("mouse")}},
            {QStringLiteral("touchscreen"), {QStringLiteral("Touchscreen"), QStringLiteral("display")}},
            {QStringLiteral("pen"), {QStringLiteral("Pen and tablet"), QStringLiteral("pencil")}},
            {QStringLiteral("controller"), {QStringLiteral("Game controller"), QStringLiteral("games")}},
            {QStringLiteral("battery"), {QStringLiteral("Battery"), QStringLiteral("battery")}},
            {QStringLiteral("printer"), {QStringLiteral("Printer"), QStringLiteral("printer-round")}},
            {QStringLiteral("scanner"), {QStringLiteral("Scanner"), QStringLiteral("printer-round")}},
            {QStringLiteral("smartcard"), {QStringLiteral("Smart card reader"), QStringLiteral("security-key")}},
            {QStringLiteral("phone"), {QStringLiteral("Phone or camera"), QStringLiteral("phone")}},
            {QStringLiteral("usbdrive"), {QStringLiteral("USB drive"), QStringLiteral("usb")}},
            {QStringLiteral("card"), {QStringLiteral("Memory card"), QStringLiteral("usb")}},
            {QStringLiteral("cardreader"), {QStringLiteral("Memory card reader"), QStringLiteral("usb")}},
            {QStringLiteral("usb"), {QStringLiteral("USB device"), QStringLiteral("usb")}},
            {QStringLiteral("usbctl"), {QStringLiteral("USB controller"), QStringLiteral("usb")}},
            {QStringLiteral("storagectl"), {QStringLiteral("Storage controller"), QStringLiteral("chip")}},
            {QStringLiteral("firewire"), {QStringLiteral("FireWire port"), QStringLiteral("plug")}},
        };
        if (d.name.isEmpty()) {
            d.name = plain.value(d.kind).first;
            if (d.kind == QLatin1String("graphics")) {
                if (d.virtualHardware) {
                    d.name = QStringLiteral("Virtual graphics");
                } else if (d.vendorId == QLatin1String("10de")) {
                    d.name = QStringLiteral("NVIDIA graphics card");
                } else if (d.vendorId == QLatin1String("1002")) {
                    d.name = QStringLiteral("AMD graphics");
                } else if (d.vendorId == QLatin1String("8086")) {
                    d.name = QStringLiteral("Intel graphics");
                }
            } else if (d.kind == QLatin1String("sound") && d.bus == QLatin1String("usb")) {
                d.name = QStringLiteral("USB audio");
            } else if (d.kind == QLatin1String("camera") && d.bus == QLatin1String("usb") && !d.builtIn) {
                d.name = QStringLiteral("Webcam");
            } else if (d.kind == QLatin1String("wifi") && d.bus == QLatin1String("usb")) {
                d.name = QStringLiteral("Wi-Fi adapter");
            } else if (d.kind == QLatin1String("ethernet") && d.bus == QLatin1String("usb")) {
                d.name = QStringLiteral("Ethernet adapter");
            }
        }
        d.icon = plain.value(d.kind).second;
        if (d.icon.isEmpty()) {
            d.icon = QStringLiteral("chip");
        }

        // The usual line, before any problem replaces it.
        if (d.line.isEmpty()) {
            if (d.kind == QLatin1String("wifi") || d.kind == QLatin1String("ethernet")) {
                if (rfkill == QLatin1String("hard")) {
                    d.line = QStringLiteral("Turned off with a switch or key");
                } else if (rfkill == QLatin1String("soft")) {
                    d.line = QStringLiteral("Turned off");
                } else if (!d.netInterface.isEmpty()) {
                    d.line = networkLine(d.netInterface, d.kind == QLatin1String("wifi"),
                                         text(QStringLiteral("/sys/class/net/%1/carrier").arg(d.netInterface)) == QLatin1String("1"));
                }
            } else if (d.kind == QLatin1String("bluetooth")) {
                QString hci;
                for (const QString &h : QDir(QStringLiteral("/sys/class/bluetooth")).entryList({QStringLiteral("hci*")}, QDir::Dirs | QDir::System)) {
                    if (!h.contains(QLatin1Char(':')) && under(real(QStringLiteral("/sys/class/bluetooth/%1/device").arg(h)), d.key)) {
                        hci = h;
                    }
                }
                if (rfkill == QLatin1String("hard")) {
                    d.line = QStringLiteral("Turned off with a switch or key");
                } else if (rfkill == QLatin1String("soft") || (adapters.contains(hci) && !adapters.value(hci).first)) {
                    d.line = QStringLiteral("Turned off");
                } else if (adapters.contains(hci)) {
                    const int paired = adapters.value(hci).second;
                    d.line = paired == 0 ? QStringLiteral("No devices paired yet")
                                         : paired == 1 ? QStringLiteral("1 device paired")
                                                       : QStringLiteral("%1 devices paired").arg(paired);
                }
            } else if (d.kind == QLatin1String("graphics") && d.driver == QLatin1String("nvidia")) {
                d.line = QStringLiteral("Using NVIDIA's driver");
            } else if (d.kind == QLatin1String("graphics") && d.driver == QLatin1String("nouveau")) {
                d.line = QStringLiteral("Using the open driver");
            } else if (d.kind == QLatin1String("usb")) {
                d.line = d.model.isEmpty() ? QStringLiteral("Plugged in") : d.model;
            }
            if (d.line.isEmpty()) {
                d.line = d.bus == QLatin1String("bluetooth") ? QStringLiteral("Connected over Bluetooth")
                       : d.builtIn                           ? QStringLiteral("Built in")
                                                             : QStringLiteral("Plugged in over USB");
            }
        }

        // Is it doing its job?
        if (bound) {
            if (d.kind == QLatin1String("graphics") && d.bus == QLatin1String("pci")) {
                d.functionMissing = !anyUnder(drmCards, d.key);
            } else if ((d.kind == QLatin1String("wifi") || d.kind == QLatin1String("ethernet")) && d.netInterface.isEmpty()) {
                d.functionMissing = !anyUnder(netDevices, d.key);
            } else if (d.kind == QLatin1String("bluetooth")) {
                d.functionMissing = !anyUnder(hciDevices, d.key);
            } else if (d.kind == QLatin1String("sound") || d.kind == QLatin1String("hdmisound")) {
                d.functionMissing = !anyUnder(soundCards, d.key);
            } else if (d.kind == QLatin1String("camera")) {
                d.functionMissing = !anyUnder(videoDevices, d.key);
            } else if (d.kind == QLatin1String("storagectl")) {
                d.functionMissing = false;
            }
        }
        QStringList firmwareFiles;
        for (const auto &f : std::as_const(firmware)) {
            if (under(f.first, d.key) && !f.second.isEmpty() && !firmwareFiles.contains(f.second)) {
                firmwareFiles << f.second;
            }
        }
        const bool wantsDriver = !bound && (d.bus == QLatin1String("pci") || d.unboundInterface || d.kind == QLatin1String("usb"));

        if (d.state == QLatin1String("attention")) {
            // Already decided: an unsupported fingerprint reader, a failing drive.
        } else if (d.kind == QLatin1String("graphics") && d.vendorId == QLatin1String("10de") && d.driver != QLatin1String("nvidia")
                   && QFile::exists(QStringLiteral("/var/lib/dpkg/info/nvidia-driver.list"))) {
            d.state = QStringLiteral("attention");
            d.problem = QStringLiteral("NVIDIA's driver is installed and starts after a restart.");
            d.badge = QStringLiteral("Restart needed");
            d.action = QStringLiteral("restart");
            d.actionLabel = QStringLiteral("Restart now");
        } else if (d.kind == QLatin1String("graphics") && d.vendorId == QLatin1String("10de") && d.driver != QLatin1String("nvidia")
                   && nvidiaSupported(d.productId) && firmwareFiles.isEmpty()) {
            d.state = QStringLiteral("attention");
            d.problem = bound && !d.functionMissing ? QStringLiteral("Running on the basic driver, so games and video will be slow.")
                                                    : QStringLiteral("Running without a driver, so games and video will be slow.");
            d.badge = QStringLiteral("Needs a driver");
            d.action = QStringLiteral("graphicsdriver");
            d.actionLabel = QStringLiteral("Install the driver");
        } else if ((wantsDriver || d.functionMissing) && !firmwareFiles.isEmpty()) {
            d.state = QStringLiteral("attention");
            d.problem = QStringLiteral("Can't start because its firmware is missing.");
            d.badge = QStringLiteral("Missing firmware");
            d.action = QStringLiteral("update");
            d.actionLabel = QStringLiteral("Check for updates");
            d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Firmware it asked for")}, {QStringLiteral("value"), firmwareFiles.join(QStringLiteral(", "))}};
        } else if (wantsDriver) {
            // settings-devices resolves the driver (the kernel's module list
            // is root's on Wild OS) and says whether something switched it off.
            const QJsonObject why = unbound.value(d.unboundPath).toObject();
            const bool known = unbound.contains(d.unboundPath);
            QStringList modules;
            for (const QJsonValue &m : why.value(QStringLiteral("modules")).toArray()) {
                modules << m.toString();
            }
            const QString security = why.value(QStringLiteral("security")).toString();
            const QString blacklist = why.value(QStringLiteral("blacklisted")).toString();
            if (!security.isEmpty() && blacklist.isEmpty()) {
                d.state = QStringLiteral("off");
                d.line = QStringLiteral("Switched off for security");
                d.badge = QStringLiteral("Off");
                d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Why it's off")},
                                       {QStringLiteral("value"), QStringLiteral("Wild OS keeps its driver, %1, switched off: it is rarely needed and has been used to attack PCs.").arg(security)}};
            } else if (d.kind == QLatin1String("usb") && modules.isEmpty() && !d.unboundInterface) {
                // A device apps talk to directly, with no kernel driver to want.
            } else {
                d.state = QStringLiteral("attention");
                d.badge = QStringLiteral("Needs a driver");
                d.action = QStringLiteral("report");
                d.actionLabel = QStringLiteral("Report it");
                if (!blacklist.isEmpty()) {
                    d.problem = QStringLiteral("Its driver is switched off on this PC.");
                    d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Why it's off")},
                                           {QStringLiteral("value"), QStringLiteral("Its driver, %1, is blocked in %2.").arg(blacklist, why.value(QStringLiteral("file")).toString())}};
                } else if (!modules.isEmpty()) {
                    d.problem = QStringLiteral("Its driver didn't start.");
                    d.extra << QVariantMap{{QStringLiteral("label"), QStringLiteral("Driver that should run it")}, {QStringLiteral("value"), modules.join(QStringLiteral(", "))}};
                } else if (!known) {
                    d.problem = d.kind == QLatin1String("graphics") ? QStringLiteral("No driver is running it, so the screen runs on a basic stand-in.")
                                                                    : QStringLiteral("No driver is running it.");
                } else if (d.kind == QLatin1String("graphics")) {
                    d.problem = QStringLiteral("Wild OS has no driver for it, so the screen runs on a basic stand-in.");
                } else {
                    d.problem = QStringLiteral("Wild OS has no driver for it yet.");
                }
            }
        } else if (d.functionMissing) {
            d.state = QStringLiteral("attention");
            d.problem = QStringLiteral("Its driver started, but it isn't working.");
            d.badge = QStringLiteral("Not working");
            d.action = QStringLiteral("report");
            d.actionLabel = QStringLiteral("Report it");
        }
        done << d;
    }

    std::stable_sort(done.begin(), done.end(), [](const Dev &a, const Dev &b) {
        const int ia = kindOrder.indexOf(a.kind);
        const int ib = kindOrder.indexOf(b.kind);
        if (ia != ib) {
            return (ia < 0 ? 999 : ia) < (ib < 0 ? 999 : ib);
        }
        return a.first && !b.first;
    });
    for (const Dev &d : std::as_const(done)) {
        out << d.toMap();
    }
    return out;
}
}

Devices::Devices(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<ManagedObjects>();
    scan();
}

void Devices::scan()
{
    if (m_scanning) {
        m_again = true;
        return;
    }
    m_scanning = true;
    Q_EMIT scanningChanged();
    QPointer<Devices> self(this);
    QThread *thread = QThread::create([self] {
        bool answered = false;
        const QJsonObject root = askRoot(&answered);
        const QVariantList found = scanAll(root, answered);
        QMetaObject::invokeMethod(qApp, [self, found, answered] {
            if (!self) {
                return;
            }
            self->m_devices = found;
            self->m_ready = true;
            self->m_healthUnknown = !answered;
            self->m_scanning = false;
            Q_EMIT self->devicesChanged();
            Q_EMIT self->scanningChanged();
            if (self->m_again) {
                self->m_again = false;
                self->scan();
            }
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Devices::allow(const QString &port)
{
    static const QRegularExpression valid(QStringLiteral("^\\d+-\\d+(\\.\\d+)*$"));
    if (!valid.match(port).hasMatch() || !QFile::exists(allowHelper)) {
        Q_EMIT failed(QStringLiteral("This device couldn't be allowed. Unplug it and try again."));
        return;
    }
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        const QString result = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        if (result == QLatin1String("denied")) {
            Q_EMIT failed(QStringLiteral("Only an administrator can allow USB devices."));
        } else if (result != QLatin1String("allowed") && result != QLatin1String("cancelled")) {
            Q_EMIT failed(QStringLiteral("This device couldn't be allowed. Unplug it and try again."));
        }
        // Its drivers take a moment to bind once it is let in.
        QTimer::singleShot(1500, this, &Devices::scan);
    });
    process->start(allowHelper, {QStringLiteral("allow"), port});
}

bool Devices::openBackup() const
{
    // Started by name: Exec in org.wildos.backup.desktop, and gtk-launch
    // isn't in the image.
    const QString program = QStandardPaths::findExecutable(QStringLiteral("wildos-backup"));
    return !program.isEmpty() && QProcess::startDetached(program, {});
}

bool Devices::openReport() const
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("wildos-report"));
    return !program.isEmpty() && QProcess::startDetached(program, {});
}
