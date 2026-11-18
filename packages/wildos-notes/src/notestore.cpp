#include "notestore.h"
#include "clockformat.h"
#include "notefile.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace
{
NoteStore *s_instance = nullptr;

// Past this many notes only the folders are watched; inotify watches are a
// per-account budget that Plasma and the file indexer share.
constexpr int kMaxWatchedFiles = 2000;

QString createdIndexPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos-notes/created.json");
}

struct Cached {
    NoteStore::Note note;
    qint64 size = -1;
};
QHash<QString, Cached> &cache()
{
    static QHash<QString, Cached> c;
    return c;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(bytes);
    return file.commit();
}

QString cleanName(QString name)
{
    name.replace(u'/', u'-').replace(u'\\', u'-');
    name = name.simplified();
    while (name.startsWith(u'.')) {
        name.remove(0, 1);
    }
    return name.trimmed();
}
}

NoteStore::NoteStore(QObject *parent)
    : QAbstractListModel(parent)
    , m_root(QDir::homePath() + QStringLiteral("/Notes"))
{
    s_instance = this;
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(150);
    connect(&m_rescan, &QTimer::timeout, this, &NoteStore::readAll);
    m_saveCreated.setSingleShot(true);
    m_saveCreated.setInterval(2000);
    connect(&m_saveCreated, &QTimer::timeout, this, &NoteStore::saveCreatedNow);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_saveCreated.isActive()) {
            m_saveCreated.stop();
            saveCreatedNow();
        }
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &NoteStore::scheduleRescan);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        Q_EMIT fileChanged(path);
        scheduleRescan();
    });
    loadCreated();
    readAll();
}

NoteStore *NoteStore::instance()
{
    if (!s_instance) {
        new NoteStore(QCoreApplication::instance());
    }
    return s_instance;
}

NoteStore *NoteStore::create(QQmlEngine *, QJSEngine *)
{
    NoteStore *store = instance();
    QQmlEngine::setObjectOwnership(store, QQmlEngine::CppOwnership);
    return store;
}

int NoteStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NoteStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Note &note = m_all[m_rows[index.row()]];
    switch (role) {
    case PathRole:
        return note.path;
    case TitleRole:
        return note.title;
    case PreviewRole:
        return note.preview;
    case NotebookRole:
        return note.notebook;
    case PinnedRole:
        return note.pinned;
    case ColourRole:
        return note.colour;
    case DateTextRole:
        return dateText(m_sort == u"created" ? note.created : note.modified);
    case TagsRole:
        return note.tags;
    }
    return {};
}

QHash<int, QByteArray> NoteStore::roleNames() const
{
    return {
        {PathRole, "path"},
        {TitleRole, "title"},
        {PreviewRole, "preview"},
        {NotebookRole, "notebook"},
        {PinnedRole, "pinned"},
        {ColourRole, "colour"},
        {DateTextRole, "dateText"},
        {TagsRole, "tags"},
    };
}

QString NoteStore::root() const
{
    return m_root;
}

QVariantList NoteStore::notebooks() const
{
    QVariantList list;
    for (const QString &name : m_notebooks) {
        list.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("count"), notebookCount(name)}});
    }
    return list;
}

QVariantList NoteStore::tags() const
{
    QMap<QString, int> counts;
    for (const Note &note : m_all) {
        for (const QString &tag : note.tags) {
            counts[tag] += 1;
        }
    }
    QVariantList list;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        list.append(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("count"), it.value()}});
    }
    return list;
}

int NoteStore::allCount() const
{
    return int(m_all.size());
}

int NoteStore::pinnedCount() const
{
    return int(std::count_if(m_all.cbegin(), m_all.cend(), [](const Note &n) {
        return n.pinned;
    }));
}

int NoteStore::notebookCount(const QString &name) const
{
    return int(std::count_if(m_all.cbegin(), m_all.cend(), [&name](const Note &n) {
        return n.notebook == name;
    }));
}

QString NoteStore::defaultNotebook() const
{
    if (m_notebooks.contains(QStringLiteral("Personal"))) {
        return QStringLiteral("Personal");
    }
    return m_notebooks.value(0, QStringLiteral("Personal"));
}

QString NoteStore::viewKind() const
{
    return m_viewKind;
}

void NoteStore::setViewKind(const QString &kind)
{
    if (kind == m_viewKind) {
        return;
    }
    m_viewKind = kind;
    Q_EMIT viewChanged();
    refilter();
}

QString NoteStore::viewKey() const
{
    return m_viewKey;
}

void NoteStore::setViewKey(const QString &key)
{
    if (key == m_viewKey) {
        return;
    }
    m_viewKey = key;
    Q_EMIT viewChanged();
    refilter();
}

QString NoteStore::search() const
{
    return m_search;
}

void NoteStore::setSearch(const QString &search)
{
    if (search == m_search) {
        return;
    }
    m_search = search;
    Q_EMIT viewChanged();
    refilter();
}

QString NoteStore::sort() const
{
    return m_sort;
}

void NoteStore::setSort(const QString &sort)
{
    if (sort == m_sort || (sort != u"edited" && sort != u"created" && sort != u"title")) {
        return;
    }
    m_sort = sort;
    Q_EMIT viewChanged();
    beginResetModel();
    m_rows.clear();
    endResetModel();
    refilter();
}

int NoteStore::count() const
{
    return int(m_rows.size());
}

QString NoteStore::lastError() const
{
    return m_lastError;
}

void NoteStore::fail(const QString &message)
{
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}

QString NoteStore::notebookDir(const QString &notebook) const
{
    return m_root + u'/' + notebook;
}

QString NoteStore::notebookOf(const QString &path) const
{
    const QFileInfo info(path);
    return info.absolutePath() == QDir(m_root).absolutePath() ? QString() : info.dir().dirName();
}

QString NoteStore::freePath(const QString &dir, const QString &name, const QString &ignore)
{
    QString candidate = dir + u'/' + name + QStringLiteral(".md");
    for (int i = 2; QFileInfo::exists(candidate) && candidate != ignore; ++i) {
        candidate = dir + u'/' + name + u' ' + QString::number(i) + QStringLiteral(".md");
    }
    return candidate;
}

void NoteStore::ensureRoot()
{
    QDir root(m_root);
    if (!root.exists()) {
        root.mkpath(QStringLiteral("Personal"));
        return;
    }
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const bool any = std::any_of(dirs.cbegin(), dirs.cend(), [](const QString &d) {
        return !d.startsWith(u'.');
    });
    if (!any) {
        root.mkpath(QStringLiteral("Personal"));
    }
}

void NoteStore::scheduleRescan()
{
    m_rescan.start();
}

void NoteStore::rescan()
{
    m_rescan.stop();
    readAll();
}

void NoteStore::readAll()
{
    ensureRoot();
    QDir root(m_root);
    QStringList notebooks;
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (!name.startsWith(u'.')) {
            notebooks.append(name);
        }
    }

    QList<Note> all;
    QStringList files;
    QSet<QString> seen;
    QStringList changed;
    for (const QString &notebook : std::as_const(notebooks)) {
        const QDir dir(root.filePath(notebook));
        const QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.md")}, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &info : entries) {
            const QString path = info.absoluteFilePath();
            seen.insert(path);
            files.append(path);
            const QDateTime modified = info.lastModified();
            auto cached = cache().constFind(path);
            if (cached != cache().constEnd() && cached->note.modified == modified && cached->size == info.size()) {
                all.append(cached->note);
                all.last().notebook = notebook;
                continue;
            }
            const auto before = m_seenModified.constFind(path);
            if (before != m_seenModified.constEnd() && *before != modified) {
                changed.append(path);
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const NoteParts parts = NoteFile::parse(file.readAll());
            Note note;
            note.path = path;
            note.notebook = notebook;
            note.title = NoteFile::title(parts, info.completeBaseName());
            note.preview = NoteFile::preview(parts.body);
            note.tags = NoteFile::tags(parts.titleLine + u'\n' + parts.body);
            note.pinned = NoteFile::pinned(parts);
            note.colour = NoteFile::colour(parts);
            note.modified = modified;
            note.created = createdAt(path);
            note.search = (note.title + u'\n' + parts.body).toLower();
            cache().insert(path, {note, info.size()});
            all.append(note);
        }
    }
    for (auto it = cache().begin(); it != cache().end();) {
        it = seen.contains(it.key()) ? std::next(it) : cache().erase(it);
    }
    for (auto it = m_created.begin(); it != m_created.end();) {
        if (seen.contains(it.key())) {
            ++it;
        } else {
            it = m_created.erase(it);
            saveCreatedSoon();
        }
    }
    m_seenModified.clear();
    for (const Note &note : std::as_const(all)) {
        m_seenModified.insert(note.path, note.modified);
    }

    QStringList watch{m_root};
    for (const QString &notebook : std::as_const(notebooks)) {
        watch.append(root.filePath(notebook));
    }
    if (files.size() <= kMaxWatchedFiles) {
        watch += files;
    }
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    QStringList stale;
    for (const QString &path : watched) {
        if (!watch.contains(path)) {
            stale.append(path);
        }
    }
    if (!stale.isEmpty()) {
        m_watcher.removePaths(stale);
    }
    QStringList fresh;
    for (const QString &path : std::as_const(watch)) {
        if (!watched.contains(path)) {
            fresh.append(path);
        }
    }
    if (!fresh.isEmpty()) {
        m_watcher.addPaths(fresh);
    }

    m_notebooks = notebooks;
    m_all = all;
    if (m_viewKind == u"notebook" && !m_notebooks.contains(m_viewKey)) {
        m_viewKind = QStringLiteral("all");
        m_viewKey.clear();
        Q_EMIT viewChanged();
    }
    Q_EMIT libraryChanged();
    refilter();
    for (const QString &path : std::as_const(changed)) {
        Q_EMIT fileChanged(path);
    }
}

bool NoteStore::matches(const Note &note) const
{
    if (!m_search.trimmed().isEmpty()) {
        const QStringList words = m_search.toLower().split(u' ', Qt::SkipEmptyParts);
        return std::all_of(words.cbegin(), words.cend(), [&note](const QString &w) {
            return note.search.contains(w);
        });
    }
    if (m_viewKind == u"pinned") {
        return note.pinned;
    }
    if (m_viewKind == u"notebook") {
        return note.notebook == m_viewKey;
    }
    if (m_viewKind == u"tag") {
        return note.tags.contains(m_viewKey);
    }
    return true;
}

void NoteStore::refilter()
{
    QList<int> rows;
    for (int i = 0; i < m_all.size(); ++i) {
        if (matches(m_all[i])) {
            rows.append(i);
        }
    }
    const bool byTitle = m_sort == u"title";
    const bool byCreated = m_sort == u"created";
    std::stable_sort(rows.begin(), rows.end(), [this, byTitle, byCreated](int a, int b) {
        const Note &x = m_all[a];
        const Note &y = m_all[b];
        if (x.pinned != y.pinned) {
            return x.pinned;
        }
        if (byTitle) {
            return QString::localeAwareCompare(x.title, y.title) < 0;
        }
        if (byCreated) {
            return x.created > y.created;
        }
        return x.modified > y.modified;
    });

    bool same = rows.size() == m_rows.size();
    if (same) {
        // m_all was rebuilt, so rows are compared by the notes they point at.
        for (int i = 0; i < rows.size() && same; ++i) {
            same = m_rowPaths.value(i) == m_all[rows[i]].path;
        }
    }
    QStringList paths;
    for (int row : std::as_const(rows)) {
        paths.append(m_all[row].path);
    }
    if (same) {
        m_rows = rows;
        m_rowPaths = paths;
        if (!m_rows.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
        }
        return;
    }
    beginResetModel();
    m_rows = rows;
    m_rowPaths = paths;
    endResetModel();
    Q_EMIT countChanged();
}

int NoteStore::indexOfPath(const QString &path) const
{
    return int(m_rowPaths.indexOf(path));
}

QString NoteStore::pathAt(int row) const
{
    return m_rowPaths.value(row);
}

bool NoteStore::exists(const QString &path) const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

QString NoteStore::createNote(const QString &notebook)
{
    const QString name = notebook.isEmpty() ? defaultNotebook() : notebook;
    const QString dir = notebookDir(name);
    QDir().mkpath(dir);
    const QString path = freePath(dir, QStringLiteral("Untitled"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        fail(tr("Notes couldn't make a new note in %1.").arg(name));
        return {};
    }
    file.close();
    m_created.insert(path, QDateTime::currentMSecsSinceEpoch());
    saveCreatedSoon();
    rescan();
    return path;
}

bool NoteStore::trashNote(const QString &path)
{
    QString inBin;
    if (!QFile::moveToTrash(path, &inBin)) {
        fail(tr("Notes couldn't move the note to the bin."));
        return false;
    }
    rememberTrashed(path, inBin);
    rescan();
    return true;
}

void NoteStore::rememberTrashed(const QString &from, const QString &inBin)
{
    m_trashed = Trashed{from, inBin, {}};
    const QString inside = from + u'/';
    for (auto it = m_created.cbegin(); it != m_created.cend(); ++it) {
        if (it.key() == from || it.key().startsWith(inside)) {
            m_trashed.created.insert(it.key(), it.value());
        }
    }
}

QString NoteStore::restoreTrashed()
{
    const Trashed last = m_trashed;
    m_trashed = Trashed();
    if (last.inBin.isEmpty() || !QFileInfo::exists(last.inBin)) {
        fail(tr("It isn't in the bin any more."));
        return {};
    }
    // An empty folder of the same name, such as the first notebook Notes
    // makes when there is none, gives way; rmdir never takes a full one.
    if (QFileInfo(last.from).isDir()) {
        QDir().rmdir(last.from);
    }
    if (QFileInfo::exists(last.from)) {
        fail(tr("Something else has that name now. Yours is still in the bin."));
        return {};
    }
    QDir().mkpath(QFileInfo(last.from).absolutePath());
    if (!QDir().rename(last.inBin, last.from)) {
        fail(tr("Notes couldn't bring it back. It is still in the bin."));
        return {};
    }
    // The bin's record of where it came from, beside its files folder.
    const QFileInfo binned(last.inBin);
    QFile::remove(binned.absolutePath() + QStringLiteral("/../info/") + binned.fileName() + QStringLiteral(".trashinfo"));
    for (auto it = last.created.cbegin(); it != last.created.cend(); ++it) {
        m_created.insert(it.key(), it.value());
    }
    saveCreatedSoon();
    rescan();
    return last.from;
}

QString NoteStore::moveNote(const QString &path, const QString &notebook)
{
    const QFileInfo info(path);
    const QString targetDir = notebookDir(notebook);
    if (info.absolutePath() == QFileInfo(targetDir).absoluteFilePath()) {
        return path;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(tr("Notes couldn't read the note to move it."));
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();
    QDir().mkpath(targetDir);
    const QString target = freePath(targetDir, info.completeBaseName());

    // Pictures are linked relative to the note, so they go along with it.
    // One by the same name already there is left alone and this note's copy
    // gets a name of its own.
    static const QRegularExpression image(QStringLiteral(R"(!\[[^\]]*\]\(<?([^)>\s]+)>?(?:\s+"[^"]*")?\))"));
    QString text = QString::fromUtf8(bytes);
    bool rewritten = false;
    const QDir from(info.absolutePath());
    const QDir to(targetDir);
    auto it = image.globalMatch(text);
    QList<QPair<QString, QString>> renames;
    while (it.hasNext()) {
        const QString link = it.next().captured(1);
        if (link.contains(QStringLiteral("://")) || link.startsWith(u'/')) {
            continue;
        }
        const QString relative = QUrl::fromPercentEncoding(link.toUtf8());
        const QString source = from.filePath(relative);
        if (!QFileInfo::exists(source)) {
            continue;
        }
        QString destination = to.filePath(relative);
        QDir().mkpath(QFileInfo(destination).absolutePath());
        if (QFileInfo::exists(destination) && QFileInfo(destination).size() != QFileInfo(source).size()) {
            const QFileInfo d(destination);
            for (int i = 2; QFileInfo::exists(destination); ++i) {
                destination = d.absolutePath() + u'/' + d.completeBaseName() + u' ' + QString::number(i) + u'.' + d.suffix();
            }
            QString newLink = to.relativeFilePath(destination);
            newLink = QString::fromUtf8(QUrl::toPercentEncoding(newLink, "/"));
            renames.append({link, newLink});
        }
        if (!QFileInfo::exists(destination)) {
            QFile::copy(source, destination);
        }
    }
    for (const auto &[oldLink, newLink] : std::as_const(renames)) {
        text.replace(u'(' + oldLink + u')', u'(' + newLink + u')');
        rewritten = true;
    }

    if (rewritten) {
        if (!writeFile(target, text.toUtf8()) || !QFile::moveToTrash(path)) {
            fail(tr("Notes couldn't move the note to %1.").arg(notebook));
            return {};
        }
    } else if (!QFile::rename(path, target)) {
        fail(tr("Notes couldn't move the note to %1.").arg(notebook));
        return {};
    }
    noteWritten(path, target);
    Q_EMIT pathMoved(path, target);
    rescan();
    return target;
}

QString NoteStore::createNotebook(const QString &name)
{
    const QString clean = cleanName(name);
    if (clean.isEmpty()) {
        fail(tr("Give the notebook a name."));
        return {};
    }
    for (const QString &existing : std::as_const(m_notebooks)) {
        if (existing.compare(clean, Qt::CaseInsensitive) == 0) {
            fail(tr("There's already a notebook called %1.").arg(existing));
            return {};
        }
    }
    if (!QDir(m_root).mkpath(clean)) {
        fail(tr("Notes couldn't make the notebook %1.").arg(clean));
        return {};
    }
    rescan();
    return clean;
}

QString NoteStore::renameNotebook(const QString &from, const QString &to)
{
    const QString clean = cleanName(to);
    if (clean.isEmpty()) {
        fail(tr("Give the notebook a name."));
        return {};
    }
    if (clean == from) {
        return clean;
    }
    for (const QString &existing : std::as_const(m_notebooks)) {
        if (existing != from && existing.compare(clean, Qt::CaseInsensitive) == 0) {
            fail(tr("There's already a notebook called %1.").arg(existing));
            return {};
        }
    }
    const QString oldDir = notebookDir(from) + u'/';
    const QString newDir = notebookDir(clean) + u'/';
    if (!QDir(m_root).rename(from, clean)) {
        fail(tr("Notes couldn't rename %1.").arg(from));
        return {};
    }
    QHash<QString, qint64> moved;
    for (auto it = m_created.begin(); it != m_created.end();) {
        if (it.key().startsWith(oldDir)) {
            moved.insert(newDir + it.key().mid(oldDir.size()), it.value());
            it = m_created.erase(it);
        } else {
            ++it;
        }
    }
    m_created.insert(moved);
    saveCreatedSoon();
    if (m_viewKind == u"notebook" && m_viewKey == from) {
        m_viewKey = clean;
        Q_EMIT viewChanged();
    }
    Q_EMIT pathMoved(oldDir, newDir);
    rescan();
    return clean;
}

bool NoteStore::trashNotebook(const QString &name)
{
    QString inBin;
    if (name.isEmpty() || !QFile::moveToTrash(notebookDir(name), &inBin)) {
        fail(tr("Notes couldn't move %1 to the bin.").arg(name));
        return false;
    }
    rememberTrashed(notebookDir(name), inBin);
    rescan();
    return true;
}

void NoteStore::showInFiles(const QString &path) const
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                                                          QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
    if (!QDBusConnection::sessionBus().send(message)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

void NoteStore::noteWritten(const QString &oldPath, const QString &newPath)
{
    if (oldPath != newPath && !oldPath.isEmpty()) {
        const qint64 created = m_created.contains(oldPath) ? m_created.take(oldPath) : createdAt(oldPath).toMSecsSinceEpoch();
        m_created.insert(newPath, created);
        saveCreatedSoon();
    }
    scheduleRescan();
}

// A save replaces the file, which gives it a new birth time, so the time
// each note was first seen is kept apart, in Notes' own data folder.
QDateTime NoteStore::createdAt(const QString &path)
{
    const auto it = m_created.constFind(path);
    if (it != m_created.constEnd()) {
        return QDateTime::fromMSecsSinceEpoch(*it);
    }
    const QFileInfo info(path);
    QDateTime when = info.birthTime();
    if (!when.isValid()) {
        when = info.lastModified();
    }
    if (when.isValid()) {
        m_created.insert(path, when.toMSecsSinceEpoch());
        saveCreatedSoon();
    }
    return when;
}

void NoteStore::loadCreated()
{
    QFile file(createdIndexPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        m_created.insert(it.key(), qint64(it.value().toDouble()));
    }
}

void NoteStore::saveCreatedSoon()
{
    m_saveCreated.start();
}

QString NoteStore::dateText(const QDateTime &when)
{
    if (!when.isValid()) {
        return {};
    }
    const QDate day = when.date();
    const QDate today = QDate::currentDate();
    const QLocale locale;
    if (day == today) {
        return tr("Today");
    }
    if (day < today && day.daysTo(today) < 7) {
        return locale.toString(day, QStringLiteral("ddd"));
    }
    if (day.year() == today.year()) {
        return locale.toString(day, QStringLiteral("d MMM"));
    }
    return locale.toString(day, QStringLiteral("d MMM yyyy"));
}

QString NoteStore::editedText(const QDateTime &when)
{
    if (!when.isValid()) {
        return {};
    }
    const QDate day = when.date();
    const QDate today = QDate::currentDate();
    const QLocale locale;
    const QString time = ClockFormat::time(when.time());
    if (day == today) {
        return tr("Edited today at %1").arg(time);
    }
    if (day == today.addDays(-1)) {
        return tr("Edited yesterday at %1").arg(time);
    }
    if (day < today && day.daysTo(today) < 7) {
        return tr("Edited %1 at %2").arg(locale.toString(day, QStringLiteral("dddd")), time);
    }
    if (day.year() == today.year()) {
        return tr("Edited %1 at %2").arg(locale.toString(day, QStringLiteral("d MMMM")), time);
    }
    return tr("Edited %1").arg(locale.toString(day, QStringLiteral("d MMMM yyyy")));
}

void NoteStore::saveCreatedNow()
{
    QJsonObject object;
    for (auto it = m_created.cbegin(); it != m_created.cend(); ++it) {
        object.insert(it.key(), double(it.value()));
    }
    const QString path = createdIndexPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void NoteStore::copyText(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}
