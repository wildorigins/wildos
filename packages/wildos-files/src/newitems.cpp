#include "fileops.h"

#include <KApplicationTrader>
#include <KDesktopFile>
#include <KConfigGroup>
#include <KIO/CopyJob>
#include <KIO/Global>
#include <KIO/StoredTransferJob>
#include <KService>
#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>

// The New menu works like the one on Windows: Folder, Shortcut, then one
// entry per kind of document there is an app for. The kinds come from the
// user's Templates folder, the templates other apps install for KDE's New
// menu, the LibreOffice documents when LibreOffice is there, and always a
// text document.

namespace
{
struct Template {
    QString id;
    QString text;
    QString icon;
    QString base;
    QString suffix;
    QString source;
};

bool hasApp(const QString &mime)
{
    return !KApplicationTrader::queryByMimeType(mime).isEmpty();
}

bool hasService(const QStringList &names)
{
    for (const QString &name : names) {
        if (KService::serviceByDesktopName(name)) {
            return true;
        }
    }
    return false;
}

QString stripDots(QString text)
{
    text = text.trimmed();
    while (text.endsWith(QLatin1Char('.')) || text.endsWith(QChar(0x2026))) {
        text.chop(1);
    }
    return text.trimmed();
}

const QStringList &commonNouns()
{
    static const QStringList nouns = {QStringLiteral("File"), QStringLiteral("Document"), QStringLiteral("Spreadsheet"), QStringLiteral("Presentation"),
                                      QStringLiteral("Drawing"), QStringLiteral("Image"), QStringLiteral("Text"), QStringLiteral("Page"),
                                      QStringLiteral("Formula"), QStringLiteral("Database"), QStringLiteral("Illustration")};
    return nouns;
}

// "Text document" makes "New text document"; "Write document" keeps its
// capital and makes "New Write document", as Windows names new items.
QString newBase(const QString &text)
{
    const QString first = text.section(QLatin1Char(' '), 0, 0);
    return QStringLiteral("New ") + (commonNouns().contains(first) ? text.at(0).toLower() + text.mid(1) : text);
}

QString sentenceCase(const QString &text)
{
    // KDE's template names are title case ("HTML File"); the common nouns
    // in them go lower case so the menu reads in sentence case, while
    // names like LibreOffice or HTML stay as they are.
    static const QStringList nouns = commonNouns();
    QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 1; i < words.size(); ++i) {
        if (nouns.contains(words.at(i))) {
            words[i] = words.at(i).toLower();
        }
    }
    return words.join(QLatin1Char(' '));
}

// Templates installed for KDE's New menu: a .desktop file with Type=Link
// and URL pointing at the file to copy.
QHash<QString, Template> installedTemplates()
{
    QHash<QString, Template> byMime;
    const QMimeDatabase db;
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("templates"), QStandardPaths::LocateDirectory);
    for (const QString &dir : dirs) {
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const KDesktopFile desktop(dir + QLatin1Char('/') + file);
            if (desktop.readType() != QLatin1String("Link") || desktop.noDisplay()) {
                continue;
            }
            QString source = desktop.readUrl();
            if (source.isEmpty()) {
                continue;
            }
            if (source.startsWith(QLatin1String("file:"))) {
                source = QUrl(source).toLocalFile();
            } else if (QDir::isRelativePath(source)) {
                source = dir + QLatin1Char('/') + source;
            }
            const QFileInfo info(source);
            // Links, folders and programs are Folder and Shortcut's job.
            if (!info.isFile() || info.suffix() == QLatin1String("desktop")) {
                continue;
            }
            const QMimeType mime = db.mimeTypeForFile(info);
            if (byMime.contains(mime.name())) {
                continue;
            }
            Template t;
            t.text = sentenceCase(stripDots(desktop.readName()));
            t.icon = desktop.readIcon().isEmpty() ? mime.iconName() : desktop.readIcon();
            t.source = info.absoluteFilePath();
            const QString suffix = db.suffixForFileName(info.fileName());
            t.suffix = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
            byMime.insert(mime.name(), t);
        }
    }
    return byMime;
}

QList<Template> templates()
{
    const QMimeDatabase db;
    QHash<QString, Template> installed = installedTemplates();
    QList<Template> list;

    auto builtIn = [&](const QString &id, const QString &text, const QString &mimeName, const QString &suffix) {
        const QMimeType mime = db.mimeTypeForName(mimeName);
        Template t;
        t.id = id;
        t.text = text;
        t.icon = mime.iconName();
        t.base = newBase(text);
        t.suffix = suffix;
        // Use the installed template when there is one; otherwise an empty
        // file, which LibreOffice opens as a new document of that kind.
        const auto it = installed.constFind(mimeName);
        if (it != installed.constEnd()) {
            t.source = it->source;
        }
        installed.remove(mimeName);
        list.append(t);
    };

    if (hasService({QStringLiteral("libreoffice-writer"), QStringLiteral("org.libreoffice.LibreOffice.writer")})) {
        // Write and Tables save as Word and Excel files, so new ones start
        // that way too; an empty file opens as a new document of its kind.
        // LibreOffice's own OpenDocument templates are left out, or they
        // would show beside these under LibreOffice's names.
        builtIn(QStringLiteral("writer"), QStringLiteral("Write document"),
                QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"), QStringLiteral(".docx"));
        installed.remove(QStringLiteral("application/vnd.oasis.opendocument.text"));
    }
    if (hasService({QStringLiteral("libreoffice-calc"), QStringLiteral("org.libreoffice.LibreOffice.calc")})) {
        builtIn(QStringLiteral("calc"), QStringLiteral("Tables spreadsheet"),
                QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"), QStringLiteral(".xlsx"));
        installed.remove(QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"));
    }
    if (hasService({QStringLiteral("libreoffice-impress"), QStringLiteral("org.libreoffice.LibreOffice.impress")})) {
        builtIn(QStringLiteral("impress"), QStringLiteral("Impress presentation"), QStringLiteral("application/vnd.oasis.opendocument.presentation"), QStringLiteral(".odp"));
    }
    builtIn(QStringLiteral("text"), QStringLiteral("Text document"), QStringLiteral("text/plain"), QStringLiteral(".txt"));
    // Anything else that would make a plain text file too is already
    // covered by Text document.
    installed.remove(QStringLiteral("application/x-zerosize"));

    QStringList texts;
    for (const Template &t : std::as_const(list)) {
        texts.append(t.text.toLower());
    }

    for (auto it = installed.constBegin(); it != installed.constEnd(); ++it) {
        Template t = it.value();
        if (t.text.isEmpty() || texts.contains(t.text.toLower()) || !hasApp(it.key())) {
            continue;
        }
        t.id = QStringLiteral("kde:") + t.source;
        t.base = newBase(t.text);
        texts.append(t.text.toLower());
        list.append(t);
    }

    // The user's own Templates folder: each file there is a kind of
    // document, named after the file, as in the Files app on GNOME.
    const QString own = QStandardPaths::writableLocation(QStandardPaths::TemplatesLocation);
    if (!own.isEmpty() && QDir::cleanPath(own) != QDir::homePath()) {
        const QFileInfoList files = QDir(own).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &info : files) {
            const QMimeType mime = db.mimeTypeForFile(info);
            const QString suffix = db.suffixForFileName(info.fileName());
            const QString base = suffix.isEmpty() ? info.fileName() : info.fileName().chopped(suffix.size() + 1);
            if (base.isEmpty() || texts.contains(base.toLower()) || !hasApp(mime.name())) {
                continue;
            }
            Template t;
            t.id = QStringLiteral("own:") + info.absoluteFilePath();
            t.text = base;
            t.icon = mime.iconName();
            t.base = base;
            t.suffix = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
            t.source = info.absoluteFilePath();
            texts.append(base.toLower());
            list.append(t);
        }
    }

    QCollator collator(QLocale(QLocale::English, QLocale::UnitedKingdom));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const Template &a, const Template &b) {
        return collator.compare(a.text, b.text) < 0;
    });
    return list;
}

// What a shortcut typed as text points at, or why it cannot.
struct Target {
    QUrl url;
    QString name;
    QString icon;
    QString error;
};

Target resolve(const QString &typed)
{
    Target target;
    QString text = typed.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
        text = text.mid(1, text.size() - 2);
    }
    if (text.isEmpty()) {
        target.error = QStringLiteral("Type where the shortcut should go.");
        return target;
    }
    if (text == QLatin1String("~") || text.startsWith(QLatin1String("~/"))) {
        text = QDir::homePath() + text.mid(1);
    }

    const QUrl asUrl(text, QUrl::StrictMode);
    if (QDir::isAbsolutePath(text) || asUrl.isLocalFile()) {
        const QFileInfo info(asUrl.isLocalFile() ? asUrl.toLocalFile() : text);
        if (!info.exists()) {
            target.error = QStringLiteral("Wild OS cannot find “%1”. Check the spelling and try again.").arg(typed.trimmed());
            return target;
        }
        target.url = QUrl::fromLocalFile(info.absoluteFilePath());
        target.name = info.isDir() ? info.fileName() : QMimeDatabase().suffixForFileName(info.fileName()).isEmpty()
                ? info.fileName()
                : info.fileName().chopped(QMimeDatabase().suffixForFileName(info.fileName()).size() + 1);
        if (target.name.isEmpty()) {
            target.name = info.absoluteFilePath();
        }
        target.icon = KIO::iconNameForUrl(target.url);
        return target;
    }

    QUrl url = asUrl;
    if (url.scheme().isEmpty() || !url.isValid()) {
        // "www.example.com" or "example.com/page" means a web address.
        if (text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char(' '))) {
            url = QUrl(QStringLiteral("https://") + text, QUrl::StrictMode);
        }
    }
    if (!url.isValid() || url.scheme().isEmpty() || (url.host().isEmpty() && url.scheme() != QLatin1String("mailto"))) {
        target.error = QStringLiteral("That is not a place Wild OS knows. Type a folder or file such as ~/Documents, or a web address.");
        return target;
    }
    target.url = url;
    QString host = url.host();
    if (host.startsWith(QLatin1String("www."))) {
        host = host.mid(4);
    }
    target.name = host.isEmpty() ? url.toDisplayString() : host;
    target.icon = url.scheme().startsWith(QLatin1String("http")) ? QStringLiteral("text-html") : KIO::iconNameForUrl(url);
    return target;
}
}

QVariantList FileOps::newFileTypes() const
{
    QVariantList list;
    for (const Template &t : templates()) {
        list.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("text"), t.text}, {QStringLiteral("icon"), t.icon}});
    }
    return list;
}

QUrl FileOps::childUrl(const QUrl &folder, const QString &name) const
{
    QUrl url = folder;
    url.setPath(folder.path() + (folder.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name);
    return url;
}

void FileOps::putNew(const QUrl &folder, const QUrl &url, const QByteArray &data, bool rename)
{
    auto job = KIO::storedPut(data, url, -1, KIO::HideProgressInfo);
    connect(job, &KJob::result, this, [this, job, folder, url, rename] {
        if (!job->error()) {
            Q_EMIT created(folder, {url}, rename);
        }
    });
    watchJob(job, QStringLiteral("make a file"));
}

void FileOps::newFile(const QUrl &folder, const QString &id)
{
    for (const Template &t : templates()) {
        if (t.id != id) {
            continue;
        }
        const QUrl url = childUrl(folder, uniqueName(folder, t.base, t.suffix));
        if (t.source.isEmpty()) {
            putNew(folder, url, QByteArray(), true);
            return;
        }
        auto job = KIO::copyAs(QUrl::fromLocalFile(t.source), url, KIO::HideProgressInfo);
        connect(job, &KJob::result, this, [this, job, folder, url] {
            if (!job->error()) {
                Q_EMIT created(folder, {url}, true);
            }
        });
        watchJob(job, QStringLiteral("make a file"));
        return;
    }
    Q_EMIT failed(QStringLiteral("That kind of document cannot be made any more. The app for it may have been removed."));
}

QVariantMap FileOps::checkShortcut(const QString &target) const
{
    const Target t = resolve(target);
    return {{QStringLiteral("ok"), t.error.isEmpty()}, {QStringLiteral("error"), t.error}, {QStringLiteral("name"), t.name}, {QStringLiteral("icon"), t.icon}};
}

void FileOps::newShortcut(const QUrl &folder, const QString &target, const QString &name)
{
    const Target t = resolve(target);
    if (!t.error.isEmpty()) {
        Q_EMIT failed(t.error);
        return;
    }
    QString label = name.trimmed();
    label.replace(QLatin1Char('/'), QLatin1Char('-'));
    if (label.isEmpty()) {
        label = t.name;
    }
    // A Link entry is what the desktop and every file manager here follow
    // when it is opened, the same job a .lnk does on Windows.
    QByteArray data = "[Desktop Entry]\nType=Link\n";
    data += "Name=" + label.toUtf8() + '\n';
    data += "URL=" + t.url.toString(QUrl::FullyEncoded).toUtf8() + '\n';
    data += "Icon=" + t.icon.toUtf8() + '\n';
    putNew(folder, childUrl(folder, uniqueName(folder, label, QStringLiteral(".desktop"))), data, false);
}
