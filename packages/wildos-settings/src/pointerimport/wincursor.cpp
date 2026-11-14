#include "cursor.h"

#include <QBuffer>
#include <QImageReader>
#include <QRegularExpression>
#include <QStringDecoder>

#include <cstring>

namespace
{
// Windows draws pointers up to 256 pixels; a PNG inside a .cur may claim
// more, which is refused rather than decoded.
constexpr int maxSide = 256;
constexpr int maxFrames = 64;
constexpr int maxSteps = 256;

quint16 u16(const QByteArray &d, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray &d, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint32 be32(const QByteArray &d, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

bool fits(const QByteArray &d, quint64 at, quint64 length)
{
    return at <= quint64(d.size()) && length <= quint64(d.size()) - at;
}

bool isPng(const QByteArray &d)
{
    return d.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1a\n"));
}

bool readPng(const QByteArray &bytes, QImage &out)
{
    // The size from the header first, so a huge PNG is never allocated.
    if (bytes.size() < 24 || be32(bytes, 16) == 0 || be32(bytes, 20) == 0 || be32(bytes, 16) > maxSide || be32(bytes, 20) > maxSide) {
        return false;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    QImageReader reader(&buffer, "png");
    reader.setAutoTransform(false);
    out = reader.read();
    return !out.isNull() && out.width() <= maxSide && out.height() <= maxSide;
}

// A device-independent bitmap as an icon stores it: the colour picture
// followed by its one-bit transparency mask, both bottom row first, and a
// header height that counts both.
bool readDib(const QByteArray &d, QImage &out)
{
    if (d.size() < 40) {
        return false;
    }
    const quint32 headerSize = u32(d, 0);
    const qint32 width = qint32(u32(d, 4));
    const qint32 doubled = qint32(u32(d, 8));
    const quint16 planes = u16(d, 12);
    const quint16 bits = u16(d, 14);
    const quint32 compression = u32(d, 16);
    quint32 coloursUsed = u32(d, 32);
    if (headerSize < 40 || headerSize > 124 || planes != 1 || compression != 0) {
        return false;
    }
    if (width <= 0 || width > maxSide || doubled <= 0 || doubled % 2 != 0 || doubled / 2 > maxSide) {
        return false;
    }
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32) {
        return false;
    }
    const int height = doubled / 2;
    quint32 paletteCount = 0;
    if (bits <= 8) {
        const quint32 full = 1u << bits;
        paletteCount = coloursUsed == 0 || coloursUsed > full ? full : coloursUsed;
    }
    const quint64 paletteAt = headerSize;
    const quint64 pixelsAt = paletteAt + quint64(paletteCount) * 4;
    const quint64 stride = ((quint64(width) * bits + 31) / 32) * 4;
    const quint64 maskStride = ((quint64(width) + 31) / 32) * 4;
    const quint64 maskAt = pixelsAt + stride * height;
    if (!fits(d, paletteAt, quint64(paletteCount) * 4) || !fits(d, pixelsAt, stride * height)) {
        return false;
    }
    // Some 32-bit pictures leave the mask out and rely on their alpha.
    const bool hasMask = fits(d, maskAt, maskStride * height);
    if (!hasMask && bits != 32) {
        return false;
    }

    const auto *bytes = reinterpret_cast<const uchar *>(d.constData());
    auto palette = [&](quint32 index) -> QRgb {
        if (index >= paletteCount) {
            return qRgb(0, 0, 0);
        }
        const uchar *p = bytes + paletteAt + index * 4;
        return qRgb(p[2], p[1], p[0]);
    };

    QImage image(width, height, QImage::Format_ARGB32);
    bool anyAlpha = false;
    for (int row = 0; row < height; ++row) {
        const uchar *line = bytes + pixelsAt + stride * (height - 1 - row);
        const uchar *mask = hasMask ? bytes + maskAt + maskStride * (height - 1 - row) : nullptr;
        auto *outLine = reinterpret_cast<QRgb *>(image.scanLine(row));
        for (int x = 0; x < width; ++x) {
            QRgb colour;
            int alpha = 255;
            switch (bits) {
            case 1:
                colour = palette((line[x / 8] >> (7 - x % 8)) & 1);
                break;
            case 4:
                colour = palette((line[x / 2] >> (x % 2 ? 0 : 4)) & 0x0f);
                break;
            case 8:
                colour = palette(line[x]);
                break;
            case 24:
                colour = qRgb(line[x * 3 + 2], line[x * 3 + 1], line[x * 3]);
                break;
            default:
                colour = qRgb(line[x * 4 + 2], line[x * 4 + 1], line[x * 4]);
                alpha = line[x * 4 + 3];
                anyAlpha = anyAlpha || alpha != 0;
                break;
            }
            const bool masked = mask && ((mask[x / 8] >> (7 - x % 8)) & 1);
            if (bits != 32) {
                // A masked pixel with colour inverts the screen on Windows
                // (the classic text pointer); black is the nearest a
                // picture can show and stays visible on light windows.
                if (masked) {
                    alpha = (colour & 0xffffff) ? 255 : 0;
                    if (alpha) {
                        colour = qRgb(0, 0, 0);
                    }
                }
            }
            outLine[x] = qRgba(qRed(colour), qGreen(colour), qBlue(colour), alpha);
        }
    }
    if (bits == 32 && !anyAlpha) {
        // Old 32-bit pictures with an empty alpha channel use the mask.
        for (int row = 0; row < height; ++row) {
            const uchar *mask = hasMask ? bytes + maskAt + maskStride * (height - 1 - row) : nullptr;
            auto *outLine = reinterpret_cast<QRgb *>(image.scanLine(row));
            for (int x = 0; x < width; ++x) {
                const bool masked = mask && ((mask[x / 8] >> (7 - x % 8)) & 1);
                outLine[x] = (outLine[x] & 0xffffff) | (masked ? 0u : 0xff000000u);
            }
        }
    }
    out = image;
    return true;
}

QString decode(const QByteArray &data)
{
    if (data.startsWith("\xff\xfe") || data.startsWith("\xfe\xff")) {
        QStringDecoder decoder(QStringDecoder::Utf16);
        return decoder.decode(data);
    }
    if (data.startsWith("\xef\xbb\xbf")) {
        return QString::fromUtf8(data.mid(3));
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(data);
    return utf8.hasError() ? QString::fromLatin1(data) : text;
}

// The fields of an inf line, split at commas outside quotes, unquoted.
QStringList fields(const QString &line)
{
    QStringList out;
    QString current;
    bool quoted = false;
    for (const QChar c : line) {
        if (c == u'"') {
            quoted = !quoted;
        } else if (c == u',' && !quoted) {
            out << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    out << current.trimmed();
    return out;
}

QString fileName(const QString &path)
{
    QString name = path.trimmed();
    name.replace(u'/', u'\\');
    name = name.mid(name.lastIndexOf(u'\\') + 1);
    return name.trimmed();
}
}

namespace WinCursor
{
bool readCur(const QByteArray &data, Frame &out)
{
    if (data.size() < 6 || u16(data, 0) != 0) {
        return false;
    }
    const quint16 type = u16(data, 2);
    const quint16 count = u16(data, 4);
    if ((type != 1 && type != 2) || count == 0 || count > 64 || !fits(data, 6, quint64(count) * 16)) {
        return false;
    }

    // The largest picture, then the deepest, judged from each picture's
    // own header rather than the directory, which old tools fill badly.
    int best = -1;
    quint64 bestScore = 0;
    for (int i = 0; i < count; ++i) {
        const qsizetype entry = 6 + i * 16;
        const quint32 length = u32(data, entry + 8);
        const quint32 offset = u32(data, entry + 12);
        if (length < 40 || !fits(data, offset, length)) {
            return false;
        }
        const QByteArray picture = QByteArray::fromRawData(data.constData() + offset, length);
        quint64 width = 0, height = 0, depth = 0;
        if (isPng(picture)) {
            if (length < 26) {
                return false;
            }
            width = be32(picture, 16);
            height = be32(picture, 20);
            depth = 32;
        } else {
            width = u32(picture, 4);
            height = u32(picture, 8) / 2;
            depth = u16(picture, 14);
        }
        if (width == 0 || height == 0 || width > maxSide || height > maxSide) {
            return false;
        }
        const quint64 score = width * height * 64 + depth;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }

    const qsizetype entry = 6 + best * 16;
    const QByteArray picture = data.mid(u32(data, entry + 12), u32(data, entry + 8));
    QImage image;
    if (!(isPng(picture) ? readPng(picture, image) : readDib(picture, image))) {
        return false;
    }
    out.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // An .ico has no hotspot; its directory holds colour counts there.
    out.hotspot = type == 2 ? QPoint(u16(data, entry + 4), u16(data, entry + 6)) : QPoint(0, 0);
    out.hotspot.setX(qBound(0, out.hotspot.x(), out.image.width() - 1));
    out.hotspot.setY(qBound(0, out.hotspot.y(), out.image.height() - 1));
    out.delay = 0;
    return true;
}

bool readAni(const QByteArray &data, Pointer &out)
{
    if (data.size() < 12 || !data.startsWith("RIFF") || data.mid(8, 4) != "ACON") {
        return false;
    }
    const quint64 end = qMin<quint64>(quint64(u32(data, 4)) + 8, quint64(data.size()));

    QList<QByteArray> icons;
    QList<quint32> rates, sequence;
    bool haveHeader = false;
    quint32 flags = 0, displayRate = 0;

    // Chunks, and the chunks inside a LIST fram, which hold the frames.
    struct Span {
        quint64 at, end;
    };
    QList<Span> spans{{12, end}};
    while (!spans.isEmpty()) {
        Span span = spans.takeLast();
        quint64 at = span.at;
        while (at + 8 <= span.end) {
            const QByteArray id = data.mid(at, 4);
            const quint64 size = u32(data, at + 4);
            const quint64 body = at + 8;
            if (size > span.end - body) {
                return false;
            }
            if (id == "anih") {
                if (size < 36) {
                    return false;
                }
                displayRate = u32(data, body + 28);
                flags = u32(data, body + 32);
                haveHeader = true;
            } else if (id == "rate" || id == "seq ") {
                if (size / 4 > maxSteps) {
                    return false;
                }
                QList<quint32> &list = id == "rate" ? rates : sequence;
                list.clear();
                for (quint64 i = 0; i + 4 <= size; i += 4) {
                    list << u32(data, body + i);
                }
            } else if (id == "LIST") {
                if (size >= 4 && data.mid(body, 4) == "fram") {
                    if (spans.size() > 4) {
                        return false;
                    }
                    spans << Span{body + 4, body + size};
                }
            } else if (id == "icon") {
                if (icons.size() >= maxFrames) {
                    return false;
                }
                icons << data.mid(body, size);
            }
            at = body + size + (size & 1);
        }
    }

    // Frames stored as bare bitmaps (flag 1 clear) were never common and
    // are not read.
    if (!haveHeader || !(flags & 1) || icons.isEmpty()) {
        return false;
    }
    QList<Frame> frames;
    for (const QByteArray &icon : icons) {
        Frame frame;
        if (!readCur(icon, frame)) {
            return false;
        }
        frames << frame;
    }

    const qsizetype steps = !sequence.isEmpty() ? sequence.size() : !rates.isEmpty() ? qMin(rates.size(), frames.size()) : frames.size();
    if (steps == 0 || steps > maxSteps) {
        return false;
    }
    out.clear();
    for (qsizetype i = 0; i < steps; ++i) {
        const quint32 index = sequence.isEmpty() ? quint32(i) : sequence.at(i);
        if (index >= quint32(frames.size())) {
            return false;
        }
        // Rates are in jiffies, sixtieths of a second.
        const quint32 jiffies = qBound<quint32>(1, i < rates.size() ? rates.at(i) : displayRate, 600);
        Frame frame = frames.at(index);
        frame.delay = int(jiffies * 1000 / 60);
        out << frame;
    }
    return true;
}

bool read(const QByteArray &data, Pointer &out)
{
    if (data.startsWith("RIFF")) {
        return readAni(data, out);
    }
    Frame frame;
    if (!readCur(data, frame)) {
        return false;
    }
    out = {frame};
    return true;
}

QMap<int, QString> readInf(const QByteArray &data)
{
    const QString text = decode(data);
    // Comments start with ; outside quotes; sections and keys ignore case.
    QMap<QString, QList<QStringList>> sections;
    QMap<QString, QString> strings;
    QString section;
    for (QString line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")))) {
        bool quoted = false;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line.at(i) == u'"') {
                quoted = !quoted;
            } else if (line.at(i) == u';' && !quoted) {
                line.truncate(i);
                break;
            }
        }
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        if (section == QLatin1String("strings")) {
            const qsizetype eq = line.indexOf(u'=');
            if (eq > 0) {
                QString value = line.mid(eq + 1).trimmed();
                if (value.size() >= 2 && value.startsWith(u'"') && value.endsWith(u'"')) {
                    value = value.mid(1, value.size() - 2);
                }
                strings.insert(line.left(eq).trimmed().toLower(), value);
            }
        } else {
            sections[section] << fields(line);
        }
    }

    // %name% from [Strings]; %10% and other numbers are Windows folders,
    // which only matter for the folder part this drops anyway.
    auto expand = [&](QString value) {
        static const QRegularExpression token(QStringLiteral("%([^%]*)%"));
        QString result;
        qsizetype last = 0;
        auto it = token.globalMatch(value);
        while (it.hasNext()) {
            const auto match = it.next();
            result += value.mid(last, match.capturedStart() - last);
            result += strings.value(match.captured(1).toLower());
            last = match.capturedEnd();
        }
        result += value.mid(last);
        return result;
    };

    static const char *const valueNames[RoleCount] = {"arrow", "help", "appstarting", "wait", "crosshair", "ibeam", "nwpen",
                                                      "no", "sizens", "sizewe", "sizenwse", "sizenesw", "sizeall", "uparrow", "hand"};
    QMap<int, QString> roles;
    for (const auto &lines : std::as_const(sections)) {
        for (const QStringList &f : lines) {
            if (f.size() < 5 || f.at(0).compare(QLatin1String("HKCU"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString key = expand(f.at(1)).replace(u'/', u'\\').toLower();
            if (key.endsWith(QLatin1String("control panel\\cursors\\schemes"))) {
                // The scheme: every role's file in the standard order,
                // comma separated inside one value.
                const QStringList files = expand(f.mid(4).join(u',')).split(u',');
                for (int role = 0; role < RoleCount && role < files.size(); ++role) {
                    const QString name = fileName(files.at(role));
                    if (!name.isEmpty()) {
                        roles.insert(role, name);
                    }
                }
            } else if (key.endsWith(QLatin1String("control panel\\cursors"))) {
                const QString valueName = expand(f.at(2)).toLower();
                for (int role = 0; role < RoleCount; ++role) {
                    const QString name = fileName(expand(f.at(4)));
                    if (valueName == QLatin1String(valueNames[role]) && !name.isEmpty() && !roles.contains(role)) {
                        roles.insert(role, name);
                    }
                }
            }
        }
    }
    return roles;
}
}
