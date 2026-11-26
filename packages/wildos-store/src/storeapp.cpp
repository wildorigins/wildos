#include "storeapp.h"

#include "catalog.h"
#include "flatpakbackend.h"

#include <AppStreamQt/developer.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/launchable.h>
#include <AppStreamQt/release-list.h>
#include <AppStreamQt/release.h>
#include <AppStreamQt/screenshot.h>

#include <KIO/ApplicationLauncherJob>
#include <KService>
#include <KSycoca>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#include <glib.h>

namespace
{
// Looking a launcher up checks KDE's service cache against the disk, which
// is far too slow to do for every card each time it is drawn, so each answer
// is kept until StoreApp::forgetServices().
QHash<QString, KService::Ptr> &serviceCache()
{
    static QHash<QString, KService::Ptr> cache;
    return cache;
}

bool serviceCacheChecked = false;

// Apps installed while the Store is open are not in KDE's service cache
// yet; reading the launcher file directly finds them anyway.
KService::Ptr findService(const QString &desktopId)
{
    if (desktopId.isEmpty()) {
        return {};
    }
    auto &cache = serviceCache();
    auto it = cache.constFind(desktopId);
    if (it != cache.cend()) {
        return it.value();
    }
    if (!serviceCacheChecked) {
        KSycoca::self()->ensureCacheValid();
        serviceCacheChecked = true;
    }
    KService::Ptr service = KService::serviceByStorageId(desktopId);
    if (!service) {
        const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
        if (!path.isEmpty()) {
            service = KService::Ptr(new KService(path));
        }
    }
    cache.insert(desktopId, service);
    return service;
}

const AppStream::Component *primary(const Record *r, const QString &source)
{
    if (!r) {
        return nullptr;
    }
    if (source == QLatin1String("flatpak") && r->flat) {
        return &*r->flat;
    }
    if (r->apt) {
        return &*r->apt;
    }
    return r->flat ? &*r->flat : nullptr;
}

QString themeIcon(const QString &name)
{
    return QStringLiteral("image://theme/") + QString::fromUtf8(QUrl::toPercentEncoding(name));
}

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

// The state an entry list gives a permission: 1 granted, 0 denied, -1 silent.
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

struct PermissionDef {
    const char *group;
    const char *key;
    const char *base;
    const char *label;
    bool always;
};

const QList<PermissionDef> &permissionDefs()
{
    static const QList<PermissionDef> defs = {
        {"filesystems", "fs", "host", QT_TRANSLATE_NOOP("StoreApp", "Every file on this computer"), false},
        {"filesystems", "fs", "home", QT_TRANSLATE_NOOP("StoreApp", "Your home folder"), false},
        {"filesystems", "fs", "xdg-documents", QT_TRANSLATE_NOOP("StoreApp", "Your Documents folder"), false},
        {"filesystems", "fs", "xdg-pictures", QT_TRANSLATE_NOOP("StoreApp", "Your Pictures folder"), false},
        {"filesystems", "fs", "xdg-music", QT_TRANSLATE_NOOP("StoreApp", "Your Music folder"), false},
        {"filesystems", "fs", "xdg-videos", QT_TRANSLATE_NOOP("StoreApp", "Your Videos folder"), false},
        {"filesystems", "fs", "xdg-download", QT_TRANSLATE_NOOP("StoreApp", "Your Downloads folder"), false},
        {"filesystems", "fs", "xdg-desktop", QT_TRANSLATE_NOOP("StoreApp", "Your Desktop"), false},
        {"sockets", "sockets", "cups", QT_TRANSLATE_NOOP("StoreApp", "Printer"), false},
        {"shared", "shared", "network", QT_TRANSLATE_NOOP("StoreApp", "Internet"), true},
        {"devices", "devices", "all", QT_TRANSLATE_NOOP("StoreApp", "Camera and other devices"), false},
        {"sockets", "sockets", "pulseaudio", QT_TRANSLATE_NOOP("StoreApp", "Sound and microphone"), false},
    };
    return defs;
}
}

void StoreApp::forgetServices()
{
    serviceCache().clear();
    serviceCacheChecked = false;
}

StoreApp::StoreApp(Catalog *catalog, const QString &key)
    : QObject(catalog)
    , m_catalog(catalog)
    , m_key(key)
{
}

bool StoreApp::isValid() const
{
    return m_catalog->record(m_key) != nullptr;
}

QString StoreApp::appstreamId() const
{
    const Record *r = m_catalog->record(m_key);
    const auto c = primary(r, source());
    return c ? c->id() : m_key;
}

QString StoreApp::serviceName() const
{
    if (!isInstalled()) {
        return {};
    }
    KService::Ptr service = findService(desktopId());
    return service ? service->name() : QString();
}

QString StoreApp::name() const
{
    // Wild OS gives some apps plainer names than their makers do; once
    // installed, the Store calls them what Start does.
    const QString fromStart = serviceName();
    if (!fromStart.isEmpty()) {
        return fromStart;
    }
    const Record *r = m_catalog->record(m_key);
    if (!r) {
        return m_key;
    }
    if (r->curated && !r->curated->name.isEmpty()) {
        return r->curated->name;
    }
    const bool flat = source() == QLatin1String("flatpak");
    const QString &first = flat ? r->flatName : r->aptName;
    const QString &second = flat ? r->aptName : r->flatName;
    return !first.isEmpty() ? first : !second.isEmpty() ? second : m_key;
}

QString StoreApp::summary() const
{
    const Record *r = m_catalog->record(m_key);
    if (!r) {
        return {};
    }
    if (r->curated && !r->curated->summary.isEmpty()) {
        return r->curated->summary;
    }
    const bool flat = source() == QLatin1String("flatpak");
    const QString &first = flat ? r->flatSummary : r->aptSummary;
    return !first.isEmpty() ? first : flat ? r->aptSummary : r->flatSummary;
}

QString StoreApp::tagline() const
{
    const Record *r = m_catalog->record(m_key);
    if (r && r->curated && !r->curated->tagline.isEmpty()) {
        return r->curated->tagline;
    }
    return summary();
}

QString StoreApp::developer() const
{
    const Record *r = m_catalog->record(m_key);
    const auto c = primary(r, source());
    QString name = c ? c->developer().name() : QString();
    if (name.isEmpty() && r && r->flat) {
        name = r->flat->developer().name();
    }
    // Desktop projects credit themselves on their own apps ("KDE", "The
    // GNOME Project"); to someone from Windows that names a stranger, not a
    // maker, so the Store leaves it out and the app reads as open source.
    static const QStringList projects = {QStringLiteral("KDE"), QStringLiteral("The KDE Community"), QStringLiteral("KDE e.V."),
                                         QStringLiteral("GNOME"), QStringLiteral("The GNOME Project"), QStringLiteral("GNOME Foundation")};
    if (projects.contains(name.trimmed(), Qt::CaseInsensitive)) {
        return QString();
    }
    return name;
}

QString StoreApp::byline() const
{
    const QString who = developer();
    if (who.isEmpty()) {
        return tagline();
    }
    return tagline() + QStringLiteral(", by ") + who;
}

QString StoreApp::description() const
{
    const Record *r = m_catalog->record(m_key);
    const auto c = primary(r, source());
    QString text = c ? c->description() : QString();
    if (text.trimmed().isEmpty() && r && r->curated) {
        text = QStringLiteral("<p>") + r->curated->description.toHtmlEscaped() + QStringLiteral("</p>");
    }
    // AppStream markup is a small subset of HTML; Qt's rich text reads it,
    // but needs the list items tightened and inline code kept plain.
    text.replace(QRegularExpression(QStringLiteral("\\s*\\n\\s*")), QStringLiteral(" "));
    text.replace(QStringLiteral("<code>"), QString()).replace(QStringLiteral("</code>"), QString());
    // Flathub packagers often add a note about the package itself ("This
    // flatpak is community maintained..."), which means nothing to people
    // installing an app; drop those paragraphs.
    static const QRegularExpression packagingNote(QStringLiteral("<p>(?:(?!</p>).)*(?:flatpak|flathub)(?:(?!</p>).)*</p>"),
                                                  QRegularExpression::CaseInsensitiveOption);
    QString cleaned = text;
    cleaned.remove(packagingNote);
    if (!cleaned.trimmed().isEmpty()) {
        text = cleaned;
    }
    // Qt gives each paragraph a full line of space above and below; the
    // design runs them closer.
    text.replace(QStringLiteral("<p>"), QStringLiteral("<p style=\"margin-top:0; margin-bottom:10px\">"));
    text.replace(QStringLiteral("<ul>"), QStringLiteral("<ul style=\"margin-top:0; margin-bottom:10px\">"));
    return text.trimmed();
}

QString StoreApp::iconSource() const
{
    const Record *r = m_catalog->record(m_key);
    if (!r) {
        return themeIcon(QStringLiteral("application-x-executable"));
    }

    if (isInstalled()) {
        KService::Ptr service = findService(desktopId());
        if (service && !service->icon().isEmpty()) {
            const QString icon = service->icon();
            if (icon.startsWith(QLatin1Char('/'))) {
                return QUrl::fromLocalFile(icon).toString();
            }
            if (QIcon::hasThemeIcon(icon)) {
                return themeIcon(icon);
            }
        }
    }

    // Worked out when the catalogue was built (see iconFor in catalog.cpp).
    const bool flat = source() == QLatin1String("flatpak");
    const QString &first = flat ? r->flatIcon : r->aptIcon;
    const QString &second = flat ? r->aptIcon : r->flatIcon;
    // Flathub's catalogue arrives a few seconds after the first launch.
    // Until then a curated app may have nothing to draw, and an empty tile
    // reads better than a broken file icon.
    return !first.isEmpty() ? first : second;
}

QVariantList StoreApp::screenshots() const
{
    const Record *r = m_catalog->record(m_key);
    // Flathub keeps every screenshot online and its CDN is quick; the Wild
    // OS catalogue's links can point at media that has since been pruned.
    // So Flathub's pictures are used whenever the app is there too.
    auto c = primary(r, source());
    if (r && r->flat && !r->flat->screenshotsAll().isEmpty()) {
        c = &*r->flat;
    }
    QVariantList out;
    if (!c) {
        return out;
    }
    for (const AppStream::Screenshot &shot : c->screenshotsAll()) {
        if (shot.mediaKind() != AppStream::Screenshot::MediaKindImage) {
            continue;
        }
        const QList<AppStream::Image> images = shot.images();
        if (images.isEmpty()) {
            continue;
        }
        // Every size the catalogue has, so the page can fetch the one
        // closest to what it shows (Catalog::pickImage). The shape comes
        // from whichever size says it.
        QVariantList sizes;
        uint width = 0;
        uint height = 0;
        for (const AppStream::Image &img : images) {
            sizes << QVariantMap{
                {QStringLiteral("url"), img.url().toString()},
                {QStringLiteral("width"), img.width()},
                {QStringLiteral("height"), img.height()},
            };
            if (img.width() > width && img.height() > 0) {
                width = img.width();
                height = img.height();
            }
        }
        out << QVariantMap{
            {QStringLiteral("images"), sizes},
            {QStringLiteral("width"), width},
            {QStringLiteral("height"), height},
            {QStringLiteral("caption"), shot.caption()},
        };
        if (out.size() >= 8) {
            break;
        }
    }
    return out;
}

QString StoreApp::categoryLabel() const
{
    const Record *r = m_catalog->record(m_key);
    if (!r) {
        return {};
    }
    if (r->categories.contains(QStringLiteral("Game"))) {
        return tr("Games");
    }
    if (r->curated && !r->curated->category.isEmpty()) {
        return m_catalog->categoryLabel(r->curated->category);
    }
    static const QList<QPair<QString, QString>> order = {
        {QStringLiteral("Office"), QStringLiteral("office")},
        {QStringLiteral("Graphics"), QStringLiteral("graphics")},
        {QStringLiteral("AudioVideo"), QStringLiteral("media")},
        {QStringLiteral("Network"), QStringLiteral("internet")},
        {QStringLiteral("Development"), QStringLiteral("development")},
        {QStringLiteral("Education"), QStringLiteral("education")},
        {QStringLiteral("Science"), QStringLiteral("education")},
        {QStringLiteral("System"), QStringLiteral("system")},
        {QStringLiteral("Utility"), QStringLiteral("utilities")},
    };
    for (const auto &pair : order) {
        if (r->categories.contains(pair.first)) {
            return m_catalog->categoryLabel(pair.second);
        }
    }
    return m_catalog->categoryLabel(QStringLiteral("utilities"));
}

bool StoreApp::openSource() const
{
    const auto c = primary(m_catalog->record(m_key), source());
    return c && c->isFloss();
}

QString StoreApp::licence() const
{
    const auto c = primary(m_catalog->record(m_key), source());
    return c ? c->projectLicense() : QString();
}

QString StoreApp::homepage() const
{
    const auto c = primary(m_catalog->record(m_key), source());
    return c ? c->url(AppStream::Component::UrlKindHomepage).toString() : QString();
}

#define CURATED_FIELD(fn, field)                                    \
    QString StoreApp::fn() const                                    \
    {                                                               \
        const Record *r = m_catalog->record(m_key);                 \
        return r && r->curated ? r->curated->field : QString();     \
    }

CURATED_FIELD(insteadOf, insteadOf)
CURATED_FIELD(cardNote, cardNote)
CURATED_FIELD(chip, chip)
CURATED_FIELD(swapTitle, swapTitle)
CURATED_FIELD(swapText, swapText)

bool StoreApp::hasFlatpak() const
{
    const Record *r = m_catalog->record(m_key);
    return r && (r->hasFlat || (!r->flatpakId.isEmpty() && m_catalog->flathubAvailable()));
}

bool StoreApp::hasApt() const
{
    const Record *r = m_catalog->record(m_key);
    return r && !r->packages.isEmpty();
}

QString StoreApp::source() const
{
    return m_catalog->source(m_key);
}

bool StoreApp::isInstalled() const
{
    return m_catalog->isInstalled(m_key);
}

bool StoreApp::updateAvailable() const
{
    return m_catalog->hasUpdate(m_key);
}

bool StoreApp::isBusy() const
{
    return m_catalog->state(m_key).busy;
}

bool StoreApp::isQueued() const
{
    return m_catalog->state(m_key).queued;
}

int StoreApp::progress() const
{
    return m_catalog->state(m_key).progress;
}

QString StoreApp::phase() const
{
    return m_catalog->state(m_key).phase;
}

QString StoreApp::action() const
{
    return m_catalog->state(m_key).action;
}

QString StoreApp::error() const
{
    return m_catalog->state(m_key).error;
}

QString StoreApp::version() const
{
    const QString installed = m_catalog->installedVersion(m_key);
    if (!installed.isEmpty()) {
        return installed.section(QLatin1Char(':'), -1).section(QLatin1Char('-'), 0, 0).section(QLatin1Char('+'), 0, 0);
    }
    const Record *r = m_catalog->record(m_key);
    const auto c = primary(r, source());
    if (c) {
        const auto releases = c->releasesPlain().entries();
        if (!releases.isEmpty()) {
            return releases.constFirst().version();
        }
    }
    const QString candidate = m_catalog->state(m_key).candidateVersion;
    return candidate.section(QLatin1Char(':'), -1).section(QLatin1Char('-'), 0, 0).section(QLatin1Char('+'), 0, 0);
}

QString StoreApp::sizeText() const
{
    const QLocale locale;
    if (isInstalled()) {
        const qint64 size = m_catalog->installedSize(m_key);
        return size > 0 ? tr("Uses %1").arg(locale.formattedDataSize(size, 0, QLocale::DataSizeTraditionalFormat)) : QString();
    }
    const AppState &s = m_catalog->state(m_key);
    // PackageKit sizes the Wild OS package; Flathub's figure is everything
    // the install would download, shared parts included.
    const qint64 download = source() == QLatin1String("apt") ? s.downloadSize : s.planTotal;
    if (download > 0) {
        return tr("About %1 to download").arg(locale.formattedDataSize(download, 0, QLocale::DataSizeTraditionalFormat));
    }
    return {};
}

QString StoreApp::progressText() const
{
    const AppState &s = m_catalog->state(m_key);
    if (!s.error.isEmpty() && !s.busy && !s.queued) {
        return s.error;
    }
    if (s.queued) {
        return tr("Waiting for the apps before it");
    }
    if (!s.busy) {
        return {};
    }
    const QLocale locale;
    auto size = [&locale](qint64 bytes) {
        return locale.formattedDataSize(bytes, 0, QLocale::DataSizeTraditionalFormat);
    };
    if (s.phase == QLatin1String("auth")) {
        return tr("Waiting for the administrator password");
    }
    if (s.phase == QLatin1String("wait")) {
        return tr("Waiting for another install to finish");
    }
    if (s.action == QLatin1String("remove")) {
        return tr("Uninstalling");
    }
    const bool downloading = s.phase == QLatin1String("download") && s.totalBytes > 0 && s.doneBytes < s.totalBytes;
    if (downloading) {
        return tr("Downloading %1 of %2").arg(size(qMax<qint64>(0, s.doneBytes)), size(s.totalBytes));
    }
    if (s.phase == QLatin1String("download")) {
        return tr("Downloading");
    }
    if (s.phase == QLatin1String("install") || s.progress >= 99) {
        return s.action == QLatin1String("update") ? tr("Updating") : tr("Installing");
    }
    return tr("Getting ready");
}

QString StoreApp::sharedNote() const
{
    const AppState &s = m_catalog->state(m_key);
    if (isInstalled() || source() != QLatin1String("flatpak")) {
        return {};
    }
    // Named while it downloads, or before installing when Flathub said this
    // app would bring it.
    const QString runtime = !s.detail.isEmpty() ? s.detail : s.planRuntime;
    if (runtime.isEmpty()) {
        return {};
    }
    if (!s.busy && !s.queued && s.planShared > 0) {
        const QLocale locale;
        return tr("Includes %1 (%2), shared with other apps").arg(runtime, locale.formattedDataSize(s.planShared, 0, QLocale::DataSizeTraditionalFormat));
    }
    return tr("Includes %1, shared with other apps").arg(runtime);
}

bool StoreApp::cancellable() const
{
    const AppState &s = m_catalog->state(m_key);
    // Once files are being put in place, stopping would leave the app half
    // installed, so only waiting and downloading can be cancelled.
    if (s.queued) {
        return true;
    }
    return s.busy && s.action != QLatin1String("remove") && s.phase != QLatin1String("install") && s.phase != QLatin1String("auth");
}

void StoreApp::cancel()
{
    m_catalog->cancel(m_key);
}

void StoreApp::pause()
{
    m_catalog->pause(m_key);
}

namespace
{
// What a Flathub app may reach: the permissions in its metadata with the
// person's overrides on top, as flatpak itself combines them.
struct Access {
    QHash<QString, QSet<QString>> granted;
    QSet<QString> requested;
    QSet<QString> busNames;
};

Access accessOf(const QString &metadata, const QString &overridePath)
{
    Access access;
    GKeyFile *meta = g_key_file_new();
    const QByteArray data = metadata.toUtf8();
    g_key_file_load_from_data(meta, data.constData(), data.size(), G_KEY_FILE_NONE, nullptr);
    GKeyFile *over = g_key_file_new();
    g_key_file_load_from_file(over, QFile::encodeName(overridePath).constData(), G_KEY_FILE_NONE, nullptr);
    for (const char *group : {"filesystems", "devices", "sockets", "shared", "features"}) {
        QSet<QString> &set = access.granted[QString::fromLatin1(group)];
        for (const QString &e : keyList(meta, group)) {
            if (!e.startsWith(QLatin1Char('!'))) {
                set.insert(baseOf(e));
                access.requested.insert(QString::fromLatin1(group) + QLatin1Char(':') + baseOf(e));
            }
        }
        for (const QString &e : keyList(over, group)) {
            if (e.startsWith(QLatin1Char('!'))) {
                set.remove(baseOf(e));
            } else {
                set.insert(baseOf(e));
            }
        }
    }
    for (const char *policy : {"Session Bus Policy", "System Bus Policy"}) {
        gchar **keys = g_key_file_get_keys(meta, policy, nullptr, nullptr);
        for (gchar **k = keys; k && *k; ++k) {
            access.busNames.insert(QString::fromUtf8(*k));
        }
        g_strfreev(keys);
    }
    g_key_file_free(meta);
    g_key_file_free(over);
    return access;
}

struct Reach {
    const char *group;
    const char *base;
    const char *icon;
    const char *label;
    const char *detail;
};

// In the order the app page lists them. The details are what each one
// lets an app do in general; nothing here claims to know why an app asks.
const QList<Reach> &reaches()
{
    static const QList<Reach> list = {
        {"filesystems", "host", "drive", QT_TRANSLATE_NOOP("StoreApp", "Every file on this computer"), QT_TRANSLATE_NOOP("StoreApp", "Including other people's files it can open")},
        {"filesystems", "home", "home", QT_TRANSLATE_NOOP("StoreApp", "Your home folder"), QT_TRANSLATE_NOOP("StoreApp", "All your documents, pictures and settings")},
        {"filesystems", "xdg-documents", "folder", QT_TRANSLATE_NOOP("StoreApp", "Documents folder"), QT_TRANSLATE_NOOP("StoreApp", "To open and save documents there")},
        {"filesystems", "xdg-pictures", "image", QT_TRANSLATE_NOOP("StoreApp", "Pictures folder"), QT_TRANSLATE_NOOP("StoreApp", "To open and save pictures there")},
        {"filesystems", "xdg-music", "music", QT_TRANSLATE_NOOP("StoreApp", "Music folder"), QT_TRANSLATE_NOOP("StoreApp", "To open and save music there")},
        {"filesystems", "xdg-videos", "film", QT_TRANSLATE_NOOP("StoreApp", "Videos folder"), QT_TRANSLATE_NOOP("StoreApp", "To open and save videos there")},
        {"filesystems", "xdg-download", "download", QT_TRANSLATE_NOOP("StoreApp", "Downloads folder"), QT_TRANSLATE_NOOP("StoreApp", "To open and save downloads there")},
        {"filesystems", "xdg-desktop", "desktop", QT_TRANSLATE_NOOP("StoreApp", "Desktop"), QT_TRANSLATE_NOOP("StoreApp", "To open and save files there")},
        {"devices", "dri", "gpu", QT_TRANSLATE_NOOP("StoreApp", "Your graphics card"), ""},
        {"devices", "all", "camera", QT_TRANSLATE_NOOP("StoreApp", "Camera and other devices"), QT_TRANSLATE_NOOP("StoreApp", "Webcams, game controllers and USB devices")},
        {"sockets", "pulseaudio", "mic", QT_TRANSLATE_NOOP("StoreApp", "Sound and microphone"), QT_TRANSLATE_NOOP("StoreApp", "To play sound and hear your microphone")},
        {"sockets", "cups", "printer", QT_TRANSLATE_NOOP("StoreApp", "Printer"), QT_TRANSLATE_NOOP("StoreApp", "To print")},
        {"shared", "network", "globe", QT_TRANSLATE_NOOP("StoreApp", "The internet"), QT_TRANSLATE_NOOP("StoreApp", "To go online")},
    };
    return list;
}

QVariantMap reachEntry(const Reach &r)
{
    return {{QStringLiteral("icon"), QString::fromLatin1(r.icon)}, {QStringLiteral("label"), QCoreApplication::translate("StoreApp", r.label)}};
}
}

bool StoreApp::sandboxed() const
{
    return source() == QLatin1String("flatpak");
}

bool StoreApp::isChecked() const
{
    const Record *r = m_catalog->record(m_key);
    if (!r) {
        return false;
    }
    return r->curated || (r->flat && r->flat->customValue(QStringLiteral("flathub::verification::verified")) == QLatin1String("true"));
}

bool StoreApp::reachKnown() const
{
    return sandboxed() && !m_catalog->state(m_key).flatpakMetadata.isEmpty();
}

QVariantList StoreApp::canReach() const
{
    QVariantList out;
    if (!reachKnown()) {
        return out;
    }
    const Access a = accessOf(m_catalog->state(m_key).flatpakMetadata, flatpakOverridePath());
    // The file picker always works: it is how a sandboxed app opens anything.
    out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("file")}, {QStringLiteral("label"), tr("Files you open with it")}};
    const bool wholeHome = a.granted.value(QStringLiteral("filesystems")).contains(QStringLiteral("home"))
        || a.granted.value(QStringLiteral("filesystems")).contains(QStringLiteral("host"));
    for (const Reach &r : reaches()) {
        if (wholeHome && QByteArray(r.base).startsWith("xdg-")) {
            continue;
        }
        if (a.granted.value(QString::fromLatin1(r.group)).contains(QString::fromLatin1(r.base))) {
            out << reachEntry(r);
        }
    }
    if (a.busNames.contains(QStringLiteral("org.freedesktop.GeoClue2"))) {
        out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("location")}, {QStringLiteral("label"), tr("Your location")}};
    }
    return out;
}

QVariantList StoreApp::cantReach() const
{
    QVariantList out;
    if (!reachKnown()) {
        return out;
    }
    const Access a = accessOf(m_catalog->state(m_key).flatpakMetadata, flatpakOverridePath());
    auto has = [&a](const char *group, const char *base) {
        return a.granted.value(QString::fromLatin1(group)).contains(QString::fromLatin1(base));
    };
    if (!has("shared", "network")) {
        out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("globe")}, {QStringLiteral("label"), tr("The internet")}};
    }
    const bool camera = has("devices", "all");
    const bool microphone = has("sockets", "pulseaudio");
    if (!camera && !microphone) {
        out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("camera")}, {QStringLiteral("label"), tr("Camera and microphone")}};
    } else if (!camera) {
        out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("camera")}, {QStringLiteral("label"), tr("Your camera")}};
    }
    if (!a.busNames.contains(QStringLiteral("org.freedesktop.GeoClue2"))) {
        out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("location")}, {QStringLiteral("label"), tr("Your location")}};
    }
    if (!has("filesystems", "home") && !has("filesystems", "host")) {
        out << QVariantMap{{QStringLiteral("icon"), QStringLiteral("home")}, {QStringLiteral("label"), tr("The rest of your files")}};
    }
    return out.mid(0, 3);
}

bool StoreApp::worksOffline() const
{
    if (!reachKnown()) {
        return false;
    }
    const Access a = accessOf(m_catalog->state(m_key).flatpakMetadata, flatpakOverridePath());
    return !a.granted.value(QStringLiteral("shared")).contains(QStringLiteral("network"));
}

QVariantList StoreApp::consentItems() const
{
    QVariantList out;
    if (!reachKnown() || isInstalled()) {
        return out;
    }
    const Access a = accessOf(m_catalog->state(m_key).flatpakMetadata, flatpakOverridePath());
    for (const Reach &r : reaches()) {
        const QString token = QString::fromLatin1(r.group) + QLatin1Char(':') + QString::fromLatin1(r.base);
        // The graphics card is left out: switching it off breaks drawing in
        // nearly every app, so it is not a real choice.
        if (!a.requested.contains(token) || qstrcmp(r.base, "dri") == 0) {
            continue;
        }
        out << QVariantMap{
            {QStringLiteral("token"), token},
            {QStringLiteral("icon"), QString::fromLatin1(r.icon)},
            {QStringLiteral("label"), tr(r.label)},
            {QStringLiteral("detail"), tr(r.detail)},
            {QStringLiteral("allowed"), a.granted.value(QString::fromLatin1(r.group)).contains(QString::fromLatin1(r.base))},
            {QStringLiteral("required"), false},
        };
    }
    return out;
}

void StoreApp::installWith(const QVariantMap &choices)
{
    for (const QVariant &v : consentItems()) {
        const QVariantMap item = v.toMap();
        const QString token = item.value(QStringLiteral("token")).toString();
        if (choices.contains(token)) {
            setPermission(token, choices.value(token).toBool());
        }
    }
    install();
}

qint64 StoreApp::spaceShort() const
{
    const AppState &s = m_catalog->state(m_key);
    if (s.planInstalled <= 0 || isInstalled() || source() != QLatin1String("flatpak")) {
        return 0;
    }
    QString dir = FlatpakBackend::userDataDir();
    while (!dir.isEmpty() && !QFileInfo::exists(dir)) {
        dir = QFileInfo(dir).absolutePath();
        if (dir == QLatin1String("/")) {
            break;
        }
    }
    const qint64 free = QStorageInfo(dir).bytesAvailable();
    return free >= 0 && free < s.planInstalled ? s.planInstalled - free : 0;
}

QString StoreApp::buttonState() const
{
    const AppState &s = m_catalog->state(m_key);
    if (s.queued) {
        return QStringLiteral("waiting");
    }
    if (s.stopping) {
        return QStringLiteral("paused");
    }
    if (s.busy) {
        if (s.action == QLatin1String("remove")) {
            return QStringLiteral("removing");
        }
        if (s.phase == QLatin1String("install") || s.progress >= 99) {
            return QStringLiteral("installing");
        }
        if (s.phase == QLatin1String("download") || s.progress > 0) {
            return QStringLiteral("downloading");
        }
        return QStringLiteral("preparing");
    }
    if (isInstalled()) {
        if (updateAvailable()) {
            return QStringLiteral("update");
        }
        return canOpen() ? QStringLiteral("open") : QStringLiteral("installed");
    }
    if (s.paused) {
        return QStringLiteral("paused");
    }
    if (!s.error.isEmpty()) {
        return QStringLiteral("retry");
    }
    if (spaceShort() > 0) {
        return QStringLiteral("nospace");
    }
    return QStringLiteral("get");
}

QString StoreApp::statusLine() const
{
    const QString state = buttonState();
    if (state == QLatin1String("nospace")) {
        return tr("Needs %1 free").arg(m_catalog->sizeText(m_catalog->state(m_key).planInstalled));
    }
    if (state == QLatin1String("retry")) {
        return tr("Download failed");
    }
    if (state == QLatin1String("paused")) {
        return tr("Paused");
    }
    return {};
}

QString StoreApp::bytesText() const
{
    const AppState &s = m_catalog->state(m_key);
    if (s.totalBytes <= 0 || s.doneBytes < 0) {
        return {};
    }
    const QLocale locale;
    // Both in the larger unit, as "412 of 1,080 MB" reads.
    if (s.totalBytes >= qint64(10) * 1000 * 1000 * 1000) {
        return tr("%1 of %2 GB").arg(locale.toString(double(s.doneBytes) / 1e9, 'f', 1), locale.toString(double(s.totalBytes) / 1e9, 'f', 1));
    }
    return tr("%1 of %2 MB").arg(locale.toString(qRound64(double(s.doneBytes) / 1e6)), locale.toString(qMax<qint64>(1, qRound64(double(s.totalBytes) / 1e6))));
}

QString StoreApp::timeLeftText() const
{
    const qint64 left = m_catalog->state(m_key).secondsLeft;
    if (left < 0) {
        return {};
    }
    if (left < 45) {
        return tr("Less than a minute left");
    }
    if (left < 90) {
        return tr("About a minute left");
    }
    if (left < 90 * 60) {
        return tr("About %1 minutes left").arg((left + 30) / 60);
    }
    return tr("About %1 hours left").arg((left + 1800) / 3600);
}

QString StoreApp::downloadText() const
{
    const AppState &s = m_catalog->state(m_key);
    const qint64 bytes = source() == QLatin1String("apt") ? s.downloadSize : s.planTotal;
    return bytes > 0 ? m_catalog->sizeText(bytes) : QString();
}

QString StoreApp::updateSizeText() const
{
    const qint64 bytes = m_catalog->updateSize(m_key);
    return bytes > 0 ? m_catalog->sizeText(bytes) : QString();
}

QString StoreApp::releaseNote() const
{
    const auto c = primary(m_catalog->record(m_key), source());
    if (!c) {
        return {};
    }
    const auto releases = c->releasesPlain().entries();
    if (releases.isEmpty()) {
        return {};
    }
    QString text = releases.constFirst().description();
    // The first paragraph or list item, without markup.
    text.replace(QRegularExpression(QStringLiteral("</(p|li)>.*"), QRegularExpression::DotMatchesEverythingOption), QString());
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    return text.simplified();
}

QString StoreApp::newVersion() const
{
    const auto c = primary(m_catalog->record(m_key), source());
    if (!c) {
        return {};
    }
    const auto releases = c->releasesPlain().entries();
    return releases.isEmpty() ? QString() : releases.constFirst().version();
}

QString StoreApp::updatedText() const
{
    return m_catalog->dayText(m_catalog->updatedTime(m_key));
}

QString StoreApp::desktopId() const
{
    const Record *r = m_catalog->record(m_key);
    if (!r) {
        return {};
    }
    const QString from = m_catalog->installedSource(m_key);
    if (from == QLatin1String("flatpak") || (from.isEmpty() && source() == QLatin1String("flatpak"))) {
        return r->flatpakId + QStringLiteral(".desktop");
    }
    if (r->curated && !r->curated->desktop.isEmpty()) {
        return r->curated->desktop + QStringLiteral(".desktop");
    }
    if (r->apt) {
        const QStringList entries = r->apt->launchable(AppStream::Launchable::KindDesktopId).entries();
        if (!entries.isEmpty()) {
            return entries.constFirst();
        }
        const QString id = r->apt->id();
        return id.endsWith(QLatin1String(".desktop")) ? id : id + QStringLiteral(".desktop");
    }
    return {};
}

bool StoreApp::canOpen() const
{
    if (!isInstalled()) {
        return false;
    }
    if (m_catalog->installedSource(m_key) == QLatin1String("flatpak")) {
        return true;
    }
    return findService(desktopId()) != nullptr;
}

bool StoreApp::inStart() const
{
    if (!isInstalled()) {
        return false;
    }
    KService::Ptr service = findService(desktopId());
    if (!service) {
        // Flathub launchers are exported to a folder Start reads.
        return m_catalog->installedSource(m_key) == QLatin1String("flatpak");
    }
    return !service->noDisplay();
}

QString StoreApp::flatpakOverridePath() const
{
    const Record *r = m_catalog->record(m_key);
    if (!r || r->flatpakId.isEmpty()) {
        return {};
    }
    return FlatpakBackend::userDataDir() + QStringLiteral("/overrides/") + r->flatpakId;
}

QVariantList StoreApp::permissions() const
{
    QVariantList out;
    if (source() != QLatin1String("flatpak")) {
        return out;
    }
    const QString metadata = m_catalog->state(m_key).flatpakMetadata;
    if (metadata.isEmpty()) {
        return out;
    }
    GKeyFile *meta = g_key_file_new();
    const QByteArray data = metadata.toUtf8();
    g_key_file_load_from_data(meta, data.constData(), data.size(), G_KEY_FILE_NONE, nullptr);
    GKeyFile *over = g_key_file_new();
    g_key_file_load_from_file(over, QFile::encodeName(flatpakOverridePath()).constData(), G_KEY_FILE_NONE, nullptr);

    const bool editable = isInstalled();
    bool homeShown = false;
    for (const PermissionDef &d : permissionDefs()) {
        const QString base = QString::fromLatin1(d.base);
        const int def = stateIn(keyList(meta, d.group), base);
        const int user = stateIn(keyList(over, d.group), base);
        const bool requested = def == 1 || user == 1;
        if (!requested && !d.always) {
            continue;
        }
        // Folders inside home add nothing when the whole home is reachable.
        if (homeShown && qstrcmp(d.key, "fs") == 0 && base.startsWith(QLatin1String("xdg-"))) {
            continue;
        }
        if (qstrcmp(d.key, "fs") == 0 && (base == QLatin1String("home") || base == QLatin1String("host")) && requested) {
            homeShown = true;
        }
        const bool allowed = user >= 0 ? user == 1 : def == 1;
        out << QVariantMap{
            {QStringLiteral("token"), QString::fromLatin1(d.group) + QLatin1Char(':') + base},
            {QStringLiteral("label"), tr(d.label)},
            {QStringLiteral("allowed"), allowed},
            {QStringLiteral("editable"), editable},
            {QStringLiteral("fixed"), false},
        };
        // The file picker is how sandboxed apps open anything else, and it
        // cannot be switched off; it sits after the folders, as designed.
        if (qstrcmp(d.key, "fs") == 0 && base == QLatin1String("xdg-desktop")) {
            out << QVariantMap{
                {QStringLiteral("token"), QString()},
                {QStringLiteral("label"), tr("Files you open with it")},
                {QStringLiteral("allowed"), true},
                {QStringLiteral("editable"), false},
                {QStringLiteral("fixed"), true},
            };
        }
    }
    bool hasPicker = false;
    for (const QVariant &v : std::as_const(out)) {
        hasPicker = hasPicker || v.toMap().value(QStringLiteral("fixed")).toBool();
    }
    if (!hasPicker) {
        int at = 0;
        while (at < out.size() && out.at(at).toMap().value(QStringLiteral("token")).toString().startsWith(QLatin1String("filesystems:"))) {
            ++at;
        }
        out.insert(at, QVariantMap{
                           {QStringLiteral("token"), QString()},
                           {QStringLiteral("label"), tr("Files you open with it")},
                           {QStringLiteral("allowed"), true},
                           {QStringLiteral("editable"), false},
                           {QStringLiteral("fixed"), true},
                       });
    }

    g_key_file_free(meta);
    g_key_file_free(over);
    return out;
}

bool StoreApp::permissionsUnknown() const
{
    return m_catalog->state(m_key).metadataFailed;
}

void StoreApp::setPermission(const QString &token, bool allowed)
{
    const QString group = token.section(QLatin1Char(':'), 0, 0);
    const QString base = token.section(QLatin1Char(':'), 1);
    const QString path = flatpakOverridePath();
    if (group.isEmpty() || base.isEmpty() || path.isEmpty()) {
        return;
    }
    const QByteArray groupName = group.toUtf8();

    GKeyFile *meta = g_key_file_new();
    const QByteArray data = m_catalog->state(m_key).flatpakMetadata.toUtf8();
    g_key_file_load_from_data(meta, data.constData(), data.size(), G_KEY_FILE_NONE, nullptr);
    const bool byDefault = stateIn(keyList(meta, groupName.constData()), base) == 1;
    g_key_file_free(meta);

    GKeyFile *over = g_key_file_new();
    const QByteArray file = QFile::encodeName(path);
    g_key_file_load_from_file(over, file.constData(), GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS), nullptr);
    QStringList entries = keyList(over, groupName.constData());
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&base](const QString &e) {
                      return baseOf(e) == base;
                  }),
                  entries.end());
    // Only differences from what the app asked for are written, the same
    // way "flatpak override" does it.
    if (allowed != byDefault) {
        entries << (allowed ? base : QStringLiteral("!") + base);
    }
    if (entries.isEmpty()) {
        g_key_file_remove_key(over, "Context", groupName.constData(), nullptr);
    } else {
        QList<QByteArray> raw;
        for (const QString &e : std::as_const(entries)) {
            raw << e.toUtf8();
        }
        QList<const char *> ptrs;
        for (const QByteArray &b : std::as_const(raw)) {
            ptrs << b.constData();
        }
        g_key_file_set_string_list(over, "Context", groupName.constData(), ptrs.constData(), gsize(ptrs.size()));
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    g_key_file_save_to_file(over, file.constData(), nullptr);
    g_key_file_free(over);
    Q_EMIT stateChanged();
}

void StoreApp::install()
{
    m_catalog->install(m_key);
}

void StoreApp::remove()
{
    m_catalog->remove(m_key);
}

void StoreApp::update()
{
    m_catalog->update(m_key);
}

bool StoreApp::open()
{
    KService::Ptr service = findService(desktopId());
    if (service) {
        auto job = new KIO::ApplicationLauncherJob(service);
        job->start();
        return true;
    }
    const Record *r = m_catalog->record(m_key);
    if (r && m_catalog->installedSource(m_key) == QLatin1String("flatpak")) {
        return QProcess::startDetached(QStringLiteral("flatpak"), {QStringLiteral("run"), r->flatpakId});
    }
    return false;
}

void StoreApp::useSource(const QString &source)
{
    if (isInstalled()) {
        return;
    }
    m_catalog->state(m_key).chosenSource = source;
    Q_EMIT dataChanged();
    Q_EMIT stateChanged();
    loadDetails();
}

void StoreApp::loadDetails()
{
    m_catalog->fetchDetails(m_key);
}

void StoreApp::clearError()
{
    m_catalog->state(m_key).error.clear();
    Q_EMIT stateChanged();
}
