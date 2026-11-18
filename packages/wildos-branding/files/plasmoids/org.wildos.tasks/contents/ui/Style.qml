pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami
import WildOS.UI as UI

// Values from the Wild OS design tokens and the Taskbar artboard, kept in one
// place so the taskbar cannot drift from Start and the flyouts.
QtObject {
    readonly property string fontFamily: "Albert Sans"
    // Settings > Personalisation > Text size sets the system font; Albert
    // Sans 10 pt is the Wild OS default, so 10 pt draws text at the design's sizes.
    readonly property real textScale: Math.max(0.75, Kirigami.Theme.defaultFont.pointSize / 10)

    function px(size: real): int {
        return Math.round(size * textScale);
    }
    // As WildOS.UI's Style.tracking: Albert Sans is set a touch tighter.
    function tracking(pixelSize: real): real {
        return -0.006 * pixelSize;
    }

    // Colours come from the shared tokens, so the buttons follow light and
    // dark mode, the accent and high contrast like Start beside them.
    readonly property color ink: UI.Style.iconInk
    readonly property color accent: UI.Style.accent
    readonly property color textOnAccent: UI.Style.textOnAccent
    readonly property color warning: UI.Style.warning

    readonly property color hover: UI.Style.surfaceHover
    readonly property color pressed: UI.Style.surfacePressed
    // The app in front sits on a soft highlight (LaunchStates, focused).
    readonly property color focused: UI.Style.tone(0.10)
    readonly property color focusedHover: UI.Style.tone(0.14)
    // Starting: a pale line in the accent and a faint glow that pulses
    // with it.
    readonly property color startingMark: UI.Style.accentText
    readonly property color startingGlow: UI.Style.tone(0.07)
    readonly property color attention: Qt.rgba(UI.Style.warning.r, UI.Style.warning.g, UI.Style.warning.b, 0.22)
    // The speaker badge is a solid disc, so the icon under it never shows
    // through.
    readonly property color badge: UI.Style.windowOpaque
    readonly property color badgeHover: Qt.tint(UI.Style.windowOpaque, UI.Style.surfacePressed)
    readonly property color badgeBorder: UI.Style.borderControl
    readonly property color progressTrack: UI.Style.tone(0.22)

    // The artboard's app buttons: 48 px square, 12 px corners, 4 px apart.
    // Icons are trimmed to their visible shape and drawn as tall as the Start
    // glyph's dots (16.6 of 24 units in a 30 px box), not the artboard's
    // 34 px, which towered over Start and Task view on the docked bar. The
    // running mark is 6 by 3 px, 2 px off the bottom; the focused one 18 px
    // (LaunchStates).
    readonly property int button: 48
    // With more apps open than the taskbar has room for, the buttons
    // narrow to this before anything is pushed off the bar: the 21 px icon
    // with a little shading either side.
    readonly property int buttonNarrow: 32
    readonly property int buttonRadius: 12
    // The shading behind an active, hovered or pressed button sits 4 px in
    // from the button, a 40 px square with 8 px corners, so it keeps clear
    // of the top and bottom of the 52 px bar.
    readonly property int highlightInset: 4
    readonly property int highlightRadius: 8
    readonly property int icon: 21
    readonly property int gap: 4
    readonly property int markWidth: 6
    readonly property int markFocusedWidth: 18
    readonly property int startingWidth: 22
    readonly property int startingCycle: 1600
    readonly property int markHeight: 3
    readonly property int markBottom: 2
    readonly property color mark: UI.Style.tone(0.7)

    // The press dip: down to 0.86, springing past full size to 1.04.
    readonly property real dipScale: 0.86
    readonly property real dipOvershoot: 1.04

    readonly property int hoverDuration: UI.Style.hoverDuration
    readonly property int menuWidth: 280
}
