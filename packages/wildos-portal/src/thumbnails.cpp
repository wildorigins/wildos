#include "thumbnails.h"

#include <KFileItem>
#include <KIO/PreviewJob>
#include <QCoreApplication>
#include <QPointer>
#include <QUrl>

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
        return m_image.isNull() ? QStringLiteral("No preview") : QString();
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
    // The stamp changes with the file, so the image cache never serves a
    // stale picture.
    const QString encoded = id.section(QLatin1Char('/'), 1);
    const QUrl url(QUrl::fromPercentEncoding(encoded.toUtf8()));
    return new ThumbnailResponse(url, requestedSize);
}
