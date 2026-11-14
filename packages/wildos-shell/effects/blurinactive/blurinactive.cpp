/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "blurinactive.h"

#include "../focus/focus.h"

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
#include <window.h>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QFile>
#include <QVector2D>
#include <QVector3D>

#include <cmath>

Q_LOGGING_CATEGORY(WILDOS_BLURINACTIVE, "wildos.blurinactive", QtWarningMsg)

static void ensureResources()
{
    Q_INIT_RESOURCE(blurinactive);
}

namespace KWin
{

struct Strength {
    int iterations;
    float offset;
    qreal dim;
};

// The five steps of the slider in Settings. Each pass count has a range of
// offsets it can use before the blur shows blocks or diagonal lines; these
// stay inside the ranges KWin's blur effect uses (BlurEffect::
// initBlurStrengthValues). The dim grows with the blur, but stays light
// enough that the windows behind can still be read when looked at. The
// lightest is a single pass, for people who only want the window in use to
// stand out a little.
static constexpr Strength s_strengths[] = {
    {1, 1.5f, 0.05},
    {2, 2.0f, 0.09},
    {3, 3.0f, 0.14},
    {3, 5.0f, 0.17},
    {4, 6.0f, 0.20},
};

static const QString s_configGroup = QStringLiteral("Effect-wildos_blurinactive");

// KWin's fade between the old and new look of every window when the colours
// or the accent change. Wild OS turns it off for its own (crossfade), which
// sits before this effect; this keeps the blur right if KWin's is turned
// back on. KWin's sits after this effect, so for its length a window drawn
// through the rest of the chain comes back as its snapshot from before,
// which this effect had already blurred and dimmed. Blurring that again,
// and keeping it once the fade was over, darkened the desktop a little more
// with every accent picked.
static const QString s_crossFade = QStringLiteral("blendchanges");

// A blurred window's outline fades as it blurs. The outline reaches past
// the window, further than a repaint of a window without a shadow covers.
static void repaintWithOutline(EffectWindow *w)
{
    w->addRepaintFull();
    effects->addRepaint(w->frameGeometry().adjusted(-8, -8, 8, 8).toAlignedRect());
}

BlurInactiveEffect::BlurInactiveEffect()
{
    reconfigure(ReconfigureAll);

    connect(effects, &EffectsHandler::windowActivated, this, [this](EffectWindow *w) {
        setFocus(WildOS::focusAfterActivation(w, m_focus));
    });
    connect(effects, &EffectsHandler::windowAdded, this, [this](EffectWindow *w) {
        const auto refresh = [this]() {
            updateTargets();
        };
        connect(w, &EffectWindow::windowFullScreenChanged, this, refresh);
        connect(w, &EffectWindow::windowKeepAboveChanged, this, refresh);
        connect(w, &EffectWindow::windowModalityChanged, this, refresh);
        connect(w, &EffectWindow::minimizedChanged, this, [this](EffectWindow *w) {
            // KWin only says which window is next if there is one.
            if (w == m_focus && w->isMinimized()) {
                m_focus = nullptr;
            }
            updateTargets();
        });
        updateTargets();
    });
    connect(effects, &EffectsHandler::windowClosed, this, [this](EffectWindow *w) {
        if (m_focus == w) {
            // KWin hands focus to the next window as this one goes; wait for
            // that rather than blurring everything for a frame.
            m_focus = WildOS::focusAfterClosing(w);
            QMetaObject::invokeMethod(
                this,
                [this] {
                    m_focus = WildOS::focusAfterActivation(effects->activeWindow(), m_focus);
                    updateTargets();
                },
                Qt::QueuedConnection);
        }
    });
    connect(effects, &EffectsHandler::windowDeleted, this, &BlurInactiveEffect::forget);
    connect(effects, &EffectsHandler::activeFullScreenEffectChanged, this, &BlurInactiveEffect::updateTargets);
    connect(effects, &EffectsHandler::desktopChanged, this, &BlurInactiveEffect::updateTargets);
    connect(effects, &EffectsHandler::screenLockingChanged, this, [] {
        effects->addRepaintFull();
    });

    m_focus = WildOS::focusAfterActivation(effects->activeWindow(), nullptr);
    updateTargets();
}

BlurInactiveEffect::~BlurInactiveEffect()
{
    effects->makeOpenGLContextCurrent();
    for (auto &[w, state] : m_windows) {
        disconnect(state->damaged);
        disconnect(state->resized);
        w->setData(WildOS::BlurProgressRole, QVariant());
    }
    m_windows.clear();
    m_downsample.reset();
    m_upsample.reset();
    m_composite.reset();
    effects->addRepaintFull();
}

bool BlurInactiveEffect::supported()
{
    return effects->isOpenGLCompositing();
}

void BlurInactiveEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    const KConfigGroup group(config, s_configGroup);
    m_strength = std::clamp(group.readEntry("Strength", 3), 1, int(std::size(s_strengths)));
    m_desktop = group.readEntry("Desktop", true);
    const Strength &strength = s_strengths[m_strength - 1];
    m_iterations = strength.iterations;
    m_offset = strength.offset;
    m_dim = strength.dim;
    m_duration = animationTime(std::chrono::milliseconds(250));

    for (auto &[w, state] : m_windows) {
        state->dirty = true;
        w->addRepaintFull();
    }
    updateTargets();
}

bool BlurInactiveEffect::shouldBlur(EffectWindow *w) const
{
    // Overview and the other full screen effects draw windows of their own;
    // the ones underneath are not seen.
    if (effects->activeFullScreenEffect() || w->isMinimized() || !w->isOnCurrentDesktop()) {
        return false;
    }
    if (w->isDesktop()) {
        return m_desktop && m_focus;
    }
    // Full screen video and windows kept above the rest (picture in
    // picture) are being watched even when another window has focus.
    if (!WildOS::isWorkWindow(w) || w->isFullScreen() || w->keepAbove()) {
        return false;
    }
    // Whatever has the keyboard is never blurred, even if the window in
    // use was worked out wrong: typing into a blurred window is worse than
    // a sharp one too many.
    EffectWindow *active = effects->activeWindow();
    if (active && WildOS::isWorkWindow(active) && WildOS::belongTogether(w, active)) {
        return false;
    }
    return !WildOS::belongTogether(w, m_focus);
}

void BlurInactiveEffect::updateTargets()
{
    const auto windows = effects->stackingOrder();
    for (EffectWindow *w : windows) {
        // A closing window keeps what it had while it animates away.
        if (w->isDeleted()) {
            continue;
        }
        const qreal target = shouldBlur(w) ? 1.0 : 0.0;
        auto it = m_windows.find(w);
        if (it == m_windows.end()) {
            if (target == 0.0) {
                continue;
            }
            auto state = std::make_unique<State>();
            state->itemEffect = ItemEffect(w->windowItem());
            state->damaged = connect(w, &EffectWindow::windowDamaged, this, [this](EffectWindow *w) {
                if (auto it = m_windows.find(w); it != m_windows.end()) {
                    it->second->dirty = true;
                    // The blur reaches past the damaged area, so all of the
                    // window is drawn again, not only what changed.
                    w->addRepaintFull();
                }
            });
            state->resized = connect(w, &EffectWindow::windowExpandedGeometryChanged, this, [this](EffectWindow *w) {
                if (auto it = m_windows.find(w); it != m_windows.end()) {
                    it->second->dirty = true;
                }
            });
            it = m_windows.emplace(w, std::move(state)).first;
        }
        if (it->second->target != target) {
            it->second->target = target;
            repaintWithOutline(w);
        }
    }
}

void BlurInactiveEffect::setFocus(EffectWindow *w)
{
    if (w == m_focus) {
        return;
    }
    m_focus = w;
    updateTargets();
}

void BlurInactiveEffect::forget(EffectWindow *w)
{
    auto it = m_windows.find(w);
    if (it == m_windows.end()) {
        return;
    }
    disconnect(it->second->damaged);
    disconnect(it->second->resized);
    effects->makeOpenGLContextCurrent();
    m_windows.erase(it);
    if (m_focus == w) {
        m_focus = nullptr;
    }
}

void BlurInactiveEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    const bool crossFading = effects->isEffectActive(s_crossFade);
    if (m_crossFading && !crossFading) {
        // The windows are drawn afresh in their new colours.
        for (auto &[w, state] : m_windows) {
            state->dirty = true;
            w->addRepaintFull();
        }
    }
    m_crossFading = crossFading;

    bool moving = false;
    for (const auto &[w, state] : m_windows) {
        moving = moving || state->progress != state->target;
    }
    if (moving) {
        // The first frame of a change only starts the clock.
        const qreal elapsed = m_lastPresent.count() ? qreal((presentTime - m_lastPresent).count()) : 0.0;
        const qreal step = m_duration > 0 ? elapsed / m_duration : 1.0;
        for (auto &[w, state] : m_windows) {
            if (state->progress < state->target) {
                state->progress = std::min(state->target, state->progress + step);
            } else if (state->progress > state->target) {
                state->progress = std::max(state->target, state->progress - step);
            }
            w->setData(WildOS::BlurProgressRole, state->progress);
        }
        m_lastPresent = presentTime;
    }
    effects->prePaintScreen(data, presentTime);
}

void BlurInactiveEffect::postPaintScreen()
{
    bool moving = false;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        State &state = *it->second;
        if (state.progress != state.target) {
            repaintWithOutline(it->first);
            moving = true;
        }
        // Painting is the one place the compositor's context is known to be
        // current, so a window back to sharp gives up its textures here.
        if (state.progress <= 0.0 && state.target <= 0.0) {
            disconnect(state.damaged);
            disconnect(state.resized);
            it->first->setData(WildOS::BlurProgressRole, QVariant());
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }
    if (!moving) {
        m_lastPresent = std::chrono::milliseconds::zero();
    }
    effects->postPaintScreen();
}

bool BlurInactiveEffect::ensureShaders()
{
    if (m_composite) {
        return true;
    }
    if (m_shadersFailed) {
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

    const auto load = [&header](const QString &name, ShaderTraits traits) -> std::unique_ptr<GLShader> {
        QFile file(QStringLiteral(":/wildos/blurinactive/") + name);
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(WILDOS_BLURINACTIVE) << "Could not read" << name;
            return nullptr;
        }
        auto shader = ShaderManager::instance()->generateCustomShader(traits, QByteArray(), header + file.readAll());
        if (!shader || !shader->isValid()) {
            qCWarning(WILDOS_BLURINACTIVE) << name << "failed to build";
            return nullptr;
        }
        return shader;
    };

    m_downsample = load(QStringLiteral("downsample.frag"), ShaderTrait::MapTexture);
    m_upsample = load(QStringLiteral("upsample.frag"), ShaderTrait::MapTexture);
    m_composite = load(QStringLiteral("composite.frag"),
                       ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::AdjustSaturation | ShaderTrait::TransformColorspace);
    if (!m_downsample || !m_upsample || !m_composite) {
        m_downsample.reset();
        m_upsample.reset();
        m_composite.reset();
        m_shadersFailed = true;
        return false;
    }

    m_downOffsetLocation = m_downsample->uniformLocation("offset");
    m_downHalfpixelLocation = m_downsample->uniformLocation("halfpixel");
    m_upOffsetLocation = m_upsample->uniformLocation("offset");
    m_upHalfpixelLocation = m_upsample->uniformLocation("halfpixel");
    m_offsetLocation = m_composite->uniformLocation("offset");
    m_halfpixelLocation = m_composite->uniformLocation("halfpixel");
    m_progressLocation = m_composite->uniformLocation("progress");
    m_dimLocation = m_composite->uniformLocation("dim");
    m_expandedOriginLocation = m_composite->uniformLocation("expandedOrigin");
    m_expandedSizeLocation = m_composite->uniformLocation("expandedSize");
    m_frameSizeLocation = m_composite->uniformLocation("frameSize");
    m_radiusLocation = m_composite->uniformLocation("radius");
    return true;
}

bool BlurInactiveEffect::render(EffectWindow *w, State &state)
{
    const Output *screen = w->screen();
    const qreal scale = screen ? screen->scale() : 1.0;
    const QRectF logical = snapToPixels(w->expandedGeometry(), scale);
    const QSize size = (logical.size() * scale).toSize();
    if (size.isEmpty()) {
        return false;
    }

    if (state.textures.size() != size_t(m_iterations + 1) || state.textures[0]->size() != size || state.textureScale != scale) {
        state.textures.clear();
        state.framebuffers.clear();
        for (int i = 0; i <= m_iterations; ++i) {
            const QSize levelSize(std::max(1, size.width() >> i), std::max(1, size.height() >> i));
            auto texture = GLTexture::allocate(GL_RGBA8, levelSize);
            if (!texture) {
                state.textures.clear();
                state.framebuffers.clear();
                return false;
            }
            texture->setFilter(GL_LINEAR);
            texture->setWrapMode(GL_CLAMP_TO_EDGE);
            auto framebuffer = std::make_unique<GLFramebuffer>(texture.get());
            if (!framebuffer->valid()) {
                state.textures.clear();
                state.framebuffers.clear();
                return false;
            }
            state.textures.push_back(std::move(texture));
            state.framebuffers.push_back(std::move(framebuffer));
        }
        state.textureScale = scale;
        state.dirty = true;
        state.drawn = false;
    }

    // While colours cross fade, a window keeps the blur it had: what the
    // chain would draw now is the fade's snapshot, made of this blur.
    if (state.dirty && !w->isDeleted() && !(m_crossFading && state.drawn)) {
        RenderTarget renderTarget(state.framebuffers[0].get());
        RenderViewport viewport(logical, scale, renderTarget);
        GLFramebuffer::pushFramebuffer(state.framebuffers[0].get());
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        WindowPaintData data;
        data.setOpacity(1.0);
        // Called from inside this effect's drawWindow, so the window is drawn
        // by the effects after this one (the rounded corners) and not by the
        // ones before it, such as KWin's blur behind translucent windows.
        effects->drawWindow(renderTarget, viewport, w, PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT, infiniteRegion(), data);
        GLFramebuffer::popFramebuffer();
        blur(state);
        state.dirty = false;
        state.drawn = true;
    }
    return true;
}

void BlurInactiveEffect::blur(State &state)
{
    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const auto map = vbo->map<GLVertex2D>(6);
    if (!map) {
        return;
    }
    // The unit square, with the texture's y turned to match the projection:
    // offscreen textures are the GL way up.
    const QVector2D corners[] = {{0, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}};
    for (int i = 0; i < 6; ++i) {
        (*map)[i] = GLVertex2D{.position = corners[i], .texcoord = QVector2D(corners[i].x(), 1.0f - corners[i].y())};
    }
    vbo->unmap();
    vbo->bindArrays();

    QMatrix4x4 projection;
    projection.ortho(QRectF(0.0, 0.0, 1.0, 1.0));
    glDisable(GL_BLEND);

    {
        ShaderBinder binder(m_downsample.get());
        m_downsample->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projection);
        m_downsample->setUniform(m_downOffsetLocation, m_offset);
        for (size_t i = 1; i < state.framebuffers.size(); ++i) {
            const GLTexture *read = state.textures[i - 1].get();
            m_downsample->setUniform(m_downHalfpixelLocation, QVector2D(0.5f / read->width(), 0.5f / read->height()));
            state.textures[i - 1]->bind();
            GLFramebuffer::pushFramebuffer(state.framebuffers[i].get());
            vbo->draw(GL_TRIANGLES, 0, 6);
            GLFramebuffer::popFramebuffer();
        }
    }

    {
        ShaderBinder binder(m_upsample.get());
        m_upsample->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projection);
        m_upsample->setUniform(m_upOffsetLocation, m_offset);
        // Back up to level 1; the last step up to full size happens as the
        // window is put on screen.
        for (size_t i = state.framebuffers.size() - 1; i > 1; --i) {
            const GLTexture *read = state.textures[i].get();
            m_upsample->setUniform(m_upHalfpixelLocation, QVector2D(0.5f / read->width(), 0.5f / read->height()));
            state.textures[i]->bind();
            GLFramebuffer::pushFramebuffer(state.framebuffers[i - 1].get());
            vbo->draw(GL_TRIANGLES, 0, 6);
            GLFramebuffer::popFramebuffer();
        }
    }

    vbo->unbindArrays();
}

void BlurInactiveEffect::composite(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, const QRegion &region, const WindowPaintData &data, State &state)
{
    const qreal scale = viewport.scale();
    const QRectF expanded = snapToPixels(w->expandedGeometry(), scale);
    const QRectF frame = snapToPixels(w->frameGeometry(), scale);
    const QRectF visible = expanded.translated(-frame.topLeft());

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const auto map = vbo->map<GLVertex2D>(6);
    if (!map) {
        return;
    }
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

    // The blur only covers the window's own rounded shape; its shadow is
    // left as it is drawn. Maximised and snapped windows and the desktop
    // have square corners.
    qreal radius = 12.0;
    const Window *window = w->window();
    if (w->isDesktop()
        || (window && (window->maximizeMode() == MaximizeFull || window->tile() || window->quickTileMode() != QuickTileMode(QuickTileFlag::None)))) {
        radius = 0.0;
    } else if (w->hasDecoration()) {
        radius = 14.0;
    }

    const GLTexture *blurred = state.textures[1].get();
    const qreal rgb = data.brightness() * data.opacity();
    const qreal a = data.opacity();
    const auto toXYZ = renderTarget.colorDescription().containerColorimetry().toXYZ();

    ShaderBinder binder(m_composite.get());
    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(std::round(frame.x() * scale), std::round(frame.y() * scale));
    m_composite->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp * data.toMatrix(scale));
    m_composite->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
    m_composite->setUniform(GLShader::FloatUniform::Saturation, data.saturation());
    m_composite->setUniform(GLShader::Vec3Uniform::PrimaryBrightness, QVector3D(toXYZ(1, 0), toXYZ(1, 1), toXYZ(1, 2)));
    m_composite->setUniform(GLShader::IntUniform::Sampler, 0);
    m_composite->setUniform(GLShader::IntUniform::Sampler1, 1);
    m_composite->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
    m_composite->setUniform(m_offsetLocation, m_offset);
    m_composite->setUniform(m_halfpixelLocation, QVector2D(0.5f / blurred->width(), 0.5f / blurred->height()));
    m_composite->setUniform(m_progressLocation, float(state.progress));
    m_composite->setUniform(m_dimLocation, float(m_dim));
    m_composite->setUniform(m_expandedOriginLocation, QVector2D(visible.x(), visible.y()));
    m_composite->setUniform(m_expandedSizeLocation, QVector2D(visible.width(), visible.height()));
    m_composite->setUniform(m_frameSizeLocation, QVector2D(frame.width(), frame.height()));
    m_composite->setUniform(m_radiusLocation, float(radius));

    const bool clipping = region != infiniteRegion();
    const QRegion clipRegion = clipping ? viewport.mapToRenderTarget(region) : infiniteRegion();
    if (clipping) {
        glEnable(GL_SCISSOR_TEST);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE1);
    state.textures[1]->bind();
    glActiveTexture(GL_TEXTURE0);
    state.textures[0]->bind();
    vbo->draw(clipRegion, GL_TRIANGLES, 0, 6, clipping);
    state.textures[0]->unbind();
    glActiveTexture(GL_TEXTURE1);
    state.textures[1]->unbind();
    glActiveTexture(GL_TEXTURE0);

    glDisable(GL_BLEND);
    if (clipping) {
        glDisable(GL_SCISSOR_TEST);
    }
    vbo->unbindArrays();
}

void BlurInactiveEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    const auto it = m_windows.find(w);
    if (it == m_windows.end() || it->second->progress <= 0.0 || !ensureShaders() || !render(w, *it->second)) {
        effects->drawWindow(renderTarget, viewport, w, mask, region, data);
        return;
    }
    composite(renderTarget, viewport, w, region, data, *it->second);
}

bool BlurInactiveEffect::isActive() const
{
    return !m_windows.empty() && !effects->isScreenLocked();
}

int BlurInactiveEffect::requestedEffectChainPosition() const
{
    // After KWin's blur behind translucent windows (20), which has to see
    // the screen rather than this effect's texture; after the outline (55),
    // which stays sharp on top; before the rounded corners (60), so the
    // window is blurred already rounded.
    return 58;
}

KWIN_EFFECT_FACTORY_SUPPORTED(BlurInactiveEffect, "metadata.json", return BlurInactiveEffect::supported();)

} // namespace KWin

#include "blurinactive.moc"
