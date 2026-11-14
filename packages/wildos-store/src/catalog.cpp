#include "catalog.h"

#include "clockformat.h"

#include "flatpakbackend.h"
#include "packagekitbackend.h"
#include "packagesizes.h"
#include "storeapp.h"
#include "timing.h"

#include <AppStreamQt/bundle.h>
#include <AppStreamQt/developer.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/launchable.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/screenshot.h>

#include <KSycoca>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTime>
#include <QTimer>
#include <QUrl>

#include <algorithm>

Catalog *Catalog::s_instance = nullptr;

Q_LOGGING_CATEGORY(lcStore, "wildos.store", QtWarningMsg)

namespace
{
struct CategoryDef {
    const char *id;
    const char *label;
    QStringList freedesktop;
};

const QList<CategoryDef> &appCategoryDefs()
{
    static const QList<CategoryDef> defs = {
        {"office", "Office", {QStringLiteral("Office")}},
        {"internet", "Internet", {QStringLiteral("Network")}},
        {"graphics", "Photos and graphics", {QStringLiteral("Graphics")}},
        {"media", "Music and video", {QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video")}},
        {"education", "Learning", {QStringLiteral("Education"), QStringLiteral("Science")}},
        {"development", "Developer tools", {QStringLiteral("Development")}},
        {"utilities", "Utilities", {QStringLiteral("Utility")}},
        {"system", "System", {QStringLiteral("System"), QStringLiteral("Settings")}},
    };
    return defs;
}

const QList<CategoryDef> &gameCategoryDefs()
{
    static const QList<CategoryDef> defs = {
        {"game-action", "Action", {QStringLiteral("ActionGame"), QStringLiteral("Shooter")}},
        {"game-adventure", "Adventure", {QStringLiteral("AdventureGame"), QStringLiteral("RolePlaying")}},
        {"game-arcade", "Arcade", {QStringLiteral("ArcadeGame")}},
        {"game-puzzle", "Puzzles", {QStringLiteral("LogicGame"), QStringLiteral("BlocksGame")}},
        {"game-board", "Board and cards", {QStringLiteral("BoardGame"), QStringLiteral("CardGame")}},
        {"game-strategy", "Strategy", {QStringLiteral("StrategyGame")}},
        {"game-sports", "Sports and racing", {QStringLiteral("SportsGame")}},
        {"game-emulator", "Emulators", {QStringLiteral("Emulator")}},
    };
    return defs;
}

const QString allApps = QStringLiteral("@apps");
const QString allGames = QStringLiteral("@games");

// Bump when the snapshot's layout changes; an older file is then ignored.
constexpr quint32 snapshotMagic = 0x57534331;
constexpr qint32 snapshotVersion = 2;

QString normalise(const QString &id)
{
    QString key = id.trimmed().toLower();
    if (key.endsWith(QLatin1String(".desktop"))) {
        key.chop(8);
    }
    return key;
}

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    if (value.isString()) {
        out << value.toString();
    }
    for (const QJsonValue &v : value.toArray()) {
        out << v.toString();
    }
    return out;
}

int qualityOf(const AppStream::Component &c)
{
    int score = 0;
    if (!c.screenshotsAll().isEmpty()) {
        score += 3;
    }
    if (c.description().size() > 160) {
        score += 1;
    }
    if (c.customValue(QStringLiteral("flathub::verification::verified")) == QLatin1String("true")) {
        score += 2;
    }
    if (!c.developer().name().isEmpty()) {
        score += 1;
    }
    return score;
}

QString themeIcon(const QString &name)
{
    return QStringLiteral("image://theme/") + QString::fromUtf8(QUrl::toPercentEncoding(name));
}

// The catalogue's own icon for a component: the largest copy on disk, then
// the remote one, then the theme's. Done while building, because looking for
// files is too slow to do for every card as it is drawn.
QString iconFor(const AppStream::Component &c, bool flat, const QString &flatDir, const QSet<QString> &flatIcons, const Curated *curated)
{
    QString best;
    uint bestSize = 0;
    QString remote;
    QString stock;
    const QList<AppStream::Icon> icons = c.icons();
    for (const AppStream::Icon &icon : icons) {
        if (icon.kind() == AppStream::Icon::KindCached || icon.kind() == AppStream::Icon::KindLocal) {
            if (icon.width() <= bestSize && bestSize > 0) {
                continue;
            }
            // Flathub's icons are looked up in a listing made once per build;
            // a file check for each of thousands of icons took most of it.
            if (flat && !flatDir.isEmpty()) {
                const QString cached = QStringLiteral("%1/icons/%2x%3/%4").arg(flatDir).arg(icon.width()).arg(icon.height()).arg(icon.name());
                if (flatIcons.contains(cached)) {
                    best = cached;
                    bestSize = icon.width();
                    continue;
                }
            }
            const QString path = icon.url().isLocalFile() ? icon.url().toLocalFile() : icon.url().toString();
            if (QFileInfo::exists(path)) {
                best = path;
                bestSize = icon.width();
            }
        } else if (icon.kind() == AppStream::Icon::KindRemote && remote.isEmpty()) {
            remote = icon.url().toString();
        } else if (icon.kind() == AppStream::Icon::KindStock && stock.isEmpty()) {
            stock = icon.name();
        }
    }
    // Catalogue icons on disk first: the remote copies of older catalogue
    // entries are not always kept online.
    if (!best.isEmpty()) {
        return QUrl::fromLocalFile(best).toString();
    }
    if (!remote.isEmpty()) {
        return remote;
    }
    if (curated && !curated->icon.isEmpty()) {
        return themeIcon(curated->icon);
    }
    if (!stock.isEmpty()) {
        return themeIcon(stock);
    }
    return themeIcon(QStringLiteral("application-x-executable"));
}

QString displayName(const Record &r)
{
    if (r.curated && !r.curated->name.isEmpty()) {
        return r.curated->name;
    }
    if (!r.flatName.isEmpty()) {
        return r.flatName;
    }
    return r.aptName.isEmpty() ? r.key : r.aptName;
}

void indexForSearch(Record &r)
{
    r.searchName = displayName(r).toLower();
    QStringList words{r.key, r.aptSummary, r.flatSummary, r.aptName, r.flatName, r.flatpakId};
    words += r.packages;
    if (r.curated) {
        words << r.curated->summary << r.curated->tagline << r.curated->insteadOf;
        words += r.curated->aliases;
    }
    for (const auto *c : {r.apt ? &*r.apt : nullptr, r.flat ? &*r.flat : nullptr}) {
        if (c) {
            words += c->categories();
        }
    }
    r.searchText = words.join(QLatin1Char(' ')).toLower();
}

QHash<QString, QStringList> categoriesFor(const QHash<QString, Record> &records, const QStringList &sorted)
{
    QHash<QString, QStringList> out;
    QList<const CategoryDef *> defs;
    for (const CategoryDef &d : appCategoryDefs()) {
        defs << &d;
    }
    for (const CategoryDef &d : gameCategoryDefs()) {
        defs << &d;
    }
    for (const QString &key : sorted) {
        const Record &r = records[key];
        const bool isGame = r.categories.contains(QStringLiteral("Game"));
        out[isGame ? allGames : allApps] << key;
        for (const CategoryDef *d : std::as_const(defs)) {
            const QString id = QString::fromLatin1(d->id);
            if (id.startsWith(QLatin1String("game")) != isGame) {
                continue;
            }
            bool in;
            if (r.curated && !r.curated->category.isEmpty()) {
                in = r.curated->category == id;
            } else {
                in = std::any_of(d->freedesktop.cbegin(), d->freedesktop.cend(), [&r](const QString &c) {
                    return r.categories.contains(c);
                });
            }
            if (in) {
                out[id] << key;
            }
        }
    }
    return out;
}

struct BuildInput {
    std::shared_ptr<AppStream::Pool> os;
    std::shared_ptr<AppStream::Pool> flat;
    const QList<Curated> *curated;
    QSet<QString> hidden;
    QString flatDir;
};

// Joins both catalogues and the curated file. Runs on a pool thread; it only
// reads the pools, which AppStream guards with its own lock.
CatalogData buildCatalog(const BuildInput &in)
{
    CatalogData out;
    QSet<QString> flatIcons;
    if (!in.flatDir.isEmpty()) {
        QDirIterator it(in.flatDir + QStringLiteral("/icons"), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            flatIcons.insert(it.next());
        }
    }
    QHash<QString, Record> &records = out.records;
    QHash<QString, QString> &aliases = out.aliases;

    auto addComponent = [&](const AppStream::Component &c, bool flat) {
        if (c.kind() != AppStream::Component::KindDesktopApp) {
            return;
        }
        const QString key = normalise(c.id());
        if (in.hidden.contains(key)) {
            return;
        }
        Record &r = records[key];
        r.key = key;
        if (flat) {
            const AppStream::Bundle bundle = c.bundle(AppStream::Bundle::KindFlatpak);
            if (bundle.isEmpty()) {
                return;
            }
            r.flat = c;
            r.flatpakRef = bundle.id();
            r.flatpakId = bundle.id().section(QLatin1Char('/'), 1, 1);
        } else {
            // The catalogue and an installed app's own metainfo can both
            // describe it; the catalogue has the package name, so keep it.
            if (r.apt && !r.apt->packageNames().isEmpty() && c.packageNames().isEmpty()) {
                return;
            }
            r.apt = c;
            r.packages = c.packageNames();
        }
        r.quality = std::max(r.quality, qualityOf(c));
        for (const QString &cat : c.categories()) {
            if (!r.categories.contains(cat)) {
                r.categories << cat;
            }
        }
    };

    if (in.os) {
        for (const AppStream::Component &c : in.os->componentsByKind(AppStream::Component::KindDesktopApp).toList()) {
            addComponent(c, false);
        }
    }
    if (in.flat) {
        for (const AppStream::Component &c : in.flat->componentsByKind(AppStream::Component::KindDesktopApp).toList()) {
            addComponent(c, true);
        }
    }

    // Some apps carry different IDs in each catalogue (Thunderbird is
    // net.thunderbird.Thunderbird in one and org.mozilla.Thunderbird in the
    // other); the curated entry joins them under its first ID.
    for (const Curated &c : *in.curated) {
        QString home;
        for (const QString &id : c.ids) {
            if (records.contains(normalise(id))) {
                home = normalise(id);
                break;
            }
        }
        if (home.isEmpty()) {
            home = c.ids.isEmpty() ? c.key : normalise(c.ids.constFirst());
        }
        Record &r = records[home];
        r.key = home;
        r.curated = &c;
        for (const QString &id : c.ids) {
            const QString other = normalise(id);
            if (other == home || !records.contains(other)) {
                continue;
            }
            const Record moved = records.take(other);
            if (!r.apt && moved.apt) {
                r.apt = moved.apt;
                r.packages = moved.packages;
            }
            if (!r.flat && moved.flat) {
                r.flat = moved.flat;
                r.flatpakRef = moved.flatpakRef;
                r.flatpakId = moved.flatpakId;
            }
            r.quality = std::max(r.quality, moved.quality);
            for (const QString &cat : moved.categories) {
                if (!r.categories.contains(cat)) {
                    r.categories << cat;
                }
            }
            aliases.insert(other, home);
        }
        if (!c.packages.isEmpty()) {
            r.packages = c.packages;
        }
        if (!c.flatpak.isEmpty() && r.flatpakId.isEmpty()) {
            // Flathub data is not loaded yet; the ref is still known.
            r.flatpakId = c.flatpak;
            r.flatpakRef = QStringLiteral("app/%1/x86_64/stable").arg(c.flatpak);
        }
        r.quality += 10;
        aliases.insert(c.key, home);
        for (const QString &id : c.ids) {
            aliases.insert(normalise(id), home);
        }
    }

    // Records the curated file named but neither catalogue carries, and that
    // have no package to fall back on, cannot be installed or shown.
    for (auto it = records.begin(); it != records.end();) {
        if (!it->apt && !it->flat && it->packages.isEmpty() && it->flatpakId.isEmpty()) {
            it = records.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = records.begin(); it != records.end(); ++it) {
        Record &r = it.value();
        for (const QString &pkg : std::as_const(r.packages)) {
            if (!aliases.contains(pkg)) {
                aliases.insert(pkg, it.key());
            }
        }
        if (!r.flatpakId.isEmpty()) {
            aliases.insert(r.flatpakId.toLower(), it.key());
        }
        // There are no download counts in either catalogue. An app packaged
        // both by Wild OS and on Flathub is almost always a well known one,
        // so that stands in for popularity.
        if (r.apt && r.flat) {
            r.quality += 4;
        }
        r.hasApt = r.apt.has_value() || !r.packages.isEmpty();
        r.hasFlat = r.flat.has_value();
        if (r.apt) {
            r.aptName = r.apt->name();
            r.aptSummary = r.apt->summary();
            r.aptIcon = iconFor(*r.apt, false, in.flatDir, flatIcons, r.curated);
        }
        if (r.flat) {
            r.flatName = r.flat->name();
            r.flatSummary = r.flat->summary();
            r.flatIcon = iconFor(*r.flat, true, in.flatDir, flatIcons, r.curated);
        }
        if (r.aptIcon.isEmpty() && r.flatIcon.isEmpty() && r.curated && !r.curated->icon.isEmpty()) {
            r.aptIcon = themeIcon(r.curated->icon);
        }
        indexForSearch(r);
    }

    QStringList sorted = records.keys();
    QHash<QString, QString> names;
    names.reserve(records.size());
    for (auto it = records.cbegin(); it != records.cend(); ++it) {
        names.insert(it.key(), displayName(it.value()));
    }
    std::sort(sorted.begin(), sorted.end(), [&records, &names](const QString &a, const QString &b) {
        const int qa = records[a].quality;
        const int qb = records[b].quality;
        if (qa != qb) {
            return qa > qb;
        }
        // The key settles ties, so the same catalogue always sorts the same
        // way and a rebuild that changes nothing leaves the lists alone.
        const int byName = names[a].compare(names[b], Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a < b;
    });
    out.sorted = std::move(sorted);
    out.categories = categoriesFor(records, out.sorted);
    return out;
}

// Every file the Wild OS catalogue is read from, by path, size and time.
QString osInputs()
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    static const QStringList roots = {
        QStringLiteral("/usr/share/swcatalog"), QStringLiteral("/var/lib/swcatalog"), QStringLiteral("/var/cache/swcatalog"),
        QStringLiteral("/usr/share/app-info"),  QStringLiteral("/var/lib/app-info"),  QStringLiteral("/var/cache/app-info"),
        QStringLiteral("/usr/share/metainfo"),  QStringLiteral("/usr/share/appdata"),
    };
    QStringList entries;
    for (const QString &root : roots) {
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            entries << QStringLiteral("%1 %2 %3").arg(info.filePath()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
        }
    }
    entries.sort();
    hash.addData(entries.join(QLatin1Char('\n')).toUtf8());
    hash.addData(QLocale::system().name().toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

QString flatInputs(const QString &dir)
{
    if (dir.isEmpty()) {
        return {};
    }
    QFileInfo info(dir + QStringLiteral("/appstream.xml.gz"));
    if (!info.exists()) {
        info.setFile(dir + QStringLiteral("/appstream.xml"));
    }
    return QStringLiteral("%1 %2 %3 %4").arg(info.canonicalFilePath()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()).arg(QLocale::system().name());
}

QString snapshotPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-store/catalogue.bin");
}

template<typename Work, typename Then>
void runAsync(QObject *context, Work work, Then then)
{
    QThreadPool::globalInstance()->start([context = QPointer<QObject>(context), work, then]() mutable {
        auto result = work();
        if (!context) {
            return;
        }
        QMetaObject::invokeMethod(
            context.data(),
            [context, then, result = std::move(result)]() mutable {
                if (context) {
                    then(std::move(result));
                }
            },
            Qt::QueuedConnection);
    });
}

// Pools are QObjects that live on the window's thread; the last reference
// may go on a pool thread, so they are always deleted from their own.
std::shared_ptr<AppStream::Pool> sharedPool(AppStream::Pool *pool)
{
    return std::shared_ptr<AppStream::Pool>(pool, [](AppStream::Pool *p) {
        p->deleteLater();
    });
}
}

Catalog::Catalog(QObject *parent)
    : QObject(parent)
    , m_packagekit(new PackageKitBackend(this))
    , m_flatpak(new FlatpakBackend(this))
    , m_sizes(new PackageSizes(this))
{
    s_instance = this;
    loadCurated();
    connect(m_sizes, &PackageSizes::changed, this, &Catalog::bumpState);
    connect(m_packagekit, &PackageKitBackend::installedChanged, this, [this] {
        m_sizes->refresh(wildosPackages());
    });

    // An app installed or removed changes which launchers exist.
    auto installsChanged = [this] {
        StoreApp::forgetServices();
        bumpState();
    };
    connect(m_packagekit, &PackageKitBackend::installedChanged, this, installsChanged);
    connect(m_packagekit, &PackageKitBackend::updatesChanged, this, &Catalog::bumpState);
    connect(m_packagekit, &PackageKitBackend::catalogueRefreshed, this, &Catalog::loadOsPool);
    connect(m_packagekit, &PackageKitBackend::refreshFinished, this, &Catalog::checkPartDone);
    connect(m_packagekit, &PackageKitBackend::progress, this, &Catalog::onProgress);
    connect(m_packagekit, &PackageKitBackend::finished, this, &Catalog::onFinished);
    connect(m_packagekit, &PackageKitBackend::detailsReady, this, [this](const QString &key, qint64 download, qint64, const QString &version) {
        AppState &s = m_states[key];
        s.downloadSize = download;
        s.candidateVersion = version;
        stateChangedFor(key);
    });

    connect(m_flatpak, &FlatpakBackend::appstreamChanged, this, &Catalog::loadFlatpakPool);
    connect(m_flatpak, &FlatpakBackend::installedChanged, this, installsChanged);
    connect(m_flatpak, &FlatpakBackend::updatesChanged, this, &Catalog::bumpState);
    connect(m_flatpak, &FlatpakBackend::unavailable, this, [this] {
        // No Flathub catalogue is coming, so stop waiting for one.
        if (!m_flatLoaded) {
            m_flatLoaded = true;
            rebuild();
        }
    });
    connect(m_flatpak, &FlatpakBackend::refreshFinished, this, &Catalog::checkPartDone);
    connect(m_flatpak, &FlatpakBackend::progress, this, &Catalog::onProgress);
    connect(m_flatpak, &FlatpakBackend::finished, this, &Catalog::onFinished);
    connect(m_flatpak, &FlatpakBackend::planReady, this, &Catalog::onPlan);
    connect(m_flatpak, &FlatpakBackend::metadataReady, this, [this](const QString &appId, const QString &metadata) {
        if (m_detailsAsked.contains(appId)) {
            qCInfo(lcStore) << "timing: app page details for" << appId << "arrived" << storeClock() - m_detailsAsked.take(appId) << "ms after the page asked";
        }
        for (auto it = m_records.cbegin(); it != m_records.cend(); ++it) {
            if (it->flatpakId == appId) {
                AppState &s = m_states[it.key()];
                if (metadata.isEmpty()) {
                    s.metadataFailed = s.flatpakMetadata.isEmpty();
                } else {
                    s.flatpakMetadata = metadata;
                    s.metadataFailed = false;
                }
                stateChangedFor(it.key());
            }
        }
    });

    // Names and icons of installed apps come from KDE's service cache;
    // StoreApp keeps what it read until the cache or the installs change.
    // Queued: a lookup made while a binding reads an app's name can find the
    // cache out of date and announce it there and then, which would make
    // that binding read itself again.
    connect(KSycoca::self(), &KSycoca::databaseChanged, this, [this] {
        StoreApp::forgetServices();
        bumpState();
    }, Qt::QueuedConnection);

    auto enabled = new QProcess(this);
    connect(enabled, &QProcess::finished, this, [this, enabled](int code) {
        enabled->deleteLater();
        m_autoUpdate = code == 0;
        Q_EMIT autoUpdateChanged();
    });
    enabled->start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("--quiet"), QStringLiteral("is-enabled"), QStringLiteral("wildos-store-update.timer")});

    const qint64 began = storeClock();
    if (loadSnapshot()) {
        qCInfo(lcStore) << "timing: snapshot read in" << storeClock() - began << "ms," << m_records.size() << "apps";
        // The snapshot fills the window, so reading the catalogues and asking
        // the package systems can wait until it is on screen instead of
        // competing with it for the processor. The timer covers a window
        // that never draws.
        QTimer::singleShot(1500, this, &Catalog::startBackgroundWork);
        return;
    }
    startBackgroundWork();
}

void Catalog::startBackgroundWork()
{
    if (m_started) {
        return;
    }
    m_started = true;
    qCInfo(lcStore) << "timing: catalogues and package systems started at" << storeClock() << "ms";
    loadOsPool();
    m_packagekit->start();
    m_flatpak->start();
}

Catalog::~Catalog()
{
    s_instance = nullptr;
}

Catalog *Catalog::instance()
{
    return s_instance;
}

Catalog *Catalog::create(QQmlEngine *, QJSEngine *)
{
    if (!s_instance) {
        new Catalog;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}


// Whether apt has this package from any repository this PC uses, installed
// or not. Only asked for the few curated apps that need it.
static bool aptKnows(const QString &package)
{
    QProcess apt;
    apt.start(QStringLiteral("apt-cache"), {QStringLiteral("show"), QStringLiteral("--no-all-versions"), package});
    if (!apt.waitForFinished(5000)) {
        apt.kill();
        return false;
    }
    return apt.exitCode() == 0 && apt.readAllStandardOutput().contains("Package: ");
}

void Catalog::loadCurated()
{
    // The installed copy wins so the list can be tuned without a rebuild;
    // the one inside the binary keeps the Store whole if it goes missing.
    QFile file(QStringLiteral(WILDOS_STORE_DATADIR "/curated.json"));
    if (!file.exists()) {
        file.setFileName(QStringLiteral(":/qt/qml/WildOS/Store/data/curated.json"));
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("apps")).toArray()) {
        const QJsonObject o = v.toObject();
        Curated c;
        c.key = o.value(QStringLiteral("key")).toString();
        c.ids = strings(o.value(QStringLiteral("ids")));
        c.packages = strings(o.value(QStringLiteral("packages")));
        c.flatpak = o.value(QStringLiteral("flatpak")).toString();
        c.prefer = o.value(QStringLiteral("prefer")).toString();
        c.name = o.value(QStringLiteral("name")).toString();
        c.summary = o.value(QStringLiteral("summary")).toString();
        c.tagline = o.value(QStringLiteral("tagline")).toString();
        c.insteadOf = o.value(QStringLiteral("insteadOf")).toString();
        c.aliases = strings(o.value(QStringLiteral("aliases")));
        c.cardNote = o.value(QStringLiteral("cardNote")).toString();
        c.chip = o.value(QStringLiteral("chip")).toString();
        c.swapTitle = o.value(QStringLiteral("swapTitle")).toString();
        c.swapText = o.value(QStringLiteral("swapText")).toString();
        c.description = o.value(QStringLiteral("description")).toString();
        c.icon = o.value(QStringLiteral("icon")).toString();
        c.desktop = o.value(QStringLiteral("desktop")).toString();
        c.category = o.value(QStringLiteral("category")).toString();
        // An app from a repository only some PCs have (such as Ganger's)
        // is listed only where apt knows its package.
        if (o.value(QStringLiteral("onlyIfAvailable")).toBool() && (c.packages.isEmpty() || !aptKnows(c.packages.constFirst()))) {
            continue;
        }
        m_curated.append(c);
    }
    m_homeSwaps = strings(root.value(QStringLiteral("homeSwaps")));
    m_homeInstalled = strings(root.value(QStringLiteral("homeInstalled")));
    m_essentials = strings(root.value(QStringLiteral("essentials")));
    m_wildos = strings(root.value(QStringLiteral("wildos")));
    const QJsonObject featured = root.value(QStringLiteral("featured")).toObject();
    for (auto it = featured.begin(); it != featured.end(); ++it) {
        m_featured.insert(it.key(), strings(it.value()));
    }
    for (const QString &id : strings(root.value(QStringLiteral("hidden")))) {
        m_hidden.insert(normalise(id));
    }
}

void Catalog::loadOsPool()
{
    const qint64 began = storeClock();
    runAsync(this, osInputs, [this, began](QString inputs) {
        // PackageKit's refresh often finds nothing new; reading the same
        // files again would only rebuild the same catalogue.
        if (m_osLoaded && inputs == m_osInputs) {
            qCInfo(lcStore) << "timing: Wild OS catalogue unchanged, not read again (checked in" << storeClock() - began << "ms)";
            return;
        }
        auto pool = new AppStream::Pool;
        pool->setLocale(QLocale::system().name());
        // Flathub is read separately so each app knows where it came from.
        pool->setFlags(AppStream::Pool::Flags(AppStream::Pool::FlagLoadOsCatalog | AppStream::Pool::FlagLoadOsMetainfo));
        connect(pool, &AppStream::Pool::loadFinished, this, [this, pool, began, inputs](bool) {
            qCInfo(lcStore) << "timing: Wild OS catalogue loaded at" << storeClock() << "ms, took" << storeClock() - began << "ms" << pool->lastError();
            m_osPool = sharedPool(pool);
            m_osInputs = inputs;
            m_osLoaded = true;
            rebuild();
        });
        pool->loadAsync();
    });
}

void Catalog::loadFlatpakPool()
{
    const QString dir = m_flatpak->appstreamDir();
    if (dir.isEmpty()) {
        return;
    }
    const qint64 began = storeClock();
    runAsync(this, [dir] { return flatInputs(dir); }, [this, dir, began](QString inputs) {
        if (m_flatPool && inputs == m_flatInputs) {
            qCInfo(lcStore) << "timing: Flathub catalogue unchanged, not read again";
            return;
        }
        auto pool = new AppStream::Pool;
        pool->setLocale(QLocale::system().name());
        pool->setFlags(AppStream::Pool::FlagNone);
        pool->setLoadStdDataLocations(false);
        pool->addExtraDataLocation(dir, AppStream::Metadata::FormatStyleCatalog);
        connect(pool, &AppStream::Pool::loadFinished, this, [this, pool, began, inputs](bool ok) {
            qCInfo(lcStore) << "timing: Flathub catalogue loaded at" << storeClock() << "ms, took" << storeClock() - began << "ms" << ok << pool->lastError();
            m_flatPool = sharedPool(pool);
            m_flatInputs = inputs;
            m_flatLoaded = true;
            rebuild();
        });
        pool->loadAsync();
    });
}

QString Catalog::flatpakAppstreamDir() const
{
    return m_flatpak->appstreamDir();
}

void Catalog::rebuild()
{
    if (!m_osLoaded) {
        return;
    }
    // With a snapshot on screen, a catalogue without Flathub would take most
    // apps away until Flathub's arrived, so wait for both.
    if (m_fromSnapshot && !m_flatLoaded && !m_flatInputs.isEmpty()) {
        return;
    }
    if (m_building) {
        m_rebuildAgain = true;
        return;
    }
    m_building = true;
    BuildInput input{m_osPool, m_flatPool, &m_curated, m_hidden, m_flatpak->appstreamDir()};
    const qint64 began = storeClock();
    runAsync(this, [input] { return buildCatalog(input); }, [this, began](CatalogData data) {
        m_building = false;
        adopt(std::move(data), storeClock() - began);
        if (m_rebuildAgain) {
            m_rebuildAgain = false;
            rebuild();
        }
    });
}

void Catalog::adopt(CatalogData data, qint64 built)
{
    QElapsedTimer took;
    took.start();
    // Lists only need drawing again when the apps in them changed, not when
    // the same apps were read again or gained their full details.
    bool listsChanged = data.sorted != m_sorted || data.aliases != m_aliases;
    if (!listsChanged) {
        for (auto it = data.records.cbegin(); it != data.records.cend() && !listsChanged; ++it) {
            const Record *old = record(it.key());
            listsChanged = !old || old->aptName != it->aptName || old->flatName != it->flatName || old->aptIcon != it->aptIcon
                || old->flatIcon != it->flatIcon || old->aptSummary != it->aptSummary || old->flatSummary != it->flatSummary
                || old->hasFlat != it->hasFlat || old->hasApt != it->hasApt;
        }
    }
    m_records = std::move(data.records);
    m_aliases = std::move(data.aliases);
    m_sorted = std::move(data.sorted);
    m_categories = std::move(data.categories);
    m_fromSnapshot = false;

    for (auto it = m_apps.begin(); it != m_apps.end();) {
        if (it.value()) {
            Q_EMIT it.value()->dataChanged();
            Q_EMIT it.value()->stateChanged();
            ++it;
        } else {
            it = m_apps.erase(it);
        }
    }
    if (!m_ready) {
        m_ready = true;
        Q_EMIT readyChanged();
    }
    if (listsChanged) {
        bumpCatalog();
    }
    qCInfo(lcStore) << "timing: rebuild took" << built << "ms on a worker and" << took.elapsed() << "ms on the UI thread, at" << storeClock()
                    << "ms," << m_records.size() << "apps," << (listsChanged ? "lists changed" : "lists unchanged");
    if (m_osLoaded && (m_flatLoaded || m_flatpak->appstreamDir().isEmpty())) {
        saveSnapshot();
    }
}

bool Catalog::loadSnapshot()
{
    QFile file(snapshotPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_8);
    quint32 magic = 0;
    qint32 version = 0;
    QString locale;
    in >> magic >> version >> locale;
    if (magic != snapshotMagic || version != snapshotVersion || locale != QLocale::system().name()) {
        return false;
    }
    QHash<QString, const Curated *> curated;
    for (const Curated &c : std::as_const(m_curated)) {
        curated.insert(c.key, &c);
    }
    QString osIn, flatIn;
    QStringList sorted;
    QHash<QString, QString> aliases;
    QStringList flatInstalled, aptInstalled;
    qint32 count = 0;
    in >> osIn >> flatIn >> sorted >> aliases >> flatInstalled >> aptInstalled >> count;
    QHash<QString, Record> records;
    records.reserve(count);
    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        Record r;
        QString curatedKey;
        in >> r.key >> curatedKey >> r.packages >> r.flatpakId >> r.flatpakRef >> r.categories >> r.quality >> r.hasApt >> r.hasFlat >> r.aptName
            >> r.flatName >> r.aptSummary >> r.flatSummary >> r.aptIcon >> r.flatIcon >> r.searchName >> r.searchText;
        r.curated = curated.value(curatedKey);
        records.insert(r.key, r);
    }
    if (in.status() != QDataStream::Ok) {
        return false;
    }
    m_records = std::move(records);
    m_aliases = std::move(aliases);
    m_sorted = std::move(sorted);
    m_categories = categoriesFor(m_records, m_sorted);
    // Kept so the first rebuild knows whether Flathub's catalogue is coming.
    m_flatInputs = flatIn;
    Q_UNUSED(osIn)
    m_flatpak->seedInstalled(QSet<QString>(flatInstalled.begin(), flatInstalled.end()));
    m_packagekit->seedInstalled(QSet<QString>(aptInstalled.begin(), aptInstalled.end()));
    m_fromSnapshot = true;
    m_ready = true;
    return true;
}

void Catalog::saveSnapshot()
{
    struct Saved {
        QHash<QString, Record> records;
        QHash<QString, QString> aliases;
        QStringList sorted;
        QString osInputs, flatInputs;
        QStringList flatInstalled, aptInstalled;
    };
    const QSet<QString> flat = m_flatpak->installed();
    const QSet<QString> apt = m_packagekit->installed();
    Saved saved{m_records, m_aliases, m_sorted, m_osInputs, m_flatInputs, QStringList(flat.begin(), flat.end()), QStringList(apt.begin(), apt.end())};
    QThreadPool::globalInstance()->start([saved = std::move(saved)] {
        const qint64 began = storeClock();
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_8);
        out << snapshotMagic << snapshotVersion << QLocale::system().name();
        out << saved.osInputs << saved.flatInputs << saved.sorted << saved.aliases << saved.flatInstalled << saved.aptInstalled
            << qint32(saved.records.size());
        for (const Record &r : saved.records) {
            out << r.key << (r.curated ? r.curated->key : QString()) << r.packages << r.flatpakId << r.flatpakRef << r.categories << r.quality << r.hasApt
                << r.hasFlat << r.aptName << r.flatName << r.aptSummary << r.flatSummary << r.aptIcon << r.flatIcon << r.searchName << r.searchText;
        }
        const QString path = snapshotPath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
            qCInfo(lcStore) << "timing: snapshot saved," << bytes.size() << "bytes in" << storeClock() - began << "ms";
        }
    });
}

void Catalog::bumpCatalog()
{
    ++m_catalogRevision;
    Q_EMIT catalogRevisionChanged();
    Q_EMIT revisionChanged();
}

void Catalog::bumpState()
{
    for (auto it = m_apps.cbegin(); it != m_apps.cend(); ++it) {
        if (it.value()) {
            Q_EMIT it.value()->stateChanged();
        }
    }
    ++m_stateRevision;
    Q_EMIT stateRevisionChanged();
    Q_EMIT revisionChanged();
}

void Catalog::stateChangedFor(const QString &key)
{
    if (auto a = m_apps.value(key)) {
        Q_EMIT a->stateChanged();
    }
    Q_EMIT appStateChanged(key);
}

const Record *Catalog::record(const QString &key) const
{
    auto it = m_records.constFind(key);
    return it == m_records.cend() ? nullptr : &it.value();
}

QString Catalog::keyFor(const QString &id) const
{
    const QString key = normalise(id);
    if (m_records.contains(key)) {
        return key;
    }
    return m_aliases.value(key);
}

StoreApp *Catalog::app(const QString &key)
{
    if (key.isEmpty()) {
        return nullptr;
    }
    if (auto existing = m_apps.value(key)) {
        return existing;
    }
    const QString found = keyFor(key);
    const QString k = found.isEmpty() ? key : found;
    if (auto existing = m_apps.value(k)) {
        return existing;
    }
    auto a = new StoreApp(this, k);
    QJSEngine::setObjectOwnership(a, QJSEngine::CppOwnership);
    m_apps.insert(k, a);
    return a;
}

QString Catalog::resolve(const QString &query) const
{
    QString q = query.trimmed();
    // appstream://org.kde.krita, appstream:org.kde.krita and the
    // apt-style appstream:krita all name the same app.
    if (q.startsWith(QLatin1String("appstream:"), Qt::CaseInsensitive)) {
        q = q.mid(10);
        while (q.startsWith(QLatin1Char('/'))) {
            q.remove(0, 1);
        }
        q = q.section(QLatin1Char('?'), 0, 0);
    }
    if (q.startsWith(QLatin1String("app/"))) {
        q = q.section(QLatin1Char('/'), 1, 1);
    }
    return keyFor(q);
}

QString Catalog::source(const QString &key) const
{
    const Record *r = record(key);
    if (!r) {
        return {};
    }
    const QString installedFrom = installedSource(key);
    if (!installedFrom.isEmpty()) {
        return installedFrom;
    }
    const QString chosen = m_states.value(key).chosenSource;
    const bool hasApt = r->hasApt;
    const bool hasFlat = r->hasFlat || (!r->flatpakId.isEmpty() && flathubAvailable());
    if (chosen == QLatin1String("apt") && hasApt) {
        return chosen;
    }
    if (chosen == QLatin1String("flatpak") && hasFlat) {
        return chosen;
    }
    if (r->curated && r->curated->prefer == QLatin1String("apt") && hasApt) {
        return QStringLiteral("apt");
    }
    // Flathub apps run in a sandbox and are usually newer, so they win when
    // an app is in both catalogues.
    if (hasFlat && r->hasFlat) {
        return QStringLiteral("flatpak");
    }
    if (hasApt) {
        return QStringLiteral("apt");
    }
    return hasFlat ? QStringLiteral("flatpak") : QString();
}

QString Catalog::installedSource(const QString &key) const
{
    const Record *r = record(key);
    if (!r) {
        return {};
    }
    if (!r->flatpakId.isEmpty() && m_flatpak->installed().contains(r->flatpakId)) {
        return QStringLiteral("flatpak");
    }
    if (!r->packages.isEmpty() && m_packagekit->installed().contains(r->packages.constFirst())) {
        return QStringLiteral("apt");
    }
    return {};
}

bool Catalog::isInstalled(const QString &key) const
{
    return !installedSource(key).isEmpty();
}

bool Catalog::hasUpdate(const QString &key) const
{
    const Record *r = record(key);
    if (!r) {
        return false;
    }
    const QString from = installedSource(key);
    if (from == QLatin1String("flatpak")) {
        return m_flatpak->updates().contains(r->flatpakId);
    }
    if (from == QLatin1String("apt")) {
        const QSet<QString> pending = m_packagekit->updates();
        return std::any_of(r->packages.cbegin(), r->packages.cend(), [&pending](const QString &p) {
            return pending.contains(p);
        });
    }
    return false;
}

QString Catalog::installedVersion(const QString &key) const
{
    const Record *r = record(key);
    if (r && installedSource(key) == QLatin1String("apt")) {
        return m_packagekit->installedVersion(r->packages.constFirst());
    }
    return {};
}

qint64 Catalog::installedSize(const QString &key) const
{
    const Record *r = record(key);
    if (r && installedSource(key) == QLatin1String("flatpak")) {
        return m_flatpak->installedSizes().value(r->flatpakId, -1);
    }
    if (r && !r->packages.isEmpty() && installedSource(key) == QLatin1String("apt")) {
        return m_sizes->size(r->packages.constFirst());
    }
    return -1;
}

qint64 Catalog::appSize(const QString &key) const
{
    const Record *r = record(key);
    return r && !r->packages.isEmpty() ? m_sizes->size(r->packages.constFirst()) : -1;
}

QStringList Catalog::wildosApps() const
{
    QStringList out;
    for (const QString &id : m_wildos) {
        const QString key = keyFor(id);
        if (!key.isEmpty() && !out.contains(key)) {
            out << key;
        }
    }
    return out;
}

QStringList Catalog::wildosPackages() const
{
    QStringList out;
    for (const Curated &c : m_curated) {
        if (m_wildos.contains(c.key) && !c.packages.isEmpty()) {
            out << c.packages.constFirst();
        }
    }
    return out;
}

void Catalog::reinstallWildOS()
{
    for (const QString &key : wildosApps()) {
        const Record *r = record(key);
        if (r && !isInstalled(key)) {
            install(key);
        }
    }
}

int Catalog::updateCount() const
{
    return updates().size();
}

int Catalog::workingCount() const
{
    int count = 0;
    for (const Job &j : m_jobs) {
        count += j.keys.size();
    }
    return count;
}

int Catalog::updateAllProgress() const
{
    // One figure for every app "Update all" started, so its button can show
    // a single ring.
    int sum = 0;
    int count = 0;
    bool any = false;
    for (const Job &j : m_jobs) {
        if (!j.all) {
            continue;
        }
        any = true;
        for (const QString &key : j.keys) {
            sum += qMax(0, m_states.value(key).progress);
            ++count;
        }
    }
    if (!any) {
        return -1;
    }
    return count > 0 ? sum / count : 0;
}

bool Catalog::flathubAvailable() const
{
    return !m_flatpak->appstreamDir().isEmpty() || !m_flatInputs.isEmpty();
}

QVariantList Catalog::appCategories() const
{
    QVariantList out;
    for (const CategoryDef &d : appCategoryDefs()) {
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(d.id)}, {QStringLiteral("label"), QString::fromLatin1(d.label)}};
    }
    return out;
}

QVariantList Catalog::gameCategories() const
{
    QVariantList out;
    for (const CategoryDef &d : gameCategoryDefs()) {
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(d.id)}, {QStringLiteral("label"), QString::fromLatin1(d.label)}};
    }
    return out;
}

QString Catalog::categoryLabel(const QString &id) const
{
    for (const CategoryDef &d : appCategoryDefs()) {
        if (id == QLatin1String(d.id)) {
            return QString::fromLatin1(d.label);
        }
    }
    for (const CategoryDef &d : gameCategoryDefs()) {
        if (id == QLatin1String(d.id)) {
            return QString::fromLatin1(d.label);
        }
    }
    if (id == QLatin1String("games")) {
        return QStringLiteral("Games");
    }
    return QStringLiteral("All apps");
}

QString Catalog::pickImage(const QVariantList &images, int width, int height) const
{
    QString best;
    int bestWidth = 0;
    QString largest;
    int largestWidth = -1;
    for (const QVariant &v : images) {
        const QVariantMap m = v.toMap();
        const int w = m.value(QStringLiteral("width")).toInt();
        const int h = m.value(QStringLiteral("height")).toInt();
        const QString url = m.value(QStringLiteral("url")).toString();
        // Sizes the catalogue leaves out (often the original) count as large.
        const int effective = w > 0 ? w : 100000;
        if (effective > largestWidth) {
            largest = url;
            largestWidth = effective;
        }
        const bool fits = w <= 0 || (w >= width && (h <= 0 || h >= height));
        if (fits && (best.isEmpty() || effective < bestWidth)) {
            best = url;
            bestWidth = effective;
        }
    }
    return best.isEmpty() ? largest : best;
}

QStringList Catalog::homeSwaps() const
{
    QStringList out;
    for (const QString &k : m_homeSwaps) {
        const QString key = keyFor(k);
        if (!key.isEmpty()) {
            out << key;
        }
    }
    return out;
}

QStringList Catalog::homeInstalled() const
{
    QStringList out;
    for (const QString &k : m_homeInstalled) {
        const QString key = keyFor(k);
        if (!key.isEmpty() && isInstalled(key) && !out.contains(key)) {
            out << key;
        }
        if (out.size() == 4) {
            return out;
        }
    }
    for (const QString &key : installedApps()) {
        if (!out.contains(key)) {
            out << key;
        }
        if (out.size() == 4) {
            break;
        }
    }
    return out;
}

QStringList Catalog::swaps() const
{
    QStringList out;
    for (const Curated &c : m_curated) {
        const QString key = keyFor(c.key);
        if (!c.insteadOf.isEmpty() && !key.isEmpty()) {
            out << key;
        }
    }
    return out;
}

QStringList Catalog::essentials() const
{
    QStringList out;
    for (const QString &k : m_essentials) {
        const QString key = keyFor(k);
        if (!key.isEmpty()) {
            out << key;
        }
    }
    return out;
}

QStringList Catalog::featured(const QString &list) const
{
    QStringList out;
    for (const QString &k : m_featured.value(list)) {
        const QString key = keyFor(k);
        if (!key.isEmpty() && !out.contains(key)) {
            out << key;
        }
    }
    return out;
}

QStringList Catalog::category(const QString &id, int limit) const
{
    bool known = false;
    for (const auto *defs : {&appCategoryDefs(), &gameCategoryDefs()}) {
        for (const CategoryDef &d : *defs) {
            known = known || id == QLatin1String(d.id);
        }
    }
    const QString list = known ? id : id.startsWith(QLatin1String("game")) ? allGames : allApps;
    const QStringList &all = m_categories[list];
    return limit > 0 ? all.mid(0, limit) : all;
}

QStringList Catalog::swapMatches(const QString &text) const
{
    const QString q = text.trimmed().toLower();
    QStringList out;
    if (q.size() < 2) {
        return out;
    }
    for (const Curated &c : m_curated) {
        QStringList words = c.aliases;
        words << c.insteadOf;
        const bool hit = std::any_of(words.cbegin(), words.cend(), [&q](const QString &w) {
            const QString lw = w.toLower();
            return !lw.isEmpty() && (lw.contains(q) || q.contains(lw));
        });
        const QString key = keyFor(c.key);
        if (hit && !key.isEmpty() && !c.insteadOf.isEmpty() && !out.contains(key)) {
            out << key;
        }
    }
    return out;
}

QStringList Catalog::search(const QString &text) const
{
    const QString q = text.trimmed().toLower();
    if (q.isEmpty()) {
        return {};
    }
    QElapsedTimer took;
    took.start();
    // Matched against text gathered when the catalogue was built, rather
    // than through AppStream, so it runs as someone types and works from the
    // snapshot before either catalogue has loaded.
    const QStringList words = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<QPair<uint, QString>> scored;
    for (auto it = m_records.cbegin(); it != m_records.cend(); ++it) {
        const Record &r = it.value();
        uint score = 0;
        if (r.searchName == q) {
            score = 1000000;
        } else if (r.searchName.startsWith(q)) {
            score = 500000;
        } else if (r.searchName.contains(QLatin1Char(' ') + q)) {
            score = 200000;
        } else if (r.searchName.contains(q)) {
            score = 100000;
        }
        bool all = true;
        for (const QString &w : words) {
            if (!r.searchName.contains(w) && !r.searchText.contains(w)) {
                all = false;
                break;
            }
        }
        if (!score && !all) {
            continue;
        }
        if (all) {
            score += 10000;
        }
        scored.append({score + uint(r.quality) * 10, it.key()});
    }
    std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) {
            return a.first > b.first;
        }
        return a.second < b.second;
    });
    QStringList out;
    out.reserve(scored.size());
    for (const auto &s : std::as_const(scored)) {
        out << s.second;
    }
    // Apps that stand in for the Windows app someone typed come first.
    const QStringList swapsFirst = swapMatches(q);
    for (auto it = swapsFirst.crbegin(); it != swapsFirst.crend(); ++it) {
        out.removeAll(*it);
        out.prepend(*it);
    }
    qCInfo(lcStore) << "timing: search" << q << "took" << took.elapsed() << "ms," << out.size() << "results";
    return out;
}

QStringList Catalog::installedApps() const
{
    QElapsedTimer took;
    took.start();
    // Names are read once each rather than in every comparison: an installed
    // app's name comes from KDE's service cache.
    QList<QPair<QString, QString>> named;
    for (const QString &key : m_sorted) {
        if (isInstalled(key)) {
            named.append({const_cast<Catalog *>(this)->app(key)->name(), key});
        }
    }
    std::sort(named.begin(), named.end(), [](const auto &a, const auto &b) {
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });
    QStringList out;
    for (const auto &n : std::as_const(named)) {
        out << n.second;
    }
    qCInfo(lcStore) << "timing: installed list took" << took.elapsed() << "ms";
    return out;
}

QStringList Catalog::updates() const
{
    QStringList out;
    // Only installed apps can have updates; there are far fewer of those.
    const QSet<QString> flat = m_flatpak->updates();
    const QSet<QString> apt = m_packagekit->updates();
    if (flat.isEmpty() && apt.isEmpty()) {
        return out;
    }
    for (const QString &key : m_sorted) {
        if (hasUpdate(key)) {
            out << key;
        }
    }
    return out;
}

qint64 Catalog::updatedTime(const QString &key) const
{
    const Record *r = record(key);
    const QString from = installedSource(key);
    if (!r || from.isEmpty()) {
        return -1;
    }
    if (from == QLatin1String("flatpak")) {
        return m_flatpak->updatedTimes().value(r->flatpakId, -1);
    }
    // dpkg rewrites a package's file list each time it unpacks a version.
    const QString package = r->packages.constFirst();
    for (const QString &name : {package + QStringLiteral(".list"), package + QStringLiteral(":amd64.list")}) {
        const QFileInfo info(QStringLiteral("/var/lib/dpkg/info/") + name);
        if (info.exists()) {
            return info.lastModified().toMSecsSinceEpoch();
        }
    }
    return -1;
}

qint64 Catalog::updateSize(const QString &key) const
{
    const Record *r = record(key);
    if (!r || installedSource(key) != QLatin1String("flatpak")) {
        return -1;
    }
    return m_flatpak->updateSizes().value(r->flatpakId, -1);
}

qint64 Catalog::updatesBytes() const
{
    qint64 total = 0;
    bool any = false;
    for (const QString &key : updates()) {
        const qint64 size = updateSize(key);
        if (size > 0) {
            total += size;
            any = true;
        }
    }
    return any ? total : -1;
}

QStringList Catalog::recentlyUpdated(int limit) const
{
    const qint64 since = QDateTime::currentDateTime().addDays(-30).toMSecsSinceEpoch();
    QList<QPair<qint64, QString>> found;
    for (const QString &key : m_sorted) {
        if (!isInstalled(key) || hasUpdate(key)) {
            continue;
        }
        const qint64 when = updatedTime(key);
        if (when >= since) {
            found.append({when, key});
        }
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    QStringList out;
    for (const auto &f : std::as_const(found)) {
        if (limit > 0 && out.size() >= limit) {
            break;
        }
        out << f.second;
    }
    return out;
}

QStringList Catalog::workingApps() const
{
    QStringList out;
    for (const Job &j : m_jobs) {
        for (const QString &key : j.keys) {
            if (!out.contains(key)) {
                out << key;
            }
        }
    }
    return out;
}

QStringList Catalog::matching(const QStringList &categories, int limit) const
{
    QStringList out;
    for (const QString &key : m_sorted) {
        const Record &r = m_records[key];
        if (std::any_of(categories.cbegin(), categories.cend(), [&r](const QString &c) {
                return r.categories.contains(c);
            })) {
            out << key;
            if (limit > 0 && out.size() >= limit) {
                break;
            }
        }
    }
    return out;
}

QString Catalog::dayText(qint64 msecs) const
{
    if (msecs <= 0) {
        return {};
    }
    const QDate day = QDateTime::fromMSecsSinceEpoch(msecs).date();
    const QDate today = QDate::currentDate();
    if (day == today) {
        return QStringLiteral("Today");
    }
    if (day == today.addDays(-1)) {
        return QStringLiteral("Yesterday");
    }
    if (day > today.addDays(-7)) {
        return QLocale().dayName(day.dayOfWeek(), QLocale::ShortFormat);
    }
    return QLocale().toString(day, QLocale::ShortFormat);
}

QString Catalog::sizeText(qint64 bytes) const
{
    if (bytes < 0) {
        return {};
    }
    return QLocale().formattedDataSize(bytes, bytes >= 1000 * 1000 * 1000 ? 1 : 0, QLocale::DataSizeSIFormat);
}

void Catalog::setAutoUpdate(bool on)
{
    if (on == m_autoUpdate) {
        return;
    }
    m_autoUpdate = on;
    Q_EMIT autoUpdateChanged();
    // The timer is switched on for every account by the package, so one
    // account turns it off for itself by masking it.
    const QString unit = QStringLiteral("wildos-store-update.timer");
    const QString script = on ? QStringLiteral("systemctl --user unmask %1; systemctl --user start %1").arg(unit)
                              : QStringLiteral("systemctl --user stop %1; systemctl --user mask %1").arg(unit);
    QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), script});
}

void Catalog::pause(const QString &key)
{
    for (const Job &j : std::as_const(m_jobs)) {
        if (j.keys.contains(key) && j.running) {
            m_pausing.insert(j.id);
            AppState &s = m_states[key];
            s.stopping = true;
            s.resumeAfterStop = false;
            stateChangedFor(key);
            break;
        }
    }
    cancel(key);
}

void Catalog::fetchDetails(const QString &key)
{
    const Record *r = record(key);
    if (!r) {
        return;
    }
    const QString from = source(key);
    if (from == QLatin1String("apt")) {
        m_packagekit->fetchDetails(key, r->packages);
    } else if (from == QLatin1String("flatpak") && !r->flatpakId.isEmpty()) {
        const AppState &s = m_states[key];
        // Asked once per run: neither the permissions nor the sizes change
        // while the Store is open.
        if (!s.flatpakMetadata.isEmpty() && (s.planTotal >= 0 || isInstalled(key))) {
            return;
        }
        if (m_detailsAsked.contains(r->flatpakId)) {
            return;
        }
        if (!isInstalled(key) && loadDetails(key)) {
            qCInfo(lcStore) << "timing: details for" << r->flatpakId << "read from disk";
            stateChangedFor(key);
            return;
        }
        m_detailsAsked.insert(r->flatpakId, storeClock());
        m_flatpak->fetchDetails(r->flatpakId, r->flatpakRef);
    }
}

namespace
{
QString detailsPath(const QString &flatpakId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-store/details/") + flatpakId + QStringLiteral(".json");
}

// flatpak touches this file whenever the installation changes; sizes worked
// out before then may count a runtime that is now installed.
qint64 installationChanged()
{
    return QFileInfo(FlatpakBackend::userDataDir() + QStringLiteral("/.changed")).lastModified().toMSecsSinceEpoch();
}
}

bool Catalog::loadDetails(const QString &key)
{
    // An app page opened again, in this run or a later one, gets its
    // permissions and sizes from disk while Flathub's catalogue and the
    // installation are as they were when they were fetched.
    const Record *r = record(key);
    QFile file(detailsPath(r->flatpakId));
    if (m_flatInputs.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o.value(QStringLiteral("inputs")).toString() != m_flatInputs || o.value(QStringLiteral("saved")).toInteger() < installationChanged()
        || o.value(QStringLiteral("metadata")).toString().isEmpty() || o.value(QStringLiteral("total")).toInteger(-1) < 0) {
        return false;
    }
    AppState &s = m_states[key];
    s.flatpakMetadata = o.value(QStringLiteral("metadata")).toString();
    s.metadataFailed = false;
    s.planTotal = o.value(QStringLiteral("total")).toInteger(-1);
    s.planShared = o.value(QStringLiteral("shared")).toInteger(-1);
    s.planInstalled = o.value(QStringLiteral("installed")).toInteger(-1);
    s.planRuntime = o.value(QStringLiteral("runtime")).toString();
    return true;
}

void Catalog::saveDetails(const QString &key)
{
    const Record *r = record(key);
    const AppState &s = m_states[key];
    if (!r || r->flatpakId.isEmpty() || m_flatInputs.isEmpty() || s.flatpakMetadata.isEmpty() || s.planTotal < 0 || isInstalled(key)) {
        return;
    }
    const QJsonObject o{
        {QStringLiteral("inputs"), m_flatInputs},
        {QStringLiteral("saved"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("metadata"), s.flatpakMetadata},
        {QStringLiteral("total"), s.planTotal},
        {QStringLiteral("shared"), s.planShared},
        {QStringLiteral("installed"), s.planInstalled},
        {QStringLiteral("runtime"), s.planRuntime},
    };
    const QString path = detailsPath(r->flatpakId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void Catalog::onPlan(const QString &appId, const FlatpakPlan &plan)
{
    for (auto it = m_records.cbegin(); it != m_records.cend(); ++it) {
        if (it->flatpakId == appId) {
            AppState &s = m_states[it.key()];
            s.planTotal = plan.total;
            s.planShared = plan.shared;
            s.planRuntime = plan.runtime;
            s.planInstalled = plan.installed;
            stateChangedFor(it.key());
            saveDetails(it.key());
        }
    }
}

void Catalog::install(const QString &key)
{
    const Record *r = record(key);
    if (r && m_states.value(key).stopping) {
        m_states[key].resumeAfterStop = true;
        return;
    }
    if (!r || isInstalled(key) || m_states.value(key).busy || m_states.value(key).queued) {
        return;
    }
    const QString from = source(key);
    if (from == QLatin1String("flatpak")) {
        enqueue(JobKind::Install, from, {key}, {r->flatpakRef});
    } else if (from == QLatin1String("apt")) {
        enqueue(JobKind::Install, from, {key}, r->packages);
    }
}

void Catalog::remove(const QString &key)
{
    const Record *r = record(key);
    if (!r || m_states.value(key).busy || m_states.value(key).queued) {
        return;
    }
    const QString from = installedSource(key);
    if (from == QLatin1String("flatpak")) {
        enqueue(JobKind::Remove, from, {key}, {r->flatpakRef});
    } else if (from == QLatin1String("apt")) {
        enqueue(JobKind::Remove, from, {key}, r->packages);
    }
}

void Catalog::update(const QString &key)
{
    const Record *r = record(key);
    if (!r || !hasUpdate(key) || m_states.value(key).busy || m_states.value(key).queued) {
        return;
    }
    const QString from = installedSource(key);
    if (from == QLatin1String("flatpak")) {
        enqueue(JobKind::Update, from, {key}, {r->flatpakRef});
    } else {
        enqueue(JobKind::Update, from, {key}, r->packages);
    }
}

void Catalog::cancel(const QString &key)
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        Job &j = m_jobs[i];
        if (!j.keys.contains(key)) {
            continue;
        }
        if (j.running) {
            m_cancelled.insert(j.id);
            if (j.source == QLatin1String("flatpak")) {
                m_flatpak->cancel(j.id);
            } else {
                m_packagekit->cancel(j.id);
            }
            return;
        }
        const Job gone = m_jobs.takeAt(i);
        for (const QString &k : gone.keys) {
            AppState &s = m_states[k];
            s.queued = false;
            s.busy = false;
            s.progress = -1;
            s.phase.clear();
            stateChangedFor(k);
        }
        bumpState();
        Q_EMIT updateAllProgressChanged();
        return;
    }
}

void Catalog::installEssentials()
{
    for (const QString &key : essentials()) {
        install(key);
    }
}

void Catalog::updateAll()
{
    QStringList aptKeys, aptPackages, flatKeys;
    const QSet<QString> pending = m_packagekit->updates();
    for (const QString &key : updates()) {
        AppState &s = m_states[key];
        if (s.busy || s.queued) {
            continue;
        }
        const Record &r = m_records[key];
        if (installedSource(key) == QLatin1String("flatpak")) {
            flatKeys << key;
        } else {
            aptKeys << key;
            for (const QString &p : r.packages) {
                if (pending.contains(p)) {
                    aptPackages << p;
                }
            }
        }
    }
    if (!flatKeys.isEmpty()) {
        // An empty list updates every Flathub app and runtime together, so
        // shared runtimes are fetched once.
        enqueue(JobKind::Update, QStringLiteral("flatpak"), flatKeys, {}, true);
    }
    if (!aptKeys.isEmpty()) {
        enqueue(JobKind::Update, QStringLiteral("apt"), aptKeys, aptPackages, true);
    }
}

void Catalog::refresh()
{
    if (m_checking) {
        return;
    }
    m_checking = true;
    m_checkParts = 2;
    m_checkOk = true;
    Q_EMIT checkChanged();
    m_packagekit->refresh();
    m_flatpak->refreshAppstream();
}

void Catalog::checkPartDone(bool ok)
{
    // PackageKit also refreshes by itself when the Store starts; only a
    // check someone asked for is reported.
    if (!m_checking) {
        return;
    }
    m_checkOk = m_checkOk && ok;
    if (--m_checkParts > 0) {
        return;
    }
    m_checking = false;
    m_checkResult = m_checkOk ? QStringLiteral("ok") : QStringLiteral("failed");
    m_checkedAt = ClockFormat::time(QTime::currentTime());
    Q_EMIT checkChanged();
}

void Catalog::openSettings(const QString &page)
{
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), page});
}

Catalog::Job *Catalog::job(int id)
{
    for (Job &j : m_jobs) {
        if (j.id == id) {
            return &j;
        }
    }
    return nullptr;
}

void Catalog::enqueue(JobKind kind, const QString &source, const QStringList &keys, const QStringList &targets, bool all)
{
    Job job{m_nextJob++, kind, source, keys, targets};
    job.all = all;
    const QString action = kind == JobKind::Install ? QStringLiteral("install") : kind == JobKind::Remove ? QStringLiteral("remove") : QStringLiteral("update");
    for (const QString &key : keys) {
        AppState &s = m_states[key];
        s.queued = true;
        s.paused = false;
        s.action = action;
        s.error.clear();
        s.progress = -1;
        s.secondsLeft = -1;
        s.rateStarted = -1;
        s.detail.clear();
        s.doneBytes = -1;
        s.totalBytes = -1;
        s.phase = QStringLiteral("queued");
        stateChangedFor(key);
    }
    m_jobs.append(job);
    bumpState();
    Q_EMIT updateAllProgressChanged();
    scheduleRun();
}

void Catalog::scheduleRun()
{
    // Installs asked for together (Install all, or clicks in quick
    // succession) are started together, so Flathub ones share a transaction.
    if (m_runQueued) {
        return;
    }
    m_runQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_runQueued = false;
        runNext();
    });
}

void Catalog::runNext()
{
    // Wild OS packages and Flathub apps go through different systems, so one
    // of each can run at once.
    for (const QString &src : {QStringLiteral("flatpak"), QStringLiteral("apt")}) {
        const bool running = std::any_of(m_jobs.cbegin(), m_jobs.cend(), [&src](const Job &j) {
            return j.running && j.source == src;
        });
        if (running) {
            continue;
        }
        QList<int> ids;
        for (const Job &j : std::as_const(m_jobs)) {
            if (j.running || j.source != src) {
                continue;
            }
            if (ids.isEmpty()) {
                ids << j.id;
                if (!(src == QLatin1String("flatpak") && j.kind == JobKind::Install)) {
                    break;
                }
                continue;
            }
            // Further Flathub installs join the first one's transaction.
            if (j.kind == JobKind::Install) {
                ids << j.id;
            }
        }
        if (!ids.isEmpty()) {
            start(ids);
        }
    }
}

void Catalog::start(const QList<int> &ids)
{
    FlatpakBatch batch;
    for (int id : ids) {
        Job *j = job(id);
        if (!j) {
            continue;
        }
        j->running = true;
        m_jobStarted.insert(j->id, storeClock());
        for (const QString &key : std::as_const(j->keys)) {
            AppState &s = m_states[key];
            s.queued = false;
            s.busy = true;
            s.phase = QStringLiteral("prepare");
            stateChangedFor(key);
        }
        if (j->source == QLatin1String("flatpak")) {
            switch (j->kind) {
            case JobKind::Install:
                batch.append({j->id, j->targets.value(0)});
                break;
            case JobKind::Remove:
                m_flatpak->uninstall(j->id, j->targets.value(0));
                break;
            case JobKind::Update:
                m_flatpak->update(j->id, j->targets);
                break;
            }
        } else {
            switch (j->kind) {
            case JobKind::Install:
                m_packagekit->install(j->id, j->targets);
                break;
            case JobKind::Remove:
                m_packagekit->remove(j->id, j->targets);
                break;
            case JobKind::Update:
                m_packagekit->update(j->id, j->targets);
                break;
            }
        }
    }
    if (!batch.isEmpty()) {
        qCInfo(lcStore) << "timing: Flathub install of" << batch.size() << "apps in one transaction";
        m_flatpak->install(batch);
    }
    bumpState();
}

void Catalog::onProgress(int id, int percent, const QString &phase, const QString &detail, qint64 done, qint64 total)
{
    const Job *j = job(id);
    if (!j) {
        return;
    }
    for (const QString &key : j->keys) {
        AppState &s = m_states[key];
        // The bar only ever moves forward; a step that cannot say how far it
        // has got leaves the last figure in place.
        if (percent >= 0) {
            s.progress = qMax(s.progress, percent);
        }
        s.phase = phase;
        s.detail = detail;
        if (total > 0) {
            s.totalBytes = total;
            s.doneBytes = qMax(s.doneBytes, done);
            // Time left from the average speed since the download began,
            // once there is enough of it to go on.
            const qint64 now = storeClock();
            if (s.rateStarted < 0 && s.doneBytes > 0) {
                s.rateStarted = now;
                s.rateStartBytes = s.doneBytes;
            } else if (s.rateStarted >= 0 && now - s.rateStarted > 2000 && s.doneBytes > s.rateStartBytes) {
                const double perMs = double(s.doneBytes - s.rateStartBytes) / double(now - s.rateStarted);
                s.secondsLeft = qint64(double(s.totalBytes - s.doneBytes) / perMs / 1000.0);
            }
        }
        stateChangedFor(key);
    }
    if (j->all) {
        Q_EMIT updateAllProgressChanged();
    }
}

void Catalog::onFinished(int id, bool ok, const QString &error)
{
    Job *found = job(id);
    if (!found) {
        return;
    }
    // A batch is one Flathub transaction: cancelling one app in it stops
    // the others too, and they go back in the queue to start again.
    if (!ok && error == QLatin1String(cancelledMarker) && !m_cancelled.contains(id)) {
        found->running = false;
        for (const QString &key : std::as_const(found->keys)) {
            AppState &s = m_states[key];
            s.busy = false;
            s.queued = true;
            s.phase = QStringLiteral("queued");
            stateChangedFor(key);
        }
        scheduleRun();
        return;
    }
    const bool cancelled = m_cancelled.remove(id);
    // Stopped with its stop button, or cut off from Flathub: either way it
    // can carry on from what it already fetched.
    const bool paused = m_pausing.remove(id) || (!ok && error.startsWith(QLatin1String("Couldn't reach")));
    Job j = *found;
    m_jobs.removeIf([id](const Job &x) {
        return x.id == id;
    });
    qCInfo(lcStore) << "timing: job" << id << j.source << j.targets << (ok ? "finished" : cancelled ? "cancelled" : "failed") << "after"
                    << storeClock() - m_jobStarted.take(id) << "ms";
    for (const QString &key : std::as_const(j.keys)) {
        AppState &s = m_states[key];
        s.busy = false;
        s.queued = false;
        s.progress = -1;
        s.phase.clear();
        s.detail.clear();
        s.doneBytes = -1;
        s.totalBytes = -1;
        s.rateStarted = -1;
        s.secondsLeft = -1;
        s.paused = paused && j.kind != JobKind::Remove;
        s.stopping = false;
        s.error = ok || cancelled || s.paused ? QString() : error;
        if (ok && j.kind == JobKind::Remove) {
            s.chosenSource.clear();
            s.planTotal = -1;
            s.planRuntime.clear();
        }
        stateChangedFor(key);
        if (ok && j.kind == JobKind::Install) {
            Q_EMIT installed(key);
        } else if (!ok && !cancelled && !paused) {
            Q_EMIT failed(key, error);
        }
    }
    // Sizes of apps on the same runtime change once it is installed.
    if (ok && j.source == QLatin1String("flatpak")) {
        for (auto &s : m_states) {
            s.planTotal = -1;
            s.planShared = -1;
            s.planRuntime.clear();
        }
        m_detailsAsked.clear();
    }
    bumpState();
    Q_EMIT updateAllProgressChanged();
    scheduleRun();
    if (ok) {
        saveSnapshot();
    }
    for (const QString &key : std::as_const(j.keys)) {
        if (m_states[key].resumeAfterStop) {
            m_states[key].resumeAfterStop = false;
            install(key);
        }
    }
}
