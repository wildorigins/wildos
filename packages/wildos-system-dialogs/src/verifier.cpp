#include "verifier.h"

#include <KService>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

namespace
{
struct Policy {
    QString file;
    QString vendor;
    QString execPath;
};

Policy findPolicy(const QString &actionId)
{
    const QDir dir(QStringLiteral("/usr/share/polkit-1/actions"));
    const auto files = dir.entryInfoList({QStringLiteral("*.policy")}, QDir::Files);
    for (const QFileInfo &info : files) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        // Cheap test first: most files do not mention the action at all.
        const QByteArray data = file.readAll();
        if (!data.contains(actionId.toUtf8())) {
            continue;
        }
        QXmlStreamReader xml(data);
        QString vendor;
        bool inAction = false;
        Policy found;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == QLatin1String("vendor") && !inAction) {
                    vendor = xml.readElementText().trimmed();
                } else if (xml.name() == QLatin1String("action")) {
                    inAction = xml.attributes().value(QLatin1String("id")) == actionId;
                    if (inAction) {
                        found.file = info.filePath();
                        found.vendor = vendor;
                    }
                } else if (inAction && xml.name() == QLatin1String("annotate")
                           && xml.attributes().value(QLatin1String("key")) == QLatin1String("org.freedesktop.policykit.exec.path")) {
                    found.execPath = xml.readElementText().trimmed();
                }
            } else if (xml.isEndElement() && xml.name() == QLatin1String("action") && inAction) {
                return found;
            }
        }
    }
    return {};
}

QString run(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.start();
    if (!process.waitForFinished(4000)) {
        process.kill();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

// The package a file belongs to. With /usr merged, dpkg may have it under
// /bin or /lib rather than /usr/bin or /usr/lib.
QString ownerOf(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    QStringList candidates{path};
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (!canonical.isEmpty() && canonical != path) {
        candidates << canonical;
    }
    for (const QString &candidate : QStringList(candidates)) {
        if (candidate.startsWith(QLatin1String("/usr/"))) {
            candidates << candidate.mid(4);
        }
    }
    for (const QString &candidate : candidates) {
        const QString out = run(QStringLiteral("dpkg-query"), {QStringLiteral("-S"), candidate});
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (line.startsWith(QLatin1String("diversion "))) {
                continue;
            }
            const int colon = line.indexOf(QLatin1String(": "));
            if (colon > 0 && line.mid(colon + 2).trimmed() == candidate) {
                return line.left(colon).section(QLatin1Char(','), 0, 0).trimmed();
            }
        }
    }
    return {};
}

// Whether the file is as its package installed it. Files dpkg keeps no
// checksum for (configuration files) are taken as they are.
bool unchanged(const QString &package, const QString &path)
{
    const QString base = package.section(QLatin1Char(':'), 0, 0);
    QFile sums(QStringLiteral("/var/lib/dpkg/info/%1.md5sums").arg(package));
    if (!sums.exists()) {
        sums.setFileName(QStringLiteral("/var/lib/dpkg/info/%1:amd64.md5sums").arg(base));
    }
    if (!sums.open(QIODevice::ReadOnly)) {
        return true;
    }
    QStringList names{path.mid(1)};
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (!canonical.isEmpty()) {
        names << canonical.mid(1);
    }
    for (const QString &name : QStringList(names)) {
        if (name.startsWith(QLatin1String("usr/"))) {
            names << name.mid(4);
        }
    }
    QByteArray expected;
    while (!sums.atEnd()) {
        const QByteArray line = sums.readLine().trimmed();
        const int space = line.indexOf(' ');
        if (space > 0 && names.contains(QString::fromUtf8(line.mid(space).trimmed()))) {
            expected = line.left(space);
            break;
        }
    }
    if (expected.isEmpty()) {
        return true;
    }
    QFile file(canonical.isEmpty() ? path : canonical);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(&file);
    return hash.result().toHex() == expected;
}

struct Source {
    bool trusted = false;
    QString origin;
};

// The exact package set the image was built with (build/hooks/
// 88-image-manifest.sh): "name<TAB>version" per line.
const QHash<QString, QString> &imagePackages()
{
    static const QHash<QString, QString> packages = [] {
        QHash<QString, QString> list;
        QFile file(QStringLiteral("/usr/share/wildos/image-packages"));
        if (file.open(QIODevice::ReadOnly)) {
            while (!file.atEnd()) {
                const QList<QByteArray> parts = file.readLine().trimmed().split('\t');
                if (parts.size() == 2) {
                    list.insert(QString::fromUtf8(parts.at(0)), QString::fromUtf8(parts.at(1)));
                }
            }
        }
        return list;
    }();
    return packages;
}

// The hosts whose archives Wild OS trusts. record-package-sources keeps
// the same list.
bool trustedHost(const QString &host)
{
    return host == QLatin1String("deb.debian.org") || host == QLatin1String("security.debian.org") || host == QLatin1String("deb.kicksecure.com")
        || host == QLatin1String("wildos.io") || host.endsWith(QLatin1String(".wildos.io"));
}

QString originOfHost(const QString &host)
{
    return host.contains(QLatin1String("wildos")) ? QStringLiteral("Wild OS") : host.contains(QLatin1String("kicksecure")) ? QStringLiteral("Kicksecure")
                                                                                                                            : QStringLiteral("Debian");
}

// The archive each installed version came from, as record-package-sources
// noted it when the version was installed: apt itself forgets as soon as
// the archive lists a newer version, which deb.wildos.io does with every
// build. Read afresh for each prompt, since updates change it.
QString recordedHost(const QString &name, const QString &version)
{
    QFile file(QStringLiteral("/var/lib/wildos/package-sources"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray prefix = name.toUtf8() + '\t' + version.toUtf8() + '\t';
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith(prefix)) {
            return QString::fromUtf8(line.mid(prefix.size()));
        }
    }
    return {};
}

// Where the installed version of a package came from. It is part of Wild
// OS when that exact version shipped in the image, which came from the
// signed build, or when apt says, or said when it was installed, that
// version comes from Debian (with its security archive), Kicksecure or the
// Wild OS repository. A package's own Maintainer field is not evidence:
// any .deb can claim any name.
Source sourceOf(const QString &package)
{
    Source source;
    const QString name = package.section(QLatin1Char(':'), 0, 0);
    const QString version = run(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${Version}"), package}).trimmed();
    // By version too: the agent runs for the whole session, and an update
    // in the meantime makes it another package as far as trust goes.
    const QString cacheKey = package + QLatin1Char(' ') + version;
    static QHash<QString, Source> cache;
    if (cache.contains(cacheKey)) {
        return cache.value(cacheKey);
    }
    const auto &image = imagePackages();
    if (!version.isEmpty() && (image.value(package) == version || image.value(name) == version)) {
        source.trusted = true;
        source.origin = QStringLiteral("Wild OS");
        cache.insert(cacheKey, source);
        return source;
    }
    const QString recorded = version.isEmpty() ? QString() : recordedHost(name, version);
    if (trustedHost(recorded)) {
        source.trusted = true;
        source.origin = originOfHost(recorded);
        cache.insert(cacheKey, source);
        return source;
    }

    const QString policy = run(QStringLiteral("apt-cache"), {QStringLiteral("policy"), package});
    QStringList hosts;
    bool installed = false;
    for (const QString &line : policy.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String(" *** "))) {
            installed = true;
            continue;
        }
        if (installed) {
            // The next version's line ends the installed one's sources.
            // "\\S": a lone "\S" in a C++ string is just "S", which no line
            // matches, so other versions' archives counted for this one.
            static const QRegularExpression versionLine(QStringLiteral("^ {5}\\S"));
            if (versionLine.match(line).hasMatch()) {
                break;
            }
            const QStringList parts = line.simplified().split(QLatin1Char(' '));
            if (parts.size() >= 2 && parts.at(1).contains(QLatin1String("://"))) {
                hosts << QUrl(parts.at(1)).host();
            }
        }
    }

    for (const QString &host : std::as_const(hosts)) {
        if (trustedHost(host)) {
            source.trusted = true;
            source.origin = originOfHost(host);
            break;
        }
    }
    if (!source.trusted && !hosts.isEmpty()) {
        source.origin = hosts.constFirst();
    }
    cache.insert(cacheKey, source);
    return source;
}

struct FileCheck {
    bool ok = false;
    QString package;
    QString origin;
    QString why;
};

FileCheck checkFile(const QString &path, const QString &what)
{
    FileCheck check;
    if (path.isEmpty()) {
        check.ok = true;
        return check;
    }
    check.package = ownerOf(path);
    if (check.package.isEmpty()) {
        check.why = QStringLiteral("%1 isn't part of Wild OS.").arg(what);
        return check;
    }
    if (!unchanged(check.package, path)) {
        check.why = QStringLiteral("%1 was changed after it was installed.").arg(what);
        return check;
    }
    const Source source = sourceOf(check.package);
    check.origin = source.origin;
    if (!source.trusted) {
        check.why = source.origin.isEmpty() ? QStringLiteral("%1 comes from outside Wild OS.").arg(what)
                                            : QStringLiteral("%1 comes from %2, not from Wild OS.").arg(what, source.origin);
        return check;
    }
    check.ok = true;
    return check;
}

QString readProc(qint64 pid, const char *name)
{
    QFile file(QStringLiteral("/proc/%1/%2").arg(pid).arg(QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString exeOf(qint64 pid)
{
    QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    exe.remove(QLatin1String(" (deleted)"));
    return exe;
}

// A script run by an interpreter is the program that matters, not the
// interpreter; "bash" with no script is an interactive shell.
QString scriptOf(qint64 pid, const QString &exe)
{
    static const QRegularExpression interpreter(QStringLiteral("^(ba|da|z|k|fi)?sh$|^python[0-9.]*$|^perl[0-9.]*$|^ruby[0-9.]*$|^node(js)?$|^lua[0-9.]*$"));
    if (!interpreter.match(QFileInfo(exe).fileName()).hasMatch()) {
        return {};
    }
    const QStringList args = readProc(pid, "cmdline").split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("-c")) {
            return {};
        }
        if (arg.startsWith(QLatin1Char('-'))) {
            continue;
        }
        const QString cwd = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(pid)).symLinkTarget();
        const QFileInfo script(QDir(cwd).absoluteFilePath(arg));
        return script.isFile() ? script.absoluteFilePath() : QString();
    }
    return {};
}

// pkexec [--user name] [--keep-cwd] [--disable-internal-agent] program args
void programOfPkexec(qint64 pid, QString *program, QString *commandLine)
{
    const QStringList args = readProc(pid, "cmdline").split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    if (args.isEmpty() || QFileInfo(args.constFirst()).fileName() != QLatin1String("pkexec")) {
        return;
    }
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--user") || arg == QLatin1String("-u")) {
            ++i;
            continue;
        }
        if (arg.startsWith(QLatin1Char('-'))) {
            continue;
        }
        QString path = arg;
        if (!path.startsWith(QLatin1Char('/'))) {
            const QString found = QStandardPaths::findExecutable(path);
            path = found.isEmpty() ? path : found;
        }
        *program = path;
        *commandLine = QStringList(args.mid(i)).join(QLatin1Char(' '));
        return;
    }
}

// The desktop's launcher puts each app in a systemd unit named after its
// desktop file: app-org.wildos.settings@1a2b.service, app-flatpak-org.
// gimp.GIMP-1234.scope.
QString appIdOf(qint64 pid, bool *flatpak)
{
    *flatpak = false;
    const QString cgroup = readProc(pid, "cgroup");
    static const QRegularExpression unit(QStringLiteral("/app-(?:(flatpak|gnome|kde|xdg)-)?([^/@]+?)(?:@[^/]*\\.service|-[0-9]+\\.scope|\\.service|\\.scope)(?:/|$)"));
    QString id;
    auto it = unit.globalMatch(cgroup);
    while (it.hasNext()) {
        const auto match = it.next();
        id = match.captured(2);
        *flatpak = match.captured(1) == QLatin1String("flatpak");
    }
    // systemd escapes "-" inside names as \x2d.
    id.replace(QLatin1String("\\x2d"), QLatin1String("-"));
    return id;
}

// The app a package provides, from the first desktop file in it.
KService::Ptr appOfPackage(const QString &package)
{
    if (package.isEmpty()) {
        return {};
    }
    const QString files = run(QStringLiteral("dpkg-query"), {QStringLiteral("-L"), package});
    for (const QString &file : files.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (file.startsWith(QLatin1String("/usr/share/applications/")) && file.endsWith(QLatin1String(".desktop"))) {
            KService::Ptr service(new KService(file));
            if (service->isValid() && !service->name().isEmpty() && !service->exec().isEmpty()
                && !service->desktopEntryName().startsWith(QLatin1String("org.kde.kwin"))) {
                return service;
            }
        }
    }
    return {};
}

struct Wording {
    const char *purpose;
    const char *app = nullptr;
    const char *icon = nullptr;
};

const QHash<QString, Wording> &wordings()
{
    static const QHash<QString, Wording> map = {
        {QStringLiteral("org.freedesktop.packagekit.system-update"), {"Wants to install updates for the system.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.freedesktop.packagekit.trigger-offline-update"), {"Wants to install updates the next time the computer restarts.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.freedesktop.packagekit.clear-offline-update"), {"Wants to cancel updates waiting for a restart.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.freedesktop.packagekit.trigger-offline-upgrade"), {"Wants to upgrade Wild OS the next time the computer restarts.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.freedesktop.packagekit.upgrade-system"), {"Wants to upgrade Wild OS to a new version.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.freedesktop.packagekit.package-install"), {"Wants to install an app for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.packagekit.package-install-untrusted"), {"Wants to install a package from a file. Its setup runs with full access to this computer."}},
        {QStringLiteral("org.freedesktop.packagekit.package-reinstall"), {"Wants to repair an app by installing it again."}},
        {QStringLiteral("org.freedesktop.packagekit.package-downgrade"), {"Wants to replace an app with an older version."}},
        {QStringLiteral("org.freedesktop.packagekit.package-remove"), {"Wants to remove an app from this computer."}},
        {QStringLiteral("org.freedesktop.packagekit.system-sources-configure"), {"Wants to change where apps and updates come from."}},
        {QStringLiteral("org.freedesktop.packagekit.repo-enable"), {"Wants to change where apps and updates come from."}},
        {QStringLiteral("org.freedesktop.packagekit.system-sources-refresh"), {"Wants to check for new apps and updates."}},
        {QStringLiteral("org.freedesktop.packagekit.system-network-proxy-configure"), {"Wants to change the proxy used to download updates."}},
        {QStringLiteral("org.freedesktop.packagekit.cancel-foreign"), {"Wants to stop an install another person started."}},
        {QStringLiteral("org.freedesktop.packagekit.repair-system"), {"Wants to repair the apps on this computer."}},
        {QStringLiteral("org.freedesktop.packagekit.system-trust-signing-key"), {"Wants to trust a new source of apps and updates."}},

        {QStringLiteral("org.freedesktop.udisks2.filesystem-mount"), {"Wants to open a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.filesystem-mount-system"), {"Wants to open a drive that belongs to the system."}},
        {QStringLiteral("org.freedesktop.udisks2.filesystem-mount-other-seat"), {"Wants to open a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.filesystem-unmount-others"), {"Wants to close a drive another person opened."}},
        {QStringLiteral("org.freedesktop.udisks2.filesystem-take-ownership"), {"Wants to take ownership of the files on a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.filesystem-fstab"), {"Wants to open a drive set up for the whole system."}},
        {QStringLiteral("org.freedesktop.udisks2.encrypted-unlock"), {"Wants to unlock an encrypted drive."}},
        {QStringLiteral("org.freedesktop.udisks2.encrypted-unlock-system"), {"Wants to unlock an encrypted drive that belongs to the system."}},
        {QStringLiteral("org.freedesktop.udisks2.encrypted-unlock-other-seat"), {"Wants to unlock an encrypted drive."}},
        {QStringLiteral("org.freedesktop.udisks2.encrypted-unlock-crypttab"), {"Wants to unlock an encrypted drive set up for the whole system."}},
        {QStringLiteral("org.freedesktop.udisks2.encrypted-change-passphrase"), {"Wants to change the password of an encrypted drive."}},
        {QStringLiteral("org.freedesktop.udisks2.encrypted-change-passphrase-system"), {"Wants to change the password of an encrypted drive."}},
        {QStringLiteral("org.freedesktop.udisks2.loop-setup"), {"Wants to open a disk image."}},
        {QStringLiteral("org.freedesktop.udisks2.eject-media"), {"Wants to eject a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.eject-media-system"), {"Wants to eject a drive that belongs to the system."}},
        {QStringLiteral("org.freedesktop.udisks2.power-off-drive"), {"Wants to switch off a drive so it can be unplugged."}},
        {QStringLiteral("org.freedesktop.udisks2.power-off-drive-system"), {"Wants to switch off a drive that belongs to the system."}},
        {QStringLiteral("org.freedesktop.udisks2.modify-device"), {"Wants to erase, format or partition a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.modify-device-system"), {"Wants to erase, format or partition a drive that belongs to the system."}},
        {QStringLiteral("org.freedesktop.udisks2.modify-system-configuration"), {"Wants to change how drives open when the computer starts."}},
        {QStringLiteral("org.freedesktop.udisks2.open-device"), {"Wants to read a whole drive directly."}},
        {QStringLiteral("org.freedesktop.udisks2.open-device-system"), {"Wants to read a system drive directly."}},
        {QStringLiteral("org.freedesktop.udisks2.ata-smart-update"), {"Wants to check the health of a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.ata-smart-selftest"), {"Wants to test the health of a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.rescan"), {"Wants to look for changes on a drive."}},
        {QStringLiteral("org.freedesktop.udisks2.manage-md-raid"), {"Wants to change a group of drives used together."}},

        {QStringLiteral("org.freedesktop.NetworkManager.settings.modify.system"), {"Wants to change network settings for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.NetworkManager.settings.modify.own"), {"Wants to change your network settings."}},
        {QStringLiteral("org.freedesktop.NetworkManager.settings.modify.hostname"), {"Wants to rename this computer."}},
        {QStringLiteral("org.freedesktop.NetworkManager.settings.modify.global-dns"), {"Wants to change how this computer looks up websites."}},
        {QStringLiteral("org.freedesktop.NetworkManager.network-control"), {"Wants to connect to or disconnect from a network."}},
        {QStringLiteral("org.freedesktop.NetworkManager.enable-disable-network"), {"Wants to turn networking on or off."}},
        {QStringLiteral("org.freedesktop.NetworkManager.enable-disable-wifi"), {"Wants to turn Wi-Fi on or off."}},
        {QStringLiteral("org.freedesktop.NetworkManager.enable-disable-wwan"), {"Wants to turn mobile broadband on or off."}},
        {QStringLiteral("org.freedesktop.NetworkManager.wifi.share.open"), {"Wants to share this computer's internet connection over open Wi-Fi."}},
        {QStringLiteral("org.freedesktop.NetworkManager.wifi.share.protected"), {"Wants to share this computer's internet connection over Wi-Fi."}},
        {QStringLiteral("org.freedesktop.NetworkManager.wifi.scan"), {"Wants to look for Wi-Fi networks."}},
        {QStringLiteral("org.freedesktop.NetworkManager.reload"), {"Wants to restart the network service."}},
        {QStringLiteral("org.freedesktop.NetworkManager.checkpoint-rollback"), {"Wants to undo a change to the network settings."}},

        {QStringLiteral("org.freedesktop.login1.reboot"), {"Wants to restart the computer."}},
        {QStringLiteral("org.freedesktop.login1.power-off"), {"Wants to shut down the computer."}},
        {QStringLiteral("org.freedesktop.login1.halt"), {"Wants to shut down the computer."}},
        {QStringLiteral("org.freedesktop.login1.suspend"), {"Wants to put the computer to sleep."}},
        {QStringLiteral("org.freedesktop.login1.hibernate"), {"Wants to hibernate the computer."}},
        {QStringLiteral("org.freedesktop.login1.set-reboot-to-firmware-setup"), {"Wants to restart into the computer's firmware settings."}},
        {QStringLiteral("org.freedesktop.login1.set-wall-message"), {"Wants to show a message to everyone signed in."}},
        {QStringLiteral("org.freedesktop.login1.lock-sessions"), {"Wants to lock everyone's screen."}},
        {QStringLiteral("org.freedesktop.login1.manage"), {"Wants to sign out or manage other people's sessions."}},
        {QStringLiteral("org.freedesktop.login1.attach-device"), {"Wants to give a device to another screen and keyboard."}},

        {QStringLiteral("org.freedesktop.timedate1.set-time"), {"Wants to change the date and time."}},
        {QStringLiteral("org.freedesktop.timedate1.set-timezone"), {"Wants to change the time zone."}},
        {QStringLiteral("org.freedesktop.timedate1.set-ntp"), {"Wants to change whether the clock sets itself."}},
        {QStringLiteral("org.freedesktop.timedate1.set-local-rtc"), {"Wants to change how the hardware clock keeps time."}},
        {QStringLiteral("org.kde.kcontrol.kcmclock.save"), {"Wants to change the date and time."}},
        {QStringLiteral("org.freedesktop.hostname1.set-hostname"), {"Wants to rename this computer."}},
        {QStringLiteral("org.freedesktop.hostname1.set-static-hostname"), {"Wants to rename this computer."}},
        {QStringLiteral("org.freedesktop.hostname1.set-machine-info"), {"Wants to change this computer's description."}},
        {QStringLiteral("org.freedesktop.locale1.set-locale"), {"Wants to change the language for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.locale1.set-keyboard"), {"Wants to change the keyboard layout for everyone on this computer."}},

        {QStringLiteral("org.freedesktop.accounts.user-administration"), {"Wants to change the accounts on this computer."}},
        {QStringLiteral("org.freedesktop.accounts.change-own-user-data"), {"Wants to change your account's details."}},
        {QStringLiteral("org.freedesktop.accounts.change-own-password"), {"Wants to change your password."}},
        {QStringLiteral("org.freedesktop.accounts.set-login-option"), {"Wants to change how people sign in."}},
        {QStringLiteral("org.freedesktop.systemd1.manage-units"), {"Wants to start, stop or restart a system service."}},
        {QStringLiteral("org.freedesktop.systemd1.manage-unit-files"), {"Wants to change which system services start with the computer."}},
        {QStringLiteral("org.freedesktop.systemd1.reload-daemon"), {"Wants to reload the system's service settings."}},
        {QStringLiteral("org.freedesktop.systemd1.set-environment"), {"Wants to change settings every system service sees."}},

        {QStringLiteral("org.freedesktop.Flatpak.app-install"), {"Wants to install an app for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.Flatpak.runtime-install"), {"Wants to install a part apps need, for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.Flatpak.app-uninstall"), {"Wants to remove an app for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.Flatpak.runtime-uninstall"), {"Wants to remove a part apps need."}},
        {QStringLiteral("org.freedesktop.Flatpak.app-update"), {"Wants to update apps for everyone on this computer."}},
        {QStringLiteral("org.freedesktop.Flatpak.runtime-update"), {"Wants to update the parts apps need."}},
        {QStringLiteral("org.freedesktop.Flatpak.modify-repo"), {"Wants to change where apps come from."}},
        {QStringLiteral("org.freedesktop.Flatpak.configure-remote"), {"Wants to change where apps come from."}},
        {QStringLiteral("org.freedesktop.Flatpak.configure"), {"Wants to change how apps are installed."}},
        {QStringLiteral("org.freedesktop.Flatpak.metadata-update"), {"Wants to refresh the list of apps."}},
        {QStringLiteral("org.freedesktop.Flatpak.override-parental-controls"), {"Wants to install an app that parental controls block."}},

        {QStringLiteral("org.freedesktop.fwupd.update-internal"), {"Wants to update your computer's firmware."}},
        {QStringLiteral("org.freedesktop.fwupd.update-internal-trusted"), {"Wants to update your computer's firmware."}},
        {QStringLiteral("org.freedesktop.fwupd.update-hotplug"), {"Wants to update the firmware of a connected device."}},
        {QStringLiteral("org.freedesktop.fwupd.update-hotplug-trusted"), {"Wants to update the firmware of a connected device."}},
        {QStringLiteral("org.freedesktop.fwupd.downgrade-internal"), {"Wants to install older firmware on your computer."}},
        {QStringLiteral("org.freedesktop.fwupd.downgrade-hotplug"), {"Wants to install older firmware on a connected device."}},
        {QStringLiteral("org.freedesktop.fwupd.modify-config"), {"Wants to change how firmware updates work."}},
        {QStringLiteral("org.freedesktop.fwupd.device-unlock"), {"Wants to unlock a device for a firmware update."}},

        {QStringLiteral("org.kde.ksysguard.processlisthelper.sendsignal"), {"Wants to stop a process that belongs to the system or another person."}},
        {QStringLiteral("org.kde.ksysguard.processlisthelper.renice"), {"Wants to change the priority of a system process."}},
        {QStringLiteral("org.kde.powerdevil.chargethresholdhelper.setchargethreshold"), {"Wants to change how full the battery charges."}},
        {QStringLiteral("org.kde.powerdevil.backlighthelper.setbrightness"), {"Wants to change the screen brightness."}},
        {QStringLiteral("org.kde.powerdevil.discretegpuhelper.hasdualgpu"), {"Wants to check which graphics card is in use."}},
        {QStringLiteral("org.kde.kcontrol.kcmsddm.save"), {"Wants to change the sign-in screen."}},
        {QStringLiteral("org.kde.fontinst.manage"), {"Wants to install fonts for everyone on this computer."}},
        {QStringLiteral("org.kde.kpmcore.externalcommand.init"), {"Wants to change how the drives are divided up."}},
        {QStringLiteral("org.opensuse.cupspkhelper.mechanism.all-edit"), {"Wants to change printer settings."}},
        {QStringLiteral("org.opensuse.cupspkhelper.mechanism.printer-local-edit"), {"Wants to set up a printer."}},
        {QStringLiteral("org.opensuse.cupspkhelper.mechanism.printer-remote-edit"), {"Wants to set up a network printer."}},
        {QStringLiteral("org.opensuse.cupspkhelper.mechanism.server-settings"), {"Wants to change how printing works on this computer."}},
        {QStringLiteral("org.opensuse.cupspkhelper.mechanism.job-not-owned-edit"), {"Wants to change another person's print job."}},
        {QStringLiteral("org.opensuse.cupspkhelper.mechanism.devices-get"), {"Wants to look for printers."}},

        {QStringLiteral("org.wildos.appbridge.enable-32bit"), {"Wants to install Windows app support.", "Windows apps", "wildos-windows-app"}},
        {QStringLiteral("org.wildos.usb.allow-device"), {"Wants to allow a USB device Wild OS blocked, until you unplug it."}},
        {QStringLiteral("org.wildos.camera-mic.block"), {"Wants to turn off the camera or microphone for every app.", "Camera and microphone", "camera-web"}},
        {QStringLiteral("org.wildos.camera-mic.unblock"), {"Wants to turn the camera or microphone back on for every app.", "Camera and microphone", "camera-web"}},
        {QStringLiteral("org.wildos.camera-mic.release"), {"Wants to stop an app using the camera.", "Camera and microphone", "camera-web"}},
        {QStringLiteral("org.wildos.update-checks"), {"Wants to change how this computer checks for updates.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.wildos.settings.update-policy"), {"Wants to change how Wild OS installs updates.", "Updates", "wildos-updates"}},
        {QStringLiteral("org.wildos.settings.users"), {"Wants to change who can use this computer."}},
        {QStringLiteral("org.wildos.settings.locale"), {"Wants to set up a new language on this computer."}},
        {QStringLiteral("org.wildos.setup.create-account"), {"Wants to create the first account on this computer."}},
        {QStringLiteral("org.wildos.setup.finish"), {"Wants to finish setting up this computer."}},
    };
    return map;
}

// Wild OS's own helpers run through pkexec under their own actions; this
// covers the same programs when something runs them with plain pkexec.
QString programPurpose(const QString &program)
{
    static const QHash<QString, QString> helpers = {
        {QStringLiteral("/usr/libexec/wildos/settings-users"), QStringLiteral("Wants to change who can use this computer.")},
        {QStringLiteral("/usr/libexec/wildos/settings-locale"), QStringLiteral("Wants to set up a new language on this computer.")},
        {QStringLiteral("/usr/libexec/wildos/settings-update-policy"), QStringLiteral("Wants to change how Wild OS installs updates.")},
        {QStringLiteral("/usr/libexec/wildos/usb-allow"), QStringLiteral("Wants to allow a USB device Wild OS blocked, until you unplug it.")},
        {QStringLiteral("/usr/libexec/wildos/camera-mic-block"), QStringLiteral("Wants to turn the camera or microphone on or off for every app.")},
        {QStringLiteral("/usr/libexec/wildos/update-checks"), QStringLiteral("Wants to change how this computer checks for updates.")},
        {QStringLiteral("/usr/libexec/wildos/appbridge-enable-32bit"), QStringLiteral("Wants to install Windows app support.")},
    };
    if (helpers.contains(program)) {
        return helpers.value(program);
    }
    if (program.isEmpty()) {
        return QStringLiteral("Wants to run a program as administrator.");
    }
    return QStringLiteral("Wants to run %1 as administrator, with full access to this computer.").arg(QFileInfo(program).fileName());
}

QString fromPolkitMessage(const QString &message)
{
    QString text = message.simplified();
    static const QRegularExpression lead(QStringLiteral("^(Authentication is (required|needed) to |Privileges are required to )"),
                                         QRegularExpression::CaseInsensitiveOption);
    const auto match = lead.match(text);
    if (match.hasMatch() && text.size() > match.capturedLength()) {
        text = QStringLiteral("Wants to ") + text.mid(match.capturedLength());
    }
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('.')) && !text.endsWith(QLatin1Char('?'))) {
        text += QLatin1Char('.');
    }
    return text;
}
}

QString Verifier::purposeOf(const QString &actionId, const QString &program)
{
    if (actionId == QLatin1String("org.freedesktop.policykit.exec")) {
        return programPurpose(program);
    }
    const auto &map = wordings();
    auto it = map.constFind(actionId);
    if (it != map.constEnd()) {
        return QString::fromUtf8(it->purpose);
    }
    // login1 adds suffixes for the harder cases of the same request.
    static const QList<std::pair<QLatin1String, const char *>> suffixes = {
        {QLatin1String("-multiple-sessions"), " while other people are signed in."},
        {QLatin1String("-ignore-inhibit"), " even though an app asked to hold it off."},
    };
    for (const auto &[suffix, tail] : suffixes) {
        if (actionId.endsWith(suffix)) {
            const QString base = purposeOf(actionId.chopped(suffix.size()), program);
            if (!base.isEmpty()) {
                return base.chopped(1) + QString::fromUtf8(tail);
            }
        }
    }
    return {};
}

QString Verifier::quickName(const QString &actionId, const QHash<QString, QString> &details, QString *iconName)
{
    const auto &map = wordings();
    const auto wording = map.constFind(actionId);
    if (wording != map.constEnd() && wording->app) {
        *iconName = QString::fromUtf8(wording->icon);
        return QString::fromUtf8(wording->app);
    }
    bool ok = false;
    const qint64 pid = details.value(QStringLiteral("polkit.subject-pid")).toLongLong(&ok);
    if (!ok || pid <= 0) {
        return {};
    }
    bool flatpak = false;
    const QString appId = appIdOf(pid, &flatpak);
    const KService::Ptr service = appId.isEmpty() ? KService::Ptr() : KService::serviceByDesktopName(appId);
    if (!service || service->name().isEmpty()) {
        return {};
    }
    *iconName = service->icon();
    return service->name();
}

Verdict Verifier::check(const QString &actionId, const QString &message, const QHash<QString, QString> &details)
{
    Verdict verdict;
    const Policy policy = findPolicy(actionId);
    verdict.vendor = policy.vendor;

    const bool exec = actionId == QLatin1String("org.freedesktop.policykit.exec");
    verdict.program = exec ? details.value(QStringLiteral("program")) : policy.execPath;
    verdict.commandLine = details.value(QStringLiteral("command_line"));
    // polkit no longer passes pkexec's own details on to agents, but the
    // caller is pkexec itself and its command line says what it will run.
    if (exec && verdict.program.isEmpty()) {
        const qint64 callerPid = details.value(QStringLiteral("polkit.caller-pid")).toLongLong();
        if (callerPid > 0) {
            programOfPkexec(callerPid, &verdict.program, &verdict.commandLine);
        }
    }

    bool subjectOk = false;
    const qint64 pid = details.value(QStringLiteral("polkit.subject-pid")).toLongLong(&subjectOk);
    bool flatpak = false;
    QString appId;
    QString script;
    if (subjectOk && pid > 0) {
        verdict.caller = exeOf(pid);
        script = scriptOf(pid, verdict.caller);
        appId = appIdOf(pid, &flatpak);
    }

    QStringList reasons;
    const FileCheck policyCheck = checkFile(policy.file, QStringLiteral("The request"));
    if (policy.file.isEmpty()) {
        reasons << QStringLiteral("Wild OS doesn't know this request.");
    } else if (!policyCheck.ok) {
        reasons << policyCheck.why;
    }
    const FileCheck programCheck = checkFile(verdict.program, QStringLiteral("The program"));
    if (!programCheck.ok) {
        reasons << programCheck.why;
    }
    FileCheck callerCheck;
    if (flatpak) {
        reasons << QStringLiteral("The app was installed from an outside app source.");
    } else {
        callerCheck = checkFile(script.isEmpty() ? verdict.caller : script, script.isEmpty() ? QStringLiteral("The app") : QStringLiteral("The script"));
        if (!callerCheck.ok) {
            reasons << callerCheck.why;
        }
    }
    // A package installed from a file (the Store's install page, or any
    // PackageKit client's InstallFiles). The request and the app asking
    // check out, both being PackageKit's and Wild OS's own, but what gets
    // installed is the file, which nothing has vouched for: whatever name
    // or maker it gives is only what its builder typed (see sourceOf). So
    // it is never verified, and the Package line does not name PackageKit
    // as if it were the thing coming from Debian.
    const bool fromFile = actionId == QLatin1String("org.freedesktop.packagekit.package-install-untrusted")
        || details.value(QStringLiteral("role")) == QLatin1String("install-files");
    if (fromFile) {
        reasons.prepend(QStringLiteral("It installs a package from a file, from outside Wild OS, and nothing has checked it."));
    }
    verdict.verified = reasons.isEmpty();
    verdict.why = reasons.isEmpty() ? QString() : reasons.constFirst();
    if (!fromFile) {
        verdict.package = !programCheck.package.isEmpty() ? programCheck.package : policyCheck.package;
        verdict.origin = !programCheck.origin.isEmpty() ? programCheck.origin : policyCheck.origin;
    }

    // The name and icon people know the asker by.
    const auto &map = wordings();
    const auto wording = map.constFind(actionId);
    if (wording != map.constEnd() && wording->app) {
        verdict.appName = QString::fromUtf8(wording->app);
        verdict.iconName = QString::fromUtf8(wording->icon);
    }
    KService::Ptr service;
    if (verdict.appName.isEmpty() && !appId.isEmpty()) {
        service = KService::serviceByDesktopName(appId);
    }
    if (verdict.appName.isEmpty() && !service && !verdict.program.isEmpty()) {
        service = appOfPackage(programCheck.package);
    }
    if (verdict.appName.isEmpty() && !service && !callerCheck.package.isEmpty()) {
        service = appOfPackage(callerCheck.package);
    }
    if (service) {
        verdict.appName = service->name();
        verdict.iconName = service->icon();
    }
    if (verdict.appName.isEmpty()) {
        const QString file = !script.isEmpty() ? script : !verdict.program.isEmpty() ? verdict.program : verdict.caller;
        verdict.appName = file.isEmpty() ? QStringLiteral("An app") : QFileInfo(file).fileName();
    }

    verdict.purpose = purposeOf(actionId, verdict.program);
    if (verdict.purpose.isEmpty()) {
        verdict.purpose = fromPolkitMessage(message);
    }
    return verdict;
}
