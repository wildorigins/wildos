#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>
#include <QThreadPool>

// image://icon/<name>[,<fallback>]: a Wild OS theme icon. At list sizes
// it uses the theme's simplified 16 px drawings, scaled crisply.
class IconProvider : public QQuickImageProvider
{
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// image://dragicon/<count>/<fill>/<ink>/<name>: what follows the pointer
// while several items are dragged: the icon as a small pile with how many
// there are in a badge of the given colours.
class DragIconProvider : public IconProvider
{
public:
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// image://thumb/<url>: a KIO preview (photos, videos, PDFs...) made in
// the background; the icon stays until it arrives.
class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

// image://picture/<now|next>/<url>: a picture for Quick Look, decoded on a
// worker thread no larger than the size asked for, so a camera file never
// takes more memory than the screen can show. "now" is the picture being
// looked at and goes ahead of the ones made ready either side of it.
class PictureProvider : public QQuickAsyncImageProvider
{
public:
    PictureProvider();
    ~PictureProvider() override;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QThreadPool m_pool;
};
