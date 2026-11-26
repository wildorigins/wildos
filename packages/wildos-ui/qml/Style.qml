pragma Singleton

import QtQuick
import WildOS.UI

// Every Wild OS design token, for both colour schemes. Dark values come from
// tokens.json and the dark artboards; light values from the light artboards
// (MainLight, SettingsLight, FlyoutsLight, StartLight, TaskbarLight) and the
// "Light mode" rules in the design system README: glass becomes
// rgba(250,251,254,a), ink is #1d1f23, and hairlines, hovers and cards use
// rgba(46,48,52,a) at three quarters of the dark white alpha.
//
// Design system v2 (docs/design-v2/DESIGN-V2.md) turns the glass into Mica
// and sets the text, cards and edges on it at WinUI's values; those tokens
// say so where they are.
//
// The accent, the scheme and the theme's glass come from the system (Theme
// reads kdeglobals and wildosrc), so nothing here needs an app's own
// backend.
QtObject {
    id: style

    readonly property bool dark: Theme.dark
    // High contrast (Settings > Accessibility): solid black instead of glass,
    // white text with no fading, edges that are always visible and a bright
    // cyan accent with black text on it, after Windows' Aquatic theme. Only
    // ever with the dark tokens, which it overrides below.
    readonly property bool contrast: Theme.highContrast
    readonly property color contrastEdge: "#ffffff"
    readonly property color contrastDivider: Qt.rgba(1, 1, 1, 0.6)
    // Stronger text on glass (Settings > Accessibility): every glass
    // surface behind text goes most of the way to solid, so the wallpaper
    // and windows behind no longer show through the letters, and muted text
    // moves halfway to full strength. High contrast is solid already.
    readonly property bool stronger: Theme.strongerText && !contrast
    // wildos-branding's theme-variants.py makes the "-solid" Plasma styles
    // behind Start and the flyouts with the same rule; keep the two in step.
    function solid(alpha) {
        return stronger ? alpha + (1 - alpha) * 0.8 : alpha;
    }
    function legible(alpha) {
        return stronger ? alpha + (1 - alpha) * 0.5 : alpha;
    }

    // Text size contract: Settings > Personalisation > Text size sets the
    // system font, Albert Sans 10 pt by default, and Theme follows it live. Every
    // font pixel size is the design size times this, so text grows with the
    // setting while spacing and icons stay at design size.
    readonly property real textScale: Math.max(0.75, Theme.fontPointSize / 10)
    function px(size) {
        return Math.round(size * textScale);
    }

    readonly property string fontFamily: "Albert Sans"
    // Albert Sans at a given weight looks heavier than the General Sans the
    // design is drawn in, so every weight above Regular sits one notch
    // lighter. The in-between values need the variable font, which Wild OS
    // ships; use these rather than Qt's Font enums.
    readonly property int weightLight: 300
    readonly property int weightRegular: 400
    readonly property int weightMedium: 480
    readonly property int weightSemiBold: 550
    readonly property int weightBold: 600
    // Albert Sans is set a touch wider than General Sans; every text in
    // Style.fontFamily takes this letter spacing at its pixel size.
    function tracking(pixelSize) {
        return -0.006 * pixelSize;
    }
    readonly property int fontClockLock: px(136)
    readonly property int fontDialogLarge: px(22)
    readonly property int fontTitleXl: px(28)
    readonly property int fontTitle: px(18)
    readonly property int fontSection: px(15)
    readonly property int fontHeading: px(14)
    readonly property int fontBody: px(14)
    readonly property int fontBodySm: px(13)
    readonly property int fontCaption: px(12)
    readonly property int fontLabelXs: px(11)

    // A white-alpha layer in dark mode is the same layer in near-black grey
    // at three quarters strength in light mode.
    function tone(alpha) {
        if (contrast) {
            return Qt.rgba(1, 1, 1, Math.min(1, alpha * 1.6));
        }
        return dark ? Qt.rgba(1, 1, 1, alpha) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, alpha * 0.75);
    }
    // Text on Mica is plain white or black at WinUI's alphas, so the glass
    // under it, not the ink, carries the theme's colour.
    function inkAlpha(darkAlpha, lightAlpha) {
        if (contrast) {
            return darkAlpha >= 0.5 ? "#ffffff" : "#a6a6a6";
        }
        return dark ? Qt.rgba(1, 1, 1, legible(darkAlpha)) : Qt.rgba(0, 0, 0, legible(lightAlpha));
    }
    // A layer of plain white or black, for the Mica cards and edges, where
    // WinUI's light values are not the dark ones at a lower strength. High
    // contrast keeps its own layers (tone), as before.
    function layer(darkAlpha, lightWhite, lightAlpha) {
        return dark ? Qt.rgba(1, 1, 1, darkAlpha) : lightWhite ? Qt.rgba(1, 1, 1, lightAlpha) : Qt.rgba(0, 0, 0, lightAlpha);
    }
    // The deep shade under a title bar or a side column (Main.dc
    // rgba(9,11,15,a)); light mode uses the dark tone instead.
    function shade(darkAlpha, lightAlpha) {
        if (contrast) {
            return "#000000";
        }
        return dark ? tinted(9, 11, 15, darkAlpha) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, lightAlpha);
    }
    function glass(r, g, b, darkAlpha, lightAlpha) {
        return mica(r, g, b, darkAlpha, 250, 251, 254, lightAlpha);
    }
    // Mica: a near neutral tint over the blurred wallpaper, its colour in each
    // scheme given in full. Dawn keeps a little blue in the dark, as Windows'
    // own dark mode does; the themes' tints are neutral, so the wallpaper
    // gives them their colour.
    function mica(r, g, b, darkAlpha, lr, lg, lb, lightAlpha) {
        if (contrast) {
            return "#000000";
        }
        const c = dark ? tinted(r, g, b, darkAlpha) : tinted(lr, lg, lb, lightAlpha);
        return Qt.rgba(c.r, c.g, c.b, solid(c.a));
    }
    // Start, the flyouts and menus: a theme may give their glass in full.
    function panel(r, g, b, darkAlpha, lr, lg, lb, lightAlpha) {
        return contrast ? "#000000" : Theme.hasPanelTint ? solidified(Theme.panelTint) : mica(r, g, b, darkAlpha, lr, lg, lb, lightAlpha);
    }
    // A theme gives one glass colour (Settings > Personalisation > Themes);
    // every surface keeps its relation to the reference surface it was
    // designed against. Each channel is scaled by the theme's share of the
    // reference, which keeps the hue of the theme in the darkest shades,
    // and the alpha moves by the theme's difference. With the stock theme
    // the tint is the reference and every value comes out as designed.
    // wildos-branding's theme-variants.py does the same for the Plasma
    // frames behind Start and the flyouts; keep the two in step.
    readonly property color glassTint: Theme.glassTint
    readonly property color glassReference: Theme.glassReference
    function tinted(r, g, b, a) {
        const t = glassTint;
        const ref = glassReference;
        return Qt.rgba(Math.min(1, r / 255 * t.r / ref.r), Math.min(1, g / 255 * t.g / ref.g), Math.min(1, b / 255 * t.b / ref.b),
            Math.max(0, Math.min(1, a + t.a - ref.a)));
    }

    readonly property color ink: contrast || dark ? "#ffffff" : stronger ? "#121418" : Qt.rgba(0, 0, 0, 0.896)
    readonly property color inkStrong: dark ? "#ffffff" : "#121418"
    readonly property color inkField: contrast ? "#ffffff" : dark ? "#f3f5fb" : "#1d1f23"
    readonly property color inkMuted: inkAlpha(0.786, 0.606)
    readonly property color inkStatus: inkAlpha(0.786, 0.606)
    readonly property color inkPlaceholder: inkAlpha(0.545, 0.446)
    readonly property color inkSubtle: inkAlpha(0.545, 0.446)
    readonly property color inkDisabled: contrast ? "#a6a6a6" : dark ? Qt.rgba(232 / 255, 236 / 255, 248 / 255, 0.40) : Qt.rgba(29 / 255, 31 / 255, 35 / 255, 0.38)
    // UI line icons; app icons never change between schemes.
    readonly property color iconInk: contrast ? "#ffffff" : dark ? "#eef1f8" : "#34363a"
    readonly property color iconMuted: contrast ? "#ffffff" : dark ? Qt.rgba(238 / 255, 241 / 255, 248 / 255, legible(0.75)) : Qt.rgba(52 / 255, 54 / 255, 58 / 255, legible(0.75))
    // Desktop labels and the lock screen sit on the photograph.
    readonly property color inkOnWallpaper: "#ffffff"
    // Everything else that sits straight on the photograph (the lock and
    // sign-in screens, Lock.dc) keeps the same dark glass and light ink in
    // either scheme, so it reads on any picture.
    readonly property color inkOnWallpaperMuted: contrast ? "#ffffff" : Qt.rgba(232 / 255, 236 / 255, 248 / 255, legible(0.72))
    readonly property color wallpaperDim: Qt.rgba(15 / 255, 17 / 255, 21 / 255, 0.22)
    readonly property color textShadowOnWallpaper: Qt.rgba(15 / 255, 17 / 255, 21 / 255, 0.45)
    // Mica, like every other floating surface: the photograph shows through
    // only as a soft gradient. Hover and pressed keep their steps above it.
    readonly property color glassOnWallpaper: contrast ? "#000000" : Qt.rgba(24 / 255, 26 / 255, 34 / 255, solid(0.92))
    readonly property color glassOnWallpaperField: contrast ? "#000000" : Qt.rgba(24 / 255, 26 / 255, 34 / 255, solid(0.94))
    readonly property color glassOnWallpaperHover: Qt.rgba(39 / 255, 41 / 255, 45 / 255, solid(0.94))
    readonly property color glassOnWallpaperPressed: Qt.rgba(50 / 255, 52 / 255, 56 / 255, solid(0.95))
    readonly property color borderOnWallpaper: contrast ? contrastEdge : Qt.rgba(1, 1, 1, 0.14)
    readonly property color borderOnWallpaperField: contrast ? contrastEdge : Qt.rgba(1, 1, 1, 0.22)
    // Controls drawn straight on a dimmed photograph (CtrlAltDel.dc): the
    // dark scheme's control fill and edge, in either scheme.
    readonly property color controlOnWallpaper: Qt.rgba(1, 1, 1, 0.06)
    readonly property color controlOnWallpaperHover: Qt.rgba(1, 1, 1, 0.12)
    readonly property color controlOnWallpaperPressed: Qt.rgba(1, 1, 1, 0.16)
    readonly property color controlOnWallpaperSelected: Qt.rgba(1, 1, 1, 0.14)
    readonly property color borderControlOnWallpaper: contrast ? contrastEdge : Qt.rgba(1, 1, 1, 0.16)

    // The accent the user picked is the dark-mode accent. Light mode deepens
    // it until white text on it reads (Glacier #8fb4ff becomes #3563d8 and
    // its text colour #2a55c4, as in the light artboards).
    readonly property color systemAccent: Theme.accent
    function luminance(c) {
        const f = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b);
    }
    function deepen(c, saturation, lightness, contrast) {
        if (c.hslSaturation <= 0) {
            return Qt.hsla(0, 0, lightness, 1);
        }
        let l = lightness;
        let out = Qt.hsla(c.hslHue, c.hslSaturation * saturation, l, 1);
        while (l > 0.2 && 1.05 / (luminance(out) + 0.05) < contrast) {
            l -= 0.01;
            out = Qt.hsla(c.hslHue, c.hslSaturation * saturation, l, 1);
        }
        return out;
    }
    readonly property bool glacier: Qt.colorEqual(systemAccent, "#8fb4ff")
    // A theme's own accent in its own mode is used as the theme gives it,
    // with the text colours the theme was checked with (WCAG AA).
    readonly property bool themeAccent: !contrast && Theme.themeAccent
    readonly property color accent: contrast ? "#75e9fc" : dark || themeAccent ? systemAccent : glacier ? "#3563d8" : deepen(systemAccent, 0.676, 0.527, 4.5)
    readonly property color textOnAccent: contrast ? "#000000" : themeAccent ? Theme.textOnAccent : dark ? "#0f1530" : "#ffffff"
    // A small fill on the accent (a checked radio's centre, an on switch's
    // knob) is a neutral dark, not the ink, so it never reads as navy.
    readonly property color accentDot: contrast ? "#000000" : themeAccent ? Theme.textOnAccent : dark ? "#1a1c20" : "#ffffff"
    // Glacier's dark accent-text is #b9cfff, 40% of the way to white; the
    // other accents get the same lift.
    readonly property color accentText: contrast ? accent : themeAccent ? Theme.accentText : dark
        ? Qt.rgba(accent.r + (1 - accent.r) * 0.4, accent.g + (1 - accent.g) * 0.4, accent.b + (1 - accent.b) * 0.4, 1)
        : glacier ? "#2a55c4" : deepen(systemAccent, 0.647, 0.467, 5.5)
    readonly property color accentHover: dark ? Qt.lighter(accent, 1.08) : Qt.darker(accent, 1.08)
    readonly property color accentPressed: dark ? Qt.darker(accent, 1.08) : Qt.darker(accent, 1.18)
    function accentAlpha(darkAlpha, lightAlpha) {
        return Qt.rgba(accent.r, accent.g, accent.b, dark ? darkAlpha : lightAlpha);
    }
    // Fills on Mica are neutral, as in WinUI: a selected, current or
    // highlighted item is white at 6.1% (black 3.7% in light), a soft fill
    // white at 4%. The accent is for controls, focus rings, selection bars
    // and links. High contrast keeps its accent fills as before.
    function fillSelected(darkAlpha, lightAlpha) {
        return contrast ? accentAlpha(darkAlpha, lightAlpha) : layer(0.061, false, 0.037);
    }
    function fillSubtle(darkAlpha, lightAlpha) {
        return contrast ? accentAlpha(darkAlpha, lightAlpha) : layer(0.04, false, 0.03);
    }
    readonly property color accentSoft: fillSubtle(0.12, 0.09)
    readonly property color accentTint: fillSubtle(0.10, 0.09)
    readonly property color accentChip: fillSubtle(0.14, 0.11)
    readonly property color accentSelected: fillSelected(0.20, 0.16)
    // Selected text keeps the accent, or the selection would not show.
    readonly property color textSelection: accentAlpha(0.20, 0.16)
    readonly property color accentBorder: accentAlpha(0.28, 0.21)
    readonly property color accentBorderStrong: accentAlpha(0.55, 0.413)

    // Floating surfaces (the taskbar pills, the window switcher, on-screen
    // displays) are Mica too, with the same blur as windows.
    readonly property color glassFloat: contrast ? "#000000" : mica(24, 26, 34, 0.92, 243, 243, 243, 0.94)
    readonly property color glassBar: glassFloat
    // The taskbar (wildos-shell): docked along the bottom, or the floating
    // pills. A theme can give the docked bar a glass of its own; the pills
    // then take the same colour, as much lighter as the stock pills are.
    readonly property color taskbarDocked: contrast ? "#000000" : solidified(Theme.hasTaskbarTint ? Theme.taskbarTint
        : dark ? tinted(18, 20, 28, 0.92) : tinted(238, 238, 238, 0.94))
    // The pills are the floating Mica: the window colour at the taskbar's
    // opacity, so they stay as much lighter than the docked bar as before.
    readonly property color taskbarFloating: glassFloat
    function solidified(c) {
        return Qt.rgba(c.r, c.g, c.b, solid(c.a));
    }
    readonly property color taskbarShadow: dark ? tinted(13, 15, 19, 0.35) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.14)
    readonly property color glassPanel: panel(35, 37, 41, 0.96, 240, 240, 240, 0.96)
    readonly property color glassWindow: mica(24, 26, 34, 0.95, 243, 243, 243, 0.96)
    // A window's deeper glass stays as much more opaque than its plain glass
    // as before Mica; dialogs take the window glass.
    readonly property color glassWindowDeep: mica(24, 26, 34, 0.97, 243, 243, 243, 0.97)
    readonly property color glassMenu: panel(35, 37, 41, 0.96, 240, 240, 240, 0.96)
    readonly property color glassDialog: mica(24, 26, 34, 0.95, 243, 243, 243, 0.96)
    // Every right-click menu (MenusShell.dc, MenusApps.dc): rgba(23,25,29,
    // 0.86) behind a 0.12 edge, 12px corners, a 0 20px 50px shadow at 0.45
    // and 7px row corners.
    readonly property color glassContextMenu: panel(35, 37, 41, 0.96, 240, 240, 240, 0.96)
    readonly property color borderContextMenu: contrast ? contrastEdge : tone(0.12)
    readonly property color shadowContextMenu: dark ? tinted(10, 12, 16, 0.45) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.18)
    // Behind a window when transparency effects are off or blur is missing.
    readonly property color windowOpaque: contrast ? "#000000" : dark ? tinted(24, 26, 34, 1) : "#f3f3f3"
    readonly property color ground: contrast ? "#000000" : dark ? tinted(22, 24, 28, 1) : "#eef1f7"
    readonly property color lakeNavy: "#333539"

    readonly property color surfaceSunken: contrast ? "#000000" : dark ? tinted(9, 11, 15, 0.35) : tinted(250, 251, 254, 0.55)
    // Cards and setting rows: in light mode white at 72%, lighter panels on
    // the grey Mica, as in Windows 11's light Settings.
    readonly property color surfaceRaised: contrast ? tone(0.05) : layer(0.051, true, 0.72)
    readonly property color surfaceRest: tone(0.04)
    readonly property color surfaceControl: tone(0.06)
    readonly property color surfaceHover: contrast ? tone(0.09) : layer(0.084, true, 0.5)
    // What a hover fades back to. Plain "transparent" is clear black, so a
    // white light-mode hover would pass through grey on its way out.
    readonly property color surfaceHoverClear: Qt.alpha(surfaceHover, 0)
    readonly property color surfaceSelected: contrast ? tone(0.10) : layer(0.061, false, 0.037)
    readonly property color surfacePressed: tone(0.12)
    readonly property color surfaceTrack: tone(0.12)
    readonly property color surfaceTile: tone(0.08)
    // The band along the foot of Start and the flyouts (account and power,
    // battery): darker than the glass in dark mode, paler in light
    // (Flyouts.dc rgba(7,9,13,0.30), FlyoutsLight.dc rgba(250,251,254,0.55)).
    readonly property color surfaceBand: contrast ? "#000000" : dark ? tinted(7, 9, 13, 0.30) : tinted(250, 251, 254, 0.55)

    readonly property color borderSubtle: contrast ? contrastEdge : layer(0.07, false, 0.058)
    readonly property color divider: contrast ? contrastDivider : tone(0.06)
    readonly property color borderField: contrast ? contrastEdge : tone(0.10)
    // Windows 11's 1px window border: in light mode a grey hairline that
    // still shows against white, as the shadow alone does not.
    readonly property color windowEdge: contrast ? contrastEdge : layer(0.093, false, 0.18)
    readonly property color borderGlass: contrast ? contrastEdge : layer(0.093, false, 0.073)
    readonly property color borderMenu: contrast ? contrastEdge : tone(0.14)
    readonly property color borderControl: contrast ? contrastEdge : tone(0.16)
    readonly property color borderDialog: contrast ? contrastEdge : tone(0.13)

    readonly property color toggleOffBorder: contrast ? contrastEdge : dark ? Qt.rgba(238 / 255, 241 / 255, 248 / 255, 0.55) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, 0.413)
    // SettingsLight draws the off knob in the dark-mode #eef1f8, which
    // vanishes on white; the light knob takes the icon ink instead.
    readonly property color toggleOffKnob: contrast ? "#ffffff" : dark ? "#eef1f8" : "#34363a"
    readonly property color radioRing: contrast ? contrastEdge : dark ? Qt.rgba(238 / 255, 241 / 255, 248 / 255, 0.6) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, 0.45)
    readonly property color scrollThumb: tone(0.30)
    readonly property color scrollThumbPressed: tone(0.45)
    readonly property color scrim: contrast ? Qt.rgba(0, 0, 0, 0.75) : dark ? Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.45) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.22)

    // Shadows keep their shape and drop to about 40% strength in light mode.
    readonly property color shadowWindow: dark ? tinted(10, 12, 16, 0.45) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.18)
    readonly property color shadowFlyout: dark ? tinted(10, 12, 16, 0.55) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.22)
    readonly property color shadowMenu: dark ? tinted(10, 12, 16, 0.60) : Qt.rgba(36 / 255, 38 / 255, 42 / 255, 0.24)

    readonly property color closeHover: "#c4463f"
    readonly property color closePressed: "#a63a34"
    // The quick settings slider knob (EmptyStates.dc): white on its 4px
    // track in either scheme; light mode gives it a hairline to stand off
    // the pale glass.
    readonly property color sliderKnob: "#ffffff"
    readonly property color danger: dark ? "#f28b82" : "#c5372c"
    // A destructive menu row (Uninstall, Empty bin, Delete) is drawn in a
    // softer red than an error, #ffb4b4 in the menu artboards.
    readonly property color dangerMenu: dark ? "#ffb4b4" : "#c5372c"
    readonly property color warning: dark ? "#f2c46b" : "#9a6400"
    readonly property color warningSoft: dark ? Qt.rgba(242 / 255, 196 / 255, 107 / 255, 0.12) : Qt.rgba(154 / 255, 100 / 255, 0, 0.10)
    // "Runs on the desktop" in FirstWinApps.dc: a green status, text #a8ecc8
    // on 110,210,160 at 0.16.
    readonly property color success: dark ? "#a8ecc8" : "#1d7a4c"
    readonly property color successSoft: dark ? Qt.rgba(110 / 255, 210 / 255, 160 / 255, 0.16) : Qt.rgba(29 / 255, 122 / 255, 76 / 255, 0.12)

    // What a control that cannot be used fades to, with its label.
    readonly property real disabledOpacity: 0.5

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20
    readonly property int space6: 24
    readonly property int space7: 28
    readonly property int space8: 40

    readonly property int radiusXs: 6
    readonly property int radiusSm: 8
    // The panels, list cards and large fields the app boards draw at 10.
    readonly property int radiusPanel: 10
    readonly property int radiusMd: 12
    readonly property int radiusLg: 14
    readonly property int radiusDialog: 18
    readonly property int radiusXl: 20
    readonly property int radius2xl: 22
    readonly property int radiusPill: 9999

    readonly property int controlHeightSmall: 32
    readonly property int controlHeight: 36
    readonly property int controlHeightLarge: 40
    readonly property int fieldHeightLarge: 42
    readonly property int menuItemHeight: 34
    readonly property int menuHeaderHeight: 28
    readonly property int radiusMenuRow: 7
    readonly property int titleBarHeight: 42
    // The taller bar of the apps whose boards draw it at 44 (Music, Notes,
    // Backup, Voice, Videos, Archive, Report): `tall: true` on TitleBar.
    readonly property int titleBarHeightTall: 44
    readonly property int windowButtonWidth: 46
    readonly property int windowRadius: 14

    // Every duration follows KDE's animation speed, so Wild OS apps keep pace
    // with window and menu animations.
    readonly property real animationFactor: Theme.animationFactor
    // Colour fades stay at full length with reduced motion, as before.
    readonly property int hoverDuration: reducedMotion ? 150 : Math.round(150 * animationFactor)
    readonly property int quickDuration: reducedMotion ? 120 : Math.round(120 * animationFactor)
    // Movement (slides, scaling, travelling highlights) stops when this is
    // on; colour and opacity changes may stay.
    readonly property bool reducedMotion: Theme.reducedMotion

    // Motion, after Windows 11: things arrive on a decelerating curve and
    // leave faster on an accelerating one, and nothing travels far. With
    // reduced motion every duration is 0, every travel is 0 and nothing
    // scales, so surfaces simply appear in their final place.
    readonly property int motionFast: reducedMotion ? 0 : Math.round(120 * animationFactor)
    readonly property int motionNormal: reducedMotion ? 0 : Math.round(200 * animationFactor)
    readonly property int motionSlow: reducedMotion ? 0 : Math.round(300 * animationFactor)
    // easing.type values: OutQuint is close to Fluent's decelerate
    // (0.1, 0.9, 0.2, 1), InCubic to its accelerate, InOutCubic to its
    // standard curve for something that moves from one place to another.
    readonly property int easeEnter: Easing.OutQuint
    readonly property int easeExit: Easing.InCubic
    readonly property int easeStandard: Easing.InOutCubic
    // How far a surface slides as it arrives: a page or a list, a flyout
    // from its button, a panel from a screen edge.
    readonly property int travelSmall: reducedMotion ? 0 : 8
    readonly property int travelMedium: reducedMotion ? 0 : 16
    readonly property int travelLarge: reducedMotion ? 0 : 32
    // A dialog grows to full size from this as it fades in.
    readonly property real scaleEnter: reducedMotion ? 1 : 0.96

    readonly property var accents: [
        { name: qsTr("Glacier"), colour: "#8fb4ff" },
        { name: qsTr("Sea glass"), colour: "#7fd6c8" },
        { name: qsTr("Moss"), colour: "#9fd38a" },
        { name: qsTr("Sand"), colour: "#f2c46b" },
        { name: qsTr("Dusk"), colour: "#f2a08a" },
        { name: qsTr("Heather"), colour: "#c4b2ff" }
    ]
}
