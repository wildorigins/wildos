#include "defaultapps.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <QCollator>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace
{
struct Category {
    const char *id;
    const char *title;
    const char *description;
    const char *icon;
    // The first type decides the current app and the choices; the choice is
    // written for all of them.
    QStringList types;
    // Only apps that say they are this kind are offered, so a text editor
    // that can open web pages is not listed as a browser.
    QString desktopCategory;
};

const QList<Category> &categoryList()
{
    static const QList<Category> list = {
        {"browser", QT_TRANSLATE_NOOP("DefaultApps", "Web browser"), QT_TRANSLATE_NOOP("DefaultApps", "Opens web links"), "globe",
         {QStringLiteral("x-scheme-handler/https"), QStringLiteral("x-scheme-handler/http"), QStringLiteral("text/html"),
          QStringLiteral("application/xhtml+xml")},
         QStringLiteral("WebBrowser")},
        {"email", QT_TRANSLATE_NOOP("DefaultApps", "Email"), QT_TRANSLATE_NOOP("DefaultApps", "Opens email addresses"), "mail", {QStringLiteral("x-scheme-handler/mailto")}, QString()},
        {"pictures", QT_TRANSLATE_NOOP("DefaultApps", "Pictures"), QT_TRANSLATE_NOOP("DefaultApps", "Photos and other images"), "image",
         {QStringLiteral("image/jpeg"), QStringLiteral("image/png"), QStringLiteral("image/gif"), QStringLiteral("image/webp"),
          QStringLiteral("image/bmp"), QStringLiteral("image/tiff"), QStringLiteral("image/heif")},
         QString()},
        {"music", QT_TRANSLATE_NOOP("DefaultApps", "Music"), QT_TRANSLATE_NOOP("DefaultApps", "Songs and other audio"), "sound",
         {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/x-vorbis+ogg"), QStringLiteral("audio/mp4"),
          QStringLiteral("audio/x-wav"), QStringLiteral("audio/ogg")},
         QString()},
        {"video", QT_TRANSLATE_NOOP("DefaultApps", "Videos"), QT_TRANSLATE_NOOP("DefaultApps", "Films and clips"), "video",
         {QStringLiteral("video/mp4"), QStringLiteral("video/x-matroska"), QStringLiteral("video/webm"), QStringLiteral("video/quicktime"),
          QStringLiteral("video/x-msvideo")},
         QString()},
        {"pdf", QT_TRANSLATE_NOOP("DefaultApps", "PDF documents"), QT_TRANSLATE_NOOP("DefaultApps", "Opens PDF files"), "document", {QStringLiteral("application/pdf")}, QString()},
        {"text", QT_TRANSLATE_NOOP("DefaultApps", "Text files"), QT_TRANSLATE_NOOP("DefaultApps", "Plain text and notes"), "pencil", {QStringLiteral("text/plain")}, QString()},
        {"files", QT_TRANSLATE_NOOP("DefaultApps", "Files and folders"), QT_TRANSLATE_NOOP("DefaultApps", "Opens folders"), "folder", {QStringLiteral("inode/directory")}, QString()},
    };
    return list;
}

QVariantList appsFor(const QString &mime, const QString &desktopCategory = QString())
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    QVariantList apps;
    QSet<QString> seen;
    const KService::List services = KApplicationTrader::queryByMimeType(mime);
    for (const KService::Ptr &service : services) {
        if (service->noDisplay() || seen.contains(service->storageId())) {
            continue;
        }
        if (!desktopCategory.isEmpty() && !service->categories().contains(desktopCategory)) {
            continue;
        }
        seen.insert(service->storageId());
        apps << QVariantMap{{QStringLiteral("value"), service->storageId()}, {QStringLiteral("text"), service->name()}};
    }
    std::sort(apps.begin(), apps.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("text")).toString(), b.toMap().value(QStringLiteral("text")).toString()) < 0;
    });
    return apps;
}

// The account's own mimeapps.list, which outranks the system one. The
// app also goes first under Added Associations, as Dolphin's "Open with"
// does, so it stays offered for each type.
void writeDefault(const QStringList &types, const QString &storageId)
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("mimeapps.list"), KConfig::NoGlobals, QStandardPaths::GenericConfigLocation);
    KConfigGroup defaults = config->group(QStringLiteral("Default Applications"));
    KConfigGroup added = config->group(QStringLiteral("Added Associations"));
    for (const QString &mime : types) {
        defaults.writeXdgListEntry(mime, {storageId});
        QStringList associated = added.readXdgListEntry(mime);
        associated.removeAll(storageId);
        associated.prepend(storageId);
        added.writeXdgListEntry(mime, associated);
    }
    config->sync();
}

const Category *findCategory(const QString &id)
{
    for (const Category &c : categoryList()) {
        if (QLatin1String(c.id) == id) {
            return &c;
        }
    }
    return nullptr;
}
}

DefaultApps::DefaultApps(QObject *parent)
    : QObject(parent)
{
    reload();
}

void DefaultApps::reload()
{
    QVariantList list;
    for (const Category &c : categoryList()) {
        const QString mime = c.types.constFirst();
        const QVariantList apps = appsFor(mime, c.desktopCategory);
        const KService::Ptr preferred = KApplicationTrader::preferredService(mime);
        list << QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(c.id)},
            {QStringLiteral("title"), tr(c.title)},
            {QStringLiteral("description"), tr(c.description)},
            {QStringLiteral("icon"), QString::fromLatin1(c.icon)},
            {QStringLiteral("current"), preferred ? preferred->storageId() : QString()},
            {QStringLiteral("apps"), apps},
        };
    }
    m_categories = list;
    Q_EMIT changed();
}

void DefaultApps::setDefault(const QString &categoryId, const QString &storageId)
{
    const Category *category = findCategory(categoryId);
    const KService::Ptr service = KService::serviceByStorageId(storageId);
    if (!category || !service) {
        return;
    }

    writeDefault(category->types, storageId);

    // KDE apps open web links through the browser named here, and email
    // through the one named in emaildefaults.
    if (categoryId == QLatin1String("browser")) {
        KSharedConfig::Ptr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        globals->group(QStringLiteral("General")).writeEntry("BrowserApplication", storageId, KConfig::Notify);
        globals->sync();
    } else if (categoryId == QLatin1String("email")) {
        KSharedConfig::Ptr email = KSharedConfig::openConfig(QStringLiteral("emaildefaults"));
        KConfigGroup group = email->group(QStringLiteral("PROFILE_Default"));
        group.writeEntry("EmailClient", service->exec());
        group.writeEntry("TerminalClient", false);
        email->sync();
    }

    rebuild();
}

void DefaultApps::rebuild()
{
    // The service database caches associations; rebuilding it makes the
    // new default count everywhere straight away.
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        reload();
    });
    process->start(QStringLiteral("kbuildsycoca6"), {});
}

QVariantList DefaultApps::fileTypes(const QString &search) const
{
    QString query = search.trimmed().toLower();
    const bool extensionOnly = query.startsWith(QLatin1Char('.'));
    if (extensionOnly) {
        query.remove(0, 1);
    }
    if (query.isEmpty()) {
        return {};
    }

    struct Match {
        QMimeType type;
        int rank;
    };
    QList<Match> matches;
    const QList<QMimeType> all = QMimeDatabase().allMimeTypes();
    for (const QMimeType &type : all) {
        // Only types a file can have by its name; the rest are not what
        // anyone looks for by file type.
        const QStringList suffixes = type.suffixes();
        if (suffixes.isEmpty()) {
            continue;
        }
        int rank = -1;
        if (suffixes.contains(query, Qt::CaseInsensitive)) {
            rank = 0;
        } else if (std::any_of(suffixes.cbegin(), suffixes.cend(), [&query](const QString &s) {
                       return s.startsWith(query, Qt::CaseInsensitive);
                   })) {
            rank = 1;
        } else if (!extensionOnly && query.size() >= 3 && type.comment().contains(query, Qt::CaseInsensitive)) {
            rank = 2;
        }
        if (rank >= 0) {
            matches << Match{type, rank};
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(matches.begin(), matches.end(), [&collator](const Match &a, const Match &b) {
        if (a.rank != b.rank) {
            return a.rank < b.rank;
        }
        return collator.compare(a.type.suffixes().constFirst(), b.type.suffixes().constFirst()) < 0;
    });

    // A search as short as one letter matches hundreds of types; the
    // first few are enough to narrow it down.
    QVariantList list;
    for (const Match &match : std::as_const(matches)) {
        if (list.size() >= 40) {
            break;
        }
        QStringList extensions;
        for (const QString &suffix : match.type.suffixes()) {
            extensions << QStringLiteral(".") + suffix;
        }
        const KService::Ptr preferred = KApplicationTrader::preferredService(match.type.name());
        list << QVariantMap{
            {QStringLiteral("mime"), match.type.name()},
            {QStringLiteral("description"), match.type.comment()},
            {QStringLiteral("extensions"), extensions.join(QStringLiteral(", "))},
            {QStringLiteral("icon"), match.type.iconName()},
            {QStringLiteral("current"), preferred ? preferred->storageId() : QString()},
            {QStringLiteral("apps"), appsFor(match.type.name())},
        };
    }
    return list;
}

void DefaultApps::setDefaultForType(const QString &mime, const QString &storageId)
{
    if (!QMimeDatabase().mimeTypeForName(mime).isValid() || !KService::serviceByStorageId(storageId)) {
        return;
    }
    writeDefault({mime}, storageId);
    rebuild();
}
