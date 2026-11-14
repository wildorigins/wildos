// pointer-import: turns a Windows pointer, a Windows pointer pack or a
// picture into the Xcursor theme Settings > Accessibility offers as "Your
// own" pointer, and carries that theme to the sign-in screen.
//
//   pointer-import convert FILE THEME-DIR [HOTSPOT-X HOTSPOT-Y]
//       FILE is a .cur, .ani, .zip pack, a pack's install.inf, or a .png or
//       .svg picture (the hotspot is a fraction of its width and height).
//   pointer-import export THEME-DIR > stream
//   pointer-import import THEME-DIR STAMP < stream
//       greeter-access runs export as the account and import as the sddm
//       user, so the sign-in screen only ever gets pictures this program
//       has checked and written again itself.
//
// Every file is untrusted: sizes and counts are capped, headers checked,
// a pack's install.inf is only read for file names, and nothing in a pack
// is run. Settings runs this as a separate process so a bad file can at
// worst end this program, never Settings. Exit status 2 means the file is
// not a pointer this can use, 1 that the theme could not be written.

#include "cursor.h"

#include <KZip>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSvgRenderer>
#include <QtMath>

#include <cstdio>
#include <sys/resource.h>

namespace
{
constexpr qint64 maxFile = 8 * 1024 * 1024;
constexpr qint64 maxPack = 32 * 1024 * 1024;
constexpr qint64 maxUnpacked = 64 * 1024 * 1024;
constexpr qint64 maxTheme = 192 * 1024 * 1024;
constexpr int maxPackEntries = 2000;
constexpr int maxImageSide = 4096;

// The pointer size slider's sizes, always made, and the larger ones a 150
// or 200 per cent display scale asks for, made while the file stays under
// a budget. Xcursor picks the nearest nominal size there is, so a long
// animation without them is drawn smaller there rather than not at all.
const QList<int> sliderSizes{24, 30, 36, 48, 60, 72, 96};
const QList<int> scaledSizes{120, 144, 192};
constexpr qint64 scaledBudget = 16 * 1024 * 1024;

QList<int> sizesFor(const Pointer &pointer, qreal fill)
{
    auto bytes = [&](int nominal) {
        const qint64 side = qCeil(nominal * fill);
        return qint64(pointer.size()) * (side * side * 4 + 36);
    };
    QList<int> sizes = sliderSizes;
    qint64 total = 0;
    for (const int nominal : sliderSizes) {
        total += bytes(nominal);
    }
    for (const int nominal : scaledSizes) {
        total += bytes(nominal);
        if (total > scaledBudget) {
            break;
        }
        sizes << nominal;
    }
    return sizes;
}

// Each role's names: the first is the file, the rest links to it. Names
// Breeze links to the same picture are added from Breeze itself, so old X11
// apps asking for a hashed name get the same pointer.
const QList<QStringList> roleNames{
    {QStringLiteral("default"), QStringLiteral("left_ptr"), QStringLiteral("arrow"), QStringLiteral("top_left_arrow")},
    {QStringLiteral("help"), QStringLiteral("question_arrow"), QStringLiteral("whats_this"), QStringLiteral("left_ptr_help")},
    {QStringLiteral("progress"), QStringLiteral("left_ptr_watch"), QStringLiteral("half-busy")},
    {QStringLiteral("wait"), QStringLiteral("watch")},
    {QStringLiteral("crosshair"), QStringLiteral("cross"), QStringLiteral("tcross")},
    {QStringLiteral("text"), QStringLiteral("xterm"), QStringLiteral("ibeam")},
    {QStringLiteral("pencil"), QStringLiteral("draft")},
    {QStringLiteral("not-allowed"), QStringLiteral("no-drop"), QStringLiteral("forbidden"), QStringLiteral("crossed_circle"), QStringLiteral("circle"),
     QStringLiteral("dnd-no-drop")},
    {QStringLiteral("ns-resize"), QStringLiteral("size_ver"), QStringLiteral("sb_v_double_arrow"), QStringLiteral("v_double_arrow"),
     QStringLiteral("n-resize"), QStringLiteral("s-resize"), QStringLiteral("top_side"), QStringLiteral("bottom_side")},
    {QStringLiteral("ew-resize"), QStringLiteral("size_hor"), QStringLiteral("sb_h_double_arrow"), QStringLiteral("h_double_arrow"),
     QStringLiteral("e-resize"), QStringLiteral("w-resize"), QStringLiteral("left_side"), QStringLiteral("right_side")},
    {QStringLiteral("nwse-resize"), QStringLiteral("size_fdiag"), QStringLiteral("nw-resize"), QStringLiteral("se-resize"),
     QStringLiteral("top_left_corner"), QStringLiteral("bottom_right_corner")},
    {QStringLiteral("nesw-resize"), QStringLiteral("size_bdiag"), QStringLiteral("ne-resize"), QStringLiteral("sw-resize"),
     QStringLiteral("top_right_corner"), QStringLiteral("bottom_left_corner")},
    {QStringLiteral("move"), QStringLiteral("all-scroll"), QStringLiteral("fleur"), QStringLiteral("size_all")},
    {QStringLiteral("up-arrow"), QStringLiteral("center_ptr"), QStringLiteral("sb_up_arrow")},
    {QStringLiteral("pointer"), QStringLiteral("hand2"), QStringLiteral("hand1"), QStringLiteral("pointing_hand"), QStringLiteral("hand")},
};

const QRegularExpression validName(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));

int unusable(const char *why)
{
    std::fprintf(stderr, "pointer-import: %s\n", why);
    return 2;
}

int failed(const QString &why)
{
    std::fprintf(stderr, "pointer-import: %s\n", qPrintable(why));
    return 1;
}

bool readCapped(const QString &path, qint64 cap, QByteArray &out)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > cap) {
        return false;
    }
    out = file.read(cap + 1);
    return out.size() <= cap;
}

// A picture as the normal arrow: a PNG, or an SVG drawn at 512 pixels so
// every size is a sharp scale down.
bool readPicture(const QByteArray &data, bool svg, QImage &out)
{
    if (svg) {
        QSvgRenderer renderer(data);
        QSizeF size = renderer.isValid() ? renderer.viewBoxF().size() : QSizeF();
        if (size.isEmpty()) {
            size = renderer.defaultSize();
        }
        if (!renderer.isValid() || size.isEmpty()) {
            return false;
        }
        size.scale(512, 512, Qt::KeepAspectRatio);
        out = QImage(qMax(1, qRound(size.width())), qMax(1, qRound(size.height())), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        renderer.render(&painter);
        return true;
    }
    QBuffer buffer;
    buffer.setData(data);
    QImageReader reader(&buffer, "png");
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > maxImageSide || size.height() > maxImageSide) {
        return false;
    }
    out = reader.read();
    if (out.isNull()) {
        return false;
    }
    // A big picture is scaled down once here rather than at every size.
    if (out.width() > 512 || out.height() > 512) {
        out = out.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return true;
}

// Every file in a zip, by its path inside it, without unpacking anything
// to disk.
struct Pack {
    QMap<QString, const KArchiveFile *> files;
};

bool listZip(const KArchiveDirectory *dir, const QString &prefix, Pack &pack, int depth)
{
    if (depth > 8) {
        return false;
    }
    for (const QString &name : dir->entries()) {
        if (pack.files.size() >= maxPackEntries) {
            return false;
        }
        const KArchiveEntry *entry = dir->entry(name);
        if (entry->isFile()) {
            pack.files.insert(prefix + name, static_cast<const KArchiveFile *>(entry));
        } else if (entry->isDirectory()) {
            if (!listZip(static_cast<const KArchiveDirectory *>(entry), prefix + name + u'/', pack, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

bool isPointerFile(const QString &name)
{
    return name.endsWith(QLatin1String(".cur"), Qt::CaseInsensitive) || name.endsWith(QLatin1String(".ani"), Qt::CaseInsensitive);
}

// The roles found in a pack, its files read through fetch(name), which
// looks the name up beside install.inf.
template<typename Fetch>
QMap<int, Pointer> readScheme(const QMap<int, QString> &scheme, Fetch fetch)
{
    QMap<int, Pointer> roles;
    QHash<QString, Pointer> done;
    qint64 total = 0;
    for (auto it = scheme.cbegin(); it != scheme.cend(); ++it) {
        const QString key = it.value().toLower();
        if (!done.contains(key)) {
            QByteArray data;
            Pointer pointer;
            // A role whose file is missing or bad keeps the white pointer;
            // the rest of the pack still works.
            if (isPointerFile(key) && fetch(it.value(), data) && (total += data.size()) <= maxUnpacked && WinCursor::read(data, pointer)) {
                done.insert(key, pointer);
            } else {
                done.insert(key, {});
            }
        }
        if (!done.value(key).isEmpty()) {
            roles.insert(it.key(), done.value(key));
        }
    }
    return roles;
}

bool readZip(const QString &path, QMap<int, Pointer> &roles)
{
    if (QFileInfo(path).size() > maxPack) {
        return false;
    }
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return false;
    }
    Pack pack;
    if (!listZip(zip.directory(), QString(), pack, 0)) {
        return false;
    }
    auto readEntry = [](const KArchiveFile *file, QByteArray &data) {
        if (file->size() > maxFile) {
            return false;
        }
        data = file->data();
        return data.size() <= maxFile;
    };

    // The install.inf nearest the top, and the files beside it. Packs that
    // come in several sizes side by side (Regular, Large, Extra Large) get
    // their regular one: the size slider makes it larger.
    QString inf;
    static const QRegularExpression regular(QStringLiteral("regular|normal|standard|default"), QRegularExpression::CaseInsensitiveOption);
    auto rank = [](const QString &name) {
        return name.count(u'/') * 2 + (regular.match(name).hasMatch() ? 0 : 1);
    };
    for (const QString &name : pack.files.keys()) {
        if (QFileInfo(name).fileName().compare(QLatin1String("install.inf"), Qt::CaseInsensitive) == 0 && (inf.isEmpty() || rank(name) < rank(inf))) {
            inf = name;
        }
    }
    if (inf.isEmpty()) {
        // No scheme: a zip holding a single pointer is that pointer.
        QStringList pointers;
        for (const QString &name : pack.files.keys()) {
            if (isPointerFile(name)) {
                pointers << name;
            }
        }
        QByteArray data;
        Pointer pointer;
        if (pointers.size() != 1 || !readEntry(pack.files.value(pointers.first()), data) || !WinCursor::read(data, pointer)) {
            return false;
        }
        roles.insert(WinCursor::Arrow, pointer);
        return true;
    }
    QByteArray infData;
    if (!readEntry(pack.files.value(inf), infData)) {
        return false;
    }
    const QString folder = inf.left(inf.lastIndexOf(u'/') + 1);
    roles = readScheme(WinCursor::readInf(infData), [&](const QString &name, QByteArray &data) {
        const KArchiveFile *beside = nullptr;
        const KArchiveFile *anywhere = nullptr;
        for (auto it = pack.files.cbegin(); it != pack.files.cend(); ++it) {
            if (QFileInfo(it.key()).fileName().compare(name, Qt::CaseInsensitive) == 0) {
                if (it.key().left(it.key().lastIndexOf(u'/') + 1) == folder) {
                    beside = it.value();
                } else if (!anywhere) {
                    anywhere = it.value();
                }
            }
        }
        const KArchiveFile *file = beside ? beside : anywhere;
        return file && readEntry(file, data);
    });
    return !roles.isEmpty();
}

bool readFolder(const QString &infPath, QMap<int, Pointer> &roles)
{
    QByteArray infData;
    if (!readCapped(infPath, 1024 * 1024, infData)) {
        return false;
    }
    const QDir folder = QFileInfo(infPath).absoluteDir();
    // Only regular files beside install.inf, never links out of the folder.
    const QStringList files = folder.entryList(QDir::Files | QDir::NoSymLinks | QDir::Readable);
    roles = readScheme(WinCursor::readInf(infData), [&](const QString &name, QByteArray &data) {
        for (const QString &file : files) {
            if (file.compare(name, Qt::CaseInsensitive) == 0) {
                return readCapped(folder.filePath(file), maxFile, data);
            }
        }
        return false;
    });
    return !roles.isEmpty();
}

// Breeze's own links: which names show the same picture.
QHash<QString, QString> breezeLinks()
{
    QHash<QString, QString> picture;
    const QDir dir(QStringLiteral("/usr/share/icons/Breeze_Light/cursors"));
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files)) {
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty()) {
            picture.insert(info.fileName(), QFileInfo(canonical).fileName());
        }
    }
    return picture;
}

bool replaceDir(const QString &fresh, const QString &target)
{
    const QString old = target + QStringLiteral(".old");
    QDir(old).removeRecursively();
    if (QFileInfo::exists(target) && !QDir().rename(target, old)) {
        return false;
    }
    if (!QDir().rename(fresh, target)) {
        QDir().rename(old, target);
        return false;
    }
    QDir(old).removeRecursively();
    return true;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QByteArray indexTheme(const QString &source, const QString &stamp)
{
    QString clean = source;
    clean.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f\\[\\]]")));
    QByteArray out = "[Icon Theme]\nName=Your own\nComment=A pointer added in Settings\nInherits=Breeze_Light\n\n[Wild OS]\n";
    if (!clean.isEmpty()) {
        out += "Source=" + clean.toUtf8() + '\n';
    }
    out += "Stamp=" + stamp.toLatin1() + '\n';
    return out;
}

QString newStamp()
{
    return QString::number(QRandomGenerator::system()->generate64(), 16);
}

int convert(const QString &input, const QString &target, qreal hotX, qreal hotY)
{
    const QFileInfo info(input);
    const QString suffix = info.suffix().toLower();
    QMap<int, Pointer> roles;
    qreal fill = 4.0 / 3.0;

    if (suffix == QLatin1String("cur") || suffix == QLatin1String("ani") || suffix == QLatin1String("ico")) {
        QByteArray data;
        Pointer pointer;
        if (!readCapped(input, maxFile, data) || !WinCursor::read(data, pointer)) {
            return unusable("not a Windows pointer");
        }
        roles.insert(WinCursor::Arrow, pointer);
    } else if (suffix == QLatin1String("png") || suffix == QLatin1String("svg")) {
        QByteArray data;
        Frame frame;
        if (!readCapped(input, suffix == QLatin1String("svg") ? 2 * 1024 * 1024 : maxFile, data) || !readPicture(data, suffix == QLatin1String("svg"), frame.image)) {
            return unusable("not a picture");
        }
        frame.hotspot = QPoint(qBound(0, int(hotX * frame.image.width()), frame.image.width() - 1),
                               qBound(0, int(hotY * frame.image.height()), frame.image.height() - 1));
        roles.insert(WinCursor::Arrow, {frame});
        // A picture is all pointer, with no empty canvas round it.
        fill = 1.0;
    } else if (suffix == QLatin1String("zip")) {
        if (!readZip(input, roles)) {
            return unusable("not a pointer pack");
        }
    } else if (info.fileName().compare(QLatin1String("install.inf"), Qt::CaseInsensitive) == 0 || suffix == QLatin1String("inf")) {
        if (!readFolder(input, roles)) {
            return unusable("not a pointer pack");
        }
    } else {
        return unusable("not a kind of file this reads");
    }

    const QString fresh = target + QStringLiteral(".new");
    QDir(fresh).removeRecursively();
    if (!QDir().mkpath(fresh + QStringLiteral("/cursors"))) {
        return failed(QStringLiteral("cannot write ") + fresh);
    }
    const QHash<QString, QString> breeze = breezeLinks();
    QSet<QString> taken;
    qint64 total = 0;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        const QStringList &names = roleNames.at(it.key());
        const QByteArray data = Xcursor::write(it.value(), sizesFor(it.value(), fill), fill);
        total += data.size();
        if (total > maxTheme) {
            QDir(fresh).removeRecursively();
            return unusable("too many or too large pictures");
        }
        const QString file = names.first();
        if (!writeFile(fresh + QStringLiteral("/cursors/") + file, data)) {
            QDir(fresh).removeRecursively();
            return failed(QStringLiteral("cannot write ") + file);
        }
        taken.insert(file);
        QSet<QString> pictures;
        for (const QString &name : names) {
            if (breeze.contains(name)) {
                pictures.insert(breeze.value(name));
            }
        }
        QStringList links = names.mid(1);
        for (auto b = breeze.cbegin(); b != breeze.cend(); ++b) {
            if (pictures.contains(b.value())) {
                links << b.key();
            }
        }
        for (const QString &link : std::as_const(links)) {
            if (!taken.contains(link) && validName.match(link).hasMatch()) {
                taken.insert(link);
                QFile::link(file, fresh + QStringLiteral("/cursors/") + link);
            }
        }
    }

    // The picture Settings shows in the "Your own" swatch.
    const Frame shown = roles.contains(WinCursor::Arrow) ? roles.value(WinCursor::Arrow).first() : roles.first().first();
    shown.image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(fresh + QStringLiteral("/preview.png"), "png");

    // A folder pack is known by its folder, not by install.inf.
    const QString source = suffix == QLatin1String("inf") ? info.absoluteDir().dirName() : info.fileName();
    if (!writeFile(fresh + QStringLiteral("/index.theme"), indexTheme(source, newStamp())) || !replaceDir(fresh, target)) {
        QDir(fresh).removeRecursively();
        return failed(QStringLiteral("cannot write ") + target);
    }
    return 0;
}

void putBytes(QByteArray &out, const QByteArray &bytes)
{
    const quint32 size = quint32(bytes.size());
    out.append(char(size & 0xff)).append(char((size >> 8) & 0xff)).append(char((size >> 16) & 0xff)).append(char(size >> 24));
    out.append(bytes);
}

bool takeBytes(const QByteArray &in, qsizetype &at, qint64 cap, QByteArray &bytes)
{
    if (at + 4 > in.size()) {
        return false;
    }
    const auto *p = reinterpret_cast<const uchar *>(in.constData() + at);
    const quint32 size = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    at += 4;
    if (size > cap || size > quint64(in.size() - at)) {
        return false;
    }
    bytes = in.mid(at, size);
    at += size;
    return true;
}

const QByteArray streamMagic = QByteArrayLiteral("WildOS pointer 1\n");

int exportTheme(const QString &theme)
{
    const QDir dir(theme + QStringLiteral("/cursors"));
    QByteArray out = streamMagic;
    qint64 total = 0;
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString name = info.fileName();
        if (!validName.match(name).hasMatch()) {
            continue;
        }
        if (info.isSymLink()) {
            const QString target = QFileInfo(info.symLinkTarget()).fileName();
            if (validName.match(target).hasMatch() && QFileInfo(info.symLinkTarget()).absolutePath() == dir.absolutePath()) {
                out += 'L';
                putBytes(out, name.toLatin1());
                putBytes(out, target.toLatin1());
            }
            continue;
        }
        QByteArray data;
        if (!readCapped(info.filePath(), 32 * 1024 * 1024, data) || (total += data.size()) > maxTheme) {
            return failed(QStringLiteral("cannot read ") + name);
        }
        out += 'F';
        putBytes(out, name.toLatin1());
        putBytes(out, data);
    }
    out += 'E';
    return std::fwrite(out.constData(), 1, size_t(out.size()), stdout) == size_t(out.size()) && std::fflush(stdout) == 0 ? 0 : 1;
}

int importTheme(const QString &target, const QString &stamp)
{
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{1,32}$")).match(stamp).hasMatch()) {
        return unusable("bad stamp");
    }
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly)) {
        return failed(QStringLiteral("cannot read the stream"));
    }
    QByteArray stream;
    for (;;) {
        const QByteArray chunk = in.read(1024 * 1024);
        if (chunk.isEmpty()) {
            break;
        }
        stream += chunk;
        if (stream.size() > maxTheme * 2) {
            return unusable("stream too large");
        }
    }
    if (!stream.startsWith(streamMagic)) {
        return unusable("not a pointer stream");
    }

    const QString fresh = target + QStringLiteral(".new");
    QDir(fresh).removeRecursively();
    if (!QDir().mkpath(fresh + QStringLiteral("/cursors"))) {
        return failed(QStringLiteral("cannot write ") + fresh);
    }
    auto fail = [&](const char *why) {
        QDir(fresh).removeRecursively();
        return unusable(why);
    };
    QSet<QString> files;
    QList<QPair<QString, QString>> links;
    qsizetype at = streamMagic.size();
    bool ended = false;
    while (at < stream.size() && !ended) {
        const char kind = stream.at(at++);
        QByteArray name, body;
        if (kind == 'E') {
            ended = true;
        } else if (kind == 'F' || kind == 'L') {
            if (!takeBytes(stream, at, 64, name) || !validName.match(QString::fromLatin1(name)).hasMatch()
                || !takeBytes(stream, at, kind == 'F' ? 32 * 1024 * 1024 : 64, body)) {
                return fail("damaged stream");
            }
            const QString n = QString::fromLatin1(name);
            if (files.contains(n) || files.size() + links.size() > 1000) {
                return fail("damaged stream");
            }
            if (kind == 'F') {
                QByteArray clean;
                if (!Xcursor::rewrite(body, clean) || !writeFile(fresh + QStringLiteral("/cursors/") + n, clean)) {
                    return fail("bad picture");
                }
                files.insert(n);
            } else {
                const QString target = QString::fromLatin1(body);
                if (!validName.match(target).hasMatch()) {
                    return fail("damaged stream");
                }
                links << qMakePair(n, target);
            }
        } else {
            return fail("damaged stream");
        }
    }
    // A stream cut short (the account's half failed) changes nothing.
    if (!ended || files.isEmpty()) {
        return fail("incomplete stream");
    }
    for (const auto &link : std::as_const(links)) {
        if (files.contains(link.second) && !files.contains(link.first)) {
            QFile::link(link.second, fresh + QStringLiteral("/cursors/") + link.first);
        }
    }
    if (!writeFile(fresh + QStringLiteral("/index.theme"), indexTheme(QString(), stamp)) || !replaceDir(fresh, target)) {
        QDir(fresh).removeRecursively();
        return failed(QStringLiteral("cannot write ") + target);
    }
    return 0;
}

void limit(int resource, rlim_t value)
{
    const rlimit lim{value, value};
    setrlimit(resource, &lim);
}
}

int main(int argc, char *argv[])
{
    // A crafted file should run out of these long before it hurts anything.
    limit(RLIMIT_CPU, 60);
    limit(RLIMIT_AS, rlim_t(4) * 1024 * 1024 * 1024);
    limit(RLIMIT_FSIZE, 64 * 1024 * 1024);
    limit(RLIMIT_CORE, 0);
    QImageReader::setAllocationLimit(64);

    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode == QLatin1String("convert") && (argc == 4 || argc == 6)) {
        // Drawing an SVG needs fonts, which need a GUI application, never
        // a screen.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        const QStringList args = app.arguments();
        bool okX = true, okY = true;
        const qreal x = argc == 6 ? args.at(4).toDouble(&okX) : 0;
        const qreal y = argc == 6 ? args.at(5).toDouble(&okY) : 0;
        if (!okX || !okY || x < 0 || x > 1 || y < 0 || y > 1) {
            return unusable("bad hotspot");
        }
        return convert(args.at(2), args.at(3), x, y);
    }
    if (mode == QLatin1String("export") && argc == 3) {
        QCoreApplication app(argc, argv);
        return exportTheme(app.arguments().at(2));
    }
    if (mode == QLatin1String("import") && argc == 4) {
        QCoreApplication app(argc, argv);
        return importTheme(app.arguments().at(2), app.arguments().at(3));
    }
    std::fprintf(stderr, "usage: pointer-import convert FILE THEME-DIR [HOTSPOT-X HOTSPOT-Y] | export THEME-DIR | import THEME-DIR STAMP\n");
    return 1;
}
