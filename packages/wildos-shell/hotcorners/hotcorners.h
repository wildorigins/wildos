/*
    SPDX-FileCopyrightText: 2026 Wild OS Developers <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <plugin.h>

#include <KConfigWatcher>

#include <QPointF>
#include <QPointer>
#include <QTimer>

#include <array>
#include <memory>

class QQmlEngine;
class QQuickWindow;

namespace KWin
{
class Output;
}

namespace WildOS
{

// Hot corners from Settings > Multitasking. KWin's own screen corners
// cannot ask for a key to be held, wait for the pointer to rest without
// pushing it back, or wait for the pointer to leave before firing again, so
// Wild OS watches the corners itself and keeps KWin's corner actions off.
//
// Settings writes wildosrc [HotCorners]: TopLeft, TopRight, BottomLeft and
// BottomRight hold an action, the same names with "Key" after them the key
// to hold, Delay the rest in milliseconds and SkipFullScreen whether a full
// screen app or game in front turns the corners off. Changes apply at once.
//
// Only the outer corners of the whole desktop count: a screen corner that
// touches another screen is a way across, not a corner.
//
// Nothing here records where the pointer went or what was pressed.
class HotCorners : public KWin::Plugin
{
    Q_OBJECT

public:
    HotCorners();
    ~HotCorners() override;

private:
    enum Corner {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        CornerCount,
    };

    struct Setting {
        QString action;
        Qt::KeyboardModifiers key = Qt::NoModifier;
    };

    struct Spot {
        Corner corner;
        QPointF point;
        QPointer<KWin::Output> output;
    };

    void readConfig();
    void migrate();
    void keepKWinCornersOff();
    void updateSpots();

    void pointerMoved(const QPointF &pos);
    void evaluate();
    bool blocked(const Spot &spot) const;
    bool keyHeld(Corner corner) const;
    void fire();
    void run(const QString &action, const Spot &spot);

    void invokeShortcut(const QString &component, const QString &name);
    void askShell(const QString &method, const QPointF &point);
    void lock();
    void turnScreenOff();
    void toggleKeepAwake(const Spot &spot);
    void applyKeepAwake(bool on);
    void toggleDoNotDisturb(const Spot &spot);
    // Takes the untranslated text: the translations come with WildOS.UI,
    // which KWin loads only once the toast is first created.
    void showToast(const char *text, const Spot &spot);

    KConfigWatcher::Ptr m_watcher;
    std::array<Setting, CornerCount> m_settings;
    int m_delay = 150;
    bool m_skipFullScreen = true;

    QList<Spot> m_spots;
    int m_current = -1;
    QPointF m_pointer;
    // Set once a corner has fired, until the pointer has moved away from it.
    bool m_fired = false;
    QPointF m_firedAt;
    QTimer m_dwell;
    QTimer m_screenOff;

    bool m_keepAwake = false;
    bool m_policyPending = false;
    bool m_screenSaverPending = false;
    uint m_policyCookie = 0;
    uint m_screenSaverCookie = 0;

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_toast;
};

}
