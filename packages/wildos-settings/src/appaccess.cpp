#include "appaccess.h"

#include <KDesktopFile>

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <glib.h>

namespace
{
const QString portalService = QStringLiteral("org.freedesktop.impl.portal.PermissionStore");
const QString portalPath = QStringLiteral("/org/freedesktop/impl/portal/PermissionStore");
const char *geoclue = "org.freedesktop.GeoClue2";

QString userData()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QStringList installs()
{
    return {QStringLiteral("/var/lib/flatpak"), userData() + QStringLiteral("/flatpak")};
}

// The next three are the Store's own (wildos-store storeapp.cpp), so a
// switch here and a switch there read and write an override the same way.
QStringList keyList(GKeyFile *file, const char *key)
{
    QStringList out;
    gsize length = 0;
    gchar **values = g_key_file_get_string_list(file, "Context", key, &length, nullptr);
    for (gsize i = 0; values && i < length; ++i) {
        const QString v = QString::fromUtf8(values[i]).trimmed();
        if (!v.isEmpty()) {
            out << v;
        }
    }
    g_strfreev(values);
    return out;
}

// "xdg-pictures:ro" and "!xdg-pictures" both concern xdg-pictures.
QString baseOf(QString entry)
{
    if (entry.startsWith(QLatin1Char('!'))) {
        entry.remove(0, 1);
    }
    const int colon = entry.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        entry.truncate(colon);
    }
    if (entry.endsWith(QLatin1Char('/'))) {
        entry.chop(1);
    }
    return entry;
}

// 1 granted, 0 denied, -1 not mentioned.
int stateIn(const QStringList &entries, const QString &base)
{
    int result = -1;
    for (const QString &e : entries) {
        if (baseOf(e) == base) {
            result = e.startsWith(QLatin1Char('!')) ? 0 : 1;
        }
    }
    return result;
}

struct Folder {
    const char *base;
    const char *label;
};

// The Store's labels for the same folders, in its order.
const QList<Folder> &folders()
{
    static const QList<Folder> list = {
        {"host", QT_TRANSLATE_NOOP("AppAccess", "Every file on this computer")},
        {"home", QT_TRANSLATE_NOOP("AppAccess", "Your home folder")},
        {"xdg-documents", QT_TRANSLATE_NOOP("AppAccess", "Your Documents folder")},
        {"xdg-pictures", QT_TRANSLATE_NOOP("AppAccess", "Your Pictures folder")},
        {"xdg-music", QT_TRANSLATE_NOOP("AppAccess", "Your Music folder")},
        {"xdg-videos", QT_TRANSLATE_NOOP("AppAccess", "Your Videos folder")},
        {"xdg-download", QT_TRANSLATE_NOOP("AppAccess", "Your Downloads folder")},
        {"xdg-desktop", QT_TRANSLATE_NOOP("AppAccess", "Your Desktop")},
    };
    return list;
}

GKeyFile *load(const QByteArray &data)
{
    GKeyFile *file = g_key_file_new();
    g_key_file_load_from_data(file, data.constData(), data.size(), G_KEY_FILE_NONE, nullptr);
    return file;
}

GKeyFile *loadFile(const QString &path, GKeyFileFlags flags = G_KEY_FILE_NONE)
{
    GKeyFile *file = g_key_file_new();
    g_key_file_load_from_file(file, QFile::encodeName(path).constData(), flags, nullptr);
    return file;
}

QString busPolicy(GKeyFile *file)
{
    gchar *value = g_key_file_get_string(file, "System Bus Policy", geoclue, nullptr);
    const QString out = QString::fromUtf8(value);
    g_free(value);
    return out;
}

QStringList groupKeys(GKeyFile *file, const char *group)
{
    QStringList out;
    gsize length = 0;
    gchar **keys = g_key_file_get_keys(file, group, &length, nullptr);
    for (gsize i = 0; keys && i < length; ++i) {
        out << QString::fromUtf8(keys[i]);
    }
    g_strfreev(keys);
    return out;
}

bool hasEntry(GKeyFile *file, const char *group, const QString &key)
{
    return g_key_file_has_key(file, group, key.toUtf8().constData(), nullptr);
}

QString entry(GKeyFile *file, const char *group, const QString &key)
{
    gchar *value = g_key_file_get_string(file, group, key.toUtf8().constData(), nullptr);
    const QString out = QString::fromUtf8(value);
    g_free(value);
    return out;
}

void setEntry(GKeyFile *file, const char *group, const QString &key, const QString &value)
{
    g_key_file_set_string(file, group, key.toUtf8().constData(), value.toUtf8().constData());
}

void removeEntry(GKeyFile *file, const char *group, const QString &key)
{
    g_key_file_remove_key(file, group, key.toUtf8().constData(), nullptr);
}

void setContextList(GKeyFile *file, const char *key, const QStringList &entries)
{
    if (entries.isEmpty()) {
        g_key_file_remove_key(file, "Context", key, nullptr);
        return;
    }
    QList<QByteArray> raw;
    for (const QString &e : entries) {
        raw << e.toUtf8();
    }
    QList<const char *> ptrs;
    for (const QByteArray &b : std::as_const(raw)) {
        ptrs << b.constData();
    }
    g_key_file_set_string_list(file, "Context", key, ptrs.constData(), gsize(ptrs.size()));
}

// The entry that grants base, with its ":ro" or ":create", if any does.
QString grantingEntry(const QStringList &entries, const QString &base)
{
    QString out;
    for (const QString &e : entries) {
        if (baseOf(e) == base) {
            out = e.startsWith(QLatin1Char('!')) ? QString() : e;
        }
    }
    return out;
}

struct Choice {
    const char *value;
    const char *label;
};

struct ContextKey {
    const char *key;
    const char *title;
    QList<Choice> choices;
};

// Every value Flatpak knows for each list (flatpak-context.c), so an app can
// be given one it did not ask for as well as lose one it did.
const QList<ContextKey> &contextKeys()
{
    static const QList<ContextKey> list = {
        {"shared", QT_TRANSLATE_NOOP("AppAccess", "Network"),
         {{"network", QT_TRANSLATE_NOOP("AppAccess", "Internet and your local network")}, {"ipc", QT_TRANSLATE_NOOP("AppAccess", "Shared memory with apps outside its sandbox")}}},
        {"devices", QT_TRANSLATE_NOOP("AppAccess", "Devices"),
         {{"dri", QT_TRANSLATE_NOOP("AppAccess", "Graphics acceleration")},
          {"input", QT_TRANSLATE_NOOP("AppAccess", "Game controllers and other input devices")},
          {"usb", QT_TRANSLATE_NOOP("AppAccess", "USB devices")},
          {"kvm", QT_TRANSLATE_NOOP("AppAccess", "Running virtual machines")},
          {"shm", QT_TRANSLATE_NOOP("AppAccess", "Shared memory between apps")},
          {"all", QT_TRANSLATE_NOOP("AppAccess", "Every device, including cameras")}}},
        {"features", QT_TRANSLATE_NOOP("AppAccess", "Other features"),
         {{"bluetooth", "Bluetooth"},
          {"canbus", QT_TRANSLATE_NOOP("AppAccess", "Vehicle and machine networks (CAN bus)")},
          {"devel", QT_TRANSLATE_NOOP("AppAccess", "Debugging other programs")},
          {"multiarch", QT_TRANSLATE_NOOP("AppAccess", "Running 32-bit programs")},
          {"per-app-dev-shm", QT_TRANSLATE_NOOP("AppAccess", "Its own shared memory folder")}}},
        {"sockets", QT_TRANSLATE_NOOP("AppAccess", "Display, sound and system"),
         {{"wayland", QT_TRANSLATE_NOOP("AppAccess", "Showing windows")},
          {"x11", QT_TRANSLATE_NOOP("AppAccess", "Showing windows the older way (X11)")},
          {"fallback-x11", QT_TRANSLATE_NOOP("AppAccess", "The older way when the newer one is missing")},
          {"inherit-wayland-socket", QT_TRANSLATE_NOOP("AppAccess", "Showing windows through the app that started it")},
          {"pulseaudio", QT_TRANSLATE_NOOP("AppAccess", "Sound and your microphone")},
          {"cups", QT_TRANSLATE_NOOP("AppAccess", "Printing")},
          {"pcsc", QT_TRANSLATE_NOOP("AppAccess", "Smart cards")},
          {"ssh-auth", QT_TRANSLATE_NOOP("AppAccess", "Your SSH keys")},
          {"gpg-agent", QT_TRANSLATE_NOOP("AppAccess", "Your GPG keys")},
          {"session-bus", QT_TRANSLATE_NOOP("AppAccess", "Every service in your session")},
          {"system-bus", QT_TRANSLATE_NOOP("AppAccess", "Every service on this computer")}}},
    };
    return list;
}

QString folderLabel(const QString &base)
{
    for (const Folder &f : folders()) {
        if (base == QLatin1String(f.base)) {
            return QCoreApplication::translate("AppAccess", f.label);
        }
    }
    static const QList<std::pair<const char *, const char *>> more = {
        {"host-os", QT_TRANSLATE_NOOP("AppAccess", "This computer's programs and libraries")},
        {"host-etc", QT_TRANSLATE_NOOP("AppAccess", "This computer's system settings")},
        {"xdg-config", QT_TRANSLATE_NOOP("AppAccess", "Settings of every app")},
        {"xdg-cache", QT_TRANSLATE_NOOP("AppAccess", "Cached files of every app")},
        {"xdg-data", QT_TRANSLATE_NOOP("AppAccess", "Data of every app")},
        {"xdg-templates", QT_TRANSLATE_NOOP("AppAccess", "Your Templates folder")},
        {"xdg-public-share", QT_TRANSLATE_NOOP("AppAccess", "Your Public folder")},
    };
    for (const auto &[key, label] : more) {
        if (base == QLatin1String(key)) {
            return QCoreApplication::translate("AppAccess", label);
        }
    }
    return base;
}

QString folderDetail(const QString &grant, bool allowed)
{
    if (!allowed) {
        return QCoreApplication::translate("AppAccess", "No access");
    }
    if (grant.endsWith(QLatin1String(":ro"))) {
        return QCoreApplication::translate("AppAccess", "Can open files, not change them");
    }
    if (grant.endsWith(QLatin1String(":create"))) {
        return QCoreApplication::translate("AppAccess", "Can open and change files, and create the folder");
    }
    return QCoreApplication::translate("AppAccess", "Can open and change files");
}

bool policyAllows(const QString &policy)
{
    return !policy.isEmpty() && policy != QLatin1String("none");
}

QString policyDetail(const QString &policy)
{
    if (policy == QLatin1String("own")) {
        return QCoreApplication::translate("AppAccess", "Can talk to it and take its name");
    }
    if (policy == QLatin1String("talk")) {
        return QCoreApplication::translate("AppAccess", "Can talk to it");
    }
    if (policy == QLatin1String("see")) {
        return QCoreApplication::translate("AppAccess", "Can see it is there");
    }
    return QCoreApplication::translate("AppAccess", "No access");
}

QVariantMap item(const QString &token, const QString &label, const QString &detail, bool allowed, const QString &grant, bool changed)
{
    return {{QStringLiteral("token"), token},
            {QStringLiteral("label"), label},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("allowed"), allowed},
            {QStringLiteral("grant"), grant},
            {QStringLiteral("changed"), changed}};
}

const char *busGroup(const QString &token)
{
    return token == QLatin1String("system-bus") ? "System Bus Policy" : "Session Bus Policy";
}
}

AppAccess::AppAccess(QObject *parent)
    : QObject(parent)
{
    reload();
}

QString AppAccess::overridePath(const QString &id) const
{
    return userData() + QStringLiteral("/flatpak/overrides/") + id;
}

void AppAccess::reload()
{
    m_apps.clear();
    QSet<QString> seen;
    for (const QString &root : installs()) {
        const auto ids = QDir(root + QStringLiteral("/app")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &id : ids) {
            QFile metadata(root + QStringLiteral("/app/") + id + QStringLiteral("/current/active/metadata"));
            if (seen.contains(id) || !metadata.open(QIODevice::ReadOnly)) {
                continue;
            }
            seen.insert(id);
            App app{id, id.section(QLatin1Char('.'), -1), id, metadata.readAll()};
            const QString desktop = root + QStringLiteral("/exports/share/applications/") + id + QStringLiteral(".desktop");
            if (QFileInfo::exists(desktop)) {
                const KDesktopFile file(desktop);
                if (!file.readName().isEmpty()) {
                    app.name = file.readName();
                }
                if (!file.readIcon().isEmpty()) {
                    app.icon = file.readIcon();
                }
            } else {
                // Runtimes and extensions have no launcher; only apps count.
                continue;
            }
            m_apps << app;
        }
    }
    std::sort(m_apps.begin(), m_apps.end(), [](const App &a, const App &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    m_camera = portal(QStringLiteral("devices"), QStringLiteral("camera"));
    m_location = portal(QStringLiteral("location"), QStringLiteral("location"));

    QVariantMap summary;
    for (const QString &kind : {QStringLiteral("camera"), QStringLiteral("microphone"), QStringLiteral("location"), QStringLiteral("files"),
                                QStringLiteral("network")}) {
        const QVariantList rows = apps(kind);
        int allowed = 0;
        for (const QVariant &row : rows) {
            allowed += row.toMap().value(QStringLiteral("allowed")).toBool() ? 1 : 0;
        }
        summary.insert(kind, allowed);
        summary.insert(kind + QStringLiteral("Asked"), rows.size());
    }
    m_summary = summary;
    Q_EMIT changed();
}

int AppAccess::state(const App &app, const QString &group, const QString &base) const
{
    const QByteArray key = group.toUtf8();
    GKeyFile *over = loadFile(overridePath(app.id));
    const int user = stateIn(keyList(over, key.constData()), base);
    g_key_file_free(over);
    if (user >= 0) {
        return user;
    }
    GKeyFile *meta = load(app.metadata);
    const int def = stateIn(keyList(meta, key.constData()), base);
    g_key_file_free(meta);
    return def;
}

QVariantMap AppAccess::portal(const QString &table, const QString &id) const
{
    auto message = QDBusMessage::createMethodCall(portalService, portalPath, portalService, QStringLiteral("Lookup"));
    message << table << id;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
    QVariantMap out;
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return out;
    }
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg.beginMap();
    while (!arg.atEnd()) {
        QString app;
        QStringList value;
        arg.beginMapEntry();
        arg >> app >> value;
        arg.endMapEntry();
        out.insert(app, value);
    }
    arg.endMap();
    return out;
}

void AppAccess::setPortal(const QString &table, const QString &id, const QString &app, const QStringList &value)
{
    auto message = QDBusMessage::createMethodCall(portalService, portalPath, portalService, QStringLiteral("SetPermission"));
    message << table << true << id << app << value;
    QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
}

QVariantList AppAccess::apps(const QString &kind) const
{
    QVariantList rows;
    for (const App &app : m_apps) {
        QVariantMap row = {{QStringLiteral("id"), app.id}, {QStringLiteral("name"), app.name}, {QStringLiteral("icon"), app.icon}};
        GKeyFile *meta = load(app.metadata);

        if (kind == QLatin1String("files")) {
            GKeyFile *over = loadFile(overridePath(app.id));
            const QStringList metaList = keyList(meta, "filesystems");
            const QStringList overList = keyList(over, "filesystems");
            g_key_file_free(over);
            QVariantList items;
            bool homeShown = false;
            bool any = false;
            for (const Folder &f : folders()) {
                const QString base = QString::fromLatin1(f.base);
                const int def = stateIn(metaList, base);
                const int user = stateIn(overList, base);
                if (def != 1 && user != 1) {
                    continue;
                }
                // Folders inside home add nothing when all of home is there.
                if (homeShown && base.startsWith(QLatin1String("xdg-"))) {
                    continue;
                }
                homeShown = homeShown || base == QLatin1String("home") || base == QLatin1String("host");
                const bool allowed = user >= 0 ? user == 1 : def == 1;
                any = any || allowed;
                items << QVariantMap{{QStringLiteral("token"), QStringLiteral("filesystems:") + base},
                                     {QStringLiteral("label"), QCoreApplication::translate("AppAccess", f.label)},
                                     {QStringLiteral("allowed"), allowed}};
            }
            if (!items.isEmpty()) {
                row.insert(QStringLiteral("items"), items);
                row.insert(QStringLiteral("allowed"), any);
                rows << row;
            }
        } else if (kind == QLatin1String("camera")) {
            const bool devices = stateIn(keyList(meta, "devices"), QStringLiteral("all")) == 1 || state(app, QStringLiteral("devices"), QStringLiteral("all")) == 1;
            const bool asked = m_camera.contains(app.id);
            if (devices || asked) {
                const bool sandbox = state(app, QStringLiteral("devices"), QStringLiteral("all")) == 1;
                const bool answer = m_camera.value(app.id).toStringList().value(0) == QLatin1String("yes");
                row.insert(QStringLiteral("allowed"), devices ? sandbox && (!asked || answer) : answer);
                row.insert(QStringLiteral("token"), QStringLiteral("camera"));
                rows << row;
            }
        } else if (kind == QLatin1String("location")) {
            const bool asked = m_location.contains(app.id);
            const bool bus = busPolicy(meta) == QLatin1String("talk");
            if (asked || bus) {
                bool allowed = !asked || m_location.value(app.id).toStringList().value(0) != QLatin1String("NONE");
                if (bus) {
                    GKeyFile *over = loadFile(overridePath(app.id));
                    const QString user = busPolicy(over);
                    g_key_file_free(over);
                    allowed = allowed && (user.isEmpty() || user == QLatin1String("talk"));
                }
                row.insert(QStringLiteral("allowed"), allowed);
                row.insert(QStringLiteral("token"), QStringLiteral("location"));
                rows << row;
            }
        } else {
            const QString group = kind == QLatin1String("microphone") ? QStringLiteral("sockets") : QStringLiteral("shared");
            const QString base = kind == QLatin1String("microphone") ? QStringLiteral("pulseaudio") : QStringLiteral("network");
            const int effective = state(app, group, base);
            // Every app is listed for the internet, as the Store does.
            if (kind == QLatin1String("network") || effective == 1 || stateIn(keyList(meta, group.toUtf8().constData()), base) == 1) {
                row.insert(QStringLiteral("allowed"), effective == 1);
                row.insert(QStringLiteral("token"), group + QLatin1Char(':') + base);
                rows << row;
            }
        }
        g_key_file_free(meta);
    }
    return rows;
}

auto AppAccess::find(const QString &id) const -> const App *
{
    auto it = std::find_if(m_apps.cbegin(), m_apps.cend(), [&id](const App &a) {
        return a.id == id;
    });
    return it == m_apps.cend() ? nullptr : &*it;
}

void AppAccess::set(const QString &id, const QString &token, bool allowed, const QString &grant)
{
    const App *found = find(id);
    if (!found) {
        return;
    }
    const App app = *found;
    const QString path = overridePath(id);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray file = QFile::encodeName(path);
    GKeyFile *meta = load(app.metadata);
    GKeyFile *over = loadFile(path, GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS));

    auto writeContext = [&](const QString &group, const QString &base) {
        const QByteArray groupName = group.toUtf8();
        const QStringList defaults = keyList(meta, groupName.constData());
        const bool byDefault = stateIn(defaults, base) == 1;
        QStringList entries = keyList(over, groupName.constData());
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&base](const QString &e) {
                          return baseOf(e) == base;
                      }),
                      entries.end());
        // Only differences from what the app asked for are written, the
        // same way "flatpak override" and the Store do it.
        const QString wanted = grant.isEmpty() ? base : grant;
        if (allowed && !byDefault) {
            entries << wanted;
        } else if (allowed && !grant.isEmpty() && wanted != grantingEntry(defaults, base)) {
            // The same folder with other access than the app asked for.
            entries << wanted;
        } else if (!allowed && byDefault) {
            entries << QStringLiteral("!") + base;
        } else if (!allowed && group == QLatin1String("filesystems") && !grant.isEmpty()) {
            // A folder the app never asked for stays listed, turned off,
            // instead of vanishing from the page the moment it is switched.
            entries << QStringLiteral("!") + base;
        }
        setContextList(over, groupName.constData(), entries);
    };

    auto writeBus = [&](const char *group, const QString &name) {
        const QString byDefault = entry(meta, group, name);
        const bool defaultAllows = policyAllows(byDefault);
        if (allowed) {
            const QString wanted = !grant.isEmpty() ? grant : defaultAllows ? byDefault : QStringLiteral("talk");
            if (defaultAllows && wanted == byDefault) {
                removeEntry(over, group, name);
            } else {
                setEntry(over, group, name, wanted);
            }
        } else {
            // "none" is what "flatpak override --no-talk-name" writes, and
            // keeps a name the app never asked for listed as off.
            setEntry(over, group, name, QStringLiteral("none"));
        }
    };

    auto writeEnv = [&](const QString &name) {
        QStringList unset = keyList(over, "unset-environment");
        unset.removeAll(name);
        const bool byDefault = hasEntry(meta, "Environment", name);
        if (allowed) {
            if (byDefault && grant == entry(meta, "Environment", name)) {
                removeEntry(over, "Environment", name);
            } else {
                setEntry(over, "Environment", name, grant);
            }
        } else {
            // Both forms, as Flatpak writes them: older versions read an
            // empty value as unset, newer ones read unset-environment.
            setEntry(over, "Environment", name, QString());
            unset << name;
        }
        setContextList(over, "unset-environment", unset);
    };

    if (token == QLatin1String("camera")) {
        const bool devices = stateIn(keyList(meta, "devices"), QStringLiteral("all")) == 1 || stateIn(keyList(over, "devices"), QStringLiteral("all")) == 1;
        if (devices) {
            writeContext(QStringLiteral("devices"), QStringLiteral("all"));
        }
        if (m_camera.contains(id) || !devices) {
            setPortal(QStringLiteral("devices"), QStringLiteral("camera"), id, {allowed ? QStringLiteral("yes") : QStringLiteral("no")});
        }
    } else if (token == QLatin1String("location")) {
        if (m_location.contains(id)) {
            QStringList value = m_location.value(id).toStringList();
            if (value.isEmpty()) {
                value = {QString(), QStringLiteral("0")};
            }
            value[0] = allowed ? QStringLiteral("EXACT") : QStringLiteral("NONE");
            setPortal(QStringLiteral("location"), QStringLiteral("location"), id, value);
        }
        if (busPolicy(meta) == QLatin1String("talk")) {
            if (allowed) {
                g_key_file_remove_key(over, "System Bus Policy", geoclue, nullptr);
            } else {
                g_key_file_set_string(over, "System Bus Policy", geoclue, "none");
            }
        }
    } else {
        const QString group = token.section(QLatin1Char(':'), 0, 0);
        const QString base = token.section(QLatin1Char(':'), 1);
        const bool contextKey = group == QLatin1String("filesystems")
            || std::any_of(contextKeys().cbegin(), contextKeys().cend(), [&group](const ContextKey &k) {
                   return group == QLatin1String(k.key);
               });
        if (!base.isEmpty()) {
            if (group == QLatin1String("session-bus") || group == QLatin1String("system-bus")) {
                writeBus(busGroup(group), base);
            } else if (group == QLatin1String("env")) {
                writeEnv(base);
            } else if (contextKey) {
                writeContext(group, base);
            }
        }
    }

    g_key_file_save_to_file(over, file.constData(), nullptr);
    g_key_file_free(over);
    g_key_file_free(meta);
    reload();
}

QVariantList AppAccess::everyApp() const
{
    QVariantList rows;
    for (const App &app : m_apps) {
        rows << QVariantMap{{QStringLiteral("id"), app.id},
                            {QStringLiteral("name"), app.name},
                            {QStringLiteral("icon"), app.icon},
                            {QStringLiteral("changed"), QFileInfo::exists(overridePath(app.id))}};
    }
    return rows;
}

QVariantList AppAccess::details(const QString &id) const
{
    const App *app = find(id);
    if (!app) {
        return {};
    }
    GKeyFile *meta = load(app->metadata);
    GKeyFile *over = loadFile(overridePath(id));
    QVariantList sections;
    auto section = [&sections](const QString &key, const QString &title, const QVariantList &items) {
        sections << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("title"), title}, {QStringLiteral("items"), items}};
    };

    {
        const QStringList defaults = keyList(meta, "filesystems");
        const QStringList user = keyList(over, "filesystems");
        QStringList bases;
        for (const QString &e : defaults + user) {
            const QString base = baseOf(e);
            // host-reset is Flatpak's marker for "forget the app's own
            // folders", not a folder.
            if (!base.isEmpty() && base != QLatin1String("host-reset") && !bases.contains(base)) {
                bases << base;
            }
        }
        QVariantList items;
        for (const QString &base : std::as_const(bases)) {
            const int def = stateIn(defaults, base);
            const int mine = stateIn(user, base);
            const bool allowed = mine >= 0 ? mine == 1 : def == 1;
            QString grant = grantingEntry(user, base);
            if (grant.isEmpty()) {
                grant = grantingEntry(defaults, base);
            }
            if (grant.isEmpty()) {
                grant = base;
            }
            const bool changed = mine >= 0 && (allowed != (def == 1) || grantingEntry(user, base) != grantingEntry(defaults, base));
            items << item(QStringLiteral("filesystems:") + base, folderLabel(base), folderDetail(grant, allowed), allowed, grant, changed);
        }
        section(QStringLiteral("filesystems"), tr("Files and folders"), items);
    }

    for (const ContextKey &key : contextKeys()) {
        const QStringList defaults = keyList(meta, key.key);
        const QStringList user = keyList(over, key.key);
        QVariantList items;
        for (const Choice &choice : key.choices) {
            const QString value = QString::fromLatin1(choice.value);
            const int def = stateIn(defaults, value);
            const int mine = stateIn(user, value);
            const bool allowed = mine >= 0 ? mine == 1 : def == 1;
            items << item(QString::fromLatin1(key.key) + QLatin1Char(':') + value, QCoreApplication::translate("AppAccess", choice.label), QString(), allowed, value,
                          mine >= 0 && allowed != (def == 1));
        }
        section(QString::fromLatin1(key.key), QCoreApplication::translate("AppAccess", key.title), items);
    }

    for (const QString &bus : {QStringLiteral("session-bus"), QStringLiteral("system-bus")}) {
        const char *group = busGroup(bus);
        QStringList names = groupKeys(meta, group);
        for (const QString &name : groupKeys(over, group)) {
            if (!names.contains(name)) {
                names << name;
            }
        }
        QVariantList items;
        for (const QString &name : std::as_const(names)) {
            const QString def = entry(meta, group, name);
            const QString mine = entry(over, group, name);
            const QString effective = mine.isEmpty() ? def : mine;
            const QString grant = policyAllows(mine) ? mine : policyAllows(def) ? def : QStringLiteral("talk");
            items << item(bus + QLatin1Char(':') + name, name, policyDetail(effective), policyAllows(effective), grant, !mine.isEmpty() && mine != def);
        }
        section(bus, bus == QLatin1String("system-bus") ? tr("System services") : tr("Services in your session"), items);
    }

    {
        const QStringList unset = keyList(over, "unset-environment");
        QStringList names = groupKeys(meta, "Environment");
        for (const QString &name : groupKeys(over, "Environment") + unset) {
            if (!names.contains(name)) {
                names << name;
            }
        }
        QVariantList items;
        for (const QString &name : std::as_const(names)) {
            const bool byDefault = hasEntry(meta, "Environment", name);
            const bool cleared = unset.contains(name);
            const bool mine = hasEntry(over, "Environment", name) && !cleared;
            const bool allowed = !cleared && (mine || byDefault);
            const QString value = mine ? entry(over, "Environment", name) : entry(meta, "Environment", name);
            const QString detail = !allowed ? tr("Not set") : value.isEmpty() ? tr("Set, but empty") : value;
            items << item(QStringLiteral("env:") + name, name, detail, allowed, value,
                          cleared || (mine && (!byDefault || value != entry(meta, "Environment", name))));
        }
        section(QStringLiteral("env"), tr("Environment variables"), items);
    }

    g_key_file_free(over);
    g_key_file_free(meta);
    return sections;
}

QString AppAccess::add(const QString &id, const QString &kind, const QString &value, const QString &extra)
{
    if (!find(id)) {
        return tr("That app is no longer installed.");
    }
    QString v = value.trimmed();
    if (kind == QLatin1String("folder")) {
        static const QStringList special = {QStringLiteral("home"), QStringLiteral("host"), QStringLiteral("host-os"), QStringLiteral("host-etc")};
        if (v == QLatin1String("/")) {
            v = QStringLiteral("host");
        }
        while (v.length() > 1 && v.endsWith(QLatin1Char('/'))) {
            v.chop(1);
        }
        // A colon would be read as the access mode, and Flatpak refuses
        // anything that is not a path or one of its own names.
        if (v.contains(QLatin1Char(':')) || v.contains(QLatin1Char('\n')) || v.startsWith(QLatin1Char('!'))
            || !(v.startsWith(QLatin1Char('/')) || v.startsWith(QLatin1String("~/")) || v.startsWith(QLatin1String("xdg-")) || special.contains(v))) {
            return tr("Type a folder such as ~/Projects or /media/backup.");
        }
        set(id, QStringLiteral("filesystems:") + v, true, extra == QLatin1String("ro") ? v + QStringLiteral(":ro") : v);
        return {};
    }
    if (kind == QLatin1String("bus")) {
        // flatpak_verify_dbus_name: two or more elements, with ".*" allowed
        // at the end for every name below one.
        static const QRegularExpression name(QStringLiteral("^[A-Za-z_-][A-Za-z0-9_-]*(\\.[A-Za-z_-][A-Za-z0-9_-]*)+(\\.\\*)?$"));
        if (v.length() > 255 || !name.match(v).hasMatch()) {
            return tr("Type a service name such as org.freedesktop.Notifications.");
        }
        const QString policy = QStringList{QStringLiteral("see"), QStringLiteral("talk"), QStringLiteral("own")}.contains(extra) ? extra : QStringLiteral("talk");
        set(id, QStringLiteral("session-bus:") + v, true, policy);
        return {};
    }
    if (kind == QLatin1String("env")) {
        static const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (!name.match(v).hasMatch()) {
            return tr("A variable's name is letters, digits and underscores, such as GTK_THEME.");
        }
        if (extra.contains(QLatin1Char('\n'))) {
            return tr("The value has to fit on one line.");
        }
        set(id, QStringLiteral("env:") + v, true, extra);
        return {};
    }
    return tr("That can't be added.");
}

void AppAccess::reset(const QString &id)
{
    if (find(id)) {
        QFile::remove(overridePath(id));
        reload();
    }
}
