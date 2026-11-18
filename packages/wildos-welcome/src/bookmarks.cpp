#include "bookmarks.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <unistd.h>

namespace
{
// Chromium gives the three top-level folders the same guids in every
// profile; a file written from scratch uses them too.
const QString barGuid = QStringLiteral("0bc5d13f-2cba-5d74-951f-3f233fe6c908");
const QString otherGuid = QStringLiteral("82b081ec-3dd3-529c-8475-ab6c344590dd");
const QString syncedGuid = QStringLiteral("4cf2e351-0e85-532b-bb37-df045d8f8d0f");

QString chromiumNow()
{
    // Microseconds since 1601, the Windows epoch Chromium uses everywhere.
    const qint64 offset = 11644473600LL * 1000000LL;
    return QString::number(QDateTime::currentMSecsSinceEpoch() * 1000LL + offset);
}

QJsonObject emptyRoot(const QString &id, const QString &name, const QString &guid)
{
    return QJsonObject{
        {QStringLiteral("children"), QJsonArray()},
        {QStringLiteral("date_added"), chromiumNow()},
        {QStringLiteral("date_modified"), QStringLiteral("0")},
        {QStringLiteral("guid"), guid},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("type"), QStringLiteral("folder")},
    };
}

qint64 highestId(const QJsonValue &node)
{
    const QJsonObject object = node.toObject();
    qint64 best = object.value(QStringLiteral("id")).toString().toLongLong();
    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children) {
        best = std::max(best, highestId(child));
    }
    return best;
}

// Copies a bookmark or folder with new ids and guids, keeping only the
// fields every Chromium version understands. Sync and per-browser metadata
// from Edge would mean nothing to Brave.
QJsonObject renumber(const QJsonObject &node, qint64 &nextId, int &count)
{
    QJsonObject out;
    out.insert(QStringLiteral("id"), QString::number(nextId++));
    out.insert(QStringLiteral("guid"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    out.insert(QStringLiteral("name"), node.value(QStringLiteral("name")).toString());
    const QString added = node.value(QStringLiteral("date_added")).toString();
    out.insert(QStringLiteral("date_added"), added.isEmpty() ? chromiumNow() : added);
    const QString type = node.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("folder")) {
        out.insert(QStringLiteral("type"), QStringLiteral("folder"));
        out.insert(QStringLiteral("date_modified"), node.value(QStringLiteral("date_modified")).toString(QStringLiteral("0")));
        QJsonArray children;
        const QJsonArray source = node.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &child : source) {
            children.append(renumber(child.toObject(), nextId, count));
        }
        out.insert(QStringLiteral("children"), children);
    } else {
        out.insert(QStringLiteral("type"), QStringLiteral("url"));
        out.insert(QStringLiteral("url"), node.value(QStringLiteral("url")).toString());
        ++count;
    }
    return out;
}

QString childIgnoringCase(const QString &dir, const QString &name)
{
    const auto entries = QDir(dir).entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
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
}

namespace Bookmarks
{
QList<BrowserProfile> findWindowsProfiles(const QString &windowsHome)
{
    const QList<std::pair<QString, QStringList>> browsers = {
        {QStringLiteral("Edge"), {QStringLiteral("AppData"), QStringLiteral("Local"), QStringLiteral("Microsoft"), QStringLiteral("Edge"), QStringLiteral("User Data")}},
        {QStringLiteral("Chrome"), {QStringLiteral("AppData"), QStringLiteral("Local"), QStringLiteral("Google"), QStringLiteral("Chrome"), QStringLiteral("User Data")}},
    };
    QList<BrowserProfile> found;
    for (const auto &[browser, parts] : browsers) {
        const QString userData = pathIgnoringCase(windowsHome, parts);
        if (userData.isEmpty()) {
            continue;
        }
        // "Default" plus "Profile 1", "Profile 2" for every extra person
        // signed in to the browser.
        const auto profiles = QDir(userData).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &profile : profiles) {
            if (profile != QLatin1String("Default") && !profile.startsWith(QLatin1String("Profile "))) {
                continue;
            }
            const QString file = childIgnoringCase(userData + QLatin1Char('/') + profile, QStringLiteral("Bookmarks"));
            if (!file.isEmpty() && QFileInfo(file).size() > 0) {
                found.append({browser, file});
            }
        }
    }
    return found;
}

bool braveInstalled()
{
    return !QStandardPaths::findExecutable(QStringLiteral("brave-browser")).isEmpty() || QFileInfo::exists(QStringLiteral("/opt/brave.com/brave/brave"));
}

bool braveRunning()
{
    const auto pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (!pid.at(0).isDigit()) {
            continue;
        }
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "brave" && QFileInfo(QStringLiteral("/proc/") + pid).ownerId() == getuid()) {
            return true;
        }
    }
    return false;
}

QString braveBookmarksFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/BraveSoftware/Brave-Browser/Default/Bookmarks");
}

int importInto(const QString &braveFile, const QList<BrowserProfile> &profiles)
{
    QJsonObject document;
    QFile existing(braveFile);
    if (existing.open(QIODevice::ReadOnly)) {
        document = QJsonDocument::fromJson(existing.readAll()).object();
        existing.close();
    }
    QJsonObject roots = document.value(QStringLiteral("roots")).toObject();
    if (!roots.contains(QStringLiteral("bookmark_bar"))) {
        roots.insert(QStringLiteral("bookmark_bar"), emptyRoot(QStringLiteral("1"), QStringLiteral("Bookmarks bar"), barGuid));
    }
    if (!roots.contains(QStringLiteral("other"))) {
        roots.insert(QStringLiteral("other"), emptyRoot(QStringLiteral("2"), QStringLiteral("Other bookmarks"), otherGuid));
    }
    if (!roots.contains(QStringLiteral("synced"))) {
        roots.insert(QStringLiteral("synced"), emptyRoot(QStringLiteral("3"), QStringLiteral("Mobile bookmarks"), syncedGuid));
    }

    qint64 nextId = 0;
    for (const QString &root : {QStringLiteral("bookmark_bar"), QStringLiteral("other"), QStringLiteral("synced")}) {
        nextId = std::max(nextId, highestId(roots.value(root)));
    }
    ++nextId;

    QJsonObject bar = roots.value(QStringLiteral("bookmark_bar")).toObject();
    QJsonArray barChildren = bar.value(QStringLiteral("children")).toArray();
    QStringList existingNames;
    for (const QJsonValue &child : std::as_const(barChildren)) {
        existingNames << child.toObject().value(QStringLiteral("name")).toString();
    }

    int added = 0;
    QHash<QString, int> perBrowser;
    for (const BrowserProfile &profile : profiles) {
        perBrowser[profile.browser]++;
    }
    QHash<QString, int> seen;
    for (const BrowserProfile &profile : profiles) {
        QFile file(profile.path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject source = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("roots")).toObject();
        const int number = ++seen[profile.browser];
        const QString folderName = perBrowser.value(profile.browser) > 1 ? QCoreApplication::translate("Bookmarks", "From %1 (%2)").arg(profile.browser).arg(number)
                                                                         : QCoreApplication::translate("Bookmarks", "From %1").arg(profile.browser);
        // Run twice, it would add the same folder twice.
        if (existingNames.contains(folderName)) {
            continue;
        }

        QJsonObject folder{
            {QStringLiteral("type"), QStringLiteral("folder")},
            {QStringLiteral("name"), folderName},
            {QStringLiteral("children"), source.value(QStringLiteral("bookmark_bar")).toObject().value(QStringLiteral("children")).toArray()},
        };
        QJsonArray children = folder.value(QStringLiteral("children")).toArray();
        const QJsonObject other = source.value(QStringLiteral("other")).toObject();
        if (!other.value(QStringLiteral("children")).toArray().isEmpty()) {
            children.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("folder")},
                {QStringLiteral("name"), QCoreApplication::translate("Bookmarks", "Other bookmarks")},
                {QStringLiteral("children"), other.value(QStringLiteral("children")).toArray()},
            });
        }
        folder.insert(QStringLiteral("children"), children);
        int count = 0;
        const QJsonObject numbered = renumber(folder, nextId, count);
        if (count == 0) {
            continue;
        }
        barChildren.append(numbered);
        existingNames << folderName;
        added += count;
    }
    if (added == 0) {
        return 0;
    }

    bar.insert(QStringLiteral("children"), barChildren);
    bar.insert(QStringLiteral("date_modified"), chromiumNow());
    roots.insert(QStringLiteral("bookmark_bar"), bar);
    document.insert(QStringLiteral("roots"), roots);
    document.insert(QStringLiteral("version"), 1);
    // The checksum covers every id and title; a stale one makes the browser
    // log a mismatch, a missing one is simply not checked.
    document.remove(QStringLiteral("checksum"));

    QDir().mkpath(QFileInfo(braveFile).absolutePath());
    QSaveFile out(braveFile);
    if (!out.open(QIODevice::WriteOnly)) {
        return -1;
    }
    // Owner only, as Brave keeps it.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    return out.commit() ? added : -1;
}
}

namespace History
{
QString braveHistoryFile()
{
    return QFileInfo(Bookmarks::braveBookmarksFile()).absolutePath() + QStringLiteral("/History");
}

bool createBraveProfile()
{
    const QString userData = QFileInfo(QFileInfo(braveHistoryFile()).absolutePath()).absolutePath();
    QProcess brave;
    brave.start(QStringLiteral("/opt/brave.com/brave/brave"),
                {QStringLiteral("--headless=new"), QStringLiteral("--user-data-dir=") + userData, QStringLiteral("about:blank")});
    if (!brave.waitForStarted(10000)) {
        return false;
    }
    for (int i = 0; i < 60 && !QFileInfo::exists(braveHistoryFile()); ++i) {
        brave.waitForFinished(500);
    }
    // A moment more, so Brave finishes setting the file up before it is
    // asked to quit.
    brave.waitForFinished(3000);
    brave.terminate();
    if (!brave.waitForFinished(10000)) {
        brave.kill();
        brave.waitForFinished(5000);
    }
    return QFileInfo::exists(braveHistoryFile());
}

int importInto(const QString &braveHistory, const QList<BrowserProfile> &profiles)
{
    if (!QFileInfo::exists(braveHistory)) {
        return -1;
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        return -1;
    }
    int added = 0;
    const QString connection = QStringLiteral("wildos-history");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(braveHistory);
        if (!db.open()) {
            QSqlDatabase::removeDatabase(connection);
            return -1;
        }
        QSqlQuery query(db);
        int copies = 0;
        bool ok = true;
        for (const BrowserProfile &profile : profiles) {
            // A copy, because the Windows drive may be read-only and the
            // browser may have left a journal beside the file.
            const QString source = QFileInfo(profile.path).absolutePath() + QStringLiteral("/History");
            const QString copy = scratch.filePath(QStringLiteral("History-%1").arg(++copies));
            if (!QFileInfo::exists(source) || !QFile::copy(source, copy)) {
                continue;
            }
            query.prepare(QStringLiteral("ATTACH DATABASE ? AS windows"));
            query.addBindValue(copy);
            if (!query.exec()) {
                continue;
            }
            // Pages added from here on have ids above this, and only their
            // visits are copied, so a page Brave already had gains nothing.
            const qint64 lastId = query.exec(QStringLiteral("SELECT coalesce(max(id), 0) FROM main.urls")) && query.next()
                ? query.value(0).toLongLong() : 0;
            db.transaction();
            // Only web pages: Edge's own pages (edge://) and files opened in
            // the browser mean nothing on Wild OS.
            ok = query.exec(QStringLiteral(
                "INSERT INTO main.urls (url, title, visit_count, typed_count, last_visit_time, hidden) "
                "SELECT url, title, visit_count, typed_count, last_visit_time, hidden FROM windows.urls "
                "WHERE (url LIKE 'http://%' OR url LIKE 'https://%') AND url NOT IN (SELECT url FROM main.urls)"));
            if (ok) {
                query.prepare(QStringLiteral(
                    "INSERT INTO main.visits (url, visit_time, from_visit, transition, segment_id, visit_duration) "
                    "SELECT m.id, v.visit_time, 0, v.transition, 0, v.visit_duration "
                    "FROM windows.visits v JOIN windows.urls w ON w.id = v.url JOIN main.urls m ON m.url = w.url "
                    "WHERE m.id > ?"));
                query.addBindValue(lastId);
                ok = query.exec();
            }
            int pages = 0;
            if (ok && query.exec(QStringLiteral("SELECT count(*) FROM main.urls WHERE id > %1").arg(lastId)) && query.next()) {
                pages = query.value(0).toInt();
            }
            if (ok && db.commit()) {
                added += pages;
            } else {
                db.rollback();
                ok = false;
            }
            query.finish();
            query.exec(QStringLiteral("DETACH DATABASE windows"));
        }
        db.close();
        if (!ok && added == 0) {
            added = -1;
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return added;
}
}
