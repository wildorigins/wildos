#include "driveformat.h"
#include "drives.h"
#include "formatsafety.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThread>

using InterfaceProps = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceProps>;
Q_DECLARE_METATYPE(InterfaceProps)
Q_DECLARE_METATYPE(ManagedObjects)

namespace
{
// Settings > Privacy and security > Advanced protection's "Password-locked
// USB sticks" (usb_encrypt in /etc/wildos/advanced-protection, on unless it
// says off): off, the format window leaves "Encrypt this drive" out.
bool encryptOffered()
{
    QFile store(QStringLiteral("/etc/wildos/advanced-protection"));
    bool on = true;
    if (store.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!store.atEnd()) {
            const QByteArray line = store.readLine().trimmed();
            if (line.startsWith("usb_encrypt=")) {
                on = line.mid(12) != "off";
            }
        }
    }
    return on;
}

const QString service = QStringLiteral("org.freedesktop.UDisks2");
const QString root = QStringLiteral("/org/freedesktop/UDisks2");
const QString blockPrefix = QStringLiteral("/org/freedesktop/UDisks2/block_devices/");
const QString blockIface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString fsIface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString partIface = QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString tableIface = QStringLiteral("org.freedesktop.UDisks2.PartitionTable");
const QString cryptIface = QStringLiteral("org.freedesktop.UDisks2.Encrypted");
const QString driveIface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString jobIface = QStringLiteral("org.freedesktop.UDisks2.Job");
const QString managerIface = QStringLiteral("org.freedesktop.UDisks2.Manager");

// Writing zeros over a large, slow drive takes hours.
constexpr int longCall = 12 * 60 * 60 * 1000;
// FAT32 as Windows offers it: up to 32 GB.
constexpr qint64 fatLimit = 32LL * 1024 * 1024 * 1024;
// MBR, which every camera, TV and car stereo reads, ends at 2 TiB.
constexpr qint64 mbrLimit = 2LL * 1024 * 1024 * 1024 * 1024;

void registerTypes()
{
    static bool done = false;
    if (!done) {
        qDBusRegisterMetaType<InterfaceProps>();
        qDBusRegisterMetaType<ManagedObjects>();
        done = true;
    }
}

ManagedObjects managedObjects()
{
    registerTypes();
    QDBusInterface manager(service, root, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QDBusConnection::systemBus());
    manager.setTimeout(10000);
    const QDBusReply<ManagedObjects> reply = manager.call(QStringLiteral("GetManagedObjects"));
    return reply.isValid() ? reply.value() : ManagedObjects();
}

QString bytes(const QVariant &v)
{
    return QString::fromUtf8(v.toByteArray().constData());
}

QString path(const QVariant &v)
{
    const QString p = v.value<QDBusObjectPath>().path();
    return p == QLatin1String("/") ? QString() : p;
}

QStringList paths(const QVariant &v)
{
    QStringList out;
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            QDBusObjectPath p;
            arg >> p;
            out << p.path();
        }
        arg.endArray();
    } else {
        for (const QDBusObjectPath &p : v.value<QList<QDBusObjectPath>>()) {
            out << p.path();
        }
    }
    return out;
}

bool mounted(const InterfaceProps &ifaces)
{
    const QVariant v = ifaces.value(fsIface).value(QStringLiteral("MountPoints"));
    if (!v.canConvert<QDBusArgument>()) {
        return false;
    }
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginArray();
    const bool any = !arg.atEnd();
    while (!arg.atEnd()) {
        QByteArray b;
        arg >> b;
    }
    arg.endArray();
    return any;
}

// The block to format: the LUKS container when given what it unlocks to.
QString formatTarget(const ManagedObjects &objects, const QString &object)
{
    const InterfaceProps ifaces = objects.value(QDBusObjectPath(object));
    const QString backing = path(ifaces.value(blockIface).value(QStringLiteral("CryptoBackingDevice")));
    return backing.isEmpty() ? object : backing;
}

bool removableDrive(const QVariantMap &drive)
{
    const QString bus = drive.value(QStringLiteral("ConnectionBus")).toString();
    return drive.value(QStringLiteral("Removable")).toBool() || drive.value(QStringLiteral("MediaRemovable")).toBool()
        || bus == QLatin1String("usb") || bus == QLatin1String("sdio") || bus == QLatin1String("ieee1394");
}

bool allowed(const ManagedObjects &objects, const QString &object)
{
    const InterfaceProps ifaces = objects.value(QDBusObjectPath(object));
    if (!ifaces.contains(blockIface)) {
        return false;
    }
    const QVariantMap block = ifaces.value(blockIface);
    const QVariantMap drive = objects.value(QDBusObjectPath(path(block.value(QStringLiteral("Drive"))))).value(driveIface);
    if (drive.isEmpty()) {
        return false;
    }
    FormatSafety::Facts facts;
    facts.blockName = QFileInfo(bytes(block.value(QStringLiteral("Device")))).fileName();
    facts.removable = removableDrive(drive);
    facts.optical = drive.value(QStringLiteral("Optical")).toBool()
        || drive.value(QStringLiteral("MediaCompatibility")).toStringList().join(QLatin1Char(',')).contains(QLatin1String("optical"));
    return FormatSafety::canFormat(QString(), facts);
}

QString kindOf(const QVariantMap &drive)
{
    const QString bus = drive.value(QStringLiteral("ConnectionBus")).toString();
    if (bus == QLatin1String("sdio") || drive.value(QStringLiteral("Media")).toString().startsWith(QLatin1String("flash_sd"))) {
        return QStringLiteral("SD card");
    }
    if (drive.value(QStringLiteral("Size")).toLongLong() > 0 && drive.value(QStringLiteral("RotationRate")).toInt() > 0) {
        return QStringLiteral("external drive");
    }
    return bus == QLatin1String("usb") ? QStringLiteral("USB drive") : QStringLiteral("external drive");
}

// Labels as each file system stores them: FAT keeps 11 upper-case
// characters and refuses some punctuation, as Windows does.
QString cleanLabel(const QString &fs, QString label)
{
    label = label.trimmed();
    if (fs == QLatin1String("vfat")) {
        static const QString banned = QStringLiteral("*?.,;:/\\|+=<>[]\"");
        QString out;
        for (const QChar c : std::as_const(label)) {
            if (!banned.contains(c) && c.unicode() < 128) {
                out += c.toUpper();
            }
        }
        return out.left(11);
    }
    if (fs == QLatin1String("exfat")) {
        return label.left(15);
    }
    if (fs == QLatin1String("ntfs")) {
        return label.left(32);
    }
    // ext4 allows 16 bytes.
    QByteArray utf8 = label.toUtf8();
    while (utf8.size() > 16) {
        label.chop(1);
        utf8 = label.toUtf8();
    }
    return label;
}

QString plainError(const QDBusError &error)
{
    const QString name = error.name();
    const QString text = error.message();
    qWarning() << "Format:" << name << text;
    if (name.endsWith(QLatin1String("NotAuthorizedDismissed")) || name.endsWith(QLatin1String("Cancelled"))) {
        return QString();
    }
    if (name.contains(QLatin1String("NotAuthorized"))) {
        return QStringLiteral("Formatting this drive needs an administrator's permission, and it wasn't given.");
    }
    if (name.endsWith(QLatin1String("DeviceBusy")) || text.contains(QLatin1String("busy"), Qt::CaseInsensitive)) {
        return QStringLiteral("The drive is in use. Close any files or apps using it, then try again.");
    }
    if (name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")) {
        return QStringLiteral("Files can't reach the disk service, so it can't format drives right now. Restart the PC and try again.");
    }
    if (name == QLatin1String("org.freedesktop.DBus.Error.UnknownObject") || text.contains(QLatin1String("No such"), Qt::CaseInsensitive)) {
        return QStringLiteral("The drive was unplugged before formatting finished. Plug it back in and format it again.");
    }
    if (text.contains(QLatin1String("read-only"), Qt::CaseInsensitive) || text.contains(QLatin1String("write-protect"), Qt::CaseInsensitive)) {
        return QStringLiteral("The drive is write-protected. If it has a lock switch, slide it off, plug it back in and try again.");
    }
    return QStringLiteral("The drive couldn't be formatted. It may be faulty or not plugged in firmly. Try another USB port, then format it again.");
}

struct Outcome {
    bool ok = false;
    QString message;
    QString mountPath;
};

struct Request {
    QString object;
    QString fs;
    QString label;
    bool quick = true;
    QString password;
};

QDBusMessage call(const QString &object, const QString &iface, const QString &method, const QVariantList &args, int timeout = 120000)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, object, iface, method);
    message.setArguments(args);
    message.setInteractiveAuthorizationAllowed(true);
    return QDBusConnection::systemBus().call(message, QDBus::Block, timeout);
}

Outcome failure(const QDBusMessage &reply)
{
    return {false, plainError(QDBusError(reply)), {}};
}

Outcome run(const Request &request)
{
    ManagedObjects objects = managedObjects();
    if (objects.isEmpty()) {
        return {false, plainError(QDBusError(QDBusError::ServiceUnknown, QString())), {}};
    }
    const QString target = formatTarget(objects, request.object);
    if (!objects.contains(QDBusObjectPath(target))) {
        return {false, QStringLiteral("The drive isn't plugged in any more."), {}};
    }
    if (!allowed(objects, target)) {
        return {false, QStringLiteral("Files only formats removable drives, never the one Wild OS runs from."), {}};
    }
    const InterfaceProps targetIfaces = objects.value(QDBusObjectPath(target));
    const QVariantMap block = targetIfaces.value(blockIface);
    const QString drive = path(block.value(QStringLiteral("Drive")));
    const bool wholeDisk = !targetIfaces.contains(partIface);

    // Everything that has to let go first: the volume, or for a whole disk
    // every partition on it, and what any unlocked container holds.
    QStringList blocks;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap b = it.value().value(blockIface);
        if (it.key().path() == target || (wholeDisk && path(b.value(QStringLiteral("Drive"))) == drive)) {
            blocks << it.key().path();
        }
    }
    QStringList cleartexts;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (blocks.contains(path(it.value().value(blockIface).value(QStringLiteral("CryptoBackingDevice"))))) {
            cleartexts << it.key().path();
        }
    }
    const QVariantMap none;
    for (const QString &object : cleartexts + blocks) {
        if (mounted(objects.value(QDBusObjectPath(object)))) {
            const QDBusMessage reply = call(object, fsIface, QStringLiteral("Unmount"), {none});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                return failure(reply);
            }
        }
    }
    for (const QString &object : std::as_const(blocks)) {
        const InterfaceProps ifaces = objects.value(QDBusObjectPath(object));
        if (ifaces.contains(cryptIface) && !path(ifaces.value(cryptIface).value(QStringLiteral("CleartextDevice"))).isEmpty()) {
            const QDBusMessage reply = call(object, cryptIface, QStringLiteral("Lock"), {none});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                return failure(reply);
            }
        }
    }

    QVariantMap options;
    const QString label = cleanLabel(request.fs, request.label);
    if (!label.isEmpty()) {
        options.insert(QStringLiteral("label"), label);
    }
    if (!request.quick) {
        options.insert(QStringLiteral("erase"), QStringLiteral("zero"));
    }
    if (!request.password.isEmpty()) {
        options.insert(QStringLiteral("encrypt.passphrase"), request.password);
        options.insert(QStringLiteral("encrypt.type"), QStringLiteral("luks2"));
    }
    if (request.fs == QLatin1String("ext4")) {
        // A new ext4 belongs to root; this hands it to the person.
        options.insert(QStringLiteral("take-ownership"), true);
    }

    QString formatted = target;
    if (wholeDisk) {
        const qint64 size = block.value(QStringLiteral("Size")).toLongLong();
        const bool gpt = size > mbrLimit;
        QDBusMessage reply = call(target, blockIface, QStringLiteral("Format"), {gpt ? QStringLiteral("gpt") : QStringLiteral("dos"), none}, longCall);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            return failure(reply);
        }
        const bool linuxOnly = request.fs == QLatin1String("ext4") || !request.password.isEmpty();
        QString type;
        if (gpt) {
            type = linuxOnly ? QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4") : QStringLiteral("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
        } else {
            type = linuxOnly ? QStringLiteral("0x83") : request.fs == QLatin1String("vfat") ? QStringLiteral("0x0c") : QStringLiteral("0x07");
        }
        reply = call(target, tableIface, QStringLiteral("CreatePartitionAndFormat"),
                     {QVariant::fromValue<qulonglong>(0), QVariant::fromValue<qulonglong>(0), type, QString(), none, request.fs, options}, longCall);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            return failure(reply);
        }
        formatted = reply.arguments().value(0).value<QDBusObjectPath>().path();
    } else {
        options.insert(QStringLiteral("update-partition-type"), true);
        const QDBusMessage reply = call(target, blockIface, QStringLiteral("Format"), {request.fs, options}, longCall);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            return failure(reply);
        }
    }

    // Open it again, as Windows leaves a formatted drive ready to use. An
    // encrypted drive is left unlocked by the format; its file system is
    // inside. UDisks can take a moment to notice the new file system.
    Outcome outcome{true, {}, {}};
    for (int attempt = 0; attempt < 20; ++attempt) {
        objects = managedObjects();
        QString fs = formatted;
        const InterfaceProps ifaces = objects.value(QDBusObjectPath(formatted));
        if (ifaces.contains(cryptIface)) {
            fs = path(ifaces.value(cryptIface).value(QStringLiteral("CleartextDevice")));
        }
        if (!fs.isEmpty() && objects.value(QDBusObjectPath(fs)).contains(fsIface)) {
            const QDBusMessage reply = call(fs, fsIface, QStringLiteral("Mount"), {none});
            if (reply.type() == QDBusMessage::ReplyMessage) {
                outcome.mountPath = reply.arguments().value(0).toString();
            } else {
                qWarning() << "Format: mounting afterwards:" << reply.errorMessage();
            }
            break;
        }
        QThread::msleep(250);
    }
    return outcome;
}
}

DriveFormat *DriveFormat::instance()
{
    static DriveFormat *format = new DriveFormat;
    return format;
}

DriveFormat *DriveFormat::create(QQmlEngine *, QJSEngine *)
{
    auto format = instance();
    QJSEngine::setObjectOwnership(format, QJSEngine::CppOwnership);
    return format;
}

DriveFormat::DriveFormat(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(700);
    connect(&m_poll, &QTimer::timeout, this, &DriveFormat::poll);
}

QList<DriveFormat::Blank> DriveFormat::blankDrives()
{
    QList<Blank> out;
    const ManagedObjects objects = managedObjects();
    const QString formatting = instance()->m_busy ? instance()->m_drive : QString();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap drive = it.value().value(driveIface);
        const QString drivePath = it.key().path();
        if (drive.isEmpty() || drivePath == formatting || !removableDrive(drive)) {
            continue;
        }
        if (drive.value(QStringLiteral("MediaRemovable")).toBool() && !drive.value(QStringLiteral("MediaAvailable")).toBool()) {
            continue;
        }
        QString whole;
        qint64 size = 0;
        bool empty = true;
        for (auto b = objects.cbegin(); b != objects.cend() && empty; ++b) {
            const QVariantMap block = b.value().value(blockIface);
            if (block.isEmpty() || path(block.value(QStringLiteral("Drive"))) != drivePath) {
                continue;
            }
            if (!block.value(QStringLiteral("IdUsage")).toString().isEmpty() || b.value().contains(fsIface) || b.value().contains(cryptIface)) {
                empty = false;
            }
            if (!b.value().contains(partIface)) {
                whole = b.key().path();
                size = block.value(QStringLiteral("Size")).toLongLong();
            }
        }
        if (!empty || whole.isEmpty() || size <= 0 || !allowed(objects, whole)) {
            continue;
        }
        out.append({whole, kindOf(drive), size});
    }
    return out;
}

QString DriveFormat::objectFor(const QString &device)
{
    if (device.startsWith(blockPrefix)) {
        return device;
    }
    const QString real = QFileInfo(device).canonicalFilePath();
    const QString name = QFileInfo(real.isEmpty() ? device : real).fileName();
    return name.isEmpty() ? QString() : blockPrefix + name;
}

bool DriveFormat::canFormat(const QString &udi) const
{
    if (!udi.startsWith(blockPrefix)) {
        return false;
    }
    const ManagedObjects objects = managedObjects();
    return allowed(objects, formatTarget(objects, udi));
}

QVariantMap DriveFormat::describe(const QString &udi) const
{
    const ManagedObjects objects = managedObjects();
    const QString target = formatTarget(objects, udi);
    const InterfaceProps ifaces = objects.value(QDBusObjectPath(target));
    const QVariantMap block = ifaces.value(blockIface);
    const QVariantMap drive = objects.value(QDBusObjectPath(path(block.value(QStringLiteral("Drive"))))).value(driveIface);
    QVariantMap out;
    if (block.isEmpty() || !allowed(objects, target)) {
        out.insert(QStringLiteral("ok"), false);
        return out;
    }
    // The label people know is the file system's, which for an unlocked
    // encrypted drive is inside the container.
    QString label = block.value(QStringLiteral("IdLabel")).toString();
    if (ifaces.contains(cryptIface)) {
        const QString inside = path(ifaces.value(cryptIface).value(QStringLiteral("CleartextDevice")));
        if (!inside.isEmpty()) {
            label = objects.value(QDBusObjectPath(inside)).value(blockIface).value(QStringLiteral("IdLabel")).toString();
        }
    }
    // Asked for a whole disk (/dev/sdb), the label is its partition's.
    if (label.trimmed().isEmpty() && !ifaces.contains(partIface)) {
        for (auto it = objects.cbegin(); it != objects.cend() && label.trimmed().isEmpty(); ++it) {
            if (path(it.value().value(partIface).value(QStringLiteral("Table"))) == target) {
                label = it.value().value(blockIface).value(QStringLiteral("IdLabel")).toString();
            }
        }
    }
    const qint64 size = block.value(QStringLiteral("Size")).toLongLong();
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("label"), label.trimmed());
    out.insert(QStringLiteral("size"), double(size));
    out.insert(QStringLiteral("kind"), kindOf(drive));
    out.insert(QStringLiteral("readOnly"), block.value(QStringLiteral("ReadOnly")).toBool());
    out.insert(QStringLiteral("fatAllowed"), size <= fatLimit);
    out.insert(QStringLiteral("encrypted"), ifaces.contains(cryptIface));

    QDBusInterface manager(service, root + QStringLiteral("/Manager"), managerIface, QDBusConnection::systemBus());
    QVariantMap tools;
    for (const QString &fs : {QStringLiteral("exfat"), QStringLiteral("ntfs"), QStringLiteral("vfat"), QStringLiteral("ext4")}) {
        const QDBusMessage reply = manager.call(QStringLiteral("CanFormat"), fs);
        bool available = true;
        if (reply.type() == QDBusMessage::ReplyMessage) {
            const QDBusArgument arg = reply.arguments().value(0).value<QDBusArgument>();
            QString missing;
            arg.beginStructure();
            arg >> available >> missing;
            arg.endStructure();
        }
        tools.insert(fs, available);
    }
    out.insert(QStringLiteral("tools"), tools);
    const QStringList crypt = manager.property("SupportedEncryptionTypes").toStringList();
    out.insert(QStringLiteral("canEncrypt"), crypt.contains(QStringLiteral("luks2")) && encryptOffered());
    return out;
}

void DriveFormat::start(const QString &udi, const QString &fs, const QString &label, bool quick, const QString &password)
{
    static const QStringList known = {QStringLiteral("exfat"), QStringLiteral("ntfs"), QStringLiteral("vfat"), QStringLiteral("ext4")};
    if (m_busy || !known.contains(fs)) {
        return;
    }
    Request request{udi, fs, label, quick, password};
    {
        const ManagedObjects objects = managedObjects();
        const QVariantMap block = objects.value(QDBusObjectPath(formatTarget(objects, udi))).value(blockIface);
        m_drive = path(block.value(QStringLiteral("Drive")));
    }
    m_busy = true;
    m_progress = -1;
    Q_EMIT busyChanged();
    Q_EMIT progressChanged();
    m_poll.start();

    QPointer<DriveFormat> guard(this);
    QThread *thread = QThread::create([guard, request] {
        const Outcome outcome = run(request);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, outcome] {
            if (!guard) {
                return;
            }
            guard->m_poll.stop();
            guard->m_busy = false;
            Q_EMIT guard->busyChanged();
            // A format that failed half way can leave the drive blank, which
            // no device event announces.
            Q_EMIT Drives::instance()->changed();
            Q_EMIT guard->finished(outcome.ok, outcome.message, outcome.mountPath);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DriveFormat::poll()
{
    // A full format's zeroing runs as a UDisks job on one of the drive's
    // blocks, with its own progress.
    const ManagedObjects objects = managedObjects();
    QStringList blocks;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap b = it.value().value(blockIface);
        if (!b.isEmpty() && path(b.value(QStringLiteral("Drive"))) == m_drive) {
            blocks << it.key().path();
        }
    }
    double progress = -1;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap job = it.value().value(jobIface);
        if (job.isEmpty() || !job.value(QStringLiteral("ProgressValid")).toBool()) {
            continue;
        }
        for (const QString &object : paths(job.value(QStringLiteral("Objects")))) {
            if (blocks.contains(object)) {
                progress = std::max(progress, job.value(QStringLiteral("Progress")).toDouble());
            }
        }
    }
    if (progress != m_progress) {
        m_progress = progress;
        Q_EMIT progressChanged();
    }
}
