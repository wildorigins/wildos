#pragma once

#include <QQuickAsyncImageProvider>

// image://thumb/<stamp>/<url>: a KIO preview of a photo, video or PDF for
// Large icons, made in the background the way Files makes its own; the
// file's icon stays until it arrives.
class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
