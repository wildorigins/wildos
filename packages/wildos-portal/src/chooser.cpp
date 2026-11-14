#include "chooser.h"

#include "dirmodel.h"

#include <KService>
#include <QDBusArgument>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace
{
// GTK writes the access key as _Open and Qt as &Open; neither is shown.
QString plainLabel(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("[_&](?=\\w)")));
    return text.trimmed();
}

QStringList suffixesOf(const Portal::Filter &filter)
{
    static QMimeDatabase db;
    QStringList list;
    for (const Portal::FilterRule &rule : filter.rules) {
        if (rule.type == 0) {
            if (rule.pattern.startsWith(QStringLiteral("*.")) && rule.pattern != QStringLiteral("*.*")) {
                const QString suffix = rule.pattern.mid(1).toLower();
                if (!list.contains(suffix)) {
                    list.append(suffix);
                }
            }
        } else {
            const QMimeType mime = db.mimeTypeForName(rule.pattern);
            for (const QString &s : mime.suffixes()) {
                const QString suffix = QLatin1Char('.') + s.toLower();
                if (!list.contains(suffix)) {
                    list.append(suffix);
                }
            }
        }
    }
    return list;
}

bool matchesAll(const Portal::Filter &filter)
{
    return filter.rules.isEmpty() || std::any_of(filter.rules.cbegin(), filter.rules.cend(), [](const Portal::FilterRule &r) {
               return r.pattern == QStringLiteral("*") || r.pattern == QStringLiteral("*.*") || r.pattern == QStringLiteral("*/*")
                   || r.pattern == QStringLiteral("application/octet-stream");
           });
}

QString filterLabel(const Portal::Filter &filter)
{
    // Apps call this "All Files", "All files (*)" or, for
    // application/octet-stream, "Unknown"; it reads the same everywhere.
    if (matchesAll(filter)) {
        return QStringLiteral("All files");
    }
    QString name = filter.name.trimmed();
    // Qt keeps the patterns in the name ("Text files (*.txt)"); they are
    // written the Wild OS way below instead.
    const int bracket = name.indexOf(QStringLiteral(" ("));
    if (bracket > 0 && name.endsWith(QLatin1Char(')'))) {
        name = name.left(bracket);
    }
    const QStringList suffixes = suffixesOf(filter);
    if (suffixes.isEmpty()) {
        return name;
    }
    QStringList shown = suffixes.mid(0, 3);
    if (suffixes.size() > 3) {
        shown.append(QStringLiteral("..."));
    }
    return QStringLiteral("%1 (%2)").arg(name.isEmpty() ? QStringLiteral("Files") : name, shown.join(QStringLiteral(", ")));
}

// What the files on offer are, from the types the app lists: "pictures",
// "videos" or "music" when every type is one of those, "files" otherwise.
// It picks the folder a first Open starts in and keeps a picture picker's
// last folder apart from a document picker's.
QString purposeOf(const Portal::Filters &filters)
{
    static QMimeDatabase db;
    QString purpose;
    for (const Portal::Filter &filter : filters) {
        if (matchesAll(filter)) {
            continue;
        }
        for (const Portal::FilterRule &rule : filter.rules) {
            QString mime = rule.type == 1 ? rule.pattern : db.mimeTypeForFile(QStringLiteral("x") + rule.pattern.mid(1), QMimeDatabase::MatchExtension).name();
            const QString kind = mime.startsWith(QLatin1String("image/")) ? QStringLiteral("pictures")
                : mime.startsWith(QLatin1String("video/"))                ? QStringLiteral("videos")
                : mime.startsWith(QLatin1String("audio/"))                ? QStringLiteral("music")
                                                                          : QStringLiteral("files");
            if (!purpose.isEmpty() && purpose != kind) {
                return QStringLiteral("files");
            }
            purpose = kind;
        }
    }
    return purpose.isEmpty() ? QStringLiteral("files") : purpose;
}

// The last folder each app (and any app) opened or saved in, per kind of
// file, so the dialog reopens where the person left off, as on Windows.
QString recentFoldersFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/wildos/file-dialog-folders.ini");
}

// Apps with no folder of their own to suggest send their working folder,
// which is Home or / for anything started from the desktop: that is no
// choice, so the remembered folder wins over it.
bool meaningfulStart(const QUrl &url)
{
    if (!url.isValid() || !url.isLocalFile()) {
        return url.isValid();
    }
    const QString path = QDir::cleanPath(url.toLocalFile());
    return path != QDir::rootPath() && path != QDir::cleanPath(QDir::homePath());
}
}

Chooser::Chooser(const Request &request, QObject *parent)
    : QObject(parent)
    , m_request(request)
    , m_folder(new DirModel(this))
{
    if (m_request.filters.isEmpty() && m_request.mode != Folder && m_request.mode != SaveFiles) {
        m_request.filters.append({QStringLiteral("All files"), {{0, QStringLiteral("*")}}});
    }
    m_currentFilter = m_request.filters.isEmpty() ? -1 : std::clamp(m_request.currentFilter, 0, int(m_request.filters.size()) - 1);
    // Some apps (Spectacle) suggest "name.png" but leave the first type in
    // their list selected, so Save as type said AVIF over a PNG name. The
    // suggested name's extension decides when the selected type lacks it.
    if (m_request.mode == Save && m_currentFilter >= 0) {
        const QString lower = m_request.name.toLower();
        const auto fits = [&lower](const Portal::Filter &f) {
            for (const QString &suffix : suffixesOf(f)) {
                if (lower.endsWith(suffix)) {
                    return true;
                }
            }
            return false;
        };
        if (!fits(m_request.filters.at(m_currentFilter))) {
            for (int i = 0; i < m_request.filters.size(); ++i) {
                if (fits(m_request.filters.at(i))) {
                    m_currentFilter = i;
                    break;
                }
            }
        }
    }
    for (const Portal::Choice &c : std::as_const(m_request.choices)) {
        m_chosen.insert(c.id, c.initial.isEmpty() && c.options.isEmpty() ? QStringLiteral("false") : c.initial);
    }

    m_folder->setMultiple(m_request.multiple && m_request.mode == Open);
    m_folder->setFoldersOnly(m_request.mode == Folder || m_request.mode == SaveFiles);
    // Save lists every file, as the board does: what is already in the
    // folder helps pick a name. Open lists only the chosen type.
    if (m_currentFilter >= 0 && m_request.mode == Open) {
        m_folder->setFilter(m_request.filters.at(m_currentFilter));
    }

    m_purpose = m_request.mode == Folder || m_request.mode == SaveFiles ? QStringLiteral("folders") : purposeOf(m_request.filters);
    m_folder->openUrl(startFolder());
    m_initialName = displayName(m_request.name);

    // Pictures open in Large icons so they can be told apart, everything
    // else in Details, until the person picks a view for that kind.
    const QSettings recent(recentFoldersFile(), QSettings::IniFormat);
    const QString saved = recent.value(QStringLiteral("view/") + m_purpose).toString();
    m_viewMode = saved == QLatin1String("icons") || saved == QLatin1String("details") ? saved
        : m_purpose == QLatin1String("pictures")                                     ? QStringLiteral("icons")
                                                                                     : QStringLiteral("details");
}

void Chooser::setViewMode(const QString &mode)
{
    if (mode == m_viewMode || (mode != QLatin1String("icons") && mode != QLatin1String("details"))) {
        return;
    }
    m_viewMode = mode;
    Q_EMIT viewModeChanged();
    QDir().mkpath(QFileInfo(recentFoldersFile()).absolutePath());
    QSettings recent(recentFoldersFile(), QSettings::IniFormat);
    recent.setValue(QStringLiteral("view/") + m_purpose, mode);
}

QString Chooser::recentKey() const
{
    return (m_request.mode == Save || m_request.mode == SaveFiles ? QStringLiteral("save-") : QStringLiteral("open-")) + m_purpose;
}

// Where the dialog opens: the folder the app asks for when it names one,
// else where this app last opened or saved this kind of file, else where
// any app did, else the folder for the kind (Pictures for pictures), as
// Windows does. Save never starts where it cannot save.
QUrl Chooser::startFolder() const
{
    const bool saving = m_request.mode == Save || m_request.mode == SaveFiles;
    const auto usable = [saving](const QUrl &url) {
        if (!url.isValid()) {
            return false;
        }
        if (!url.isLocalFile()) {
            return true;
        }
        const QFileInfo info(url.toLocalFile());
        return info.isDir() && (!saving || info.isWritable());
    };
    if (meaningfulStart(m_request.folder) && usable(m_request.folder)) {
        return m_request.folder;
    }

    const QSettings recent(recentFoldersFile(), QSettings::IniFormat);
    QStringList groups;
    if (!m_request.appId.isEmpty()) {
        groups << m_request.appId;
    }
    groups << QStringLiteral("any-app");
    for (const QString &group : std::as_const(groups)) {
        const QString path = recent.value(group + QLatin1Char('/') + recentKey()).toString();
        if (!path.isEmpty() && usable(QUrl::fromLocalFile(path))) {
            return QUrl::fromLocalFile(path);
        }
    }

    QStandardPaths::StandardLocation location = QStandardPaths::DocumentsLocation;
    if (m_purpose == QLatin1String("pictures")) {
        location = QStandardPaths::PicturesLocation;
    } else if (m_purpose == QLatin1String("videos")) {
        location = QStandardPaths::MoviesLocation;
    } else if (m_purpose == QLatin1String("music")) {
        location = QStandardPaths::MusicLocation;
    }
    for (const QString &path : {QStandardPaths::writableLocation(location), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)}) {
        if (usable(QUrl::fromLocalFile(path))) {
            return QUrl::fromLocalFile(path);
        }
    }
    // An app started from / and offered as is would open there.
    return usable(m_request.folder) ? m_request.folder : QUrl::fromLocalFile(QDir::homePath());
}

void Chooser::rememberFolder(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !urls.first().isLocalFile()) {
        return;
    }
    // Files are remembered by the folder they are in, and a chosen folder
    // by the one it sits in, so the next pick starts beside it.
    const QString folder = QFileInfo(QDir::cleanPath(urls.first().toLocalFile())).absolutePath();
    // Sandboxed apps get files through the document portal's mount, which
    // means nothing to the next dialog.
    if (folder.startsWith(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/')) || !QFileInfo(folder).isDir()) {
        return;
    }
    QDir().mkpath(QFileInfo(recentFoldersFile()).absolutePath());
    QSettings recent(recentFoldersFile(), QSettings::IniFormat);
    if (!m_request.appId.isEmpty()) {
        recent.setValue(m_request.appId + QLatin1Char('/') + recentKey(), folder);
    }
    recent.setValue(QStringLiteral("any-app/") + recentKey(), folder);
}

QString Chooser::mode() const
{
    switch (m_request.mode) {
    case Save:
        return QStringLiteral("save");
    case SaveFiles:
        return QStringLiteral("saveFiles");
    case Folder:
        return QStringLiteral("folder");
    default:
        return QStringLiteral("open");
    }
}

QString Chooser::title() const
{
    const QString own = m_request.title.trimmed();
    if (!own.isEmpty()) {
        return own;
    }
    switch (m_request.mode) {
    case Save:
        return QStringLiteral("Save as");
    case SaveFiles:
        return QStringLiteral("Choose where to save");
    case Folder:
        return QStringLiteral("Select folder");
    default:
        return QStringLiteral("Open");
    }
}

QString Chooser::acceptLabel() const
{
    const QString own = plainLabel(m_request.acceptLabel);
    if (!own.isEmpty()) {
        return own;
    }
    switch (m_request.mode) {
    case Save:
    case SaveFiles:
        return QStringLiteral("Save");
    case Folder:
        return QStringLiteral("Select folder");
    default:
        return QStringLiteral("Open");
    }
}

QString Chooser::appIcon() const
{
    if (m_request.appId.isEmpty()) {
        return QString();
    }
    const KService::Ptr service = KService::serviceByDesktopName(m_request.appId);
    return service ? service->icon() : QString();
}

QVariantList Chooser::filters() const
{
    QVariantList list;
    for (int i = 0; i < m_request.filters.size(); ++i) {
        list.append(QVariantMap{{QStringLiteral("value"), i}, {QStringLiteral("text"), filterLabel(m_request.filters.at(i))}});
    }
    return list;
}

void Chooser::setCurrentFilter(int index)
{
    if (index == m_currentFilter || index < 0 || index >= m_request.filters.size()) {
        return;
    }
    m_currentFilter = index;
    if (m_request.mode == Open) {
        m_folder->setFilter(m_request.filters.at(index));
    }
    Q_EMIT currentFilterChanged();
}

QVariantList Chooser::choices() const
{
    QVariantList list;
    for (const Portal::Choice &c : m_request.choices) {
        QVariantList options;
        for (const Portal::ChoiceOption &o : c.options) {
            options.append(QVariantMap{{QStringLiteral("value"), o.id}, {QStringLiteral("text"), plainLabel(o.label)}});
        }
        list.append(QVariantMap{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("label"), plainLabel(c.label)},
            {QStringLiteral("options"), options},
            {QStringLiteral("value"), m_chosen.value(c.id)},
        });
    }
    return list;
}

QString Chooser::nameLabel() const
{
    return m_request.mode == Folder || m_request.mode == SaveFiles ? QStringLiteral("Folder") : QStringLiteral("File name");
}

QString Chooser::typeLabel() const
{
    return m_request.mode == Save ? QStringLiteral("Save as type") : QStringLiteral("File type");
}

void Chooser::setChoice(const QString &id, const QString &value)
{
    if (m_chosen.contains(id) && m_chosen.value(id) != value) {
        m_chosen.insert(id, value);
        Q_EMIT choicesChanged();
    }
}

int Chooser::allFilesIndex() const
{
    for (int i = 0; i < m_request.filters.size(); ++i) {
        if (matchesAll(m_request.filters.at(i))) {
            return i;
        }
    }
    return -1;
}

QString Chooser::suffixFor(int filter) const
{
    if (filter < 0 || filter >= m_request.filters.size() || matchesAll(m_request.filters.at(filter))) {
        return QString();
    }
    const QStringList suffixes = suffixesOf(m_request.filters.at(filter));
    return suffixes.isEmpty() ? QString() : suffixes.constFirst();
}

QString Chooser::swapSuffix(const QString &name, int from, int to) const
{
    const QString next = suffixFor(to);
    if (next.isEmpty() || from < 0 || from >= m_request.filters.size()) {
        return name;
    }
    for (const QString &old : suffixesOf(m_request.filters.at(from))) {
        if (name.endsWith(old, Qt::CaseInsensitive) && name.size() > old.size()) {
            return name.left(name.size() - old.size()) + next;
        }
    }
    return name;
}

int Chooser::suffixStart(const QString &name) const
{
    static QMimeDatabase db;
    // The app's own patterns can name extensions the MIME database does
    // not know, so they are tried first.
    if (m_currentFilter >= 0) {
        for (const QString &suffix : suffixesOf(m_request.filters.at(m_currentFilter))) {
            if (name.endsWith(suffix, Qt::CaseInsensitive) && name.size() > suffix.size()) {
                return int(name.size() - suffix.size());
            }
        }
    }
    const QString suffix = db.suffixForFileName(name);
    return suffix.isEmpty() || name.size() <= suffix.size() + 1 ? -1 : int(name.size() - suffix.size() - 1);
}

bool Chooser::hasKnownSuffix(const QString &name) const
{
    static QMimeDatabase db;
    if (m_currentFilter >= 0) {
        for (const QString &suffix : suffixesOf(m_request.filters.at(m_currentFilter))) {
            if (name.endsWith(suffix, Qt::CaseInsensitive) && name.size() > suffix.size()) {
                return true;
            }
        }
    }
    return !db.suffixForFileName(name).isEmpty();
}

QString Chooser::displayName(const QString &fileName) const
{
    if (fileName.isEmpty() || DirModel::showExtensions()) {
        return fileName;
    }
    // The extension the chosen type adds anyway is not shown, as Files
    // hides known extensions.
    const QString suffix = suffixFor(m_currentFilter);
    if (!suffix.isEmpty() && fileName.endsWith(suffix, Qt::CaseInsensitive) && fileName.size() > suffix.size()) {
        return fileName.left(fileName.size() - suffix.size());
    }
    return fileName;
}

QString Chooser::finalName(const QString &typed) const
{
    QString name = typed.trimmed();
    if (m_request.mode != Save || name.isEmpty()) {
        return name;
    }
    const QString suffix = suffixFor(m_currentFilter);
    if (!suffix.isEmpty() && !hasKnownSuffix(name)) {
        name += suffix;
    }
    return name;
}

QString Chooser::nameForSelection() const
{
    const int row = m_folder->currentIndex();
    if (m_request.mode == Folder || m_request.mode == SaveFiles) {
        return row >= 0 && m_folder->isDirAt(row) && m_folder->selectedCount() > 0 ? m_folder->nameAt(row) : QString();
    }
    QStringList names;
    for (int i = 0; i < m_folder->count(); ++i) {
        if (m_folder->data(m_folder->index(i), DirModel::SelectedRole).toBool() && !m_folder->isDirAt(i)) {
            names.append(m_request.mode == Save ? displayName(m_folder->fileNameAt(i)) : m_folder->nameAt(i));
        }
    }
    if (names.size() == 1) {
        return names.constFirst();
    }
    QStringList quoted;
    for (const QString &n : std::as_const(names)) {
        quoted.append(QLatin1Char('"') + n + QLatin1Char('"'));
    }
    return quoted.join(QLatin1Char(' '));
}

QString Chooser::accept(const QString &typedText)
{
    if (m_done) {
        return QString();
    }
    const QString typed = typedText.trimmed();
    DirModel *folder = m_folder;

    // A folder typed or picked is opened rather than chosen, except when a
    // folder is what is being chosen.
    auto openIfFolder = [folder](const QString &text) {
        if (text.isEmpty() || text.contains(QLatin1Char('"'))) {
            return false;
        }
        if (text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1Char('~')) || text.contains(QStringLiteral(":/")) || text == QStringLiteral("..")) {
            return text == QStringLiteral("..") ? (folder->up(), true) : folder->openText(text);
        }
        const int row = folder->rowForFileName(text);
        if (row >= 0 && folder->isDirAt(row)) {
            folder->openUrl(folder->urlAt(row));
            return true;
        }
        return false;
    };

    if (m_request.mode == Folder || m_request.mode == SaveFiles) {
        if (folder->isComputer()) {
            const int row = folder->currentIndex();
            if (row >= 0 && folder->selectedCount() > 0) {
                folder->openUrl(folder->urlAt(row));
            }
            return QString();
        }
        QUrl target = folder->url();
        if (!typed.isEmpty() && typed != folder->title()) {
            const int row = folder->rowForFileName(typed);
            if (row >= 0 && folder->isDirAt(row)) {
                target = folder->urlAt(row);
            } else if (!openIfFolder(typed)) {
                return QStringLiteral("There is no folder called \"%1\" here.").arg(typed);
            } else {
                return QString();
            }
        } else if (folder->selectedCount() == 1 && folder->isDirAt(folder->currentIndex())) {
            target = folder->urlAt(folder->currentIndex());
        }
        if (m_request.mode == Folder) {
            finish({target});
            return QString();
        }
        QList<QUrl> urls;
        QStringList clashes;
        for (const QString &file : std::as_const(m_request.files)) {
            QUrl u = target;
            u.setPath(target.path() + (target.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + file);
            urls.append(u);
            if (u.isLocalFile() && QFileInfo::exists(u.toLocalFile())) {
                clashes.append(file);
            }
        }
        if (!clashes.isEmpty()) {
            m_pending = urls;
            return QStringLiteral("replace:") + (clashes.size() == 1 ? clashes.constFirst() : QString::number(clashes.size()));
        }
        finish(urls);
        return QString();
    }

    if (folder->isComputer() && typed.isEmpty()) {
        const int row = folder->currentIndex();
        if (row >= 0) {
            folder->openUrl(folder->urlAt(row));
        }
        return QString();
    }

    if (m_request.mode == Open) {
        if (openIfFolder(typed)) {
            return QString();
        }
        QList<QUrl> urls;
        if (typed.contains(QLatin1Char('"'))) {
            static const QRegularExpression quoted(QStringLiteral("\"([^\"]+)\""));
            auto it = quoted.globalMatch(typed);
            while (it.hasNext()) {
                const QString name = it.next().captured(1);
                int row = -1;
                for (int i = 0; i < folder->count(); ++i) {
                    if (folder->nameAt(i) == name || folder->fileNameAt(i) == name) {
                        row = i;
                    }
                }
                if (row < 0) {
                    return QStringLiteral("\"%1\" is not in this folder.").arg(name);
                }
                urls.append(folder->urlAt(row));
            }
        } else if (!typed.isEmpty()) {
            int row = folder->rowForFileName(typed);
            for (int i = 0; row < 0 && i < folder->count(); ++i) {
                if (folder->nameAt(i) == typed && !folder->isDirAt(i)) {
                    row = i;
                }
            }
            if (row >= 0) {
                urls.append(folder->urlAt(row));
            } else if (typed.contains(QLatin1Char('/')) && !typed.contains(QStringLiteral(":/"))) {
                // A path typed in full opens that file, as Save does.
                QString path = typed.startsWith(QStringLiteral("~/")) ? QDir::homePath() + typed.mid(1) : typed;
                if (!path.startsWith(QLatin1Char('/')) && folder->url().isLocalFile()) {
                    path = QDir(folder->url().toLocalFile()).absoluteFilePath(path);
                }
                if (!QFileInfo(path).isFile()) {
                    return QStringLiteral("\"%1\" does not exist. Check the name, or pick the file in the list.").arg(typed);
                }
                urls.append(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
            } else {
                const QUrl url = folder->childUrl(typed);
                if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) {
                    return QStringLiteral("\"%1\" is not in this folder. Check the name, or pick the file in the list.").arg(typed);
                }
                urls.append(url);
            }
        } else {
            const QList<QUrl> picked = folder->selectedUrls();
            for (int i = 0; i < folder->count(); ++i) {
                if (picked.contains(folder->urlAt(i))) {
                    if (folder->isDirAt(i)) {
                        if (picked.size() == 1) {
                            folder->openUrl(folder->urlAt(i));
                            return QString();
                        }
                        continue;
                    }
                    urls.append(folder->urlAt(i));
                }
            }
        }
        if (urls.isEmpty()) {
            return QString();
        }
        if (!m_request.multiple && urls.size() > 1) {
            urls = {urls.constFirst()};
        }
        finish(urls);
        return QString();
    }

    // Save.
    if (typed.isEmpty()) {
        const int row = folder->currentIndex();
        if (row >= 0 && folder->isDirAt(row) && folder->selectedCount() > 0) {
            folder->openUrl(folder->urlAt(row));
        }
        return QString();
    }
    if (openIfFolder(typed)) {
        return QString();
    }
    QString name = typed;
    QUrl base = folder->url();
    if (typed.contains(QLatin1Char('/'))) {
        // A path typed in full saves there, as on Windows.
        QString path = typed.startsWith(QStringLiteral("~/")) ? QDir::homePath() + typed.mid(1) : typed;
        if (!path.startsWith(QLatin1Char('/')) && base.isLocalFile()) {
            path = QDir(base.toLocalFile()).absoluteFilePath(path);
        }
        const QFileInfo info(path);
        if (!info.absoluteDir().exists()) {
            return QStringLiteral("The folder \"%1\" does not exist.").arg(info.absolutePath());
        }
        base = QUrl::fromLocalFile(info.absolutePath());
        name = info.fileName();
    }
    static const QRegularExpression bad(QStringLiteral("^\\.{1,2}$"));
    if (bad.match(name).hasMatch() || name.isEmpty()) {
        return QStringLiteral("A file name cannot be just dots. Type a different name.");
    }
    if (base.isLocalFile() && !QFileInfo(base.toLocalFile()).isWritable()) {
        return QStringLiteral("You cannot save in this folder. Pick a folder of your own, such as Documents.");
    }
    if (folder->isComputer()) {
        return QStringLiteral("Pick a folder to save in first.");
    }
    const QString fileName = finalName(name);
    QUrl url = base;
    url.setPath(base.path() + (base.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + fileName);
    const bool exists = url.isLocalFile() ? QFileInfo::exists(url.toLocalFile()) : folder->rowForFileName(fileName) >= 0;
    if (exists) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
            folder->openUrl(url);
            return QString();
        }
        m_pending = {url};
        return QStringLiteral("replace:") + displayName(fileName);
    }
    finish({url});
    return QString();
}

void Chooser::confirmReplace()
{
    if (!m_pending.isEmpty()) {
        finish(m_pending);
    }
}

void Chooser::cancel()
{
    if (m_done) {
        return;
    }
    m_done = true;
    QVariantMap results;
    Q_EMIT finished(1, results);
}

void Chooser::finish(const QList<QUrl> &urls)
{
    if (m_done) {
        return;
    }
    m_done = true;
    rememberFolder(urls);
    QStringList uris;
    for (const QUrl &url : urls) {
        uris.append(url.toString(QUrl::FullyEncoded));
    }
    QVariantMap results;
    results.insert(QStringLiteral("uris"), uris);
    if (!m_request.choices.isEmpty()) {
        Portal::ChosenList chosen;
        for (const Portal::Choice &c : std::as_const(m_request.choices)) {
            chosen.append({c.id, m_chosen.value(c.id)});
        }
        results.insert(QStringLiteral("choices"), QVariant::fromValue(chosen));
    }
    if (m_currentFilter >= 0 && m_currentFilter < m_request.filters.size()) {
        results.insert(QStringLiteral("current_filter"), QVariant::fromValue(m_request.filters.at(m_currentFilter)));
    }
    Q_EMIT finished(0, results);
}
