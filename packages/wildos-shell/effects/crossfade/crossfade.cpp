/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "crossfade.h"

#include <core/output.h>
#include <core/pixelgrid.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glplatform.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>
#include <opengl/openglcontext.h>
#include <scene/windowitem.h>

#include <QDBusConnection>
#include <QFile>
#include <QTimer>
#include <QVector3D>

#include <cmath>

Q_LOGGING_CATEGORY(WILDOS_CROSSFADE, "wildos.crossfade", QtWarningMsg)

static void ensureResources()
{
    Q_INIT_RESOURCE(crossfade);
}

namespace KWin
{

static const QString s_path = QStringLiteral("/org/kde/KWin/BlendChanges");
static const QString s_interface = QStringLiteral("org.kde.KWin.BlendChanges");

ColourFadeEffect::ColourFadeEffect()
{
    m_timeline.setEasingCurve(QEasingCurve::InOutCubic);

    // KWin's own Blend Changes holds the path while it is loaded; Wild OS
    // turns it off in /etc/xdg/kwinrc. If someone turns it back on, theirs
    // answers and this effect stays idle.
    m_registered = QDBusConnection::sessionBus().registerObject(s_path, s_interface, this, QDBusConnection::ExportAllSlots);
    if (!m_registered) {
        qCWarning(WILDOS_CROSSFADE) << "Another effect answers" << s_path;
    }

    connect(effects, &EffectsHandler::windowDeleted, this, [this](EffectWindow *w) {
        if (auto it = m_windows.find(w); it != m_windows.end()) {
            effects->makeOpenGLContextCurrent();
            m_windows.erase(it);
        }
    });
}

ColourFadeEffect::~ColourFadeEffect()
{
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject(s_path);
    }
    effects->makeOpenGLContextCurrent();
    m_windows.clear();
    m_shader.reset();
}

bool ColourFadeEffect::supported()
{
    return effects->isOpenGLCompositing() && effects->animationsSupported();
}

bool ColourFadeEffect::allocate(const QSize &size, std::unique_ptr<GLTexture> &texture, std::unique_ptr<GLFramebuffer> &target)
{
    // Shadows are mostly faint alpha, which eight bits round off visibly.
    texture = GLTexture::allocate(GL_RGBA16F, size);
    if (!texture) {
        texture = GLTexture::allocate(GL_RGBA8, size);
    }
    if (!texture) {
        return false;
    }
    texture->setFilter(GL_LINEAR);
    texture->setWrapMode(GL_CLAMP_TO_EDGE);
    target = std::make_unique<GLFramebuffer>(texture.get());
    if (!target->valid()) {
        target.reset();
        texture.reset();
        return false;
    }
    return true;
}

void ColourFadeEffect::render(EffectWindow *w, const QRectF &geometry, qreal scale, GLFramebuffer *target)
{
    RenderTarget renderTarget(target);
    RenderViewport viewport(geometry, scale, renderTarget);
    GLFramebuffer::pushFramebuffer(target);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    WindowPaintData data;
    data.setOpacity(1.0);
    effects->drawWindow(renderTarget, viewport, w, PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT, infiniteRegion(), data);
    GLFramebuffer::popFramebuffer();
}

void ColourFadeEffect::start(int delay)
{
    if (!supported() || m_state != State::Off || effects->hasActiveFullScreenEffect()) {
        return;
    }

    effects->makeOpenGLContextCurrent();
    const auto windows = effects->stackingOrder();
    for (EffectWindow *w : windows) {
        // Full screen windows are games and video, left to draw at once,
        // as KWin's version does.
        if (w->isFullScreen() || w->isDeleted()) {
            continue;
        }
        const Output *screen = w->screen();
        const qreal scale = screen ? screen->scale() : 1.0;
        const QRectF geometry = snapToPixels(w->expandedGeometry(), scale);
        const QSize size = (geometry.size() * scale).toSize();
        if (size.isEmpty()) {
            continue;
        }
        auto snapshot = std::make_unique<Snapshot>();
        if (!allocate(size, snapshot->before, snapshot->beforeTarget)) {
            continue;
        }

        // Outside a frame, drawing a window runs the whole chain, this
        // effect included (it passes the window on, not holding it yet).
        // Blur and contrast behind the window would see an empty texture
        // rather than the screen, so they are kept out, as KWin does.
        const QVariant blurRole = w->data(WindowForceBlurRole);
        const QVariant contrastRole = w->data(WindowForceBackgroundContrastRole);
        w->setData(WindowForceBlurRole, QVariant());
        w->setData(WindowForceBackgroundContrastRole, QVariant());
        render(w, geometry, scale, snapshot->beforeTarget.get());
        w->setData(WindowForceBlurRole, blurRole);
        w->setData(WindowForceBackgroundContrastRole, contrastRole);

        snapshot->scale = scale;
        snapshot->itemEffect = ItemEffect(w->windowItem());
        m_windows[w] = std::move(snapshot);
    }
    if (m_windows.empty()) {
        return;
    }

    m_state = State::Holding;
    QTimer::singleShot(delay, this, [this] {
        if (m_state != State::Holding) {
            return;
        }
        m_timeline.reset();
        m_timeline.setDuration(std::chrono::milliseconds(std::lround(animationTime(std::chrono::milliseconds(400)))));
        m_state = State::Fading;
        effects->addRepaintFull();
    });
    effects->addRepaintFull();
}

void ColourFadeEffect::finish()
{
    effects->makeOpenGLContextCurrent();
    m_windows.clear();
    m_state = State::Off;
    effects->addRepaintFull();
}

void ColourFadeEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if (m_state == State::Fading) {
        m_timeline.advance(presentTime);
    }
    effects->prePaintScreen(data, presentTime);
}

void ColourFadeEffect::postPaintScreen()
{
    if (m_state == State::Fading && m_timeline.done()) {
        finish();
    } else if (m_state != State::Off) {
        effects->addRepaintFull();
    }
    effects->postPaintScreen();
}

bool ColourFadeEffect::ensureShader()
{
    if (m_shader) {
        return true;
    }
    if (m_shaderFailed) {
        return false;
    }
    ensureResources();

    // Same choice of dialect as KWin's own generated shaders.
    const auto context = OpenGlContext::currentContext();
    QByteArray header;
    bool modern;
    if (context->isOpenGLES()) {
        modern = context->glslVersion() >= Version(3, 0);
        header = modern ? QByteArrayLiteral("#version 300 es\n") : QByteArray();
        header += QByteArrayLiteral("precision highp float;\n");
    } else {
        modern = context->glslVersion() >= Version(1, 40);
        header = modern ? QByteArrayLiteral("#version 140\n") : QByteArray();
    }
    if (modern) {
        header += QByteArrayLiteral("#define IN in\n#define TEXTURE texture\n#define FRAG_COLOR fragColor\nout vec4 fragColor;\n");
    } else {
        header += QByteArrayLiteral("#define IN varying\n#define TEXTURE texture2D\n#define FRAG_COLOR gl_FragColor\n");
    }

    QFile file(QStringLiteral(":/wildos/crossfade/crossfade.frag"));
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(WILDOS_CROSSFADE) << "Could not read the shader";
        m_shaderFailed = true;
        return false;
    }
    m_shader = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::AdjustSaturation | ShaderTrait::TransformColorspace,
                                                              QByteArray(), header + file.readAll());
    if (!m_shader || !m_shader->isValid()) {
        qCWarning(WILDOS_CROSSFADE) << "The shader failed to build";
        m_shader.reset();
        m_shaderFailed = true;
        return false;
    }
    m_progressLocation = m_shader->uniformLocation("progress");
    return true;
}

void ColourFadeEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    const auto it = m_windows.find(w);
    if (it == m_windows.end() || !ensureShader()) {
        effects->drawWindow(renderTarget, viewport, w, mask, region, data);
        return;
    }
    Snapshot &snapshot = *it->second;

    // A window that changed size or moved to a screen of another scale
    // since the snapshot cannot be mixed with it pixel for pixel; it shows
    // its new look straight away.
    const qreal scale = viewport.scale();
    const QRectF expanded = snapToPixels(w->expandedGeometry(), scale);
    const QSize size = (expanded.size() * scale).toSize();
    if (scale != snapshot.scale || size != snapshot.before->size()) {
        m_windows.erase(it);
        effects->drawWindow(renderTarget, viewport, w, mask, region, data);
        return;
    }

    const qreal progress = m_state == State::Fading ? m_timeline.value() : 0.0;
    GLTexture *now = snapshot.before.get();
    if (progress > 0.0 && !w->isDeleted()) {
        if (snapshot.now || allocate(size, snapshot.now, snapshot.nowTarget)) {
            render(w, expanded, scale, snapshot.nowTarget.get());
            now = snapshot.now.get();
        }
    }

    const QRectF frame = snapToPixels(w->frameGeometry(), scale);
    const QRectF visible = expanded.translated(-frame.topLeft());

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const auto map = vbo->map<GLVertex2D>(6);
    if (!map) {
        return;
    }
    // Offscreen textures are the GL way up: y runs bottom to top.
    const float x0 = visible.left() * scale;
    const float y0 = visible.top() * scale;
    const float x1 = visible.right() * scale;
    const float y1 = visible.bottom() * scale;
    (*map)[0] = GLVertex2D{.position = {x0, y0}, .texcoord = {0, 1}};
    (*map)[1] = GLVertex2D{.position = {x1, y1}, .texcoord = {1, 0}};
    (*map)[2] = GLVertex2D{.position = {x0, y1}, .texcoord = {0, 0}};
    (*map)[3] = GLVertex2D{.position = {x0, y0}, .texcoord = {0, 1}};
    (*map)[4] = GLVertex2D{.position = {x1, y0}, .texcoord = {1, 1}};
    (*map)[5] = GLVertex2D{.position = {x1, y1}, .texcoord = {1, 0}};
    vbo->unmap();
    vbo->bindArrays();

    const qreal rgb = data.brightness() * data.opacity();
    const qreal a = data.opacity();
    const auto toXYZ = renderTarget.colorDescription().containerColorimetry().toXYZ();

    ShaderBinder binder(m_shader.get());
    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(std::round(frame.x() * scale), std::round(frame.y() * scale));
    m_shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp * data.toMatrix(scale));
    m_shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
    m_shader->setUniform(GLShader::FloatUniform::Saturation, data.saturation());
    m_shader->setUniform(GLShader::Vec3Uniform::PrimaryBrightness, QVector3D(toXYZ(1, 0), toXYZ(1, 1), toXYZ(1, 2)));
    m_shader->setUniform(GLShader::IntUniform::Sampler, 0);
    m_shader->setUniform(GLShader::IntUniform::Sampler1, 1);
    m_shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
    m_shader->setUniform(m_progressLocation, float(progress));

    const bool clipping = region != infiniteRegion();
    const QRegion clipRegion = clipping ? viewport.mapToRenderTarget(region) : infiniteRegion();
    if (clipping) {
        glEnable(GL_SCISSOR_TEST);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE1);
    snapshot.before->bind();
    glActiveTexture(GL_TEXTURE0);
    now->bind();
    vbo->draw(clipRegion, GL_TRIANGLES, 0, 6, clipping);
    now->unbind();
    glActiveTexture(GL_TEXTURE1);
    snapshot.before->unbind();
    glActiveTexture(GL_TEXTURE0);

    glDisable(GL_BLEND);
    if (clipping) {
        glDisable(GL_SCISSOR_TEST);
    }
    vbo->unbindArrays();
}

bool ColourFadeEffect::isActive() const
{
    return m_state != State::Off;
}

int ColourFadeEffect::requestedEffectChainPosition() const
{
    // After KWin's blur and contrast behind translucent windows (20, 21),
    // which draw onto the screen and so are never part of either look;
    // before the outline (55), the background blur (58) and the rounded
    // corners (60), which are.
    return 52;
}

KWIN_EFFECT_FACTORY_SUPPORTED(ColourFadeEffect, "metadata.json", return ColourFadeEffect::supported();)

} // namespace KWin

#include "crossfade.moc"
