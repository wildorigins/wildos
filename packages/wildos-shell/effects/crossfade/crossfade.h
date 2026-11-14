/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <effect/effect.h>
#include <effect/timeline.h>
#include <scene/item.h>

#include <chrono>
#include <map>
#include <memory>

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class EffectWindow;

// Fades every window from its old look to its new one when the colour
// scheme or the accent changes, in place of KWin's Blend Changes, and
// answers the same D-Bus call (plasma-apply-colorscheme makes it).
//
// KWin's version draws the window as it is now and lays the snapshot from
// before over it, fading the snapshot out. Where a window is see-through,
// its shadow and its outline, the two add up, so every accent picked in
// Settings flashed a darker shadow round the window. Here the old and new
// looks are mixed in one pass instead, so anything that did not change
// stays exactly as it was.
//
// It sits before the outline and the background blur in the chain, so both
// looks include them, and the background blur never draws a window from a
// snapshot of its own work.
class ColourFadeEffect : public Effect
{
    Q_OBJECT

public:
    ColourFadeEffect();
    ~ColourFadeEffect() override;

    static bool supported();

    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data) override;
    void postPaintScreen() override;
    bool isActive() const override;
    int requestedEffectChainPosition() const override;

public Q_SLOTS:
    // Called before the colours change. The windows keep their old look
    // for delay milliseconds, long enough for every app to repaint, and
    // then fade to the new one.
    void start(int delay = 300);

private:
    struct Snapshot {
        std::unique_ptr<GLTexture> before;
        std::unique_ptr<GLFramebuffer> beforeTarget;
        std::unique_ptr<GLTexture> now;
        std::unique_ptr<GLFramebuffer> nowTarget;
        qreal scale = 1;
        // Keeps the window off direct scanout while it is drawn by us.
        ItemEffect itemEffect;
    };

    enum class State {
        Off,
        Holding,
        Fading,
    };

    bool ensureShader();
    static bool allocate(const QSize &size, std::unique_ptr<GLTexture> &texture, std::unique_ptr<GLFramebuffer> &target);
    static void render(EffectWindow *w, const QRectF &geometry, qreal scale, GLFramebuffer *target);
    void finish();

    std::map<EffectWindow *, std::unique_ptr<Snapshot>> m_windows;
    State m_state = State::Off;
    TimeLine m_timeline;
    bool m_registered = false;

    bool m_shaderFailed = false;
    std::unique_ptr<GLShader> m_shader;
    int m_progressLocation = -1;
};

} // namespace KWin
