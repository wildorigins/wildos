#include "reader.h"

#include "format.h"
#include "tools.h"

#include <archive.h>
#include <archive_entry.h>

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtEndian>

#include <cerrno>
#include <cstring>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QCoreApplication>

namespace
{
constexpr size_t blockSize = 256 * 1024;

// Frees the archive however the function returns.
struct Handle {
    archive *a = nullptr;
    explicit Handle(archive *archive)
        : a(archive)
    {
    }
    ~Handle()
    {
        if (a) {
            archive_read_free(a);
        }
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

// Only the formats Archive says it opens, and their usual companions. With
// every format on, libarchive takes some text files for mtree listings.
archive *newReader(const QByteArray &password, bool raw)
{
    archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    if (raw) {
        archive_read_support_format_raw(a);
    } else {
        archive_read_support_format_zip(a);
        archive_read_support_format_7zip(a);
        archive_read_support_format_rar(a);
        archive_read_support_format_rar5(a);
        archive_read_support_format_tar(a);
        archive_read_support_format_cpio(a);
        archive_read_support_format_cab(a);
        archive_read_support_format_lha(a);
        archive_read_support_format_empty(a);
    }
    if (!password.isEmpty()) {
        archive_read_add_passphrase(a, password.constData());
    }
    return a;
}

bool openFile(archive *a, const QString &path)
{
    return archive_read_open_filename(a, QFile::encodeName(path).constData(), 64 * 1024) == ARCHIVE_OK;
}

QString errorOf(archive *a)
{
    const char *e = archive_error_string(a);
    return e ? QString::fromLocal8Bit(e) : QCoreApplication::translate("Reader", "unknown error");
}

bool passwordError(archive *a)
{
    const char *e = archive_error_string(a);
    return e && (std::strstr(e, "assphrase") || std::strstr(e, "assword"));
}

QString text(const char *utf8, const char *local)
{
    if (utf8) {
        return QString::fromUtf8(utf8);
    }
    return local ? QString::fromLocal8Bit(local) : QString();
}

QString storedPath(archive_entry *e)
{
    return text(archive_entry_pathname_utf8(e), archive_entry_pathname(e));
}

QString formatName(archive *a, bool raw)
{
    QString filter;
    switch (archive_filter_code(a, 0)) {
    case ARCHIVE_FILTER_GZIP: filter = QStringLiteral("GZ"); break;
    case ARCHIVE_FILTER_BZIP2: filter = QStringLiteral("BZ2"); break;
    case ARCHIVE_FILTER_XZ: filter = QStringLiteral("XZ"); break;
    case ARCHIVE_FILTER_LZMA: filter = QStringLiteral("LZMA"); break;
    case ARCHIVE_FILTER_ZSTD: filter = QStringLiteral("ZST"); break;
    case ARCHIVE_FILTER_LZIP: filter = QStringLiteral("LZ"); break;
    case ARCHIVE_FILTER_LZ4: filter = QStringLiteral("LZ4"); break;
    case ARCHIVE_FILTER_COMPRESS: filter = QStringLiteral("Z"); break;
    default: break;
    }
    if (raw) {
        return filter;
    }
    QString format;
    switch (archive_format(a) & ARCHIVE_FORMAT_BASE_MASK) {
    case ARCHIVE_FORMAT_ZIP: format = QStringLiteral("ZIP"); break;
    case ARCHIVE_FORMAT_7ZIP: format = QStringLiteral("7z"); break;
    case ARCHIVE_FORMAT_RAR:
    case ARCHIVE_FORMAT_RAR_V5: format = QStringLiteral("RAR"); break;
    case ARCHIVE_FORMAT_TAR: format = QStringLiteral("TAR"); break;
    case ARCHIVE_FORMAT_CPIO: format = QStringLiteral("CPIO"); break;
    case ARCHIVE_FORMAT_CAB: format = QStringLiteral("CAB"); break;
    case ARCHIVE_FORMAT_LHA: format = QStringLiteral("LHA"); break;
    default: format = QCoreApplication::translate("Reader", "Archive"); break;
    }
    return filter.isEmpty() ? format : format + QLatin1Char('.') + filter;
}

quint16 u16(const char *p)
{
    return qFromLittleEndian<quint16>(p);
}
quint32 u32(const char *p)
{
    return qFromLittleEndian<quint32>(p);
}
quint64 u64(const char *p)
{
    return qFromLittleEndian<quint64>(p);
}

// The packed size of each file in a ZIP, from its central directory, in
// the order of the files in the archive, which is the order libarchive
// reads them in. libarchive has no packed size per entry.
QList<qint64> zipPacked(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const qint64 size = file.size();
    const qint64 tailSize = qMin<qint64>(size, 65535 + 22);
    file.seek(size - tailSize);
    const QByteArray tail = file.read(tailSize);
    qint64 at = -1;
    for (qint64 i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail.constData() + i) == 0x06054b50) {
            at = i;
            break;
        }
    }
    if (at < 0) {
        return {};
    }
    quint64 count = u16(tail.constData() + at + 10);
    quint64 cdSize = u32(tail.constData() + at + 12);
    quint64 cdOffset = u32(tail.constData() + at + 16);
    if (cdOffset == 0xffffffffu || count == 0xffffu || cdSize == 0xffffffffu) {
        if (at < 20 || u32(tail.constData() + at - 20) != 0x07064b50) {
            return {};
        }
        file.seek(qint64(u64(tail.constData() + at - 20 + 8)));
        const QByteArray end = file.read(56);
        if (end.size() < 56 || u32(end.constData()) != 0x06064b50) {
            return {};
        }
        count = u64(end.constData() + 32);
        cdSize = u64(end.constData() + 40);
        cdOffset = u64(end.constData() + 48);
    }
    if (cdSize > 512u * 1024 * 1024 || qint64(cdOffset + cdSize) > size) {
        return {};
    }
    file.seek(qint64(cdOffset));
    const QByteArray cd = file.read(qint64(cdSize));
    QList<QPair<quint64, qint64>> found;
    qsizetype p = 0;
    for (quint64 n = 0; n < count && p + 46 <= cd.size(); ++n) {
        const char *h = cd.constData() + p;
        if (u32(h) != 0x02014b50) {
            return {};
        }
        quint64 packed = u32(h + 20);
        const quint64 unpacked = u32(h + 24);
        const int nameLength = u16(h + 28);
        const int extraLength = u16(h + 30);
        const int commentLength = u16(h + 32);
        quint64 offset = u32(h + 42);
        if (p + 46 + nameLength + extraLength > cd.size()) {
            return {};
        }
        // ZIP64 keeps the large values in an extra field, in this order,
        // each only when its short field is full.
        const char *extra = h + 46 + nameLength;
        for (int e = 0; e + 4 <= extraLength;) {
            const int id = u16(extra + e);
            const int length = u16(extra + e + 2);
            if (id == 0x0001) {
                int f = e + 4;
                if (unpacked == 0xffffffffu && f + 8 <= e + 4 + length) {
                    f += 8;
                }
                if (packed == 0xffffffffu && f + 8 <= e + 4 + length) {
                    packed = u64(extra + f);
                    f += 8;
                }
                if (offset == 0xffffffffu && f + 8 <= e + 4 + length) {
                    offset = u64(extra + f);
                }
            }
            e += 4 + length;
        }
        found.append({offset, qint64(packed)});
        p += 46 + nameLength + extraLength + commentLength;
    }
    std::sort(found.begin(), found.end());
    QList<qint64> sizes;
    for (const auto &f : found) {
        sizes.append(f.second);
    }
    return sizes;
}

// Folders of `steps` under `root`, made as needed. A step that is a link
// is refused, or an entry could write through a link an earlier entry
// made.
enum class Made { Ok, Link, Blocked };
Made makeDirs(const QString &root, const QStringList &steps)
{
    QString at = root;
    for (const QString &step : steps) {
        at += QLatin1Char('/') + step;
        const QByteArray name = QFile::encodeName(at);
        struct stat st;
        if (::lstat(name.constData(), &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                return Made::Link;
            }
            if (!S_ISDIR(st.st_mode)) {
                return Made::Blocked;
            }
            continue;
        }
        if (::mkdir(name.constData(), 0755) != 0 && errno != EEXIST) {
            return Made::Blocked;
        }
    }
    return Made::Ok;
}

void setTimes(const QString &path, const QDateTime &modified)
{
    if (!modified.isValid()) {
        return;
    }
    struct timespec times[2];
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = modified.toSecsSinceEpoch();
    times[1].tv_nsec = 0;
    ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, AT_SYMLINK_NOFOLLOW);
}

QString numbered(const QString &name, int n)
{
    const QFileInfo info(name);
    const QString suffix = info.suffix();
    const QString base = suffix.isEmpty() ? name : name.left(name.size() - suffix.size() - 1);
    return suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(n) : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
}

Reader::PasswordState passwordState(const QByteArray &password)
{
    return password.isEmpty() ? Reader::PasswordState::Needed : Reader::PasswordState::Wrong;
}

// Reads the current entry's data to the end into `out` (when given),
// counting it in `progress`. False when the data could not be read.
bool readData(archive *a, QIODevice *out, Progress *progress, QString *error, bool *cancelled, bool *writeFailed = nullptr)
{
    static thread_local QByteArray buffer(blockSize, Qt::Uninitialized);
    for (;;) {
        if (progress && progress->cancel) {
            *cancelled = true;
            return false;
        }
        const la_ssize_t n = archive_read_data(a, buffer.data(), blockSize);
        if (n == 0) {
            return true;
        }
        if (n < 0) {
            *error = errorOf(a);
            return false;
        }
        if (out && out->write(buffer.constData(), n) != n) {
            *error = out->errorString();
            if (writeFailed) {
                *writeFailed = true;
            }
            return false;
        }
        if (progress) {
            progress->done += n;
        }
    }
}
}

namespace Reader
{

bool safePath(const QString &stored, QString *clean)
{
    QString p = stored;
    // Archives made on Windows sometimes separate folders with
    // backslashes; ..\ must not get past the check either.
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    bool safe = !p.startsWith(QLatin1Char('/'));
    if (p.size() >= 2 && p.at(1) == QLatin1Char(':') && p.at(0).isLetter()) {
        safe = false;
    }
    QStringList steps;
    for (const QString &step : p.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (step == QLatin1String(".")) {
            continue;
        }
        if (step == QLatin1String("..")) {
            safe = false;
        }
        steps.append(step);
    }
    *clean = steps.join(QLatin1Char('/'));
    return safe;
}

namespace
{
// The tree of entries, built as they come in archive order.
struct Builder {
    Listing &listing;
    QHash<QString, int> dirs;

    int add(Entry entry)
    {
        const int index = int(listing.entries.size());
        if (entry.parent >= 0) {
            listing.entries[entry.parent].children.append(index);
        } else {
            listing.roots.append(index);
        }
        listing.entries.append(std::move(entry));
        return index;
    }

    int ensureDir(const QString &dirPath)
    {
        if (dirPath.isEmpty()) {
            return -1;
        }
        const auto it = dirs.constFind(dirPath);
        if (it != dirs.constEnd()) {
            return it.value();
        }
        const int slash = int(dirPath.lastIndexOf(QLatin1Char('/')));
        Entry dir;
        dir.parent = ensureDir(slash < 0 ? QString() : dirPath.left(slash));
        dir.path = dirPath;
        dir.name = dirPath.mid(slash + 1);
        dir.dir = true;
        const int index = add(dir);
        dirs.insert(dirPath, index);
        return index;
    }

    // `entry` comes with its order, type, sizes, time and link.
    void place(Entry entry, const QString &stored)
    {
        QString clean;
        const bool safe = safePath(stored, &clean);
        if (safe && clean.isEmpty()) {
            listing.byOrder.append(-1);
            return;
        }
        if (!safe) {
            entry.unsafe = true;
            entry.path = stored;
            entry.name = stored;
            listing.byOrder.append(add(entry));
            return;
        }
        const int slash = int(clean.lastIndexOf(QLatin1Char('/')));
        entry.path = clean;
        entry.name = clean.mid(slash + 1);
        if (entry.dir) {
            const auto it = dirs.constFind(clean);
            if (it != dirs.constEnd()) {
                Entry &known = listing.entries[it.value()];
                known.order = entry.order;
                known.modified = entry.modified;
                known.toolPath = entry.toolPath;
                listing.byOrder.append(it.value());
                return;
            }
        }
        entry.parent = ensureDir(slash < 0 ? QString() : clean.left(slash));
        entry.image = !entry.dir && !entry.symlink && Format::isImage(entry.name);
        const bool isDir = entry.dir;
        const int index = add(entry);
        if (isDir) {
            dirs.insert(clean, index);
        }
        listing.byOrder.append(index);
    }
};

// Children always come after their folder, so one pass from the end adds
// every file into each folder above it.
void addUp(Listing &listing)
{
    for (qsizetype i = listing.entries.size() - 1; i >= 0; --i) {
        Entry &entry = listing.entries[i];
        if (!entry.dir) {
            entry.fileCount = 1;
            listing.totalSize += entry.size;
            ++listing.fileCount;
        }
        if (entry.parent >= 0) {
            Entry &parent = listing.entries[entry.parent];
            parent.size += entry.size;
            parent.fileCount += entry.fileCount;
            if (entry.packed >= 0 && listing.packedPerFile) {
                parent.packed = qMax<qint64>(parent.packed, 0) + entry.packed;
            }
        }
    }
    for (Entry &entry : listing.entries) {
        if (entry.dir) {
            entry.packed = -1;
        }
    }
}

Listing listLibarchive(const QString &path, Progress *progress)
{
    Listing listing;
    const QFileInfo info(path);
    listing.packedSize = info.size();
    progress->total = info.size();

    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool raw = attempt == 1;
        listing = Listing();
        listing.packedSize = info.size();
        listing.raw = raw;
        Handle h(newReader({}, raw));
        if (!openFile(h.a, path)) {
            // libarchive picks the format as it opens the file; a lone
            // compressed file has none, so it is read as one raw stream.
            if (!raw && errorOf(h.a).contains(QLatin1String("Unrecognized archive format"))) {
                continue;
            }
            listing.error = errorOf(h.a);
            return listing;
        }
        Builder builder{listing, {}};
        int order = 0;
        bool retry = false;

        archive_entry *e = nullptr;
        for (;;) {
            if (progress->cancel) {
                listing.error = QStringLiteral("cancelled");
                return listing;
            }
            const int r = archive_read_next_header(h.a, &e);
            if (r == ARCHIVE_EOF) {
                break;
            }
            if (r == ARCHIVE_RETRY) {
                continue;
            }
            if (r < ARCHIVE_WARN) {
                if (!raw && order == 0 && QString::fromLocal8Bit(archive_error_string(h.a)).contains(QLatin1String("Unrecognized archive format"))) {
                    retry = true;
                    break;
                }
                const QString message = errorOf(h.a);
                if (message.contains(QLatin1String("ncrypt")) || passwordError(h.a)) {
                    listing.lockedHeaders = true;
                }
                listing.error = message;
                listing.format = formatName(h.a, raw);
                return listing;
            }
            if (order == 0) {
                if (raw && archive_filter_count(h.a) <= 1) {
                    listing.error = QCoreApplication::translate("Reader", "This file isn't an archive.");
                    return listing;
                }
                listing.format = formatName(h.a, raw);
            }
            progress->done = archive_filter_bytes(h.a, -1);

            const QString stored = raw ? Format::archiveBaseName(info.fileName()) : storedPath(e);
            const mode_t type = archive_entry_filetype(e);
            Entry entry;
            entry.order = order++;
            entry.dir = type == AE_IFDIR || stored.endsWith(QLatin1Char('/'));
            entry.size = !entry.dir && archive_entry_size_is_set(e) ? archive_entry_size(e) : 0;
            if (archive_entry_mtime_is_set(e)) {
                entry.modified = QDateTime::fromSecsSinceEpoch(archive_entry_mtime(e));
            }
            entry.symlink = type == AE_IFLNK;
            if (entry.symlink) {
                entry.linkTarget = text(archive_entry_symlink_utf8(e), archive_entry_symlink(e));
            }
            entry.encrypted = archive_entry_is_data_encrypted(e);
            listing.encrypted = listing.encrypted || entry.encrypted;

            if (raw) {
                // A lone compressed file says nothing of its size; reading
                // it through is the only way to know.
                QString error;
                bool cancelled = false;
                const qint64 before = progress->done;
                Progress counting;
                readData(h.a, nullptr, &counting, &error, &cancelled);
                entry.size = counting.done;
                progress->done = before;
                entry.modified = info.lastModified();
            }
            builder.place(entry, stored);
        }
        if (retry) {
            continue;
        }

        const int base = archive_format(h.a) & ARCHIVE_FORMAT_BASE_MASK;
        if (base == ARCHIVE_FORMAT_ZIP) {
            const QList<qint64> packed = zipPacked(path);
            if (packed.size() == listing.byOrder.size()) {
                for (int i = 0; i < packed.size(); ++i) {
                    if (listing.byOrder[i] >= 0) {
                        listing.entries[listing.byOrder[i]].packed = packed[i];
                    }
                }
                listing.packedPerFile = true;
            }
        } else if (raw && !listing.entries.isEmpty()) {
            listing.entries.first().packed = listing.packedSize;
            listing.packedPerFile = true;
        }
        addUp(listing);
        progress->done = progress->total.load();
        return listing;
    }
    listing.error = QCoreApplication::translate("Reader", "This file isn't an archive Archive can open.");
    return listing;
}

enum class Magic { Other, SevenZip, Rar4, Rar5 };

Magic magic(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Magic::Other;
    }
    const QByteArray head = file.read(8);
    static const QByteArray sevenZip("7z\xbc\xaf\x27\x1c", 6);
    static const QByteArray rar5("Rar!\x1a\x07\x01\x00", 8);
    static const QByteArray rar4("Rar!\x1a\x07\x00", 7);
    if (head.startsWith(sevenZip)) {
        return Magic::SevenZip;
    }
    if (head.startsWith(rar5)) {
        return Magic::Rar5;
    }
    return head.startsWith(rar4) ? Magic::Rar4 : Magic::Other;
}

// Whether libarchive can decompress the first file it would need to. It
// lists every RAR but cannot unpack some of them (old methods, some RAR 5
// features), and that is better found now than when every file fails.
bool decompresses(const QString &path)
{
    Handle h(newReader({}, false));
    if (!openFile(h.a, path)) {
        return false;
    }
    archive_entry *e = nullptr;
    while (archive_read_next_header(h.a, &e) >= ARCHIVE_WARN) {
        if (archive_entry_is_data_encrypted(e) || archive_entry_filetype(e) != AE_IFREG || archive_entry_size(e) == 0) {
            continue;
        }
        char buffer[16384];
        return archive_read_data(h.a, buffer, sizeof buffer) >= 0;
    }
    return true;
}

Listing lockedListing(const QString &tool, const QString &format, qint64 size)
{
    Listing listing;
    listing.tool = tool;
    listing.format = format;
    listing.packedSize = size;
    listing.encrypted = true;
    listing.lockedHeaders = true;
    return listing;
}

Listing fromTool(const Tools::Contents &contents, const QString &tool, const QString &format, qint64 size)
{
    Listing listing;
    listing.tool = tool;
    listing.format = format;
    listing.packedSize = size;
    Builder builder{listing, {}};
    bool packedPerFile = true;
    for (int i = 0; i < contents.items.size(); ++i) {
        const Tools::Item &item = contents.items[i];
        if (item.path.isEmpty()) {
            listing.byOrder.append(-1);
            continue;
        }
        Entry entry;
        entry.order = i;
        entry.dir = item.dir;
        entry.size = item.dir ? 0 : item.size;
        entry.packed = item.dir ? -1 : item.packed;
        entry.modified = item.modified;
        entry.symlink = item.symlink && !item.dir;
        entry.linkTarget = item.linkTarget;
        entry.encrypted = item.encrypted;
        entry.toolPath = item.path;
        entry.executable = item.executable;
        listing.encrypted = listing.encrypted || item.encrypted;
        packedPerFile = packedPerFile && (item.dir || item.packed >= 0);
        builder.place(entry, item.path);
    }

    // A path under a file or a link is never extracted: a tool could
    // write it through a link it made a moment before.
    QSet<QString> notFolders;
    for (const Entry &e : std::as_const(listing.entries)) {
        if (!e.dir && !e.unsafe) {
            notFolders.insert(e.path);
        }
    }
    for (Entry &e : listing.entries) {
        for (qsizetype at = e.path.indexOf(QLatin1Char('/')); !e.unsafe && at >= 0; at = e.path.indexOf(QLatin1Char('/'), at + 1)) {
            e.unsafe = notFolders.contains(e.path.left(at));
        }
    }

    listing.packedPerFile = packedPerFile;
    if (!packedPerFile) {
        for (Entry &e : listing.entries) {
            e.packed = -1;
        }
    }
    addUp(listing);
    return listing;
}

QString missingTool(const QString &program)
{
    return program == QLatin1String("7z") ? QCoreApplication::translate("Reader", "Archive needs 7-Zip (the 7zip package) for this archive, and it isn't installed.")
                                          : QCoreApplication::translate("Reader", "Archive needs The Unarchiver (the unar package) for this archive, and it isn't installed.");
}

const QString rar4Locked = QStringLiteral(
    "It's protected with the older RAR 4 encryption, which Archive can't unlock. Ask whoever sent it for a 7z or ZIP copy.");

Listing with7z(const QString &path, const QByteArray &password, Listing fallback, const QString &format, Progress *progress)
{
    if (!Tools::available(QStringLiteral("7z"))) {
        fallback.error = missingTool(QStringLiteral("7z"));
        fallback.cantDecrypt = fallback.encrypted;
        return fallback;
    }
    const Tools::Contents contents = Tools::list7z(path, password, progress);
    if (contents.cancelled) {
        Listing cancelled;
        cancelled.error = QStringLiteral("cancelled");
        return cancelled;
    }
    if (contents.needsPassword) {
        return lockedListing(QStringLiteral("7z"), format, fallback.packedSize);
    }
    if (!contents.ok) {
        if (fallback.error.isEmpty()) {
            fallback.error = contents.error;
        }
        return fallback;
    }
    return fromTool(contents, QStringLiteral("7z"), format, fallback.packedSize);
}

Listing withUnar(const QString &path, const QByteArray &password, Listing fallback, Magic kind, Progress *progress)
{
    if (!Tools::available(QStringLiteral("unar")) || !Tools::available(QStringLiteral("lsar"))) {
        fallback.error = missingTool(QStringLiteral("unar"));
        fallback.cantDecrypt = fallback.encrypted;
        return fallback;
    }
    const QString format = QStringLiteral("RAR");
    Tools::Contents contents = Tools::listLsar(path, progress);
    if (contents.needsPassword) {
        if (kind == Magic::Rar4) {
            Listing locked;
            locked.format = format;
            locked.error = rar4Locked;
            return locked;
        }
        if (password.isEmpty()) {
            return lockedListing(QStringLiteral("unar"), format, fallback.packedSize);
        }
        // lsar takes a password only on its command line, so the hidden
        // names come from 7-Zip, which reads RAR headers (but cannot
        // decompress RAR without the non-free plugin) and lists them in
        // the same order. unar still does the unpacking.
        if (!Tools::available(QStringLiteral("7z"))) {
            fallback.error = missingTool(QStringLiteral("7z"));
            return fallback;
        }
        contents = Tools::list7z(path, password, progress);
        if (contents.needsPassword) {
            return lockedListing(QStringLiteral("unar"), format, fallback.packedSize);
        }
    }
    if (contents.cancelled) {
        Listing cancelled;
        cancelled.error = QStringLiteral("cancelled");
        return cancelled;
    }
    if (!contents.ok) {
        if (fallback.error.isEmpty()) {
            fallback.error = contents.error;
        }
        fallback.cantDecrypt = fallback.encrypted;
        return fallback;
    }
    Listing listing = fromTool(contents, QStringLiteral("unar"), format, fallback.packedSize);
    // lsar says "RAR" or "RAR 5", 7-Zip "Rar" or "Rar5".
    const bool rar4 = kind == Magic::Rar4 || contents.type.compare(QLatin1String("RAR"), Qt::CaseInsensitive) == 0;
    listing.cantDecrypt = rar4 && listing.encrypted;
    return listing;
}

QString privateStaging()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base);
    return Tools::makeStaging(base);
}

// Removed however the function returns, cancel included.
struct Staging {
    QString path;
    explicit Staging(const QString &p)
        : path(p)
    {
    }
    ~Staging()
    {
        Tools::removeTree(path);
    }
    Staging(const Staging &) = delete;
    Staging &operator=(const Staging &) = delete;
    QString into() const
    {
        return path + QStringLiteral("/x");
    }
};

// Unpacks the files at `orders` into staging.into(), counting progress
// towards `total`.
Tools::Outcome unpack(const QString &archive, const Listing &listing, const QByteArray &password, const QList<int> &orders, const Staging &staging,
                      qint64 total, Progress *progress)
{
    ::mkdir(QFile::encodeName(staging.into()).constData(), 0700);
    Tools::Job job;
    job.password = password;
    if (listing.tool == QLatin1String("7z")) {
        QFile names(staging.path + QStringLiteral("/names"));
        if (names.open(QIODevice::WriteOnly)) {
            for (int order : orders) {
                names.write(listing.entries[listing.byOrder[order]].toolPath.toUtf8() + '\n');
            }
            names.close();
        }
        job.program = QStringLiteral("7z");
        job.arguments = {QStringLiteral("x"),         QStringLiteral("-y"),       QStringLiteral("-bso0"),
                         QStringLiteral("-bse2"),     QStringLiteral("-bsp1"),    QStringLiteral("-spd"),
                         QStringLiteral("-sccUTF-8"), QStringLiteral("-scsUTF-8"), QStringLiteral("-o") + staging.into(),
                         archive,                     QStringLiteral("-i@") + names.fileName()};
        job.line = [progress, total](const QByteArray &line) {
            // " 45% 3 - name"
            const QByteArray t = line.trimmed();
            const qsizetype percent = t.indexOf('%');
            bool ok = false;
            const int value = percent > 0 ? t.left(percent).toInt(&ok) : 0;
            if (ok) {
                progress->done = total * qBound(0, value, 100) / 100;
            }
        };
    } else {
        job.program = QStringLiteral("unar");
        job.terminal = true;
        // -D: no folder of its own; -nr: not into archives inside it;
        // -k skip: no Mac resource forks; -s: skip, never ask, if a name
        // is there twice.
        job.arguments = {QStringLiteral("-o"), staging.into(),       QStringLiteral("-D"), QStringLiteral("-nr"), QStringLiteral("-k"),
                         QStringLiteral("skip"), QStringLiteral("-s"), QStringLiteral("-i"), archive};
        auto sizes = std::make_shared<QList<qint64>>();
        for (int order : orders) {
            job.arguments.append(QString::number(order));
            sizes->append(listing.entries[listing.byOrder[order]].size);
        }
        auto finished = std::make_shared<int>(0);
        job.line = [progress, sizes, finished](const QByteArray &line) {
            // unar reports each entry as "  name  (size)... OK." or
            // "... Failed! (why)", in the order asked.
            if ((line.endsWith("OK.") || line.contains("Failed!")) && *finished < sizes->size()) {
                progress->done += sizes->at((*finished)++);
            }
        };
    }
    return Tools::run(job, progress);
}

// The unpacked file for `entry` in staging, when it is a whole plain file.
QString unpacked(const Staging &staging, const Entry &entry)
{
    const QString file = staging.into() + QLatin1Char('/') + entry.path;
    struct stat st;
    if (::lstat(QFile::encodeName(file).constData(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != entry.size) {
        return QString();
    }
    return file;
}

// The text of a 7z symlink entry: 7-Zip lists links in 7z archives as
// small files that hold where they point.
QString linkTargetOf(const QString &archive, const Entry &entry, const QByteArray &password, Progress *progress)
{
    QByteArray target;
    Tools::Job job;
    job.program = QStringLiteral("7z");
    job.arguments = {QStringLiteral("x"), QStringLiteral("-so"), QStringLiteral("-bsp0"), QStringLiteral("-spd"), archive, QStringLiteral("-i!") + entry.toolPath};
    job.password = password;
    job.data = [&target](const char *data, qsizetype size) {
        target.append(data, size);
        return target.size() <= 4096;
    };
    const Tools::Outcome outcome = Tools::run(job, progress);
    if (outcome.exitCode != 0 || target.size() > 4096) {
        return QString();
    }
    return QString::fromUtf8(target);
}

Reader::PasswordState checkLibarchive(const QString &path, const Listing &listing, const QByteArray &password)
{
    Handle h(newReader(password, listing.raw));
    if (!openFile(h.a, path)) {
        return Reader::PasswordState::Fine;
    }
    archive_entry *e = nullptr;
    while (archive_read_next_header(h.a, &e) >= ARCHIVE_WARN) {
        if (!archive_entry_is_data_encrypted(e) || archive_entry_filetype(e) != AE_IFREG || archive_entry_size(e) == 0) {
            continue;
        }
        char buffer[16384];
        if (archive_read_data(h.a, buffer, sizeof buffer) < 0) {
            return passwordError(h.a) ? passwordState(password) : Reader::PasswordState::Fine;
        }
        return Reader::PasswordState::Fine;
    }
    return Reader::PasswordState::Fine;
}
}

Listing list(const QString &path, const QByteArray &password, Progress *progress)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        Listing listing;
        listing.error = QCoreApplication::translate("Reader", "This file isn't there any more.");
        return listing;
    }
    if (!info.isReadable()) {
        Listing listing;
        listing.error = QCoreApplication::translate("Reader", "You don't have permission to open this file.");
        return listing;
    }
    Listing listing = listLibarchive(path, progress);
    if (listing.error == QLatin1String("cancelled")) {
        return listing;
    }
    // libarchive stays first: the tools only take over what it cannot
    // decrypt or read.
    const Magic kind = magic(path);
    const bool failed = !listing.error.isEmpty();
    if (kind == Magic::SevenZip || listing.format == QLatin1String("7z")) {
        return failed || listing.encrypted ? with7z(path, password, listing, QStringLiteral("7z"), progress) : listing;
    }
    if (kind == Magic::Rar4 || kind == Magic::Rar5 || listing.format == QLatin1String("RAR")) {
        return failed || listing.encrypted || !decompresses(path) ? withUnar(path, password, listing, kind, progress) : listing;
    }
    if (listing.format == QLatin1String("ZIP") && failed && listing.entries.isEmpty() && Tools::available(QStringLiteral("7z"))) {
        Listing other = with7z(path, password, listing, QStringLiteral("ZIP"), progress);
        return other.error.isEmpty() || other.lockedHeaders ? other : listing;
    }
    return listing;
}

Unlock unlock(const QString &path, const Listing &listing, const QByteArray &password, Progress *progress)
{
    Unlock result;
    if (listing.tool.isEmpty()) {
        result.state = checkLibarchive(path, listing, password);
        return result;
    }
    if (listing.lockedHeaders) {
        Listing named = list(path, password, progress);
        if (named.error == QLatin1String("cancelled")) {
            result.state = PasswordState::Needed;
            return result;
        }
        if (named.lockedHeaders) {
            result.state = PasswordState::Wrong;
            return result;
        }
        // 7z and RAR use the one password for the names and the files.
        result.relisted = true;
        result.listing = std::move(named);
        return result;
    }

    // A small protected file, tried with the password.
    const Entry *pick = nullptr;
    for (const Entry &e : listing.entries) {
        if (e.encrypted && !e.dir && !e.symlink && !e.unsafe && e.order >= 0 && e.size > 0 && (!pick || e.size < pick->size)) {
            pick = &e;
        }
    }
    if (!pick) {
        return result;
    }
    if (listing.tool == QLatin1String("7z")) {
        Tools::Job job;
        job.program = QStringLiteral("7z");
        job.arguments = {QStringLiteral("t"), QStringLiteral("-bso0"), QStringLiteral("-bsp0"), QStringLiteral("-spd"), path, QStringLiteral("-i!") + pick->toolPath};
        job.password = password;
        result.state = Tools::passwordOutcome(Tools::run(job, progress), password);
        return result;
    }
    const Staging staging(privateStaging());
    if (staging.path.isEmpty()) {
        return result;
    }
    const Tools::Outcome outcome = unpack(path, listing, password, {pick->order}, staging, pick->size, progress);
    result.state = Tools::passwordOutcome(outcome, password);
    return result;
}

namespace
{
QList<int> sortedKeys(const QHash<int, QString> &targets)
{
    QList<int> orders = targets.keys();
    std::sort(orders.begin(), orders.end());
    return orders;
}

QImage scaledImage(QByteArray &data, QSize box)
{
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid()) {
        const QSize scaled = size.scaled(box, Qt::KeepAspectRatioByExpanding);
        if (scaled.width() < size.width()) {
            reader.setScaledSize(scaled);
        }
    }
    return reader.read();
}

// The tool only ever unpacks plain files, into a private folder inside
// the destination. Archive then checks each one and moves it into place
// with the same rules as for what libarchive reads, and makes the folders
// and links itself, so the tool never writes where it shouldn't, and a
// cancel or a failure leaves no half file.
Result extractWithTool(const ExtractOptions &options, const Listing &listing, const QHash<int, QString> &targets, const QString &root, qint64 total,
                       Progress *progress, Result result)
{
    const QList<int> orders = sortedKeys(targets);
    QList<int> files;
    for (int order : orders) {
        const Entry &entry = listing.entries[listing.byOrder[order]];
        if (!entry.dir && !entry.symlink) {
            files.append(order);
        }
    }
    const Staging staging(Tools::makeStaging(root));
    if (staging.path.isEmpty()) {
        result.ok = false;
        result.error = QCoreApplication::translate("Reader", "Couldn't write in %1: %2").arg(Format::folder(root), QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }
    if (!files.isEmpty()) {
        const Tools::Outcome outcome = unpack(options.archive, listing, options.password, files, staging, total, progress);
        if (outcome.cancelled) {
            result.cancelled = true;
            return result;
        }
        if (!outcome.started) {
            result.ok = false;
            result.error = QString::fromUtf8(outcome.errors);
            return result;
        }
        const PasswordState state = Tools::passwordOutcome(outcome, options.password);
        if (state != PasswordState::Fine) {
            result.ok = false;
            result.password = state;
            return result;
        }
    }
    progress->done = progress->total.load();

    for (int order : orders) {
        if (progress->cancel) {
            result.cancelled = true;
            return result;
        }
        const Entry &entry = listing.entries[listing.byOrder[order]];
        const QString rel = targets.value(order);
        QStringList steps = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const QString target = root + QLatin1Char('/') + rel;
        const QString leaf = steps.isEmpty() ? QString() : steps.takeLast();
        if (leaf.isEmpty()) {
            continue;
        }
        const Made made = makeDirs(root, entry.dir ? steps + QStringList{leaf} : steps);
        if (made == Made::Link) {
            result.unsafe.append(entry.path);
            continue;
        }
        if (made == Made::Blocked) {
            result.failed.append(QCoreApplication::translate("Reader", "%1: a file is in the way of its folder").arg(entry.path));
            continue;
        }
        if (entry.dir) {
            continue;
        }
        progress->setCurrent(entry.name);

        const QByteArray targetName = QFile::encodeName(target);
        struct stat st;
        const bool exists = ::lstat(targetName.constData(), &st) == 0;
        if (exists && S_ISDIR(st.st_mode)) {
            result.failed.append(QCoreApplication::translate("Reader", "%1: a folder has the same name").arg(entry.path));
            continue;
        }
        if (exists && !options.replace) {
            ++result.kept;
            continue;
        }
        if (exists && S_ISLNK(st.st_mode)) {
            ::unlink(targetName.constData());
        }

        if (entry.symlink) {
            QString linkTarget = entry.linkTarget;
            if (linkTarget.isEmpty() && listing.tool == QLatin1String("7z")) {
                linkTarget = linkTargetOf(options.archive, entry, options.password, progress);
            }
            const QString dirRel = steps.join(QLatin1Char('/'));
            const QString resolved = QDir::cleanPath(dirRel.isEmpty() ? linkTarget : dirRel + QLatin1Char('/') + linkTarget);
            if (linkTarget.isEmpty() || linkTarget.startsWith(QLatin1Char('/')) || resolved == QLatin1String("..")
                || resolved.startsWith(QLatin1String("../"))) {
                result.unsafe.append(entry.path);
                continue;
            }
            if (exists) {
                ::unlink(targetName.constData());
            }
            if (::symlink(QFile::encodeName(linkTarget).constData(), targetName.constData()) != 0) {
                result.failed.append(QStringLiteral("%1: %2").arg(entry.path, QString::fromLocal8Bit(std::strerror(errno))));
            }
            continue;
        }

        const QString source = unpacked(staging, entry);
        struct stat from;
        if (source.isEmpty() || ::lstat(QFile::encodeName(source).constData(), &from) != 0) {
            result.failed.append(QCoreApplication::translate("Reader", "%1: it couldn't be unpacked").arg(entry.path));
            continue;
        }
        bool placed = false;
        QString error;
        if (from.st_nlink > 1) {
            // A hard link the tool made: copied, so the new file shares
            // nothing with any other.
            QFile in(source);
            QSaveFile out(target);
            out.setDirectWriteFallback(false);
            if (in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly)) {
                placed = out.write(in.readAll()) == from.st_size && out.commit();
            }
            error = out.errorString();
        } else {
            // On the same file system, so a rename: the file appears whole
            // under its name, or not at all.
            placed = ::rename(QFile::encodeName(source).constData(), targetName.constData()) == 0;
            error = QString::fromLocal8Bit(std::strerror(errno));
        }
        if (!placed) {
            result.ok = false;
            result.error = QCoreApplication::translate("Reader", "Couldn't write %1: %2").arg(entry.name, error);
            return result;
        }
        ::chmod(targetName.constData(), (from.st_mode & 0111) || entry.executable ? 0755 : 0644);
        setTimes(target, entry.modified);
        ++result.written;
        if (result.firstWritten.isEmpty()) {
            result.firstWritten = root + QLatin1Char('/') + rel.section(QLatin1Char('/'), 0, 0);
        }
    }
    result.ok = result.failed.isEmpty();
    return result;
}

QList<int> plainFiles(const Listing &listing)
{
    QList<int> files;
    for (int order = 0; order < listing.byOrder.size(); ++order) {
        const int index = listing.byOrder[order];
        if (index < 0) {
            continue;
        }
        const Entry &e = listing.entries[index];
        if (!e.dir && !e.symlink && !e.unsafe) {
            files.append(order);
        }
    }
    return files;
}

Result testWithTool(const QString &path, const QByteArray &password, const Listing &listing, Progress *progress)
{
    Result result;
    const QList<int> files = plainFiles(listing);
    const qint64 total = qMax<qint64>(listing.totalSize, 1);
    progress->total = total;
    progress->done = 0;

    if (listing.tool == QLatin1String("7z")) {
        Tools::Job job;
        job.program = QStringLiteral("7z");
        job.arguments = {QStringLiteral("t"), QStringLiteral("-bso0"), QStringLiteral("-bse2"), QStringLiteral("-bsp1"), QStringLiteral("-spd"),
                         QStringLiteral("-sccUTF-8"), path};
        job.password = password;
        job.line = [progress, total](const QByteArray &line) {
            const QByteArray t = line.trimmed();
            const qsizetype percent = t.indexOf('%');
            bool ok = false;
            const int value = percent > 0 ? t.left(percent).toInt(&ok) : 0;
            if (ok) {
                progress->done = total * qBound(0, value, 100) / 100;
            }
        };
        const Tools::Outcome outcome = Tools::run(job, progress);
        if (outcome.cancelled) {
            result.cancelled = true;
            return result;
        }
        if (!outcome.started) {
            result.ok = false;
            result.error = QString::fromUtf8(outcome.errors);
            return result;
        }
        // "ERROR: Data Error in encrypted file. Wrong password? : name"
        int wrong = 0;
        for (const QByteArray &raw : outcome.errors.split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            const qsizetype colon = line.lastIndexOf(QLatin1String(" : "));
            if (!line.startsWith(QLatin1String("ERROR: ")) || colon < 0) {
                continue;
            }
            const QString name = line.mid(colon + 3);
            const QString why = line.mid(7, colon - 7);
            if (line.mid(7).startsWith(path + QLatin1String(" : "))) {
                continue;
            }
            wrong += why.contains(QLatin1String("Wrong password")) ? 1 : 0;
            result.failed.append(QStringLiteral("%1: %2").arg(name, why));
        }
        // Every file failing on the password is the password; one or two
        // among good ones is damage.
        const PasswordState state = Tools::passwordOutcome(outcome, password);
        if (state != PasswordState::Fine && (result.failed.isEmpty() || (wrong == result.failed.size() && wrong >= files.size()))) {
            result.ok = false;
            result.failed.clear();
            result.password = state;
            return result;
        }
        result.written = int(qMax<qsizetype>(0, files.size() - result.failed.size()));
        if (outcome.exitCode != 0 && result.failed.isEmpty()) {
            result.ok = false;
            result.error = QString::fromUtf8(outcome.errors).trimmed();
            return result;
        }
        result.ok = result.failed.isEmpty();
        return result;
    }

    if (!listing.encrypted) {
        // Nothing protected: lsar checks every file against its checksum
        // and says how each went.
        Tools::Job job;
        job.program = QStringLiteral("lsar");
        job.arguments = {QStringLiteral("-t"), QStringLiteral("-ja"), QStringLiteral("-nr"), path};
        auto seen = std::make_shared<int>(0);
        job.line = [progress, seen, &listing](const QByteArray &line) {
            if (line.contains("\"lsarTestResult\"")) {
                const int index = listing.byOrder.value((*seen)++, -1);
                if (index >= 0) {
                    progress->done += listing.entries[index].dir ? 0 : listing.entries[index].size;
                }
            }
        };
        const Tools::Outcome outcome = Tools::run(job, progress);
        if (outcome.cancelled) {
            result.cancelled = true;
            return result;
        }
        const QJsonDocument document = QJsonDocument::fromJson(outcome.output);
        const QJsonArray contents = document.object().value(QLatin1String("lsarContents")).toArray();
        if (contents.isEmpty()) {
            result.ok = false;
            result.error = QString::fromUtf8(outcome.errors).trimmed();
            return result;
        }
        for (qsizetype i = 0; i < contents.size(); ++i) {
            const int index = listing.byOrder.value(int(i), -1);
            if (index < 0 || listing.entries[index].dir || listing.entries[index].symlink || listing.entries[index].unsafe) {
                continue;
            }
            const QString test = contents.at(i).toObject().value(QLatin1String("lsarTestResult")).toString();
            if (test == QLatin1String("wrong_checksum")) {
                result.failed.append(QCoreApplication::translate("Reader", "%1: its checksum is wrong").arg(listing.entries[index].path));
            } else if (test == QLatin1String("unpacking_failed")) {
                result.failed.append(QCoreApplication::translate("Reader", "%1: it couldn't be unpacked").arg(listing.entries[index].path));
            } else {
                ++result.written;
            }
        }
        result.ok = result.failed.isEmpty();
        return result;
    }

    // lsar can't take a password safely, so a protected RAR is tested by
    // unpacking it into a private folder that goes straight after.
    const Staging staging(privateStaging());
    if (staging.path.isEmpty()) {
        result.ok = false;
        result.error = QCoreApplication::translate("Reader", "Couldn't make a private folder to test in.");
        return result;
    }
    const Tools::Outcome outcome = unpack(path, listing, password, files, staging, total, progress);
    if (outcome.cancelled) {
        result.cancelled = true;
        return result;
    }
    for (int order : files) {
        const Entry &entry = listing.entries[listing.byOrder[order]];
        if (unpacked(staging, entry).isEmpty()) {
            result.failed.append(QCoreApplication::translate("Reader", "%1: it couldn't be unpacked").arg(entry.path));
        } else {
            ++result.written;
        }
    }
    const PasswordState state = Tools::passwordOutcome(outcome, password);
    if (state != PasswordState::Fine && result.written == 0) {
        result.ok = false;
        result.failed.clear();
        result.password = state;
        return result;
    }
    result.ok = result.failed.isEmpty();
    return result;
}

Result readOneWithTool(const QString &path, const QByteArray &password, const Listing &listing, int order, const QString &target, Progress *progress)
{
    Result result;
    const int index = listing.byOrder.value(order, -1);
    if (index < 0) {
        result.ok = false;
        result.error = QCoreApplication::translate("Reader", "The file isn't in the archive any more.");
        return result;
    }
    const Entry &entry = listing.entries[index];
    progress->total = qMax<qint64>(entry.size, 1);
    progress->done = 0;

    if (listing.tool == QLatin1String("7z")) {
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            result.ok = false;
            result.error = out.errorString();
            return result;
        }
        Tools::Job job;
        job.program = QStringLiteral("7z");
        job.arguments = {QStringLiteral("x"), QStringLiteral("-so"), QStringLiteral("-bsp0"), QStringLiteral("-spd"), path, QStringLiteral("-i!") + entry.toolPath};
        job.password = password;
        bool writeFailed = false;
        job.data = [&](const char *data, qsizetype size) {
            if (out.write(data, size) != size) {
                writeFailed = true;
                return false;
            }
            progress->done += size;
            return true;
        };
        const Tools::Outcome outcome = Tools::run(job, progress);
        const PasswordState state = Tools::passwordOutcome(outcome, password);
        if (outcome.cancelled || writeFailed || state != PasswordState::Fine || outcome.exitCode != 0) {
            out.cancelWriting();
            out.commit();
            result.ok = false;
            result.cancelled = outcome.cancelled;
            result.password = state;
            result.error = writeFailed ? out.errorString() : QString::fromUtf8(outcome.errors).trimmed();
            return result;
        }
        if (!out.commit()) {
            result.ok = false;
            result.error = out.errorString();
            return result;
        }
        ::chmod(QFile::encodeName(target).constData(), 0600);
        return result;
    }

    const Staging staging(Tools::makeStaging(QFileInfo(target).absolutePath()));
    if (staging.path.isEmpty()) {
        result.ok = false;
        result.error = QString::fromLocal8Bit(std::strerror(errno));
        return result;
    }
    const Tools::Outcome outcome = unpack(path, listing, password, {order}, staging, entry.size, progress);
    result.password = Tools::passwordOutcome(outcome, password);
    const QString source = unpacked(staging, entry);
    if (outcome.cancelled || result.password != PasswordState::Fine || source.isEmpty()) {
        result.ok = false;
        result.cancelled = outcome.cancelled;
        result.error = QCoreApplication::translate("Reader", "It couldn't be unpacked.");
        return result;
    }
    if (::rename(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) != 0) {
        result.ok = false;
        result.error = QString::fromLocal8Bit(std::strerror(errno));
        return result;
    }
    ::chmod(QFile::encodeName(target).constData(), 0600);
    return result;
}

// A batch of pictures at a time, unpacked into a private folder and each
// deleted the moment it is read.
void thumbnailsWithTool(const QString &path, const QByteArray &password, const Listing &listing, const QSet<int> &orders, QSize box,
                        Progress *progress, const std::function<void(int, const QImage &)> &found)
{
    QList<int> wanted;
    for (int order : orders) {
        const int index = listing.byOrder.value(order, -1);
        if (index >= 0 && (!listing.entries[index].encrypted || !password.isEmpty())) {
            wanted.append(order);
        }
    }
    std::sort(wanted.begin(), wanted.end());
    constexpr int batchSize = 40;
    for (qsizetype start = 0; start < wanted.size() && !progress->cancel; start += batchSize) {
        const QList<int> batch = wanted.mid(start, batchSize);
        const Staging staging(privateStaging());
        if (staging.path.isEmpty()) {
            return;
        }
        const Tools::Outcome outcome = unpack(path, listing, password, batch, staging, 0, progress);
        if (outcome.cancelled || Tools::passwordOutcome(outcome, password) != PasswordState::Fine) {
            return;
        }
        for (int order : batch) {
            if (progress->cancel) {
                return;
            }
            const QString file = unpacked(staging, listing.entries[listing.byOrder[order]]);
            if (file.isEmpty()) {
                continue;
            }
            QFile in(file);
            QByteArray data;
            if (in.open(QIODevice::ReadOnly)) {
                data = in.readAll();
                in.close();
            }
            ::unlink(QFile::encodeName(file).constData());
            const QImage image = scaledImage(data, box);
            if (!image.isNull()) {
                found(order, image);
            }
        }
    }
}
}

Result extract(const ExtractOptions &options, const Listing &listing, Progress *progress)
{
    Result result;
    if (!QDir().mkpath(options.destination)) {
        result.ok = false;
        result.error = QCoreApplication::translate("Reader", "Couldn't make the folder %1.").arg(Format::folder(options.destination));
        return result;
    }
    const QString root = QDir::cleanPath(QFileInfo(options.destination).absoluteFilePath());
    const QString prefix = options.stripPrefix.isEmpty() ? QString() : options.stripPrefix + QLatin1Char('/');

    // Where each entry lands, worked out before anything is written, so
    // the progress total is known and links can find their files.
    QHash<int, QString> targets;
    QSet<QString> taken;
    qint64 total = 0;
    for (int order = 0; order < listing.byOrder.size(); ++order) {
        const int index = listing.byOrder[order];
        if (index < 0 || (!options.orders.isEmpty() && !options.orders.contains(order))) {
            continue;
        }
        const Entry &entry = listing.entries[index];
        if (entry.unsafe) {
            result.unsafe.append(entry.path);
            continue;
        }
        QString rel = entry.path;
        if (!prefix.isEmpty()) {
            if (!rel.startsWith(prefix)) {
                continue;
            }
            rel = rel.mid(prefix.size());
        }
        if (!options.keepStructure) {
            if (entry.dir) {
                continue;
            }
            rel = entry.name;
            for (int n = 2; taken.contains(rel); ++n) {
                rel = numbered(entry.name, n);
            }
        }
        taken.insert(rel);
        targets.insert(order, rel);
        if (!entry.dir) {
            total += entry.size;
        }
    }
    progress->total = qMax<qint64>(total, 1);
    progress->done = 0;
    if (!listing.tool.isEmpty()) {
        return extractWithTool(options, listing, targets, root, total, progress, result);
    }

    Handle h(newReader(options.password, listing.raw));
    if (!openFile(h.a, options.archive)) {
        result.ok = false;
        result.error = errorOf(h.a);
        return result;
    }
    QHash<QString, QString> written;
    archive_entry *e = nullptr;
    for (int order = 0;; ++order) {
        if (progress->cancel) {
            result.cancelled = true;
            return result;
        }
        const int r = archive_read_next_header(h.a, &e);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_WARN) {
            result.ok = false;
            result.error = QCoreApplication::translate("Reader", "The archive is damaged: %1").arg(errorOf(h.a));
            return result;
        }
        const auto found = targets.constFind(order);
        if (found == targets.constEnd()) {
            continue;
        }
        const Entry &entry = listing.entries[listing.byOrder[order]];
        const QString rel = found.value();
        QStringList steps = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const QString target = root + QLatin1Char('/') + rel;
        const QString leaf = steps.isEmpty() ? QString() : steps.takeLast();
        if (leaf.isEmpty()) {
            continue;
        }
        const Made made = makeDirs(root, entry.dir ? steps + QStringList{leaf} : steps);
        if (made == Made::Link) {
            result.unsafe.append(entry.path);
            continue;
        }
        if (made == Made::Blocked) {
            result.failed.append(QCoreApplication::translate("Reader", "%1: a file is in the way of its folder").arg(entry.path));
            continue;
        }
        if (entry.dir) {
            continue;
        }
        progress->setCurrent(entry.name);

        const QByteArray targetName = QFile::encodeName(target);
        struct stat st;
        const bool exists = ::lstat(targetName.constData(), &st) == 0;
        if (exists && S_ISDIR(st.st_mode)) {
            result.failed.append(QCoreApplication::translate("Reader", "%1: a folder has the same name").arg(entry.path));
            continue;
        }
        if (exists && !options.replace) {
            ++result.kept;
            progress->done += entry.size;
            continue;
        }
        if (exists && S_ISLNK(st.st_mode)) {
            ::unlink(targetName.constData());
        }

        if (entry.symlink) {
            const QString dirRel = steps.join(QLatin1Char('/'));
            const QString resolved = QDir::cleanPath(dirRel.isEmpty() ? entry.linkTarget : dirRel + QLatin1Char('/') + entry.linkTarget);
            if (entry.linkTarget.isEmpty() || entry.linkTarget.startsWith(QLatin1Char('/')) || resolved == QLatin1String("..")
                || resolved.startsWith(QLatin1String("../"))) {
                result.unsafe.append(entry.path);
                continue;
            }
            if (exists) {
                ::unlink(targetName.constData());
            }
            if (::symlink(QFile::encodeName(entry.linkTarget).constData(), targetName.constData()) != 0) {
                result.failed.append(QStringLiteral("%1: %2").arg(entry.path, QString::fromLocal8Bit(std::strerror(errno))));
            }
            continue;
        }

        if (const char *hard = archive_entry_hardlink(e)) {
            QString other;
            if (!safePath(text(archive_entry_hardlink_utf8(e), hard), &other)) {
                result.unsafe.append(entry.path);
                continue;
            }
            const QString source = written.value(other);
            if (source.isEmpty()) {
                result.failed.append(QCoreApplication::translate("Reader", "%1: it links to a file that wasn't extracted").arg(entry.path));
                continue;
            }
            if (exists) {
                ::unlink(targetName.constData());
            }
            if (!QFile::copy(source, target)) {
                result.failed.append(QCoreApplication::translate("Reader", "%1: couldn't copy the file it links to").arg(entry.path));
            } else {
                ++result.written;
            }
            continue;
        }

        // Written beside its final name and renamed into place when
        // complete, so a cancel or a failure leaves no half file.
        QSaveFile out(target);
        out.setDirectWriteFallback(false);
        if (!out.open(QIODevice::WriteOnly)) {
            result.ok = false;
            result.error = QCoreApplication::translate("Reader", "Couldn't write in %1: %2").arg(Format::folder(QFileInfo(target).absolutePath()), out.errorString());
            return result;
        }
        QString error;
        bool cancelled = false;
        bool writeFailed = false;
        const qint64 before = progress->done;
        if (!readData(h.a, &out, progress, &error, &cancelled, &writeFailed)) {
            out.cancelWriting();
            out.commit();
            if (cancelled) {
                result.cancelled = true;
                return result;
            }
            if (passwordError(h.a)) {
                result.ok = false;
                result.password = passwordState(options.password);
                return result;
            }
            if (writeFailed) {
                result.ok = false;
                result.error = QCoreApplication::translate("Reader", "Couldn't write %1: %2").arg(entry.name, error);
                return result;
            }
            result.failed.append(QStringLiteral("%1: %2").arg(entry.path, error));
            progress->done = before + entry.size;
            continue;
        }
        if (!out.commit()) {
            result.ok = false;
            result.error = QCoreApplication::translate("Reader", "Couldn't write %1: %2").arg(entry.name, out.errorString());
            return result;
        }
        const mode_t mode = archive_entry_perm(e);
        ::chmod(targetName.constData(), (mode & 0111) ? 0755 : 0644);
        setTimes(target, entry.modified);
        written.insert(entry.path, target);
        ++result.written;
        if (result.firstWritten.isEmpty()) {
            result.firstWritten = root + QLatin1Char('/') + rel.section(QLatin1Char('/'), 0, 0);
        }
    }
    result.ok = result.failed.isEmpty();
    return result;
}

Result test(const QString &path, const QByteArray &password, const Listing &listing, Progress *progress)
{
    if (!listing.tool.isEmpty()) {
        return testWithTool(path, password, listing, progress);
    }
    Result result;
    progress->total = qMax<qint64>(listing.totalSize, 1);
    progress->done = 0;
    Handle h(newReader(password, listing.raw));
    if (!openFile(h.a, path)) {
        result.ok = false;
        result.error = errorOf(h.a);
        return result;
    }
    archive_entry *e = nullptr;
    for (int order = 0;; ++order) {
        const int r = archive_read_next_header(h.a, &e);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_WARN) {
            result.ok = false;
            result.error = QCoreApplication::translate("Reader", "The archive is damaged: %1").arg(errorOf(h.a));
            return result;
        }
        if (archive_entry_filetype(e) != AE_IFREG) {
            continue;
        }
        const int index = order < listing.byOrder.size() ? listing.byOrder[order] : -1;
        const QString name = index >= 0 ? listing.entries[index].path : storedPath(e);
        progress->setCurrent(index >= 0 ? listing.entries[index].name : name);
        QString error;
        bool cancelled = false;
        if (!readData(h.a, nullptr, progress, &error, &cancelled)) {
            if (cancelled) {
                result.cancelled = true;
                return result;
            }
            if (passwordError(h.a)) {
                result.ok = false;
                result.password = passwordState(password);
                return result;
            }
            result.failed.append(QStringLiteral("%1: %2").arg(name, error));
            continue;
        }
        ++result.written;
    }
    result.ok = result.failed.isEmpty();
    return result;
}

Result readOne(const QString &path, const QByteArray &password, const Listing &listing, int order, const QString &target, Progress *progress)
{
    if (!listing.tool.isEmpty()) {
        return readOneWithTool(path, password, listing, order, target, progress);
    }
    Result result;
    Handle h(newReader(password, listing.raw));
    if (!openFile(h.a, path)) {
        result.ok = false;
        result.error = errorOf(h.a);
        return result;
    }
    archive_entry *e = nullptr;
    for (int n = 0;; ++n) {
        const int r = archive_read_next_header(h.a, &e);
        if (r == ARCHIVE_EOF || r < ARCHIVE_WARN) {
            result.ok = false;
            result.error = r == ARCHIVE_EOF ? QCoreApplication::translate("Reader", "The file isn't in the archive any more.") : errorOf(h.a);
            return result;
        }
        if (n != order) {
            continue;
        }
        progress->total = qMax<qint64>(archive_entry_size(e), 1);
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            result.ok = false;
            result.error = out.errorString();
            return result;
        }
        QString error;
        bool cancelled = false;
        if (!readData(h.a, &out, progress, &error, &cancelled)) {
            out.cancelWriting();
            out.commit();
            result.ok = false;
            result.cancelled = cancelled;
            if (passwordError(h.a)) {
                result.password = passwordState(password);
            }
            result.error = error;
            return result;
        }
        if (!out.commit()) {
            result.ok = false;
            result.error = out.errorString();
        }
        ::chmod(QFile::encodeName(target).constData(), 0600);
        return result;
    }
}

void thumbnails(const QString &path, const QByteArray &password, const Listing &listing, const QSet<int> &orders, QSize box,
                Progress *progress, const std::function<void(int, const QImage &)> &found)
{
    if (!listing.tool.isEmpty()) {
        thumbnailsWithTool(path, password, listing, orders, box, progress, found);
        return;
    }
    Handle h(newReader(password, listing.raw));
    if (!openFile(h.a, path)) {
        return;
    }
    int left = int(orders.size());
    archive_entry *e = nullptr;
    for (int order = 0; left > 0 && !progress->cancel; ++order) {
        if (archive_read_next_header(h.a, &e) < ARCHIVE_WARN) {
            return;
        }
        if (!orders.contains(order)) {
            continue;
        }
        --left;
        if (archive_entry_is_data_encrypted(e) && password.isEmpty()) {
            continue;
        }
        QByteArray data;
        data.reserve(qMin<qint64>(archive_entry_size(e), 64 * 1024 * 1024));
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        QString error;
        bool cancelled = false;
        if (!readData(h.a, &buffer, nullptr, &error, &cancelled)) {
            if (passwordError(h.a)) {
                return;
            }
            continue;
        }
        buffer.close();
        const QImage image = scaledImage(data, box);
        if (!image.isNull()) {
            found(order, image);
        }
    }
}

Result create(const CreateOptions &options, Progress *progress)
{
    Result result;
    const QFileInfo targetInfo(options.target);
    const QString targetPath = targetInfo.absoluteFilePath();

    struct Item {
        QString path;
        QString name;
    };
    QList<Item> items;
    QSet<QString> names;
    qint64 total = 0;
    for (const QString &top : options.paths) {
        const QFileInfo info(top);
        const QString base = info.fileName();
        if (info.absoluteFilePath() == targetPath || base.isEmpty()) {
            continue;
        }
        items.append({info.absoluteFilePath(), base});
        names.insert(base);
        if (info.isDir() && !info.isSymLink()) {
            const QDir dir(info.absoluteFilePath());
            QDirIterator it(info.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                if (QFileInfo(file).absoluteFilePath() == targetPath) {
                    continue;
                }
                const QString name = base + QLatin1Char('/') + dir.relativeFilePath(file);
                items.append({file, name});
                names.insert(name);
                if (it.fileInfo().isFile() && !it.fileInfo().isSymLink()) {
                    total += it.fileInfo().size();
                }
            }
        } else if (info.isFile() && !info.isSymLink()) {
            total += info.size();
        }
    }

    Listing source;
    // An archive only a tool can read is unpacked first, into a private
    // folder beside the new one, and its files go in from there.
    std::unique_ptr<Staging> unpackedSource;
    if (!options.source.isEmpty()) {
        Progress quiet;
        source = list(options.source, options.sourcePassword, &quiet);
        if (!source.error.isEmpty() || source.lockedHeaders || source.cantDecrypt) {
            result.ok = false;
            result.error = QCoreApplication::translate("Reader", "Couldn't read %1: %2")
                               .arg(QFileInfo(options.source).fileName(),
                                    source.error.isEmpty() ? QCoreApplication::translate("Reader", "Archive can't unlock it.") : source.error);
            return result;
        }
        if (!source.tool.isEmpty()) {
            unpackedSource = std::make_unique<Staging>(Tools::makeStaging(targetInfo.absolutePath()));
            ExtractOptions unpackOptions;
            unpackOptions.archive = options.source;
            unpackOptions.password = options.sourcePassword;
            unpackOptions.destination = unpackedSource->into();
            Result unpackedResult;
            if (!unpackedSource->path.isEmpty()) {
                unpackedResult = extract(unpackOptions, source, progress);
            }
            if (unpackedSource->path.isEmpty() || unpackedResult.cancelled || unpackedResult.password != PasswordState::Fine
                || !unpackedResult.error.isEmpty()) {
                result.ok = false;
                result.cancelled = unpackedResult.cancelled;
                result.password = unpackedResult.password;
                result.error = QCoreApplication::translate("Reader", "Couldn't read %1: %2")
                                   .arg(QFileInfo(options.source).fileName(),
                                        unpackedResult.error.isEmpty() ? QCoreApplication::translate("Reader", "it couldn't be unpacked.") : unpackedResult.error);
                return result;
            }
            result.failed += unpackedResult.failed;
            const QDir dir(unpackedSource->into());
            QList<Item> fromSource;
            QDirIterator it(dir.path(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                const QString name = dir.relativeFilePath(file);
                if (names.contains(name)) {
                    continue;
                }
                fromSource.append({file, name});
                if (it.fileInfo().isFile() && !it.fileInfo().isSymLink()) {
                    total += it.fileInfo().size();
                }
            }
            items = fromSource + items;
        } else {
            total += source.totalSize;
        }
    }
    progress->total = qMax<qint64>(total, 1);
    progress->done = 0;

    QTemporaryFile temp(targetInfo.absolutePath() + QStringLiteral("/.") + targetInfo.fileName() + QStringLiteral(".XXXXXX"));
    if (!temp.open()) {
        result.ok = false;
        result.error = QCoreApplication::translate("Reader", "Couldn't save in %1: %2").arg(Format::folder(targetInfo.absolutePath()), temp.errorString());
        return result;
    }
    archive *w = archive_write_new();
    if (options.format == QLatin1String("7z")) {
        archive_write_set_format_7zip(w);
        archive_write_set_options(w, "7zip:compression=lzma2");
    } else {
        archive_write_set_format_zip(w);
    }
    const auto fail = [&](const QString &message) {
        archive_write_fail(w);
        archive_write_free(w);
        result.ok = false;
        result.error = message;
        return result;
    };
    if (archive_write_open_fd(w, temp.handle()) != ARCHIVE_OK) {
        return fail(QString::fromLocal8Bit(archive_error_string(w)));
    }
    static thread_local QByteArray buffer(blockSize, Qt::Uninitialized);

    if (!options.source.isEmpty() && source.tool.isEmpty()) {
        Handle h(newReader(options.sourcePassword, source.raw));
        if (!openFile(h.a, options.source)) {
            return fail(errorOf(h.a));
        }
        archive_entry *e = nullptr;
        for (int order = 0;; ++order) {
            if (progress->cancel) {
                result.cancelled = true;
                return fail(QString());
            }
            const int r = archive_read_next_header(h.a, &e);
            if (r == ARCHIVE_EOF) {
                break;
            }
            if (r < ARCHIVE_WARN) {
                return fail(QCoreApplication::translate("Reader", "%1 is damaged: %2").arg(QFileInfo(options.source).fileName(), errorOf(h.a)));
            }
            const int index = order < source.byOrder.size() ? source.byOrder[order] : -1;
            if (index < 0) {
                continue;
            }
            const Entry &entry = source.entries[index];
            if (entry.unsafe || names.contains(entry.path)) {
                continue;
            }
            if (archive_entry_hardlink(e)) {
                result.failed.append(QCoreApplication::translate("Reader", "%1: hard links can't go into a new archive").arg(entry.path));
                continue;
            }
            archive_entry *o = archive_entry_new();
            archive_entry_set_pathname_utf8(o, entry.path.toUtf8().constData());
            const mode_t perm = archive_entry_perm(e);
            if (entry.dir) {
                archive_entry_set_filetype(o, AE_IFDIR);
                archive_entry_set_perm(o, 0755);
            } else if (entry.symlink) {
                archive_entry_set_filetype(o, AE_IFLNK);
                archive_entry_set_perm(o, 0777);
                archive_entry_set_symlink_utf8(o, entry.linkTarget.toUtf8().constData());
            } else {
                archive_entry_set_filetype(o, AE_IFREG);
                archive_entry_set_perm(o, (perm & 0111) ? 0755 : 0644);
                archive_entry_set_size(o, entry.size);
            }
            if (entry.modified.isValid()) {
                archive_entry_set_mtime(o, entry.modified.toSecsSinceEpoch(), 0);
            }
            progress->setCurrent(entry.name);
            const int written = archive_write_header(w, o);
            archive_entry_free(o);
            if (written < ARCHIVE_WARN) {
                return fail(QString::fromLocal8Bit(archive_error_string(w)));
            }
            if (entry.dir || entry.symlink) {
                continue;
            }
            for (;;) {
                if (progress->cancel) {
                    result.cancelled = true;
                    return fail(QString());
                }
                const la_ssize_t n = archive_read_data(h.a, buffer.data(), blockSize);
                if (n == 0) {
                    break;
                }
                if (n < 0) {
                    if (passwordError(h.a)) {
                        result.password = passwordState(options.sourcePassword);
                    }
                    return fail(QCoreApplication::translate("Reader", "Couldn't read %1 from %2: %3").arg(entry.path, QFileInfo(options.source).fileName(), errorOf(h.a)));
                }
                if (archive_write_data(w, buffer.constData(), size_t(n)) < 0) {
                    return fail(QString::fromLocal8Bit(archive_error_string(w)));
                }
                progress->done += n;
            }
        }
    }

    for (const Item &item : items) {
        if (progress->cancel) {
            result.cancelled = true;
            return fail(QString());
        }
        const QByteArray local = QFile::encodeName(item.path);
        struct stat st;
        if (::lstat(local.constData(), &st) != 0) {
            result.failed.append(QStringLiteral("%1: %2").arg(item.name, QString::fromLocal8Bit(std::strerror(errno))));
            continue;
        }
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            continue;
        }
        archive_entry *o = archive_entry_new();
        archive_entry_copy_stat(o, &st);
        archive_entry_set_pathname_utf8(o, item.name.toUtf8().constData());
        if (S_ISLNK(st.st_mode)) {
            archive_entry_set_size(o, 0);
            char link[4096];
            const ssize_t length = ::readlink(local.constData(), link, sizeof link - 1);
            if (length > 0) {
                link[length] = 0;
                archive_entry_set_symlink(o, link);
            }
        }
        QFile file(item.path);
        if (S_ISREG(st.st_mode) && !file.open(QIODevice::ReadOnly)) {
            archive_entry_free(o);
            result.failed.append(QStringLiteral("%1: %2").arg(item.name, file.errorString()));
            progress->done += st.st_size;
            continue;
        }
        progress->setCurrent(QFileInfo(item.path).fileName());
        const int written = archive_write_header(w, o);
        archive_entry_free(o);
        if (written < ARCHIVE_WARN) {
            return fail(QString::fromLocal8Bit(archive_error_string(w)));
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        for (;;) {
            if (progress->cancel) {
                result.cancelled = true;
                return fail(QString());
            }
            const qint64 n = file.read(buffer.data(), blockSize);
            if (n == 0) {
                break;
            }
            if (n < 0) {
                return fail(QCoreApplication::translate("Reader", "Couldn't read %1: %2").arg(item.name, file.errorString()));
            }
            if (archive_write_data(w, buffer.constData(), size_t(n)) < 0) {
                return fail(QString::fromLocal8Bit(archive_error_string(w)));
            }
            progress->done += n;
        }
    }

    if (archive_write_close(w) != ARCHIVE_OK) {
        return fail(QString::fromLocal8Bit(archive_error_string(w)));
    }
    archive_write_free(w);
    temp.close();
    if (QFileInfo::exists(targetPath) || !temp.rename(targetPath)) {
        result.ok = false;
        result.error = QCoreApplication::translate("Reader", "Couldn't save %1: a file with that name is there now.").arg(targetInfo.fileName());
        return result;
    }
    temp.setAutoRemove(false);
    QFile::setPermissions(targetPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    result.firstWritten = targetPath;
    return result;
}

}
