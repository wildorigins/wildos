#include "cursor.h"

#include <QHash>
#include <QtEndian>

#include <cmath>

// The Xcursor format, as libXcursor and KWin read it: a header, a table of
// contents, and one chunk per picture holding its nominal size, size,
// hotspot, delay and premultiplied ARGB pixels, all little endian.
namespace
{
constexpr quint32 magic = 0x72756358; // "Xcur"
constexpr quint32 imageType = 0xfffd0002;
constexpr quint32 fileHeader = 16;
constexpr quint32 chunkHeader = 36;
constexpr quint32 maxPictureSide = 1024;
constexpr quint32 maxPictures = 4096;

void put(QByteArray &out, quint32 value)
{
    const quint32 le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char *>(&le), 4);
}

quint32 get(const QByteArray &d, qsizetype at)
{
    return qFromLittleEndian<quint32>(d.constData() + at);
}

struct Picture {
    quint32 nominal;
    QImage image;
    QPoint hotspot;
    quint32 delay;
};

QByteArray assemble(const QList<Picture> &pictures)
{
    QByteArray out;
    put(out, magic);
    put(out, fileHeader);
    put(out, 0x10000);
    put(out, quint32(pictures.size()));
    quint32 position = fileHeader + 12 * quint32(pictures.size());
    for (const Picture &p : pictures) {
        put(out, imageType);
        put(out, p.nominal);
        put(out, position);
        position += chunkHeader + 4 * quint32(p.image.width() * p.image.height());
    }
    for (const Picture &p : pictures) {
        put(out, chunkHeader);
        put(out, imageType);
        put(out, p.nominal);
        put(out, 1);
        put(out, quint32(p.image.width()));
        put(out, quint32(p.image.height()));
        put(out, quint32(p.hotspot.x()));
        put(out, quint32(p.hotspot.y()));
        put(out, p.delay);
        for (int y = 0; y < p.image.height(); ++y) {
            const auto *line = reinterpret_cast<const quint32 *>(p.image.constScanLine(y));
            for (int x = 0; x < p.image.width(); ++x) {
                put(out, line[x]);
            }
        }
    }
    return out;
}
}

namespace Xcursor
{
QByteArray write(const Pointer &pointer, const QList<int> &sizes, qreal fill)
{
    QList<Picture> pictures;
    // An animation repeats frames; each is scaled once per size.
    QHash<QPair<qint64, int>, Picture> scaled;
    for (const int nominal : sizes) {
        for (const Frame &frame : pointer) {
            const auto key = qMakePair(frame.image.cacheKey(), nominal);
            auto it = scaled.constFind(key);
            if (it == scaled.constEnd()) {
                const QImage &src = frame.image;
                const qreal scale = nominal * fill / qMax(src.width(), src.height());
                const int width = qBound(1, qRound(src.width() * scale), int(maxPictureSide));
                const int height = qBound(1, qRound(src.height() * scale), int(maxPictureSide));
                Picture p;
                p.nominal = quint32(nominal);
                p.image = (src.size() == QSize(width, height) ? src : src.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation))
                              .convertToFormat(QImage::Format_ARGB32_Premultiplied);
                // The middle of the hotspot pixel lands in the same place.
                p.hotspot = QPoint(qBound(0, int(std::floor((frame.hotspot.x() + 0.5) * width / src.width())), width - 1),
                                   qBound(0, int(std::floor((frame.hotspot.y() + 0.5) * height / src.height())), height - 1));
                it = scaled.insert(key, p);
            }
            Picture p = it.value();
            p.delay = quint32(pointer.size() > 1 ? qMax(frame.delay, 1) : 0);
            pictures << p;
        }
    }
    return assemble(pictures);
}

bool rewrite(const QByteArray &data, QByteArray &out)
{
    if (data.size() < qsizetype(fileHeader) || get(data, 0) != magic) {
        return false;
    }
    const quint32 header = get(data, 4);
    const quint32 count = get(data, 12);
    if (header < fileHeader || count == 0 || count > maxPictures || quint64(header) + 12ull * count > quint64(data.size())) {
        return false;
    }
    QList<Picture> pictures;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype entry = header + 12 * i;
        if (get(data, entry) != imageType) {
            continue; // comments and other chunk types are left behind
        }
        const quint64 at = get(data, entry + 8);
        if (at + chunkHeader > quint64(data.size()) || get(data, at) != chunkHeader || get(data, at + 4) != imageType) {
            return false;
        }
        Picture p;
        p.nominal = get(data, at + 8);
        const quint32 width = get(data, at + 16);
        const quint32 height = get(data, at + 20);
        const quint32 x = get(data, at + 24);
        const quint32 y = get(data, at + 28);
        p.delay = qMin<quint32>(get(data, at + 32), 60000);
        if (p.nominal == 0 || p.nominal > maxPictureSide || width == 0 || height == 0 || width > maxPictureSide || height > maxPictureSide
            || x >= width || y >= height || at + chunkHeader + 4ull * width * height > quint64(data.size())) {
            return false;
        }
        p.image = QImage(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
        for (quint32 row = 0; row < height; ++row) {
            auto *line = reinterpret_cast<quint32 *>(p.image.scanLine(int(row)));
            for (quint32 col = 0; col < width; ++col) {
                // Premultiplied: no channel may be brighter than alpha.
                const quint32 v = get(data, at + chunkHeader + 4 * (row * width + col));
                const quint32 a = v >> 24;
                line[col] = (a << 24) | (qMin((v >> 16) & 0xff, a) << 16) | (qMin((v >> 8) & 0xff, a) << 8) | qMin(v & 0xff, a);
            }
        }
        p.hotspot = QPoint(int(x), int(y));
        pictures << p;
    }
    if (pictures.isEmpty()) {
        return false;
    }
    out = assemble(pictures);
    return true;
}
}
