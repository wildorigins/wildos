/*
    SPDX-FileCopyrightText: 2012 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.1
import QtQml 2.15

import org.kde.plasma.core as PlasmaCore
import org.kde.ksvg 1.0 as KSvg
import org.kde.taskmanager 0.1 as TaskManager
import org.kde.kwindowsystem 1.0
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.shell.panel 0.1 as Panel

import org.kde.plasma.plasmoid 2.0

import Qt5Compat.GraphicalEffects
import QtQuick.Templates as T
import Qt.labs.folderlistmodel
import org.kde.plasma.plasma5support as P5Support
import org.kde.plasma.workspace.dbus as DBus
import org.wildos.shell as WildOS
import WildOS.UI as UI

Item {
    id: root

    property Item containment

    // Wild OS draws the horizontal taskbar's glass itself, in one of the two
    // styles of the Taskbar artboard (Settings > Personalisation > Taskbar
    // style). Docked, the default: one bar 52 px tall across the bottom of
    // the screen. Floating: one pill around each run of widgets between
    // expanding spacers, so the apps in the middle and the status at the
    // right are separate panels, 62 px tall with 20 px corners and 14 px
    // above the screen edge (76 px thick in all). Both come from a single
    // panel because two Plasma panels on one edge stack on top of each other
    // on Wayland.
    readonly property bool usePills: !verticalPanel
    readonly property bool docked: !WildOS.TaskbarStyle.floating
    readonly property int pillHeight: docked ? 52 : 62
    readonly property int pillRadius: docked ? 0 : 20
    // Floating at the top of the screen, the 14 px gap to the screen edge is
    // above the pills instead of below, so their band moves down.
    readonly property bool topEdge: containment?.plasmoid?.location === PlasmaCore.Types.TopEdge
    readonly property int pillTop: topEdge && !docked ? Math.max(0, height - pillHeight) : 0
    onPillTopChanged: pillTimer.restart()
    // The panel's own height (52 docked, 76 floating) is what keeps
    // maximised windows clear of the taskbar. Settings sets it through
    // Plasma's panel scripting when the style changes; setting it from
    // here while the view is being built crashes plasmashell.
    onDockedChanged: pillTimer.restart()
    property var pillRects: []
    property var watched: []
    // KWin's underglow lights the floating pills' top edges from these.
    readonly property string screenName: Screen.name
    function publishPills() {
        WildOS.PillMask.publish(screenName, pillRects, pillRadius);
    }
    onPillRectsChanged: publishPills()
    onScreenNameChanged: publishPills()

    function updatePills() {
        const layout = containment?.layoutManager?.layout;
        if (!usePills || !layout) {
            pillRects = [];
            return;
        }
        if (docked) {
            pillRects = [Qt.rect(0, pillTop, width, pillHeight)];
            return;
        }
        const runs = [];
        let run = null;
        // Widgets are often still zero wide when first seen at login, so every
        // child is watched, not only the ones already laid out.
        for (const child of Array.from(layout.children)) {
            if (watched.indexOf(child) < 0) {
                watched.push(child);
                child.xChanged.connect(pillTimer.restart);
                child.widthChanged.connect(pillTimer.restart);
                child.visibleChanged.connect(pillTimer.restart);
            }
        }
        const children = Array.from(layout.children).filter(c => c.visible && c.width > 0).sort((a, b) => a.x - b.x);
        for (const child of children) {
            const plugin = child.applet?.plasmoid?.pluginName;
            if (!child.applet || plugin === "org.kde.plasma.panelspacer") {
                run = null;
                continue;
            }
            const p = child.mapToItem(root, 0, 0);
            if (!run) {
                run = { left: p.x, right: p.x + child.width, first: plugin };
                runs.push(run);
            } else {
                run.right = p.x + child.width;
            }
        }
        // The status pill at the right end has 6 px inside its left edge and
        // 8 px inside its right; the apps pill 9 px each side. Windows mode's
        // item at the left end mirrors the status pill: it carries its own
        // 13 px and its pill starts 5 px in, 8 px before its button.
        pillRects = runs.map((r, i) => {
            const last = runs.length > 1 && i === runs.length - 1;
            const winmode = i === 0 && runs.length > 2 && r.first === "org.wildos.winmode";
            const left = Math.round(r.left - (last ? 6 : winmode ? -5 : 9));
            const right = Math.round(r.right + (last ? 8 : winmode ? 6 : 9));
            return Qt.rect(left, pillTop, right - left, pillHeight);
        });
    }

    Timer {
        id: pillTimer
        interval: 0
        onTriggered: root.updatePills()
    }
    onWidthChanged: pillTimer.restart()
    // The containment fills in its layout after it is handed to this view,
    // and the layout is reached through a plain script object that sends no
    // change signal, so keep looking until it has widgets to draw around.
    Timer {
        interval: 250
        repeat: true
        running: root.usePills && root.pillRects.length === 0
        onTriggered: root.updatePills()
    }
    Connections {
        target: root.containment
        function onWidthChanged() { pillTimer.restart(); }
    }
    Connections {
        target: root.containment?.layoutManager?.layout ?? null
        function onChildrenChanged() { pillTimer.restart(); }
        function onWidthChanged() { pillTimer.restart(); }
    }

    // KDE's network and disk activity monitors open as a line graph, which at
    // taskbar height is a faint scribble on the dark glass (#89). Dropped on
    // the taskbar they show their figures as text instead, like the clock;
    // appletCreated fires only for a widget just added, so a face someone
    // picks later in the widget's settings is kept.
    Connections {
        target: root.containment?.plasmoid ?? null
        function onAppletCreated(applet) {
            if (!applet || !String(applet.pluginName).startsWith("org.kde.plasma.systemmonitor")) {
                return;
            }
            textFace.applets.push(applet);
            textFace.tries = 0;
            textFace.restart();
        }
    }
    // Network Speed widgets added before the coloured arrows (builds 919 and
    // 920) carry Text Only with the plain arrows; move those on as well.
    Timer {
        interval: 2000
        running: root.containment !== null
        onTriggered: {
            const applets = root.containment?.plasmoid?.applets ?? [];
            for (const applet of applets) {
                if (String(applet.pluginName) === "org.kde.plasma.systemmonitor.net") {
                    textFace.applets.push(applet);
                }
            }
            textFace.tries = 0;
            textFace.restart();
        }
    }
    Timer {
        id: textFace
        property var applets: []
        property int tries: 0
        readonly property string netFace: "org.wildos.ksysguard.colourtext"
        // The face controller is made once the widget has loaded its preset.
        interval: 300
        onTriggered: {
            if (++tries > 20) {
                applets = [];
                return;
            }
            const waiting = [];
            for (const applet of applets) {
                const face = applet.faceController;
                const net = String(applet.pluginName) === "org.kde.plasma.systemmonitor.net";
                if (!face) {
                    waiting.push(applet);
                } else if (face.faceId === "org.kde.ksysguard.linechart") {
                    face.faceId = net ? netFace : "org.kde.ksysguard.textonly";
                    if (net) {
                        netArrows(face);
                    }
                } else if (net && face.faceId === "org.kde.ksysguard.textonly"
                           && face.sensorLabels["network/all/download"] === "↓") {
                    face.faceId = netFace;
                    netArrows(face);
                }
            }
            applets = waiting;
            if (waiting.length > 0) {
                restart();
            }
        }
        // Arrows read at a glance in any language, where "Download Rate"
        // pushed the numbers along the bar. Blue and orange, bold, tell them
        // apart on light and dark glass alike.
        function netArrows(face) {
            face.sensorLabels = {
                "network/all/download": "↓",
                "network/all/upload": "↑"
            };
            face.sensorColors = {
                "network/all/download": "#2f9bff",
                "network/all/upload": "#ff8a2f"
            };
        }
    }

    Repeater {
        model: root.pillRects
        delegate: Item {
            id: pill
            required property rect modelData
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height

            // The design's 0 16px 40px shadow, cut away under the glass so it
            // cannot darken the translucent pill. The panel window ends at
            // the pill's top edge, so only the part below and beside shows.
            // A docked bar sits on the screen edge and has none.
            Item {
                id: shadowLayer
                anchors.fill: parent
                anchors.margins: -48
                visible: false
                Rectangle {
                    id: shadowSource
                    x: 48
                    y: 48
                    width: pill.width
                    height: pill.height
                    radius: root.pillRadius
                    color: "black"
                    visible: false
                }
                DropShadow {
                    anchors.fill: shadowSource
                    source: shadowSource
                    transparentBorder: true
                    verticalOffset: 16
                    radius: 40
                    samples: 81
                    color: UI.Style.taskbarShadow
                }
            }
            Item {
                id: shadowMask
                anchors.fill: shadowLayer
                visible: false
                Rectangle {
                    x: 48
                    y: 48
                    width: pill.width
                    height: pill.height
                    radius: root.pillRadius
                    color: "black"
                }
            }
            OpacityMask {
                visible: !root.docked
                anchors.fill: shadowLayer
                source: shadowLayer
                maskSource: shadowMask
                invert: true
            }

            Rectangle {
                visible: !root.docked
                anchors.fill: parent
                radius: root.pillRadius
                color: UI.Style.taskbarFloating
                border.width: 1
                border.color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, 0.09)
            }
            // The inset 0 1px 0 highlight along the top, inside the border.
            // Light pills only: on dark Mica it reads as a glass tell.
            Rectangle {
                visible: !root.docked && !UI.Style.dark
                x: root.pillRadius
                y: 1
                width: parent.width - root.pillRadius * 2
                height: 1
                color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.7)
            }

            // Docked: the bar fills its edge of the screen, with a hairline
            // along the edge that faces the desktop.
            Rectangle {
                visible: root.docked
                anchors.fill: parent
                color: UI.Style.taskbarDocked
            }
            Rectangle {
                visible: root.docked
                y: root.containment?.plasmoid?.location === PlasmaCore.Types.TopEdge ? parent.height - 1 : 0
                width: parent.width
                height: 1
                color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, 0.10)
            }
        }
    }

    property bool floatingPrefix: floatingPanelSvg.usedPrefix === "floating"
    readonly property bool verticalPanel: containment?.plasmoid?.formFactor === PlasmaCore.Types.Vertical

    readonly property real spacingAtMinSize: Math.floor(Math.max(1, panel.thickness - Kirigami.Units.iconSizes.smallMedium)/2)
    KSvg.FrameSvgItem {
        id: thickPanelSvg
        visible: false
        prefix: 'thick'
        imagePath: "widgets/panel-background"
    }
    KSvg.FrameSvgItem {
        id: floatingPanelSvg
        visible: false
        prefix: ['floating', '']
        imagePath: "widgets/panel-background"
    }

    readonly property bool rightEdge: containment?.plasmoid?.location === PlasmaCore.Types.RightEdge
    readonly property bool bottomEdge: containment?.plasmoid?.location === PlasmaCore.Types.BottomEdge

    readonly property int bottomFloatingPadding: Math.round(fixedBottomFloatingPadding * floatingness)
    readonly property int leftFloatingPadding: Math.round(fixedLeftFloatingPadding * floatingness)
    readonly property int rightFloatingPadding: Math.round(fixedRightFloatingPadding * floatingness)
    readonly property int topFloatingPadding: Math.round(fixedTopFloatingPadding * floatingness)


    // NOTE: Many of the properties in this file are accessed directly in C++ PanelView!
    // If you change these, make sure to also correct the related code in panelview.cpp.
    readonly property int fixedBottomFloatingPadding: floating && (floatingPrefix ? floatingPanelSvg.fixedMargins.bottom : 8)
    readonly property int fixedLeftFloatingPadding: floating && (floatingPrefix ? floatingPanelSvg.fixedMargins.left   : 8)
    readonly property int fixedRightFloatingPadding: floating && (floatingPrefix ? floatingPanelSvg.fixedMargins.right  : 8)
    readonly property int fixedTopFloatingPadding: floating && (floatingPrefix ? floatingPanelSvg.fixedMargins.top    : 8)

    readonly property int topPadding: Math.round(Math.min(thickPanelSvg.fixedMargins.top + Kirigami.Units.smallSpacing, spacingAtMinSize));
    readonly property int bottomPadding: Math.round(Math.min(thickPanelSvg.fixedMargins.bottom + Kirigami.Units.smallSpacing, spacingAtMinSize));
    readonly property int leftPadding: Math.round(Math.min(thickPanelSvg.fixedMargins.left + Kirigami.Units.smallSpacing, spacingAtMinSize));
    // Plasma's panel moves every pointer event inside these paddings onto
    // the nearest widget, so the show desktop sliver in the last 8 px would
    // never see the pointer. With the sliver on, nothing is padded there.
    readonly property int rightPadding: usePills && WildOS.TaskbarStyle.showDesktop ? 0 : Math.round(Math.min(thickPanelSvg.fixedMargins.right + Kirigami.Units.smallSpacing, spacingAtMinSize));

    readonly property int minPanelHeight: translucentItem.minimumDrawingHeight
    readonly property int minPanelWidth: translucentItem.minimumDrawingWidth

    // This value is read from panelview.cpp which needs it to decide which border should be enabled
    property real topShadowMargin: -floatingTranslucentItem.y
    property real leftShadowMargin: -floatingTranslucentItem.x
    property real rightShadowMargin: -(width - floatingTranslucentItem.width - floatingTranslucentItem.x)
    property real bottomShadowMargin: -(height - floatingTranslucentItem.height - floatingTranslucentItem.y)

    property var panelMask: usePills ? WildOS.PillMask.region(pillRects, pillRadius) : floatingness === 0 ? (panelOpacity === 1 ? opaqueItem.mask : translucentItem.mask) : (panelOpacity === 1 ? floatingOpaqueItem.mask : floatingTranslucentItem.mask)

    // The point is read from panelview.cpp and is used as an offset for the mask
    readonly property point floatingTranslucentItemOffset: Qt.point(floatingTranslucentItem.x, floatingTranslucentItem.y)

    TaskManager.VirtualDesktopInfo {
        id: virtualDesktopInfo
    }

    TaskManager.ActivityInfo {
        id: activityInfo
    }

    // We need to have a little gap between the raw visibleWindowsModel count
    // and actually determining if a window is touching.
    // This is because certain dialog windows start off with a position of (screenwidth/2, screenheight/2)
    // and they register as "touching" in the split-second before KWin can place them correctly.
    // This avoids the panel flashing if it is auto-hide etc and such a window is shown.
    // Examples of such windows: properties of a file on desktop, or portal "open with" dialog
    property bool touchingWindow: false
    property bool touchingWindowDirect: visibleWindowsModel.count > 0
    property bool showingDesktop: KWindowSystem.showingDesktop
    Timer {
        id: touchingWindowDebounceTimer
        interval: 10  // ms, I find that this value is enough while not causing unresponsiveness while dragging windows close
        onTriggered: root.touchingWindow = !KWindowSystem.showingDesktop && root.touchingWindowDirect
    }
    onTouchingWindowDirectChanged: touchingWindowDebounceTimer.start()
    onShowingDesktopChanged: touchingWindowDebounceTimer.start()

    TaskManager.TasksModel {
        id: visibleWindowsModel
        filterByVirtualDesktop: true
        filterByActivity: true
        filterByScreen: false
        filterByRegion: TaskManager.RegionFilterMode.Intersect
        filterHidden: true
        filterMinimized: true

        screenGeometry: panel.screenGeometry
        virtualDesktop: virtualDesktopInfo.currentDesktop
        activity: activityInfo.currentActivity

        groupMode: TaskManager.TasksModel.GroupDisabled

        Binding on regionGeometry {
            delayed: true
            value: panel.width, panel.height, panel.x, panel.y, panel.dogdeGeometryByDistance(panel.visibilityMode === Panel.Global.DodgeWindows ? -1 : 1) // +1 is for overlap detection, -1 is for snapping to panel
        }
    }

    Connections {
        target: root.containment?.plasmoid ?? null
        function onActivated() {
            if (root.containment.plasmoid.status === PlasmaCore.Types.AcceptingInputStatus) {
                root.containment.plasmoid.status = PlasmaCore.Types.PassiveStatus;
            } else {
                root.containment.plasmoid.status = PlasmaCore.Types.AcceptingInputStatus;
            }
        }
    }

    // Floatingness is a value in [0, 1] that's multiplied to the floating margin; 0: not floating, 1: floating, between 0 and 1: animation between the two states
    readonly property int floatingnessAnimationDuration: Kirigami.Units.longDuration
    property double floatingnessTarget: 0.0 // The animation is handled in panelview.cpp for efficiency
    property double floatingness: 0.0

    // PanelOpacity is a value in [0, 1] that's used as the opacity of the opaque elements over the transparent ones; values between 0 and 1 are used for animations
    property double panelOpacity
    Behavior on panelOpacity {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
            easing.type: Easing.OutCubic
        }
    }

    KSvg.FrameSvgItem {
        id: translucentItem
        visible: floatingness === 0 && panelOpacity !== 1
        enabledBorders: panel.enabledBorders
        anchors.fill: floatingTranslucentItem
        imagePath: root.usePills || containment?.plasmoid?.backgroundHints === PlasmaCore.Types.NoBackground ? "" : "widgets/panel-background"
    }
    KSvg.FrameSvgItem {
        id: floatingTranslucentItem
        visible: floatingness !== 0 && panelOpacity !== 1
        x: root.rightEdge ? fixedLeftFloatingPadding + fixedRightFloatingPadding * (1 - floatingness) : leftFloatingPadding
        y: root.bottomEdge ? fixedTopFloatingPadding + fixedBottomFloatingPadding * (1 - floatingness) : topFloatingPadding
        width: verticalPanel ? panel.thickness : parent.width - leftFloatingPadding - rightFloatingPadding
        height: verticalPanel ? parent.height - topFloatingPadding - bottomFloatingPadding : panel.thickness

        imagePath: root.usePills || containment?.plasmoid?.backgroundHints === PlasmaCore.Types.NoBackground ? "" : "widgets/panel-background"
    }
    KSvg.FrameSvgItem {
        id: floatingOpaqueItem
        visible: floatingness !== 0 && panelOpacity !== 0
        opacity: panelOpacity
        anchors.fill: floatingTranslucentItem
        imagePath: root.usePills || containment?.plasmoid?.backgroundHints === PlasmaCore.Types.NoBackground ? "" : "solid/widgets/panel-background"
    }
    KSvg.FrameSvgItem {
        id: opaqueItem
        visible: panelOpacity !== 0 && floatingness === 0
        opacity: panelOpacity
        enabledBorders: panel.enabledBorders
        anchors.fill: floatingTranslucentItem
        imagePath: root.usePills || containment?.plasmoid?.backgroundHints === PlasmaCore.Types.NoBackground ? "" : "solid/widgets/panel-background"
    }

    Keys.onEscapePressed: {
        root.parent.focus = false
    }

    property bool isOpaque: panel.opacityMode === Panel.Global.Opaque
    property bool isTransparent: panel.opacityMode === Panel.Global.Translucent
    property bool isAdaptive: panel.opacityMode === Panel.Global.Adaptive
    property bool floating: panel.floating
    property bool hasCompositing: KWindowSystem.isPlatformX11 ? KX11Extras.compositingActive : true
    readonly property bool screenCovered: touchingWindow && panel.visibilityMode == Panel.Global.NormalPanel
    property var stateTriggers: [floating, screenCovered, isOpaque, isAdaptive, isTransparent, hasCompositing, containment]
    onStateTriggersChanged: {
        // The pills stay glass and keep their place whether or not a window
        // touches the panel, as the Windows taskbar does.
        if (usePills) {
            panelOpacity = 0
            floatingnessTarget = 0
            if (containment) {
                containment.plasmoid.containmentDisplayHints &= ~(PlasmaCore.Types.ContainmentPrefersOpaqueBackground | PlasmaCore.Types.ContainmentPrefersFloatingApplets)
            }
            return
        }
        let opaqueApplets = false
        let floatingApplets = false
        if ((!floating || screenCovered) && (isOpaque || (screenCovered && isAdaptive))) {
            panelOpacity = 1
            opaqueApplets = true
            floatingnessTarget = 0
        } else if ((!floating || screenCovered) && (isTransparent || (!screenCovered && isAdaptive))) {
            panelOpacity = 0
            floatingnessTarget = 0
        } else if ((floating && !screenCovered) && (isTransparent || isAdaptive)) {
            panelOpacity = 0
            floatingnessTarget = 1
            floatingApplets = true
        } else if (floating && !screenCovered && isOpaque) {
            panelOpacity = 1
            opaqueApplets = true
            floatingnessTarget = 1
            floatingApplets = true
        }
        if (!KWindowSystem.isPlatformWayland && !KX11Extras.compositingActive) {
            opaqueApplets = false
            panelOpacity = 0
        }
        // Not using panelOpacity to check as it has a NumberAnimation, and it will thus
        // be still read as the initial value here, before the animation starts.
        if (containment) {
            if (opaqueApplets) {
                containment.plasmoid.containmentDisplayHints |= PlasmaCore.Types.ContainmentPrefersOpaqueBackground
            } else {
                containment.plasmoid.containmentDisplayHints &= ~PlasmaCore.Types.ContainmentPrefersOpaqueBackground
            }
            if (floatingApplets) {
                containment.plasmoid.containmentDisplayHints |= PlasmaCore.Types.ContainmentPrefersFloatingApplets
            } else {
                containment.plasmoid.containmentDisplayHints &= ~PlasmaCore.Types.ContainmentPrefersFloatingApplets
            }
        }
    }

    function adjustPrefix() {
        if (!containment) {
            return "";
        }
        var pre;
        switch (containment.plasmoid.location) {
        case PlasmaCore.Types.LeftEdge:
            pre = "west";
            break;
        case PlasmaCore.Types.TopEdge:
            pre = "north";
            break;
        case PlasmaCore.Types.RightEdge:
            pre = "east";
            break;
        case PlasmaCore.Types.BottomEdge:
            pre = "south";
            break;
        default:
            pre = "";
            break;
        }
        translucentItem.prefix = opaqueItem.prefix = floatingTranslucentItem.prefix = floatingOpaqueItem.prefix = [pre, ""];
    }

    onContainmentChanged: {
        if (!containment) {
            return;
        }
        containment.parent = containmentParent;
        containment.visible = true;
        containment.anchors.fill = containmentParent;
        containment.plasmoid.locationChanged.connect(adjustPrefix);
        adjustPrefix();
        pillTimer.restart();
    }

    Binding {
        target: panel
        property: "length"
        when: containment
        value: {
            if (!containment) {
                return;
            }
            if (verticalPanel) {
                if (containment.Layout.fillHeight) {
                    if (panel.lengthMode == Panel.Global.Custom) {
                        return panel.maximumHeight
                    } else {
                        return panel.screenGeometry.height
                    }
                }
                return containment.Layout.preferredHeight
            } else {
                if (containment.Layout.fillWidth) {
                    if (panel.lengthMode == Panel.Global.Custom) {
                        return panel.maximumWidth
                    } else {
                        return panel.screenGeometry.width
                    }
                }
                return containment.Layout.preferredWidth
            }
        }
        restoreMode: Binding.RestoreBinding
    }

    Binding {
        target: panel
        property: "backgroundHints"
        when: containment
        value: {
            if (!containment) {
                return;
            }

            return containment.plasmoid.backgroundHints;
        }
        restoreMode: Binding.RestoreBinding
    }

    KSvg.FrameSvgItem {

        Accessible.name: i18n("Panel Focus Indicator")

        x: root.verticalPanel || !panel.activeFocusItem
            ? translucentItem.x
            : Math.max(panel.activeFocusItem.Kirigami.ScenePosition.x, panel.activeFocusItem.Kirigami.ScenePosition.x)
        y: root.verticalPanel && panel.activeFocusItem
            ? Math.max(panel.activeFocusItem.Kirigami.ScenePosition.y, panel.activeFocusItem.Kirigami.ScenePosition.y)
            : translucentItem.y

        width: panel.activeFocusItem
            ? (root.verticalPanel ? translucentItem.width : Math.min(panel.activeFocusItem.width, panel.activeFocusItem.width))
            : 0
        height: panel.activeFocusItem
            ? (root.verticalPanel ?  Math.min(panel.activeFocusItem.height, panel.activeFocusItem.height) : translucentItem.height)
            : 0

        visible: panel.active && panel.activeFocusItem

        imagePath: "widgets/tabbar"
        prefix: {
            if (!root.containment) {
                return "";
            }
            var prefix = ""
            switch (root.containment.plasmoid.location) {
                case PlasmaCore.Types.LeftEdge:
                    prefix = "west-active-tab";
                    break;
                case PlasmaCore.Types.TopEdge:
                    prefix = "north-active-tab";
                    break;
                case PlasmaCore.Types.RightEdge:
                    prefix = "east-active-tab";
                    break;
                default:
                    prefix = "south-active-tab";
            }
            if (!hasElementPrefix(prefix)) {
                prefix = "active-tab";
            }
            return prefix;
        }
    }
    Item {
        id: containmentParent
        anchors.centerIn: root.usePills ? undefined : (isOpaque ? floatingOpaqueItem : floatingTranslucentItem)
        // In pill mode the widgets sit in the pills' band: 62 px floating,
        // with the 14 px gap to the screen edge below it (above it when the
        // taskbar is at the top), or the whole 52 px docked bar.
        y: root.usePills ? root.pillTop : 0
        width: root.verticalPanel ? panel.thickness : root.width - fixedLeftFloatingPadding - fixedRightFloatingPadding
        height: root.verticalPanel ? root.height - fixedBottomFloatingPadding - fixedTopFloatingPadding : (root.usePills ? root.pillHeight : panel.thickness)
    }

    // Wild OS: a right click on the taskbar's empty space (around the pills,
    // on a spacer, between app buttons) opens the taskbar menu of
    // MenusShell.dc in place of Plasma's widget menu. A press on any other
    // widget is passed on, so app buttons, Start and the status icons keep
    // their own menus.
    function emptyAt(x: real, y: real): bool {
        const layout = containment?.layoutManager?.layout;
        if (!layout) {
            return true;
        }
        for (const child of Array.from(layout.children)) {
            if (!child.visible || !child.applet) {
                continue;
            }
            const p = child.mapFromItem(root, x, y);
            if (p.x < 0 || p.y < 0 || p.x >= child.width || p.y >= child.height) {
                continue;
            }
            const plugin = child.applet.plasmoid?.pluginName;
            if (plugin === "org.kde.plasma.panelspacer" || plugin === "org.wildos.divider") {
                return true;
            }
            if (plugin !== "org.wildos.tasks") {
                return false;
            }
            // In the taskbar apps widget only the gaps are empty; each app
            // button carries its launcher keys.
            let item = child.applet;
            let q = item.mapFromItem(root, x, y);
            while (item) {
                if (item.launchKeys !== undefined) {
                    return false;
                }
                const next = item.childAt(q.x, q.y);
                if (!next) {
                    break;
                }
                q = item.mapToItem(next, q.x, q.y);
                item = next;
            }
            return true;
        }
        return true;
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.usePills
        acceptedButtons: Qt.RightButton
        onPressed: mouse => mouse.accepted = root.emptyAt(mouse.x, mouse.y)
        onClicked: mouse => root.openTaskbarMenu(mouse.x)
    }

    // Show desktop, as on Windows: the last 8 px of the bar, down to the
    // screen's corner pixel, so a flick of the pointer into the corner finds
    // it. Clicking minimises every window and clicking again brings the same
    // windows back (the wildos_gestures KWin effect, also on Win+D).
    // With peek on, resting the pointer there fades the windows out until it
    // moves away, and a click while peeking minimises them. Settings >
    // Personalisation > Taskbar turns both on and off (wildosrc [Taskbar]).
    MouseArea {
        id: showDesktopCorner
        visible: root.usePills && WildOS.TaskbarStyle.showDesktop
        x: root.width - width
        y: 0
        width: 8
        height: root.height
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Show the desktop")

        // The desktop is showing because of a peek, so leaving undoes it.
        property bool peeking: false
        property var pendingToggle: null

        onContainsMouseChanged: {
            if (containsMouse) {
                if (WildOS.TaskbarStyle.peek && !KWindowSystem.showingDesktop) {
                    peekTimer.restart();
                }
                return;
            }
            peekTimer.stop();
            if (peeking) {
                peeking = false;
                KWindowSystem.showingDesktop = false;
            }
        }
        onClicked: {
            peekTimer.stop();
            if (!peeking) {
                root.toggleDesktop();
                return;
            }
            // The peek ends once the windows are minimised, so they stay
            // out of sight rather than fading back first.
            peeking = false;
            // Held here so the reply is not collected before it arrives.
            pendingToggle = root.toggleDesktop();
            pendingToggle.finished.connect(() => {
                KWindowSystem.showingDesktop = false;
                pendingToggle = null;
            });
        }
        onVisibleChanged: {
            if (!visible && peeking) {
                peeking = false;
                KWindowSystem.showingDesktop = false;
            }
        }

        // Windows waits about half a second before peeking, so passing
        // through the corner on the way somewhere does nothing.
        Timer {
            id: peekTimer
            interval: 500
            onTriggered: {
                showDesktopCorner.peeking = true;
                KWindowSystem.showingDesktop = true;
            }
        }
        Connections {
            target: root
            function onShowingDesktopChanged() {
                if (!root.showingDesktop) {
                    showDesktopCorner.peeking = false;
                }
            }
        }

        // A hairline on its left edge while the pointer is on it, so it can
        // be found, and nothing otherwise. Floating, the status pill's edge
        // can reach into the sliver, so the line sits nearer the screen edge.
        Rectangle {
            visible: showDesktopCorner.containsMouse
            x: root.docked ? 0 : 5
            y: Math.round((root.pillHeight - height) / 2)
            width: 1
            height: 24
            color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(46 / 255, 48 / 255, 52 / 255, 0.35)
        }
    }

    // The same as Win+D: runs the wildos_gestures KWin effect's
    // shortcut through the global shortcut service.
    function toggleDesktop(): var {
        return DBus.SessionBus.asyncCall({
            service: "org.kde.kglobalaccel",
            path: "/component/kwin",
            iface: "org.kde.kglobalaccel.Component",
            member: "invokeShortcut",
            arguments: [new DBus.string("Wild OS show desktop")],
        });
    }

    function openTaskbarMenu(x: real): void {
        const entries = [];
        if (taskManagerEntry.count > 0) {
            entries.push({ text: qsTr("Task Manager"), icon: "menu-task-manager", shortcut: qsTr("Ctrl+Shift+Esc"),
                run: () => runner.connectSource("kstart --application org.wildos.taskmanager") });
            entries.push({ separator: true });
        }
        if (settingsEntry.count > 0) {
            entries.push({ text: qsTr("Taskbar settings"), icon: "menu-settings",
                run: () => runner.connectSource("kstart --desktopfile org.wildos.settings -- wildos-settings --page taskbar") });
        }
        entries.push({ text: qsTr("Show the desktop"), icon: "menu-desktop", shortcut: "Win+D",
            run: () => root.toggleDesktop() });
        taskbarMenu.entries = entries;
        taskbarMenu.x = Math.round(Math.max(8, Math.min(root.width - taskbarMenu.implicitWidth - 8, x)));
        taskbarMenu.y = -12 - taskbarMenu.implicitHeight;
        taskbarMenu.open();
    }

    UI.Menu {
        id: taskbarMenu
        popupType: T.Popup.Window
        minimumWidth: 280
        maximumWidth: 280
    }

    FolderListModel {
        id: taskManagerEntry
        folder: "file:///usr/share/applications"
        nameFilters: ["org.wildos.taskmanager.desktop"]
        showDirs: false
    }

    FolderListModel {
        id: settingsEntry
        folder: "file:///usr/share/applications"
        nameFilters: ["org.wildos.settings.desktop"]
        showDirs: false
    }

    P5Support.DataSource {
        id: runner
        engine: "executable"
        onNewData: source => disconnectSource(source)
    }
}
