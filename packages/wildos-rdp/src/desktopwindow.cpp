#include "desktopwindow.h"

#include "client.h"
#include "desktop.h"
#include "input.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QWheelEvent>

#include <algorithm>
#include <cstring>

DesktopWindow::DesktopWindow(const QString &title)
    : m_session(new DesktopSession(this))
{
    setTitle(title);
    setSurfaceType(QSurface::RasterSurface);
    m_store = std::make_unique<QBackingStore>(this);

    // Windows lays the desktop out again once the size has settled, not for
    // every step of a drag.
    m_resize.setSingleShot(true);
    m_resize.setInterval(300);
    connect(&m_resize, &QTimer::timeout, this, [this] {
        if (m_connected) {
            m_session->resize(physicalSize(), devicePixelRatio());
        }
    });

    connect(m_session, &DesktopSession::certificateNeeded, this, [this](const QVariantMap &, bool changed) {
        m_session->answerCertificate(!changed, true);
    });
    connect(m_session, &DesktopSession::credentialsNeeded, this, [this](const QString &, const QString &, const QString &) {
        // The password comes with the arguments; asked for again means
        // Windows refused it, and the agent sets a new one next time.
        m_session->answerCredentials(false, {}, {}, {});
    });
    connect(m_session, &DesktopSession::connected, this, [this] {
        m_connected = true;
        m_session->resize(physicalSize(), devicePixelRatio());
    });
    connect(m_session, &DesktopSession::frameChanged, this, &DesktopWindow::damaged);
    connect(m_session, &DesktopSession::pointerImage, this, [this](const QImage &image, const QPoint &hotspot) {
        // Windows sends the pointer at its own pixels; the screen's scale
        // makes it the right size here.
        QPixmap pixmap = QPixmap::fromImage(image);
        const qreal dpr = devicePixelRatio();
        pixmap.setDevicePixelRatio(dpr);
        setCursor(QCursor(pixmap, int(hotspot.x() / dpr), int(hotspot.y() / dpr)));
    });
    connect(m_session, &DesktopSession::pointerHidden, this, [this] { setCursor(Qt::BlankCursor); });
    connect(m_session, &DesktopSession::pointerDefault, this, [this] { setCursor(Qt::ArrowCursor); });
    connect(m_session, &DesktopSession::finished, this, [this](const QString &code, bool authFailed) {
        m_connected = false;
        if (authFailed) {
            // Windows mode's agent reads its log for this word, and sets a
            // new password next time.
            qWarning("wildos-rdp: LOGON_FAILURE, Windows refused the password");
        }
        Q_EMIT ended(code);
    });
}

DesktopWindow::~DesktopWindow()
{
    // The connection thread is joined before anything it uses goes.
    m_session->stop();
}

bool DesktopWindow::start(const QList<QByteArray> &args, QString *error)
{
    if (!m_session->configure(args, error)) {
        return false;
    }
    // A window a little smaller than the screen, as Remote opens one.
    QScreen *screen = QGuiApplication::primaryScreen();
    const QSize logical = screen ? screen->availableSize() * 0.8 : QSize(1280, 800);
    resize(logical);
    const double dpr = screen ? screen->devicePixelRatio() : 1.0;
    m_session->setDisplay(logical * dpr, dpr);
    show();
    m_session->start();
    return true;
}

QSize DesktopWindow::physicalSize() const
{
    return (QSizeF(size()) * devicePixelRatio()).toSize();
}

void DesktopWindow::damaged(const QRegion &region)
{
    m_session->client()->withPrimary([this, &region](const uchar *data, int stride, QSize size) {
        if (m_image.size() != size) {
            // A new desktop size: the whole picture again.
            m_image = QImage(data, size.width(), size.height(), stride, QImage::Format_RGB32).copy();
            return;
        }
        for (const QRect &r : region) {
            const QRect c = r.intersected(QRect(QPoint(0, 0), size));
            for (int y = c.top(); y <= c.bottom(); ++y) {
                std::memcpy(m_image.scanLine(y) + qsizetype(c.left()) * 4, data + qsizetype(y) * stride + qsizetype(c.left()) * 4,
                            size_t(c.width()) * 4);
            }
        }
    });
    requestUpdate();
}

QRectF DesktopWindow::pictureRect() const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return {};
    }
    const double s = std::min(double(width()) / m_image.width(), double(height()) / m_image.height());
    const QSizeF shown(m_image.width() * s, m_image.height() * s);
    return QRectF(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
}

QPoint DesktopWindow::toRemote(const QPointF &local) const
{
    const QRectF pic = pictureRect();
    if (pic.isEmpty()) {
        return {};
    }
    const double x = (local.x() - pic.x()) / pic.width() * m_image.width();
    const double y = (local.y() - pic.y()) / pic.height() * m_image.height();
    return QPoint(std::clamp(int(x), 0, m_image.width() - 1), std::clamp(int(y), 0, m_image.height() - 1));
}

void DesktopWindow::render()
{
    if (!isExposed()) {
        return;
    }
    const QRect all(QPoint(0, 0), size());
    m_store->beginPaint(all);
    {
        QPainter p(m_store->paintDevice());
        p.fillRect(all, Qt::black);
        if (!m_image.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(pictureRect(), m_image);
        }
    }
    m_store->endPaint();
    m_store->flush(all);
}

bool DesktopWindow::event(QEvent *event)
{
    if (event->type() == QEvent::UpdateRequest) {
        render();
        return true;
    }
    return QWindow::event(event);
}

void DesktopWindow::exposeEvent(QExposeEvent *)
{
    render();
}

void DesktopWindow::resizeEvent(QResizeEvent *event)
{
    m_store->resize(event->size());
    m_resize.start();
    requestUpdate();
}

void DesktopWindow::mousePressEvent(QMouseEvent *event)
{
    rdpinput::button(m_session->client(), event->button(), true, toRemote(event->position()));
}

void DesktopWindow::mouseReleaseEvent(QMouseEvent *event)
{
    rdpinput::button(m_session->client(), event->button(), false, toRemote(event->position()));
}

void DesktopWindow::mouseMoveEvent(QMouseEvent *event)
{
    rdpinput::move(m_session->client(), toRemote(event->position()));
}

void DesktopWindow::wheelEvent(QWheelEvent *event)
{
    rdpinput::wheel(m_session->client(), event->angleDelta(), toRemote(event->position()), m_wheelX, m_wheelY);
}

void DesktopWindow::keyPressEvent(QKeyEvent *event)
{
    rdpinput::key(m_session->client(), event, true, m_keysDown);
}

void DesktopWindow::keyReleaseEvent(QKeyEvent *event)
{
    rdpinput::key(m_session->client(), event, false, m_keysDown);
}

void DesktopWindow::focusOutEvent(QFocusEvent *)
{
    rdpinput::releaseAll(m_session->client(), m_keysDown);
}
