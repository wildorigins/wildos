#include "thumbnail.h"

#include <KFileItem>
#include <KIO/PreviewJob>
#include <QPainter>

#include <cmath>

Thumbnail::Thumbnail(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

Thumbnail::~Thumbnail()
{
    if (m_job) {
        m_job->kill();
    }
}

void Thumbnail::setUrl(const QUrl &url)
{
    if (url != m_url) {
        m_url = url;
        Q_EMIT urlChanged();
        if (isComponentComplete()) {
            load();
        }
    }
}

void Thumbnail::setStamp(qint64 stamp)
{
    if (stamp != m_stamp) {
        m_stamp = stamp;
        Q_EMIT urlChanged();
        if (isComponentComplete()) {
            load();
        }
    }
}

void Thumbnail::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    load();
}

void Thumbnail::load()
{
    if (m_job) {
        m_job->kill();
    }
    const bool had = ready();
    m_pixmap = QPixmap();
    if (had) {
        Q_EMIT readyChanged();
    }
    update();
    if (!m_url.isValid() || m_url.isEmpty()) {
        return;
    }
    const int side = std::max(256, int(std::ceil(std::max(width(), height()) * 2)));
    // Without a list KIO only runs its image and folder thumbnailers,
    // so videos, songs and documents kept their plain icons.
    static const QStringList plugins = KIO::PreviewJob::defaultPlugins();
    auto job = KIO::filePreview(KFileItemList{KFileItem(m_url)}, QSize(side, side), &plugins);
    job->setScaleType(KIO::PreviewJob::ScaledAndCached);
    m_job = job;
    connect(job, &KIO::PreviewJob::gotPreview, this, [this](const KFileItem &, const QPixmap &pixmap) {
        m_pixmap = pixmap;
        Q_EMIT readyChanged();
        update();
    });
}

void Thumbnail::paint(QPainter *painter)
{
    if (m_pixmap.isNull()) {
        return;
    }
    QSizeF size = m_pixmap.size();
    size.scale(boundingRect().size(), Qt::KeepAspectRatio);
    const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawPixmap(target, m_pixmap, QRectF(m_pixmap.rect()));
}
