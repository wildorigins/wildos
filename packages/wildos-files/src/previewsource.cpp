#include "previewsource.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>
#include <QStringDecoder>
#include <QtConcurrentRun>

namespace
{
// Enough for any source file people read in a preview; a log of hundreds of
// megabytes would otherwise be loaded whole into a text view.
constexpr qint64 TextLimit = 200 * 1024;

struct Result {
    QString kind;
    QString mimeType;
    QString text;
    bool truncated = false;
};

// Text is a file with no NUL bytes near its start. Catches JSON, YAML, logs
// and config files whose MIME type does not say text/plain.
bool looksLikeText(const QByteArray &head)
{
    return !head.isEmpty() && !head.contains('\0');
}

// A picture shown still: animated formats play in their own view.
bool stillPicture(const QString &mime)
{
    static const QList<QByteArray> images = QImageReader::supportedMimeTypes();
    return mime.startsWith(QLatin1String("image/")) && mime != QLatin1String("image/gif") && mime != QLatin1String("image/webp")
        && images.contains(mime.toLatin1());
}

Result inspect(const QString &path)
{
    Result r;
    const QFileInfo info(path);
    if (info.isDir()) {
        r.kind = QStringLiteral("folder");
        return r;
    }
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    const QString name = mime.name();
    r.mimeType = name;

    if (name == QLatin1String("application/pdf")) {
        r.kind = QStringLiteral("pdf");
        return r;
    }
    if (name == QLatin1String("image/gif") || name == QLatin1String("image/webp")) {
        r.kind = QStringLiteral("animated");
        return r;
    }
    if (stillPicture(name)) {
        r.kind = QStringLiteral("image");
        return r;
    }

    const bool textType = mime.inherits(QStringLiteral("text/plain"));
    if (!textType && name.startsWith(QLatin1String("video/"))) {
        r.kind = QStringLiteral("video");
        return r;
    }
    if (!textType && name.startsWith(QLatin1String("audio/"))) {
        r.kind = QStringLiteral("audio");
        return r;
    }

    r.kind = QStringLiteral("other");
    if (info.size() == 0) {
        return r;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return r;
    }
    const QByteArray data = file.read(TextLimit);
    if (!looksLikeText(data.left(8192))) {
        return r;
    }
    r.truncated = info.size() > data.size();
    QByteArray body = data;
    // Cut at the last whole line, so a character split by the limit never
    // shows up as a replacement mark.
    if (r.truncated) {
        const qsizetype end = body.lastIndexOf('\n');
        if (end > 0) {
            body.truncate(end);
        }
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(body);
    if (utf8.hasError()) {
        // Older Windows text files are not UTF-8; Latin-1 shows them close
        // enough to read.
        text = QString::fromLatin1(body);
    }
    r.kind = QStringLiteral("text");
    r.text = text;
    return r;
}
}

PreviewSource::PreviewSource(QObject *parent)
    : QObject(parent)
{
}

bool PreviewSource::showsAsPicture(const QString &mimeType) const
{
    return stillPicture(mimeType);
}

void PreviewSource::setUrl(const QUrl &url)
{
    if (url == m_url) {
        return;
    }
    m_url = url;
    Q_EMIT urlChanged();
    m_kind.clear();
    m_mimeType.clear();
    m_text.clear();
    m_truncated = false;
    const int generation = ++m_generation;
    if (url.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    // Anything outside the local file system (the Recycle Bin, network
    // places) is read through KIO, which the image and media views cannot
    // do, so those get the icon and details.
    if (!url.isLocalFile()) {
        m_kind = QStringLiteral("other");
        Q_EMIT changed();
        return;
    }
    Q_EMIT changed();

    const QString path = url.toLocalFile();
    QPointer<PreviewSource> self(this);
    (void)QtConcurrent::run([path, self, generation] {
        const Result r = inspect(path);
        QMetaObject::invokeMethod(qApp, [self, generation, r] {
            if (!self || self->m_generation != generation) {
                return;
            }
            self->m_kind = r.kind;
            self->m_mimeType = r.mimeType;
            self->m_text = r.text;
            self->m_truncated = r.truncated;
            Q_EMIT self->changed();
        });
    });
}
