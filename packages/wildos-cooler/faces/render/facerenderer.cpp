#include "facerenderer.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSurfaceFormat>

FaceRenderer::FaceRenderer(int size, QObject *parent)
    : QObject(parent)
    , m_size(size)
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);

    m_context = std::make_unique<QOpenGLContext>();
    m_context->setFormat(format);
    if (!m_context->create()) {
        m_error = tr("No OpenGL context could be made.");
        return;
    }

    m_surface = std::make_unique<QOffscreenSurface>();
    m_surface->setFormat(m_context->format());
    m_surface->create();

    m_control = std::make_unique<QQuickRenderControl>();
    m_window = std::make_unique<QQuickWindow>(m_control.get());
    m_window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(m_context.get()));
    m_window->setColor(Qt::black);
    m_window->setGeometry(0, 0, size, size);
    m_window->contentItem()->setSize(QSizeF(size, size));

    if (!m_context->makeCurrent(m_surface.get()) || !m_control->initialize()) {
        m_error = tr("The scene could not be set up for drawing.");
        return;
    }

    m_target = std::make_unique<QOpenGLFramebufferObject>(QSize(size, size));
    // A context that cannot draw into a texture gives black pictures and
    // only a warning from Qt; say so here instead.
    if (!m_target->isValid()) {
        m_error = tr("This OpenGL cannot draw into a picture.");
        return;
    }
    m_window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(m_target->texture(), QSize(size, size)));

    m_engine = std::make_unique<QQmlEngine>();
    QQmlComponent component(m_engine.get());
    component.loadFromModule("WildOS.Cooler.Faces", "FaceView");
    QObject *object = component.createWithInitialProperties({
        { QStringLiteral("width"), size },
        { QStringLiteral("height"), size },
        { QStringLiteral("faceId"), QString() },
    });
    m_view = qobject_cast<QQuickItem *>(object);
    if (!m_view) {
        delete object;
        m_error = component.errorString();
        return;
    }
    m_view->setParentItem(m_window->contentItem());
}

FaceRenderer::~FaceRenderer()
{
    // The scene's textures and the target belong to the context, so they go
    // while it is current, and the window before the control that drives it.
    if (m_context && m_surface)
        m_context->makeCurrent(m_surface.get());
    delete m_view;
    m_engine.reset();
    m_window.reset();
    m_control.reset();
    m_target.reset();
    if (m_context)
        m_context->doneCurrent();
}

bool FaceRenderer::setFace(const QString &id)
{
    if (!m_view)
        return false;
    m_view->setProperty("faceId", id);
    if (!m_view->property("ready").toBool()) {
        m_error = tr("There is no face called %1.").arg(id);
        return false;
    }
    return true;
}

void FaceRenderer::setReadings(const QVariantMap &readings)
{
    if (m_view)
        m_view->setProperty("readings", readings);
}

void FaceRenderer::setThemeColours(const QVariant &colours)
{
    if (m_view)
        m_view->setProperty("themeColours", colours);
}

void FaceRenderer::setShowMark(bool show)
{
    if (m_view)
        m_view->setProperty("showMark", show);
}

void FaceRenderer::setCircular(bool circular)
{
    if (m_view)
        m_view->setProperty("circular", circular);
}

QImage FaceRenderer::render(qreal seconds)
{
    if (!m_view)
        return {};

    m_view->setProperty("time", seconds);
    QCoreApplication::processEvents();

    if (!m_context->makeCurrent(m_surface.get()))
        return {};
    m_control->polishItems();
    m_control->beginFrame();
    m_control->sync();
    m_control->render();
    m_control->endFrame();
    m_context->functions()->glFlush();

    return m_target->toImage().convertToFormat(QImage::Format_RGB888);
}
