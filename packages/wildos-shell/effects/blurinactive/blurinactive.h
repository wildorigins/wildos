/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <effect/effect.h>
#include <scene/item.h>

#include <QMetaObject>

#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class EffectWindow;

// Settings > Accessibility > Blur windows in the background: every window
// but the one in use (and, if chosen, the desktop) is softened and dimmed a
// little, so the window in use is the only sharp thing on screen. KWin's own
// Dim Inactive only darkens.
//
// A blurred window is drawn into a texture of its own, blurred with the same
// dual Kawase passes KWin's blur effect uses, and composited back with its
// own alpha so its outline and rounded corners stay crisp. The blur is only
// redone when the window's content changes; a window that is not blurred
// costs nothing, and with the setting off the effect is not loaded at all.
class BlurInactiveEffect : public Effect
{
    Q_OBJECT

public:
    BlurInactiveEffect();
    ~BlurInactiveEffect() override;

    static bool supported();

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data) override;
    void postPaintScreen() override;
    bool isActive() const override;
    int requestedEffectChainPosition() const override;

private:
    struct State {
        // 0 sharp, 1 fully blurred and dimmed.
        qreal progress = 0;
        qreal target = 0;
        bool dirty = true;
        // Whether textures hold a drawing of the window yet.
        bool drawn = false;
        qreal textureScale = 1;
        // Level 0 is the window as drawn, each further level half the size
        // of the one before; after blurring, level 1 holds the result.
        std::vector<std::unique_ptr<GLTexture>> textures;
        std::vector<std::unique_ptr<GLFramebuffer>> framebuffers;
        // Keeps the window off direct scanout while it is drawn by us.
        ItemEffect itemEffect;
        QMetaObject::Connection damaged;
        QMetaObject::Connection resized;
    };

    bool shouldBlur(EffectWindow *w) const;
    void updateTargets();
    void setFocus(EffectWindow *w);
    void forget(EffectWindow *w);
    bool ensureShaders();
    bool render(EffectWindow *w, State &state);
    void blur(State &state);
    void composite(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, const QRegion &region, const WindowPaintData &data, State &state);

    std::map<EffectWindow *, std::unique_ptr<State>> m_windows;
    EffectWindow *m_focus = nullptr;

    int m_strength = 3;
    bool m_desktop = true;
    int m_iterations = 3;
    float m_offset = 5.0;
    qreal m_dim = 0.14;
    qreal m_duration = 250;
    std::chrono::milliseconds m_lastPresent = std::chrono::milliseconds::zero();
    bool m_crossFading = false;

    bool m_shadersFailed = false;
    std::unique_ptr<GLShader> m_downsample;
    std::unique_ptr<GLShader> m_upsample;
    std::unique_ptr<GLShader> m_composite;
    int m_downOffsetLocation = -1;
    int m_downHalfpixelLocation = -1;
    int m_upOffsetLocation = -1;
    int m_upHalfpixelLocation = -1;
    int m_offsetLocation = -1;
    int m_halfpixelLocation = -1;
    int m_progressLocation = -1;
    int m_dimLocation = -1;
    int m_expandedOriginLocation = -1;
    int m_expandedSizeLocation = -1;
    int m_frameSizeLocation = -1;
    int m_radiusLocation = -1;
};

} // namespace KWin
