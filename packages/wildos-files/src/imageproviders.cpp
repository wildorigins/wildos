#include "imageproviders.h"

#include <KFileItem>
#include <KIO/PreviewJob>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QMutex>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QRunnable>
#include <QSvgRenderer>
#include <QUrl>

#include <atomic>
#include <memory>

namespace
{
QString smallIconPath(const QString &name)
{
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return it.value();
    }
    QString found;
    const QStringList dirs = QIcon::themeSearchPaths();
    for (const QString &dir : dirs) {
        for (const char *context : {"mimetypes", "places", "apps"}) {
            const QString path = QStringLiteral("%1/%2/16x16/%3/%4.svg").arg(dir, QIcon::themeName(), QLatin1String(context), name);
            if (QFile::exists(path)) {
                found = path;
                break;
            }
        }
        if (!found.isEmpty()) {
            break;
        }
    }
    cache.insert(name, found);
    return found;
}

QStringList candidates(const QString &id)
{
    QStringList names;
    const QStringList parts = id.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        // The icon naming spec's fallbacks: x-office-spreadsheet, then
        // x-office, then x.
        QString name = part;
        while (!name.isEmpty()) {
            names.append(name);
            const auto dash = name.lastIndexOf(QLatin1Char('-'));
            if (dash <= 0) {
                break;
            }
            name.truncate(dash);
        }
    }
    names.append(QStringLiteral("text-x-generic"));
    return names;
}
}

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int side = requestedSize.isValid() ? std::max(requestedSize.width(), requestedSize.height()) : 48;
    if (size) {
        *size = QSize(side, side);
    }
    const QStringList names = candidates(QUrl::fromPercentEncoding(id.toUtf8()));
    if (side <= 24) {
        for (const QString &name : names) {
            const QString path = smallIconPath(name);
            if (path.isEmpty()) {
                if (QIcon::hasThemeIcon(name)) {
                    break;
                }
                continue;
            }
            QSvgRenderer svg(path);
            QPixmap pixmap(side, side);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            svg.render(&painter, QRectF(0, 0, side, side));
            return pixmap;
        }
    }
    for (const QString &name : names) {
        if (QIcon::hasThemeIcon(name)) {
            return QIcon::fromTheme(name).pixmap(side, side);
        }
    }
    return QIcon::fromTheme(QStringLiteral("unknown")).pixmap(side, side);
}

QPixmap DragIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int side = requestedSize.isValid() ? std::max(requestedSize.width(), requestedSize.height()) : 64;
    if (size) {
        *size = QSize(side, side);
    }
    const int count = id.section(QLatin1Char('/'), 0, 0).toInt();
    QColor fill(QUrl::fromPercentEncoding(id.section(QLatin1Char('/'), 1, 1).toUtf8()));
    QColor ink(QUrl::fromPercentEncoding(id.section(QLatin1Char('/'), 2, 2).toUtf8()));
    if (!fill.isValid() || !ink.isValid()) {
        fill = QGuiApplication::palette().color(QPalette::Highlight);
        ink = QGuiApplication::palette().color(QPalette::HighlightedText);
    }
    const int iconSide = side * 3 / 4;
    const QPixmap icon = IconProvider::requestPixmap(id.section(QLatin1Char('/'), 3), nullptr, QSize(iconSide, iconSide));

    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // A fainter copy behind and to one side reads as a pile.
    const int step = side / 8;
    painter.setOpacity(0.55);
    painter.drawPixmap(QRect(step, 0, iconSide, iconSide), icon);
    painter.setOpacity(1);
    painter.drawPixmap(QRect(0, step, iconSide, iconSide), icon);

    const QString text = count > 99 ? QStringLiteral("99+") : QString::number(count);
    QFont font(QStringLiteral("Albert Sans"));
    font.setPixelSize(std::max(10, side * 3 / 16));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    const int height = std::max(16, side * 5 / 16);
    const int width = std::max(height, painter.fontMetrics().horizontalAdvance(text) + height * 3 / 5);
    const QRectF badge(side - width, side - height, width, height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(badge, height / 2.0, height / 2.0);
    painter.setPen(ink);
    painter.drawText(badge, Qt::AlignCenter, text);
    return pixmap;
}

namespace
{
class ThumbnailResponse : public QQuickImageResponse
{
public:
    ThumbnailResponse(const QUrl &url, const QSize &size)
    {
        // KIO jobs belong to the main thread.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, url, size, guard = QPointer<ThumbnailResponse>(this)] {
            if (!guard) {
                return;
            }
            const KFileItem item(url);
            auto job = KIO::filePreview(KFileItemList{item}, size.isValid() ? size : QSize(256, 256));
            job->setScaleType(KIO::PreviewJob::ScaledAndCached);
            job->setIgnoreMaximumSize(false);
            m_job = job;
            QObject::connect(job, &KIO::PreviewJob::gotPreview, job, [guard](const KFileItem &, const QPixmap &pixmap) {
                if (guard) {
                    guard->m_image = pixmap.toImage();
                }
            });
            QObject::connect(job, &KJob::result, job, [guard] {
                if (guard) {
                    Q_EMIT guard->finished();
                }
            });
        }, Qt::QueuedConnection);
    }

    ~ThumbnailResponse() override
    {
        stop();
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QCoreApplication::translate("ImageProviders", "No preview") : QString();
    }

    void cancel() override
    {
        stop();
    }

private:
    void stop()
    {
        // Responses can die on the image loader thread; the job lives on
        // the main one.
        if (KIO::PreviewJob *job = m_job.data()) {
            QMetaObject::invokeMethod(job, [job = QPointer<KIO::PreviewJob>(job)] {
                if (job) {
                    job->kill();
                }
            }, Qt::QueuedConnection);
        }
    }

    QImage m_image;
    QPointer<KIO::PreviewJob> m_job;
};
}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // The id is "<stamp>/<url>": the stamp changes with the file, so the
    // image cache never serves a stale picture.
    const QString encoded = id.section(QLatin1Char('/'), 1);
    const QUrl url(QUrl::fromPercentEncoding(encoded.toUtf8()));
    return new ThumbnailResponse(url, requestedSize);
}

namespace
{
class PictureResponse;

// What a response and the worker decoding for it share. The response can go
// away first, when Quick Look has moved on, so the worker reaches it only
// through here, under the lock.
struct PictureTask {
    QMutex mutex;
    PictureResponse *response = nullptr;
    std::atomic_bool cancelled{false};
};

class PictureResponse : public QQuickImageResponse
{
public:
    explicit PictureResponse(const std::shared_ptr<PictureTask> &task)
        : m_task(task)
    {
        m_task->response = this;
    }

    ~PictureResponse() override
    {
        const QMutexLocker lock(&m_task->mutex);
        m_task->response = nullptr;
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QCoreApplication::translate("ImageProviders", "No preview") : QString();
    }

    void cancel() override
    {
        m_task->cancelled.store(true);
    }

    void deliver(const QImage &image)
    {
        m_image = image;
        Q_EMIT finished();
    }

private:
    std::shared_ptr<PictureTask> m_task;
    QImage m_image;
};

QImage readPicture(const QString &path, QSize bounds)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid() && !full.isEmpty()) {
        // The reader scales before it turns a photo the way the camera was
        // held, so the box is turned to match.
        if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) {
            bounds.transpose();
        }
        // Drawings fill the box; photos are only ever made smaller.
        const QByteArray format = reader.format();
        const bool drawing = format == "svg" || format == "svgz";
        if (drawing || full.width() > bounds.width() || full.height() > bounds.height()) {
            reader.setScaledSize(full.scaled(bounds, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
        }
    }
    return reader.read();
}

class PictureWork : public QRunnable
{
public:
    PictureWork(const std::shared_ptr<PictureTask> &task, const QString &path, const QSize &bounds)
        : m_task(task)
        , m_path(path)
        , m_bounds(bounds)
    {
    }

    void run() override
    {
        // A picture skipped past while it waited its turn is not decoded.
        QImage image;
        if (!m_task->cancelled.load()) {
            image = readPicture(m_path, m_bounds);
        }
        const QMutexLocker lock(&m_task->mutex);
        if (PictureResponse *response = m_task->response) {
            QMetaObject::invokeMethod(response, [task = m_task, image] {
                if (task->response) {
                    task->response->deliver(image);
                }
            }, Qt::QueuedConnection);
        }
    }

private:
    std::shared_ptr<PictureTask> m_task;
    QString m_path;
    QSize m_bounds;
};
}

PictureProvider::PictureProvider()
{
    // Two at a time: the picture on screen never waits behind more than
    // one neighbour, and two large photos are the most ever held in full.
    m_pool.setMaxThreadCount(2);
}

PictureProvider::~PictureProvider()
{
    m_pool.clear();
    m_pool.waitForDone();
}

QQuickImageResponse *PictureProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const bool now = id.section(QLatin1Char('/'), 0, 0) == QLatin1String("now");
    const QUrl url(QUrl::fromPercentEncoding(id.section(QLatin1Char('/'), 1).toUtf8()));
    const QSize bounds = requestedSize.width() > 0 && requestedSize.height() > 0 ? requestedSize : QSize(3840, 3840);
    auto task = std::make_shared<PictureTask>();
    auto response = new PictureResponse(task);
    m_pool.start(new PictureWork(task, url.toLocalFile(), bounds), now ? 1 : 0);
    return response;
}
