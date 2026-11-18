#include "localpackage.h"

#include "catalog.h"
#include "flatpakbackend.h"
#include "packagekitbackend.h"
#include "storeapp.h"

#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <KIO/ApplicationLauncherJob>
#include <KService>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QXmlStreamReader>

#include <flatpak.h>

using PackageKit::Daemon;
using PackageKit::Transaction;

struct LocalInfo {
    QString kind;
    bool readable = false;
    bool blocked = false;
    QString problem;
    QString name;
    QString id;
    QString summary;
    QString description;
    QString version;
    QString installedVersion;
    bool installed = false;
    // The file's version against the installed one: 1 newer, 0 the same,
    // -1 older.
    int compare = 0;
    qint64 fileSize = -1;
    qint64 installedSize = -1;
    QString homepage;
    QString maker;
    QString icon;
    QString origin;
    QString flathubId;
    QString desktopId;
    QStringList depends;
};

namespace
{
QString unreadableText()
{
    return QCoreApplication::translate("LocalPackage", "This file can't be read. It may be damaged or only partly downloaded.");
}

QString elsewhereText()
{
    return QCoreApplication::translate("LocalPackage",
                                       "It was probably made for Ubuntu or another system rather than Debian, which Wild OS is built on. "
                                       "Look for the app in the Store, or for a Flatpak or Debian version where you got this file.");
}

QString run(const QString &program, const QStringList &arguments, int *exitCode = nullptr)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.start(program, arguments);
    const bool done = process.waitForFinished(60000);
    if (!done) {
        process.kill();
        process.waitForFinished(2000);
    }
    if (exitCode) {
        *exitCode = done && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QString kindOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("deb")) {
        return QStringLiteral("deb");
    }
    if (suffix == QLatin1String("flatpakref")) {
        return QStringLiteral("flatpakref");
    }
    if (suffix == QLatin1String("flatpak")) {
        return QStringLiteral("flatpak");
    }
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    if (mime.inherits(QStringLiteral("application/vnd.debian.binary-package"))) {
        return QStringLiteral("deb");
    }
    if (mime.inherits(QStringLiteral("application/vnd.flatpak.ref"))) {
        return QStringLiteral("flatpakref");
    }
    if (mime.inherits(QStringLiteral("application/vnd.flatpak"))) {
        return QStringLiteral("flatpak");
    }
    return {};
}

QString archLabel(const QString &arch)
{
    if (arch == QLatin1String("amd64") || arch == QLatin1String("x86_64")) {
        return QCoreApplication::translate("LocalPackage", "64-bit Intel and AMD computers");
    }
    if (arch == QLatin1String("i386") || arch == QLatin1String("i686")) {
        return QCoreApplication::translate("LocalPackage", "older 32-bit computers");
    }
    if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")) {
        return QCoreApplication::translate("LocalPackage", "64-bit ARM computers");
    }
    if (arch.startsWith(QLatin1String("arm"))) {
        return QCoreApplication::translate("LocalPackage", "32-bit ARM computers");
    }
    return QCoreApplication::translate("LocalPackage", "another kind of computer");
}

QString archProblem(const QString &arch, const QString &native)
{
    return QCoreApplication::translate("LocalPackage", "This package is made for %1 (%2), so it can't run on this computer. Look for the %3 version where you got it.")
        .arg(archLabel(arch), arch, native);
}

QString listText(QStringList items)
{
    if (items.size() > 4) {
        const int more = int(items.size()) - 3;
        items = items.mid(0, 3);
        items << QCoreApplication::translate("LocalPackage", "%1 more").arg(more);
    }
    if (items.size() < 2) {
        return items.value(0);
    }
    return QCoreApplication::translate("LocalPackage", "%1 and %2").arg(QStringList(items.mid(0, items.size() - 1)).join(QStringLiteral(", ")), items.constLast());
}

// PackageKit passes apt's own explanation on: "foo : Depends: libbar (>=
// 2) but it is not installable".
QString dependsProblem(const QString &details)
{
    QStringList missing;
    static const QRegularExpression depends(QStringLiteral("(?:Pre-)?Depends: ([A-Za-z0-9.+-]+)"));
    auto it = depends.globalMatch(details);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (!missing.contains(name)) {
            missing << name;
        }
    }
    if (missing.isEmpty()) {
        return QCoreApplication::translate("LocalPackage", "It needs parts Wild OS doesn't have. %1").arg(elsewhereText());
    }
    return QCoreApplication::translate("LocalPackage", "It needs %1, which Wild OS doesn't have. %2").arg(listText(missing), elsewhereText());
}

QString debError(Transaction::Error error, const QString &details)
{
    switch (error) {
    case Transaction::ErrorDepResolutionFailed:
        return dependsProblem(details);
    case Transaction::ErrorIncompatibleArchitecture:
        return QCoreApplication::translate("LocalPackage", "This package is made for another kind of computer, so it can't run on this one.");
    case Transaction::ErrorInvalidPackageFile:
    case Transaction::ErrorPackageCorrupt:
        return unreadableText();
    case Transaction::ErrorFileConflicts:
        return QCoreApplication::translate("LocalPackage", "It would overwrite files that belong to another part of Wild OS, so it wasn't installed.");
    case Transaction::ErrorPackageFailedToConfigure:
    case Transaction::ErrorPackageFailedToInstall:
        return QCoreApplication::translate("LocalPackage", "The package's own setup failed, so it may be only partly installed. Its maker may have a newer version.");
    default:
        return PackageKitBackend::errorText(error, false);
    }
}

// A Debian control file: "Field: value", with continuation lines that start
// with a space.
QHash<QString, QString> controlFields(const QString &text)
{
    QHash<QString, QString> fields;
    QString last;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if ((line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) && !last.isEmpty()) {
            fields[last] += QLatin1Char('\n') + line.mid(1);
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            last = line.left(colon).trimmed();
            fields.insert(last, line.mid(colon + 1).trimmed());
        }
    }
    return fields;
}

// The long description's own markup: a lone "." is a blank line, a line
// with more indent is kept as it is, the rest runs on.
QString debDescription(const QString &body)
{
    QStringList paragraphs;
    QString current;
    for (const QString &line : body.split(QLatin1Char('\n'))) {
        if (line.trimmed() == QLatin1String(".")) {
            if (!current.isEmpty()) {
                paragraphs << current;
                current.clear();
            }
        } else if (line.startsWith(QLatin1Char(' '))) {
            current += (current.isEmpty() ? QString() : QStringLiteral("\n")) + line.trimmed();
        } else {
            current += (current.isEmpty() ? QString() : QStringLiteral(" ")) + line.trimmed();
        }
    }
    if (!current.isEmpty()) {
        paragraphs << current;
    }
    return paragraphs.join(QStringLiteral("\n\n"));
}

QStringList dependNames(const QString &field)
{
    QStringList names;
    for (const QString &clause : field.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QStringList alternatives;
        for (QString alt : clause.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
            alt = alt.section(QLatin1Char('('), 0, 0).section(QLatin1Char('['), 0, 0).section(QLatin1Char('<'), 0, 0).trimmed();
            alt = alt.section(QLatin1Char(':'), 0, 0);
            if (!alt.isEmpty()) {
                alternatives << alt;
            }
        }
        if (!alternatives.isEmpty()) {
            names << alternatives.join(QStringLiteral(" or "));
        }
    }
    return names;
}

// The launcher and icon a .deb carries, so the page can call the app what
// Start will. Only regular files leave the archive, each through cat into a
// file named by a hash of its path, so nothing in it can write outside the
// scratch folder by way of a link or an odd name.
void readDebLauncher(const QString &path, const QString &scratch, LocalInfo &info)
{
    const QString out = QDir(scratch).filePath(QStringLiteral("files"));
    QDir().mkpath(out);
    QProcess unpack;
    QProcess tar;
    unpack.setStandardOutputProcess(&tar);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WILDOS_OUT"), out);
    tar.setProcessEnvironment(env);
    unpack.start(QStringLiteral("dpkg-deb"), {QStringLiteral("--fsys-tarfile"), path});
    tar.start(QStringLiteral("tar"), {
        QStringLiteral("-x"),
        QStringLiteral("--wildcards"),
        QStringLiteral("--to-command=n=$(printf %s \"$TAR_FILENAME\" | md5sum | cut -c1-32); printf %s \"$TAR_FILENAME\" > \"$WILDOS_OUT/$n.name\"; cat > \"$WILDOS_OUT/$n\""),
        QStringLiteral("*usr/share/applications/*.desktop"),
        QStringLiteral("*usr/share/icons/hicolor/*/apps/*"),
        QStringLiteral("*usr/share/pixmaps/*"),
    });
    unpack.waitForFinished(120000);
    tar.waitForFinished(120000);

    QHash<QString, QString> files;
    const auto names = QDir(out).entryInfoList({QStringLiteral("*.name")}, QDir::Files);
    for (const QFileInfo &name : names) {
        QFile file(name.filePath());
        if (file.open(QIODevice::ReadOnly)) {
            QString member = QString::fromUtf8(file.readAll());
            if (member.startsWith(QLatin1String("./"))) {
                member = member.mid(1);
            } else if (!member.startsWith(QLatin1Char('/'))) {
                member.prepend(QLatin1Char('/'));
            }
            files.insert(member, name.path() + QLatin1Char('/') + name.completeBaseName());
        }
    }

    QStringList launchers;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("/usr/share/applications/")) && it.key().endsWith(QLatin1String(".desktop"))) {
            launchers << it.key();
        }
    }
    launchers.sort();
    QString iconName;
    for (const QString &launcher : std::as_const(launchers)) {
        GKeyFile *key = g_key_file_new();
        if (g_key_file_load_from_file(key, files.value(launcher).toUtf8().constData(), G_KEY_FILE_NONE, nullptr)) {
            const char *group = "Desktop Entry";
            char *type = g_key_file_get_string(key, group, "Type", nullptr);
            const bool app = qstrcmp(type, "Application") == 0 && !g_key_file_get_boolean(key, group, "NoDisplay", nullptr);
            g_free(type);
            if (app) {
                char *name = g_key_file_get_locale_string(key, group, "Name", nullptr, nullptr);
                char *comment = g_key_file_get_locale_string(key, group, "Comment", nullptr, nullptr);
                char *icon = g_key_file_get_string(key, group, "Icon", nullptr);
                info.name = QString::fromUtf8(name).trimmed();
                if (info.summary.isEmpty()) {
                    info.summary = QString::fromUtf8(comment).trimmed();
                }
                iconName = QString::fromUtf8(icon).trimmed();
                info.desktopId = QFileInfo(launcher).fileName();
                g_free(name);
                g_free(comment);
                g_free(icon);
            }
        }
        g_key_file_free(key);
        if (!info.desktopId.isEmpty()) {
            break;
        }
    }

    if (iconName.isEmpty()) {
        return;
    }
    QStringList candidates;
    if (iconName.startsWith(QLatin1Char('/'))) {
        candidates << iconName;
    } else {
        const QString hicolor = QStringLiteral("/usr/share/icons/hicolor/%1/apps/%2.%3");
        candidates << hicolor.arg(QStringLiteral("scalable"), iconName, QStringLiteral("svg"));
        for (const char *size : {"512x512", "256x256", "192x192", "128x128", "96x96", "64x64", "48x48"}) {
            candidates << hicolor.arg(QLatin1String(size), iconName, QStringLiteral("png"));
        }
        candidates << QStringLiteral("/usr/share/pixmaps/%1.png").arg(iconName) << QStringLiteral("/usr/share/pixmaps/%1.svg").arg(iconName);
    }
    for (const QString &candidate : std::as_const(candidates)) {
        const QString source = files.value(candidate);
        if (source.isEmpty()) {
            continue;
        }
        const QString target = QDir(scratch).filePath(QStringLiteral("icon.") + QFileInfo(candidate).suffix());
        if (QFile::copy(source, target)) {
            info.icon = QUrl::fromLocalFile(target).toString();
        }
        return;
    }
}

LocalInfo readDeb(const QString &path, const QString &scratch)
{
    LocalInfo info;
    info.kind = QStringLiteral("deb");
    int code = 0;
    const auto fields = controlFields(run(QStringLiteral("dpkg-deb"), {QStringLiteral("-f"), path}, &code));
    info.id = fields.value(QStringLiteral("Package"));
    if (code != 0 || info.id.isEmpty()) {
        info.problem = unreadableText();
        return info;
    }
    info.readable = true;
    info.version = fields.value(QStringLiteral("Version"));
    const QString description = fields.value(QStringLiteral("Description"));
    info.summary = description.section(QLatin1Char('\n'), 0, 0).trimmed();
    info.description = debDescription(description.section(QLatin1Char('\n'), 1));
    info.homepage = fields.value(QStringLiteral("Homepage"));
    info.maker = fields.value(QStringLiteral("Maintainer")).section(QLatin1Char('<'), 0, 0).trimmed();
    info.depends = dependNames(fields.value(QStringLiteral("Pre-Depends"))) + dependNames(fields.value(QStringLiteral("Depends")));
    bool sized = false;
    const qint64 kib = fields.value(QStringLiteral("Installed-Size")).toLongLong(&sized);
    info.installedSize = sized ? kib * 1024 : -1;

    const QString arch = fields.value(QStringLiteral("Architecture"));
    const QString native = run(QStringLiteral("dpkg"), {QStringLiteral("--print-architecture")}).trimmed();
    const QStringList foreign = run(QStringLiteral("dpkg"), {QStringLiteral("--print-foreign-architectures")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (!arch.isEmpty() && arch != QLatin1String("all") && !native.isEmpty() && arch != native && !foreign.contains(arch)) {
        info.blocked = true;
        info.problem = archProblem(arch, native);
    }

    const QString status = run(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${db:Status-Status}\\n${Version}\\n"), info.id}, &code);
    const QStringList lines = status.split(QLatin1Char('\n'));
    if (code == 0 && lines.value(0) == QLatin1String("installed")) {
        info.installed = true;
        info.installedVersion = lines.value(1);
        const auto test = [&](const char *op) {
            int result = 1;
            run(QStringLiteral("dpkg"), {QStringLiteral("--compare-versions"), info.version, QLatin1String(op), info.installedVersion}, &result);
            return result == 0;
        };
        info.compare = test("gt") ? 1 : test("eq") ? 0 : -1;
    }

    readDebLauncher(path, scratch, info);
    if (info.name.isEmpty()) {
        info.name = info.id;
    }
    return info;
}

// Whether the app is in the user's or the system's Flatpak installation,
// and the version it says it is.
void flatpakInstalled(LocalInfo &info, const QString &branch)
{
    FlatpakInstallation *installations[2] = {flatpak_installation_new_user(nullptr, nullptr), flatpak_installation_new_system(nullptr, nullptr)};
    for (FlatpakInstallation *installation : installations) {
        if (!installation) {
            continue;
        }
        if (!info.installed) {
            FlatpakInstalledRef *ref = flatpak_installation_get_installed_ref(installation, FLATPAK_REF_KIND_APP, info.id.toUtf8().constData(), nullptr,
                                                                              branch.isEmpty() ? nullptr : branch.toUtf8().constData(), nullptr, nullptr);
            if (ref) {
                info.installed = true;
                info.installedVersion = QString::fromUtf8(flatpak_installed_ref_get_appdata_version(ref));
                g_object_unref(ref);
            }
        }
        g_object_unref(installation);
    }
}

QString keyString(GKeyFile *file, const char *key)
{
    char *value = g_key_file_get_string(file, "Flatpak Ref", key, nullptr);
    const QString text = QString::fromUtf8(value).trimmed();
    g_free(value);
    return text;
}

LocalInfo readFlatpakRef(const QString &path)
{
    LocalInfo info;
    info.kind = QStringLiteral("flatpakref");
    GKeyFile *file = g_key_file_new();
    if (!g_key_file_load_from_file(file, path.toUtf8().constData(), G_KEY_FILE_NONE, nullptr) || keyString(file, "Name").isEmpty()) {
        g_key_file_free(file);
        info.problem = unreadableText();
        return info;
    }
    info.readable = true;
    info.id = keyString(file, "Name");
    info.name = keyString(file, "Title");
    if (info.name.isEmpty()) {
        info.name = info.id.section(QLatin1Char('.'), -1);
    }
    info.summary = keyString(file, "Comment");
    info.description = keyString(file, "Description");
    info.homepage = keyString(file, "Homepage");
    const QString branch = keyString(file, "Branch");
    const QUrl url(keyString(file, "Url"));
    info.origin = url.host();
    if (info.origin == QLatin1String("dl.flathub.org") || info.origin == QLatin1String("flathub.org")) {
        info.flathubId = info.id;
    }
    if (g_key_file_get_boolean(file, "Flatpak Ref", "IsRuntime", nullptr)) {
        info.blocked = true;
        info.problem = QCoreApplication::translate("LocalPackage", "This file is for a part apps run on, not an app. Wild OS fetches those by itself when an app needs one.");
    }
    g_key_file_free(file);
    flatpakInstalled(info, branch);
    return info;
}

QByteArray gunzip(GBytes *bytes)
{
    QByteArray out;
    GConverter *converter = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP));
    GInputStream *memory = g_memory_input_stream_new_from_bytes(bytes);
    GInputStream *stream = g_converter_input_stream_new(memory, converter);
    char buffer[16384];
    gssize n = 0;
    while ((n = g_input_stream_read(stream, buffer, sizeof buffer, nullptr, nullptr)) > 0 && out.size() < 32 * 1024 * 1024) {
        out.append(buffer, n);
    }
    g_object_unref(stream);
    g_object_unref(memory);
    g_object_unref(converter);
    return out;
}

// The first component of the AppStream data a bundle carries, in the
// untranslated text.
void readAppstream(const QByteArray &xml, LocalInfo &info)
{
    QXmlStreamReader reader(xml);
    bool inComponent = false;
    bool inDescription = false;
    bool inDeveloper = false;
    QStringList paragraphs;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement()) {
            if (reader.name() == QLatin1String("component")) {
                break;
            }
            if (reader.name() == QLatin1String("description")) {
                inDescription = false;
            } else if (reader.name() == QLatin1String("developer")) {
                inDeveloper = false;
            }
            continue;
        }
        if (!reader.isStartElement()) {
            continue;
        }
        const QString name = reader.name().toString();
        if (name == QLatin1String("component")) {
            inComponent = true;
            continue;
        }
        if (!inComponent) {
            continue;
        }
        if (!reader.attributes().value(QLatin1String("xml:lang")).isEmpty()
            || !reader.attributes().value(QLatin1String("http://www.w3.org/XML/1998/namespace"), QLatin1String("lang")).isEmpty()) {
            reader.skipCurrentElement();
            continue;
        }
        if (name == QLatin1String("description")) {
            inDescription = true;
        } else if (name == QLatin1String("developer")) {
            inDeveloper = true;
        } else if (inDescription && (name == QLatin1String("p") || name == QLatin1String("li"))) {
            const QString text = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            if (!text.isEmpty()) {
                paragraphs << (name == QLatin1String("li") ? QStringLiteral("- ") + text : text);
            }
        } else if (name == QLatin1String("name")) {
            const QString text = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            if (inDeveloper) {
                info.maker = text;
            } else if (info.name.isEmpty()) {
                info.name = text;
            }
        } else if (name == QLatin1String("developer_name") && info.maker.isEmpty()) {
            info.maker = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        } else if (name == QLatin1String("summary") && info.summary.isEmpty()) {
            info.summary = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        } else if (name == QLatin1String("url") && reader.attributes().value(QLatin1String("type")) == QLatin1String("homepage")) {
            info.homepage = reader.readElementText().trimmed();
        } else if (name == QLatin1String("release") && info.version.isEmpty()) {
            info.version = reader.attributes().value(QLatin1String("version")).toString();
        }
    }
    info.description = paragraphs.join(QStringLiteral("\n\n"));
}

LocalInfo readBundle(const QString &path, const QString &scratch)
{
    LocalInfo info;
    info.kind = QStringLiteral("flatpak");
    GFile *file = g_file_new_for_path(path.toUtf8().constData());
    FlatpakBundleRef *bundle = flatpak_bundle_ref_new(file, nullptr);
    g_object_unref(file);
    if (!bundle) {
        info.problem = unreadableText();
        return info;
    }
    info.readable = true;
    FlatpakRef *ref = FLATPAK_REF(bundle);
    info.id = QString::fromUtf8(flatpak_ref_get_name(ref));
    const QString arch = QString::fromUtf8(flatpak_ref_get_arch(ref));
    const QString branch = QString::fromUtf8(flatpak_ref_get_branch(ref));
    info.installedSize = qint64(flatpak_bundle_ref_get_installed_size(bundle));

    char *origin = flatpak_bundle_ref_get_origin(bundle);
    info.origin = QUrl(QString::fromUtf8(origin)).host();
    g_free(origin);

    if (GBytes *appstream = flatpak_bundle_ref_get_appstream(bundle)) {
        readAppstream(gunzip(appstream), info);
        g_bytes_unref(appstream);
    }
    if (info.name.isEmpty()) {
        info.name = info.id.section(QLatin1Char('.'), -1);
    }
    GBytes *icon = flatpak_bundle_ref_get_icon(bundle, 128);
    if (!icon) {
        icon = flatpak_bundle_ref_get_icon(bundle, 64);
    }
    if (icon) {
        QFile out(QDir(scratch).filePath(QStringLiteral("icon.png")));
        gsize size = 0;
        const auto data = static_cast<const char *>(g_bytes_get_data(icon, &size));
        if (out.open(QIODevice::WriteOnly) && out.write(data, qint64(size)) == qint64(size)) {
            info.icon = QUrl::fromLocalFile(out.fileName()).toString();
        }
        g_bytes_unref(icon);
    }

    if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP) {
        info.blocked = true;
        info.problem = QCoreApplication::translate("LocalPackage", "This file is for a part apps run on, not an app. Wild OS fetches those by itself when an app needs one.");
    } else {
        bool supported = false;
        const char *const *arches = flatpak_get_supported_arches();
        for (int i = 0; arches && arches[i]; ++i) {
            supported = supported || arch == QLatin1String(arches[i]);
        }
        if (!supported) {
            info.blocked = true;
            info.problem = archProblem(arch, QString::fromUtf8(flatpak_get_default_arch()));
        }
    }
    g_object_unref(bundle);
    flatpakInstalled(info, branch);
    return info;
}

KService::Ptr launcher(const QString &desktopId)
{
    if (desktopId.isEmpty()) {
        return {};
    }
    KService::Ptr service = KService::serviceByStorageId(desktopId);
    if (service && service->isValid()) {
        return service;
    }
    // Installed a moment ago: KDE's service cache has not caught up yet.
    const QString file = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
    if (!file.isEmpty()) {
        service = KService::Ptr(new KService(file));
        if (service->isValid()) {
            return service;
        }
    }
    return {};
}

int nextJob = -1;
}

LocalPackage::LocalPackage(QObject *parent)
    : QObject(parent)
{
    Catalog *catalog = Catalog::instance();
    if (!catalog) {
        return;
    }
    FlatpakBackend *flatpak = catalog->flatpakBackend();
    connect(flatpak, &FlatpakBackend::progress, this, [this](int job, int percent, const QString &status, const QString &, qint64, qint64) {
        if (job != m_job || m_state != QLatin1String("working")) {
            return;
        }
        m_progress = percent;
        m_phase = status;
        Q_EMIT changed();
    });
    connect(flatpak, &FlatpakBackend::finished, this, [this](int job, bool ok, const QString &error) {
        if (job == m_job && m_state == QLatin1String("working")) {
            finish(ok, error);
        }
    });
}

LocalPackage::~LocalPackage() = default;

QString LocalPackage::fileArgument(const QString &argument)
{
    if (argument.isEmpty()) {
        return {};
    }
    QString local;
    if (argument.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        local = QUrl(argument).toLocalFile();
    } else if (argument.contains(QLatin1String("://")) || argument.startsWith(QLatin1String("appstream:"), Qt::CaseInsensitive)) {
        return {};
    } else {
        local = argument;
    }
    const QFileInfo info(local);
    if (!info.isFile() || kindOf(info.absoluteFilePath()).isEmpty()) {
        return {};
    }
    return info.absoluteFilePath();
}

void LocalPackage::setPath(const QString &path)
{
    const QString local = path.startsWith(QLatin1String("file:")) ? QUrl(path).toLocalFile() : path;
    if (local == m_path) {
        return;
    }
    m_path = local;
    Q_EMIT pathChanged();
    read();
}

void LocalPackage::read()
{
    ++m_generation;
    m_info.reset();
    m_extras.clear();
    m_removes.clear();
    m_checkingDepends = false;
    m_problem.clear();
    m_progress = -1;
    m_phase.clear();
    m_state = QStringLiteral("reading");
    m_scratch = std::make_shared<QTemporaryDir>();
    Q_EMIT changed();

    const QString kind = kindOf(m_path);
    if (kind.isEmpty() || !QFileInfo(m_path).isFile()) {
        LocalInfo info;
        info.problem = QFileInfo::exists(m_path) ? tr("The Store can install .deb, .flatpakref and .flatpak files, and this isn't one of them.")
                                                 : tr("This file isn't there any more. It may have been moved or deleted.");
        adopt(info);
        return;
    }

    QPointer<LocalPackage> self(this);
    const int generation = m_generation;
    const QString path = m_path;
    const QString scratch = m_scratch->isValid() ? m_scratch->path() : QDir::tempPath();
    // Holds the folder until the reading is done, even if the page closes.
    std::shared_ptr<QTemporaryDir> keep = m_scratch;
    QThreadPool::globalInstance()->start([self, generation, path, scratch, kind, keep] {
        LocalInfo info = kind == QLatin1String("deb") ? readDeb(path, scratch)
            : kind == QLatin1String("flatpakref")     ? readFlatpakRef(path)
                                                      : readBundle(path, scratch);
        info.fileSize = QFileInfo(path).size();
        QMetaObject::invokeMethod(qApp, [self, generation, info, keep] {
            if (self && self->m_generation == generation) {
                self->adopt(info);
            }
        }, Qt::QueuedConnection);
    });
}

void LocalPackage::adopt(const LocalInfo &info)
{
    m_info = std::make_unique<LocalInfo>(info);
    m_problem = info.problem;
    if (!info.readable) {
        m_state = QStringLiteral("unreadable");
    } else if (info.blocked) {
        m_state = QStringLiteral("blocked");
    } else if (!info.installed) {
        m_state = QStringLiteral("get");
    } else if (info.kind != QLatin1String("deb")) {
        m_state = QStringLiteral("installed");
    } else {
        m_state = info.compare > 0 ? QStringLiteral("update") : info.compare == 0 ? QStringLiteral("installed") : QStringLiteral("newer");
    }
    Q_EMIT changed();
    if (info.kind == QLatin1String("deb") && (m_state == QLatin1String("get") || m_state == QLatin1String("update"))) {
        checkDepends();
    }
}

// PackageKit works out what the package would bring with it, or that it
// can't be installed at all, without changing anything. A simulation needs
// no password; it is marked non-interactive so it can never ask for one.
void LocalPackage::checkDepends()
{
    Catalog *catalog = Catalog::instance();
    if (!catalog || !catalog->packageKitBackend()->isAvailable()) {
        return;
    }
    m_checkingDepends = true;
    Q_EMIT changed();

    const int generation = m_generation;
    const QString self = m_info->id;
    auto extras = std::make_shared<QStringList>();
    auto removes = std::make_shared<QStringList>();
    auto error = std::make_shared<std::pair<Transaction::Error, QString>>(Transaction::ErrorUnknown, QString());
    auto failed = std::make_shared<bool>(false);
    Transaction *t = Daemon::installFiles({m_path}, Transaction::TransactionFlagSimulate);
    t->setHints(QStringList{QStringLiteral("interactive=false")});
    connect(t, &Transaction::package, this, [extras, removes, self](Transaction::Info info, const QString &id, const QString &) {
        const QString name = Transaction::packageName(id);
        if (name == self) {
            return;
        }
        if (info == Transaction::InfoRemoving || info == Transaction::InfoObsoleting) {
            removes->append(name);
        } else if (info == Transaction::InfoInstalling || info == Transaction::InfoUpdating) {
            extras->append(name);
        }
    });
    connect(t, &Transaction::errorCode, this, [error, failed](Transaction::Error code, const QString &details) {
        *failed = true;
        *error = {code, details};
    });
    connect(t, &Transaction::finished, this, [this, generation, extras, removes, error, failed](Transaction::Exit status, uint) {
        if (generation != m_generation) {
            return;
        }
        m_checkingDepends = false;
        if (status == Transaction::ExitSuccess && !*failed) {
            extras->removeDuplicates();
            removes->removeDuplicates();
            extras->sort();
            removes->sort();
            m_extras = *extras;
            m_removes = *removes;
        } else if (m_state == QLatin1String("get") || m_state == QLatin1String("update")) {
            // Only what the package itself rules out stops it here; a check
            // that could not run (no answer, no network) leaves the install
            // to say what went wrong.
            const auto code = error->first;
            if (code == Transaction::ErrorDepResolutionFailed || code == Transaction::ErrorIncompatibleArchitecture
                || code == Transaction::ErrorInvalidPackageFile || code == Transaction::ErrorPackageCorrupt
                || code == Transaction::ErrorFileConflicts) {
                m_problem = debError(code, error->second);
                m_state = QStringLiteral("blocked");
            }
        }
        Q_EMIT changed();
    });
}

void LocalPackage::install()
{
    if (!m_info || (m_state != QLatin1String("get") && m_state != QLatin1String("update") && m_state != QLatin1String("failed"))) {
        return;
    }
    m_problem.clear();
    m_progress = -1;
    m_phase = QStringLiteral("prepare");
    m_state = QStringLiteral("working");
    Q_EMIT changed();
    if (m_info->kind == QLatin1String("deb")) {
        installDeb();
    } else {
        installFlatpak();
    }
}

void LocalPackage::installDeb()
{
    Catalog *catalog = Catalog::instance();
    if (!catalog || !catalog->packageKitBackend()->isAvailable()) {
        finish(false, tr("The part of Wild OS that installs packages isn't running. Restart the computer and try again."));
        return;
    }
    auto error = std::make_shared<QString>();
    // Not "only trusted": a file is never signed by a repository Wild OS
    // knows, so PackageKit asks under its own action for unsigned packages,
    // which the permission prompt shows as not verified.
    Transaction *t = Daemon::installFiles({m_path}, Transaction::TransactionFlagNone);
    connect(t, &Transaction::percentageChanged, this, [this, t] {
        if (m_state != QLatin1String("working")) {
            return;
        }
        const uint pct = t->percentage();
        switch (t->status()) {
        case Transaction::StatusWaitingForAuth:
            m_phase = QStringLiteral("auth");
            break;
        case Transaction::StatusDownload:
            m_phase = QStringLiteral("download");
            break;
        case Transaction::StatusInstall:
        case Transaction::StatusUpdate:
        case Transaction::StatusCommit:
        case Transaction::StatusRunHook:
            m_phase = QStringLiteral("install");
            break;
        default:
            break;
        }
        m_progress = pct > 100 ? -1 : int(pct);
        Q_EMIT changed();
    });
    connect(t, &Transaction::statusChanged, t, &Transaction::percentageChanged);
    connect(t, &Transaction::errorCode, this, [error](Transaction::Error code, const QString &details) {
        *error = debError(code, details);
    });
    connect(t, &Transaction::finished, this, [this, error](Transaction::Exit status, uint) {
        if (Catalog *catalog = Catalog::instance()) {
            catalog->packageKitBackend()->listInstalled();
        }
        if (status == Transaction::ExitSuccess) {
            finish(true, {});
        } else if (status == Transaction::ExitCancelled) {
            finish(false, QString::fromLatin1(cancelledMarker));
        } else {
            finish(false, error->isEmpty() ? tr("Something went wrong. Try again in a few minutes.") : *error);
        }
    });
}

void LocalPackage::installFlatpak()
{
    Catalog *catalog = Catalog::instance();
    if (!catalog) {
        finish(false, tr("Something went wrong. Try again in a few minutes."));
        return;
    }
    m_job = nextJob--;
    catalog->flatpakBackend()->installFile(m_job, m_path, m_info->kind == QLatin1String("flatpak"));
}

void LocalPackage::finish(bool ok, const QString &error)
{
    m_progress = -1;
    m_phase.clear();
    if (ok) {
        StoreApp::forgetServices();
        m_info->installed = true;
        m_info->installedVersion = m_info->version;
        m_info->compare = 0;
        m_state = QStringLiteral("done");
    } else if (error == QLatin1String(cancelledMarker)) {
        m_state = m_info->installed ? QStringLiteral("update") : QStringLiteral("get");
    } else {
        m_problem = error;
        m_state = QStringLiteral("failed");
    }
    Q_EMIT changed();
}

QString LocalPackage::kind() const
{
    return m_info ? m_info->kind : kindOf(m_path);
}

QString LocalPackage::fileName() const
{
    return QFileInfo(m_path).fileName();
}

#define INFO_FIELD(fn, field)                              \
    QString LocalPackage::fn() const                       \
    {                                                      \
        return m_info ? m_info->field : QString();         \
    }

INFO_FIELD(id, id)
INFO_FIELD(summary, summary)
INFO_FIELD(description, description)
INFO_FIELD(version, version)
INFO_FIELD(installedVersion, installedVersion)
INFO_FIELD(homepage, homepage)
INFO_FIELD(maker, maker)
INFO_FIELD(origin, origin)
INFO_FIELD(flathubId, flathubId)

QString LocalPackage::name() const
{
    if (m_info && !m_info->name.isEmpty()) {
        return m_info->name;
    }
    return QFileInfo(m_path).completeBaseName();
}

QStringList LocalPackage::depends() const
{
    return m_info ? m_info->depends : QStringList();
}

QString LocalPackage::fileSizeText() const
{
    if (!m_info || m_info->fileSize < 0 || kind() == QLatin1String("flatpakref")) {
        return {};
    }
    return QLocale().formattedDataSize(m_info->fileSize, m_info->fileSize >= 1000 * 1000 * 1000 ? 1 : 0, QLocale::DataSizeSIFormat);
}

QString LocalPackage::installedSizeText() const
{
    if (!m_info || m_info->installedSize <= 0) {
        return {};
    }
    return QLocale().formattedDataSize(m_info->installedSize, m_info->installedSize >= 1000 * 1000 * 1000 ? 1 : 0, QLocale::DataSizeSIFormat);
}

QString LocalPackage::iconSource() const
{
    if (m_info && !m_info->icon.isEmpty()) {
        return m_info->icon;
    }
    if (m_state == QLatin1String("done") || m_state == QLatin1String("installed")) {
        const QString id = m_info ? (m_info->kind == QLatin1String("deb") ? m_info->desktopId : m_info->id + QStringLiteral(".desktop")) : QString();
        if (KService::Ptr service = launcher(id); service && !service->icon().isEmpty()) {
            return QStringLiteral("image://theme/") + QString::fromUtf8(QUrl::toPercentEncoding(service->icon()));
        }
    }
    const QString k = kind();
    const QString icon = k == QLatin1String("deb") ? QStringLiteral("application-vnd.debian.binary-package")
        : k == QLatin1String("flatpakref")         ? QStringLiteral("application-vnd.flatpak.ref")
                                                   : QStringLiteral("application-vnd.flatpak");
    return QStringLiteral("image://theme/") + icon;
}

bool LocalPackage::canOpen() const
{
    if (!m_info || (m_state != QLatin1String("done") && m_state != QLatin1String("installed") && m_state != QLatin1String("newer"))) {
        return false;
    }
    if (m_info->kind == QLatin1String("deb")) {
        return launcher(m_info->desktopId).data() != nullptr;
    }
    return true;
}

bool LocalPackage::open()
{
    if (!canOpen()) {
        return false;
    }
    const QString desktopId = m_info->kind == QLatin1String("deb") ? m_info->desktopId : m_info->id + QStringLiteral(".desktop");
    if (KService::Ptr service = launcher(desktopId)) {
        auto job = new KIO::ApplicationLauncherJob(service);
        job->start();
        return true;
    }
    return QProcess::startDetached(QStringLiteral("flatpak"), {QStringLiteral("run"), m_info->id});
}
