#include "windowsprograms.h"
#include "firstrun.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent>

#include <hivex.h>

#include <algorithm>

namespace
{
const QString appBridgeList = QStringLiteral("/usr/share/wildos-appbridge/alternatives.json");
const QString ownList = QStringLiteral("/usr/share/wildos-welcome/windows-programs.json");

// Windows paths are case-insensitive and the drive is read as Linux sees
// it, so each step of a path is looked up ignoring case.
QString childIgnoringCase(const QString &dir, const QString &name)
{
    const auto entries = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString &entry : entries) {
        if (entry.compare(name, Qt::CaseInsensitive) == 0) {
            return dir + QLatin1Char('/') + entry;
        }
    }
    return {};
}

QString pathIgnoringCase(QString path, const QStringList &parts)
{
    for (const QString &part : parts) {
        path = childIgnoringCase(path, part);
        if (path.isEmpty()) {
            return {};
        }
    }
    return path;
}

// One hive, opened read-only; closed when it goes out of scope.
class Hive
{
public:
    explicit Hive(const QString &path)
        : m_h(path.isEmpty() ? nullptr : hivex_open(QFile::encodeName(path).constData(), 0))
    {
    }
    ~Hive()
    {
        if (m_h) {
            hivex_close(m_h);
        }
    }
    bool ok() const { return m_h != nullptr; }

    hive_node_h node(const QStringList &path) const
    {
        hive_node_h node = hivex_root(m_h);
        for (const QString &part : path) {
            if (!node) {
                return 0;
            }
            node = hivex_node_get_child(m_h, node, part.toUtf8().constData());
        }
        return node;
    }

    QList<hive_node_h> children(hive_node_h node) const
    {
        QList<hive_node_h> list;
        hive_node_h *children = node ? hivex_node_children(m_h, node) : nullptr;
        for (hive_node_h *c = children; c && *c; ++c) {
            list << *c;
        }
        free(children);
        return list;
    }

    QString string(hive_node_h node, const char *name) const
    {
        const hive_value_h value = node ? hivex_node_get_value(m_h, node, name) : 0;
        char *text = value ? hivex_value_string(m_h, value) : nullptr;
        const QString result = text ? QString::fromUtf8(text).trimmed() : QString();
        free(text);
        return result;
    }

    qint64 dword(hive_node_h node, const char *name, qint64 fallback = -1) const
    {
        const hive_value_h value = node ? hivex_node_get_value(m_h, node, name) : 0;
        if (!value) {
            return fallback;
        }
        errno = 0;
        const int32_t number = hivex_value_dword(m_h, value);
        return errno ? fallback : qint64(uint32_t(number));
    }

private:
    hive_h *m_h;
};

// Updates, runtimes and parts of Windows itself are in the same keys as the
// programs a person chose; none of them is worth a row.
bool isNoise(const QString &name)
{
    static const QRegularExpression noise(
        QStringLiteral("redistributable|runtime|update for|hotfix|security update|\\.net (framework|desktop)|windows sdk|webview2|"
                       "edge update|update health|pc health check|onedrive|visual c\\+\\+|vcredist|directx|language pack|driver|"
                       "chipset|realtek|intel\\(r\\)|bonjour|python launcher|windows app certification|teams machine-wide|"
                       "microsoft update|gamebar|windows 1[01] update assistant"),
        QRegularExpression::CaseInsensitiveOption);
    return name.isEmpty() || noise.match(name).hasMatch();
}

void readUninstall(const Hive &hive, const QStringList &path, QList<WindowsPrograms::Found> &out)
{
    const auto children = hive.children(hive.node(path));
    for (const hive_node_h key : children) {
        const QString name = hive.string(key, "DisplayName");
        if (isNoise(name) || hive.dword(key, "SystemComponent", 0) == 1 || !hive.string(key, "ParentKeyName").isEmpty()) {
            continue;
        }
        const QString release = hive.string(key, "ReleaseType");
        if (release.contains(QLatin1String("Update")) || release.contains(QLatin1String("Hotfix"))) {
            continue;
        }
        WindowsPrograms::Found found;
        found.name = name;
        found.publisher = hive.string(key, "Publisher");
        found.folder = hive.string(key, "InstallLocation");
        // EstimatedSize is in KB.
        const qint64 size = hive.dword(key, "EstimatedSize");
        found.bytes = size > 0 ? size * 1024 : -1;
        out << found;
    }
}

qint64 folderSize(const QString &path)
{
    qint64 total = 0;
    int files = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext() && ++files < 200000) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString formatBytes(qint64 bytes)
{
    const QLocale locale = QLocale::system();
    if (bytes >= 1000000000) {
        return QCoreApplication::translate("WindowsPrograms", "%1 GB").arg(locale.toString(bytes / 1e9, 'f', 1));
    }
    return QCoreApplication::translate("WindowsPrograms", "%1 MB").arg(qMax<qint64>(1, bytes / 1000000));
}

QJsonArray loadList(const QString &path, const QString &resource, const char *key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        file.setFileName(resource);
        if (resource.isEmpty() || !file.open(QIODevice::ReadOnly)) {
            return {};
        }
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String(key)).toArray();
}

QJsonObject matchIn(const QJsonArray &list, const QString &text)
{
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        for (const QJsonValue &word : entry.value(QStringLiteral("match")).toArray()) {
            if (text.contains(word.toString())) {
                return entry;
            }
        }
    }
    return {};
}

bool storeAppInstalled(const QString &id)
{
    const QString desktop = id + QStringLiteral(".desktop");
    return !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktop).isEmpty()
        || QFile::exists(QStringLiteral("/var/lib/flatpak/exports/share/applications/") + desktop);
}
}

WindowsPrograms::WindowsPrograms(QObject *parent)
    : QObject(parent)
{
}

WindowsPrograms::Scan WindowsPrograms::read(const QString &windowsRoot, const QString &userHome)
{
    Scan scan;
    const QString software = pathIgnoringCase(windowsRoot, {QStringLiteral("Windows"), QStringLiteral("System32"), QStringLiteral("config"), QStringLiteral("SOFTWARE")});
    {
        const Hive hive(software);
        if (hive.ok()) {
            scan.registryRead = true;
            readUninstall(hive, {QStringLiteral("Microsoft"), QStringLiteral("Windows"), QStringLiteral("CurrentVersion"), QStringLiteral("Uninstall")}, scan.programs);
            readUninstall(hive, {QStringLiteral("WOW6432Node"), QStringLiteral("Microsoft"), QStringLiteral("Windows"), QStringLiteral("CurrentVersion"), QStringLiteral("Uninstall")},
                          scan.programs);

            const hive_node_h version = hive.node({QStringLiteral("Microsoft"), QStringLiteral("Windows NT"), QStringLiteral("CurrentVersion")});
            QString product = hive.string(version, "ProductName");
            // Windows 11 still calls itself Windows 10 here; its builds start at 22000.
            if (hive.string(version, "CurrentBuildNumber").toInt() >= 22000) {
                product.replace(QLatin1String("Windows 10"), QLatin1String("Windows 11"));
            }
            scan.product = product;
            const QString key = hive.string(hive.node({QStringLiteral("Microsoft"), QStringLiteral("Windows NT"), QStringLiteral("CurrentVersion"),
                                                       QStringLiteral("SoftwareProtectionPlatform")}),
                                            "BackupProductKeyDefault");
            static const QRegularExpression shape(QStringLiteral("^([A-Z0-9]{5}-){4}[A-Z0-9]{5}$"));
            if (shape.match(key).hasMatch()) {
                scan.productKeyEnd = key.right(5);
            }
        }
    }
    if (!userHome.isEmpty()) {
        const Hive hive(childIgnoringCase(userHome, QStringLiteral("NTUSER.DAT")));
        if (hive.ok()) {
            readUninstall(hive, {QStringLiteral("Software"), QStringLiteral("Microsoft"), QStringLiteral("Windows"), QStringLiteral("CurrentVersion"), QStringLiteral("Uninstall")},
                          scan.programs);
        }
    }

    // The same program can be listed twice, once per architecture.
    QSet<QString> seen;
    QList<Found> unique;
    for (const Found &found : std::as_const(scan.programs)) {
        if (!seen.contains(found.name.toLower())) {
            seen.insert(found.name.toLower());
            unique << found;
        }
    }
    scan.programs = unique;

    // Program Files holds what the registry does not know of: programs
    // copied in by hand, and everything when the hive could not be read.
    static const QStringList ignored = {QStringLiteral("common files"), QStringLiteral("windows defender"), QStringLiteral("windows nt"),
                                        QStringLiteral("internet explorer"), QStringLiteral("windowsapps"), QStringLiteral("modifiablewindowsapps"),
                                        QStringLiteral("windows mail"), QStringLiteral("windows media player"), QStringLiteral("windows photo viewer"),
                                        QStringLiteral("windows portable devices"), QStringLiteral("windows security"), QStringLiteral("windows sidebar"),
                                        QStringLiteral("windowspowershell"), QStringLiteral("uninstall information"), QStringLiteral("reference assemblies"),
                                        QStringLiteral("msbuild"), QStringLiteral("dotnet"), QStringLiteral("microsoft.net"), QStringLiteral("microsoft update health tools"),
                                        QStringLiteral("windows defender advanced threat protection"), QStringLiteral("microsoft"), QStringLiteral("package cache"),
                                        QStringLiteral("installshield installation information"), QStringLiteral("microsoft onedrive")};
    for (const QString &top : {QStringLiteral("Program Files"), QStringLiteral("Program Files (x86)")}) {
        const QString dir = childIgnoringCase(windowsRoot, top);
        if (dir.isEmpty()) {
            continue;
        }
        const auto entries = QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QFileInfo &entry : entries) {
            const QString folder = entry.fileName();
            const QString lower = folder.toLower();
            if (ignored.contains(lower) || isNoise(folder)) {
                continue;
            }
            const bool known = std::any_of(scan.programs.cbegin(), scan.programs.cend(), [&](const Found &f) {
                return f.name.toLower().contains(lower) || lower.contains(f.name.toLower())
                    || QString(f.folder).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower().contains(QLatin1Char('/') + lower)
                    || f.publisher.toLower() == lower;
            });
            if (known) {
                continue;
            }
            Found found;
            found.name = folder;
            found.bytes = folderSize(entry.absoluteFilePath());
            if (found.bytes > 0) {
                scan.programs << found;
            }
        }
    }
    return scan;
}

void WindowsPrograms::scan(const QString &windowsRoot, const QString &userHome)
{
    if (windowsRoot.isEmpty()) {
        m_state = QStringLiteral("none");
        m_programs.clear();
        Q_EMIT changed();
        return;
    }
    if (m_state == QLatin1String("scanning")) {
        return;
    }
    m_state = QStringLiteral("scanning");
    Q_EMIT changed();
    const int generation = ++m_generation;
    auto *watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_generation) {
            classify(watcher->result());
        }
    });
    watcher->setFuture(QtConcurrent::run(&WindowsPrograms::read, windowsRoot, userHome));
}

void WindowsPrograms::classify(const Scan &scan)
{
    const QJsonArray bridge = loadList(appBridgeList, {}, "apps");
    const QJsonArray own = loadList(ownList, QStringLiteral(":/qt/qml/WildOS/Welcome/data/windows-programs.json"), "programs");
    m_registryRead = scan.registryRead;
    m_product = scan.product;
    m_productKeyEnd = scan.productKeyEnd;

    QVariantList rows;
    for (const Found &found : scan.programs) {
        const QString text = (found.name + QLatin1Char(' ') + found.publisher).toLower();
        const QJsonObject mine = matchIn(own, text);
        const QJsonObject theirs = matchIn(bridge, text);
        // The App Bridge's own list says drivers are not programs to bring.
        const QString listed = theirs.value(QStringLiteral("name")).toString();
        if (listed == QLatin1String("Drivers") || listed == QLatin1String("NVIDIA drivers")) {
            continue;
        }
        const QString storeId = theirs.value(QStringLiteral("storeId")).toString();
        const QString runs = theirs.value(QStringLiteral("runs")).toString();
        const QString size = found.bytes > 0 ? formatBytes(found.bytes) : QString();

        QVariantMap row{
            {QStringLiteral("key"), found.name},
            {QStringLiteral("name"), found.name},
            {QStringLiteral("selectable"), false},
            {QStringLiteral("checked"), false},
            {QStringLiteral("action"), QString()},
            {QStringLiteral("storeId"), storeId},
        };
        auto set = [&row](const QString &kind, const QString &action, const QString &detail) {
            row.insert(QStringLiteral("kind"), kind);
            row.insert(QStringLiteral("action"), action);
            row.insert(QStringLiteral("detail"), detail);
            row.insert(QStringLiteral("selectable"), !action.isEmpty());
            row.insert(QStringLiteral("checked"), !action.isEmpty());
        };

        if (mine.value(QStringLiteral("mode")).toString() == QLatin1String("windows")) {
            set(QStringLiteral("windows"), QStringLiteral("windows"), mine.value(QStringLiteral("detail")).toString(size));
        } else if (mine.contains(QStringLiteral("installer"))) {
            row.insert(QStringLiteral("installer"), mine.value(QStringLiteral("installer")).toObject().toVariantMap());
            set(QStringLiteral("desktop"), QStringLiteral("download"), QCoreApplication::translate("WindowsPrograms", "A fresh installer from its makers, set up with Windows app support"));
        } else if (!storeId.isEmpty()) {
            // "Discord has a Wild OS version..." says no more than the chip;
            // "Pinta, free in the Store, works a lot like Paint.NET" names
            // what is installed instead, so it stays.
            QString native = theirs.value(QStringLiteral("native")).toString();
            if (native.startsWith(theirs.value(QStringLiteral("name")).toString() + QStringLiteral(" has a Wild OS version"))) {
                native = QCoreApplication::translate("WindowsPrograms", "Its Wild OS version, from the Store");
            }
            if (storeAppInstalled(storeId)) {
                set(QStringLiteral("desktop"), QString(), native.isEmpty() ? QCoreApplication::translate("WindowsPrograms", "Already on Wild OS") : native);
            } else {
                set(QStringLiteral("desktop"), QStringLiteral("store"), native);
            }
        } else if (runs == QLatin1String("no")) {
            set(QStringLiteral("unsupported"), QString(), theirs.value(QStringLiteral("reason")).toString());
        } else if (runs == QLatin1String("well") || runs == QLatin1String("some")) {
            set(QStringLiteral("desktop"), QString(), QCoreApplication::translate("WindowsPrograms", "Open its Windows installer on Wild OS to set it up"));
        } else {
            set(QStringLiteral("unknown"), QString(), size.isEmpty() ? QCoreApplication::translate("WindowsPrograms", "Wild OS hasn't tried this one yet") : QCoreApplication::translate("WindowsPrograms", "%1 · Wild OS hasn't tried this one yet").arg(size));
        }
        rows << row;
    }

    // What can be set up first, then what needs Windows mode, then the rest.
    auto rank = [](const QVariantMap &row) {
        const QString kind = row.value(QStringLiteral("kind")).toString();
        const bool selectable = row.value(QStringLiteral("selectable")).toBool();
        return kind == QLatin1String("windows") ? 1 : selectable ? 0 : kind == QLatin1String("desktop") ? 2 : kind == QLatin1String("unknown") ? 3 : 4;
    };
    std::stable_sort(rows.begin(), rows.end(), [&rank](const QVariant &a, const QVariant &b) {
        const int ra = rank(a.toMap());
        const int rb = rank(b.toMap());
        if (ra != rb) {
            return ra < rb;
        }
        return QString::localeAwareCompare(a.toMap().value(QStringLiteral("name")).toString(), b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_programs = rows;
    m_state = QStringLiteral("done");
    Q_EMIT changed();
}

void WindowsPrograms::setChecked(const QString &key, bool checked)
{
    for (int i = 0; i < m_programs.size(); ++i) {
        QVariantMap row = m_programs.at(i).toMap();
        if (row.value(QStringLiteral("key")) == key && row.value(QStringLiteral("selectable")).toBool()) {
            row.insert(QStringLiteral("checked"), checked);
            m_programs[i] = row;
            Q_EMIT changed();
            return;
        }
    }
}

QStringList WindowsPrograms::windowsModeNames() const
{
    QStringList names;
    for (const QVariant &value : m_programs) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("kind")) == QLatin1String("windows")) {
            names << row.value(QStringLiteral("name")).toString();
        }
    }
    return names;
}

int WindowsPrograms::downloadCount() const
{
    int count = 0;
    for (const QVariant &value : m_programs) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("checked")).toBool() && row.value(QStringLiteral("action")) == QLatin1String("download")) {
            ++count;
        }
    }
    return count;
}

void WindowsPrograms::setLicence(const QString &licence, const QString &text)
{
    m_licence = licence;
    m_licenceText = text;
    Q_EMIT licenceChanged();
}

void WindowsPrograms::checkLicence()
{
    if (m_licence == QLatin1String("checking")) {
        return;
    }
    setLicence(QStringLiteral("checking"), QCoreApplication::translate("WindowsPrograms", "Looking for your Windows licence."));
    // Only the setup service can read the firmware's key; without the
    // system bus it cannot be asked, and the drive is all there is.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Setup1"), QStringLiteral("/org/wildos/Setup1"), QStringLiteral("org.wildos.Setup1"),
                                                       QStringLiteral("FirmwareLicence"));
    const auto found = [this](const QString &firmware) {
        if (!firmware.isEmpty()) {
            setLicence(QStringLiteral("firmware"),
                       QCoreApplication::translate("WindowsPrograms", "This computer has a Windows licence built in, with a key ending %1. Windows mode will use it and finish setting up later, in the background.")
                           .arg(firmware));
        } else if (!m_productKeyEnd.isEmpty()) {
            setLicence(QStringLiteral("registry"),
                       QCoreApplication::translate("WindowsPrograms", "%1 on your Windows drive has a product key ending %2. Windows mode will use it and finish setting up later, in the background. "
                                      "Microsoft confirms the licence when Windows mode first starts.")
                           .arg(m_product.isEmpty() ? QStringLiteral("Windows") : m_product, m_productKeyEnd));
        } else {
            setLicence(QStringLiteral("none"),
                       QCoreApplication::translate("WindowsPrograms", "Windows mode needs a Windows licence, and Wild OS didn't find one in this computer or on its Windows drive. "
                                      "Nothing was set up."));
        }
    };
    if (!QDBusConnection::systemBus().isConnected()) {
        found(QString());
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 20000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, found] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        found(reply.isError() ? QString() : reply.value());
    });
}

QVariantMap WindowsPrograms::plan() const
{
    QVariantList programs;
    QStringList windowsMode;
    for (const QVariant &value : m_programs) {
        const QVariantMap row = value.toMap();
        if (!row.value(QStringLiteral("checked")).toBool()) {
            continue;
        }
        const QString action = row.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("windows")) {
            windowsMode << row.value(QStringLiteral("name")).toString();
        } else if (!action.isEmpty()) {
            programs << QVariantMap{
                {QStringLiteral("name"), row.value(QStringLiteral("name"))},
                {QStringLiteral("action"), action},
                {QStringLiteral("storeId"), row.value(QStringLiteral("storeId"))},
                {QStringLiteral("installer"), row.value(QStringLiteral("installer"))},
            };
        }
    }
    QVariantMap plan{{QStringLiteral("programs"), programs}};
    if (m_licence == QLatin1String("firmware") || m_licence == QLatin1String("registry")) {
        plan.insert(QStringLiteral("windowsMode"), QVariantMap{
                                                       {QStringLiteral("licence"), m_licence},
                                                       {QStringLiteral("product"), m_product},
                                                       {QStringLiteral("programs"), windowsMode},
                                                   });
    }
    return plan;
}

void WindowsPrograms::start() const
{
    const QVariantMap plan = this->plan();
    FirstRun::startPrograms(plan.value(QStringLiteral("programs")).toList());
    FirstRun::saveWindowsMode(plan.value(QStringLiteral("windowsMode")).toMap());
}
