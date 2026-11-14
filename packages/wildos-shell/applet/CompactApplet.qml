/*
    SPDX-FileCopyrightText: 2013 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

import org.kde.plasma.core as PlasmaCore
import org.kde.ksvg 1.0 as KSvg
import org.kde.plasma.plasmoid 2.0
import org.kde.kquickcontrolsaddons 2.0
import org.kde.kirigami 2.20 as Kirigami
import org.wildos.shell as WildOS

PlasmaCore.ToolTipArea {
    id: root
    objectName: "org.kde.desktop-CompactApplet"
    anchors.fill: parent

    mainText: plasmoidItem ? plasmoidItem.toolTipMainText : ""
    subText: plasmoidItem ? plasmoidItem.toolTipSubText : ""
    location: Plasmoid.location
    active: plasmoidItem ? !plasmoidItem.expanded : false
    textFormat: plasmoidItem ? plasmoidItem.toolTipTextFormat : 0
    mainItem: plasmoidItem && plasmoidItem.toolTipItem ? plasmoidItem.toolTipItem : null

    readonly property bool vertical: location === PlasmaCore.Types.RightEdge || location === PlasmaCore.Types.LeftEdge

    property Item fullRepresentation
    property Item compactRepresentation
    property Item expandedFeedback: expandedItem
    property PlasmoidItem plasmoidItem

    onCompactRepresentationChanged: {
        if (compactRepresentation) {
            compactRepresentation.anchors.fill = null;
            compactRepresentation.parent = compactRepresentationParent;
            compactRepresentation.anchors.fill = compactRepresentationParent;
            compactRepresentation.visible = true;
        }
        root.visible = true;
    }

    onFullRepresentationChanged: {
        if (fullRepresentation) {
            fullRepresentation.anchors.fill = null;
            fullRepresentation.parent = appletParent;
            fullRepresentation.anchors.fill = appletParent;
        }
        maybeWarmUp();
    }

    // Wild OS: an applet that preloads (Start) also draws its pop-up once
    // while the sign-in splash still covers the screen, and the splash waits
    // for that (wildos-wait-desktop). Preloading only builds the contents;
    // the window and every image in it were still made on the first click,
    // which stuttered.
    property bool warming: false
    property bool warmedUp: false
    property int warmFrames: 0

    onPlasmoidItemChanged: maybeWarmUp()
    Component.onCompleted: maybeWarmUp()
    Connections {
        target: root.plasmoidItem
        function onFullRepresentationItemChanged() { root.maybeWarmUp(); }
        // Opened for real during the first draw: that is now the real pop-up.
        function onExpandedChanged() {
            if (root.plasmoidItem.expanded && dialog.warmShown) {
                root.finishWarmUp();
                dialog.warmShown = false;
                dialog.oldStatus = Plasmoid.status;
                Plasmoid.status = PlasmaCore.Types.RequiresAttentionStatus;
                dialog.requestActivate();
            }
        }
    }

    function warmUpWanted() {
        return !!(plasmoidItem && plasmoidItem.preloadFullRepresentation
            && (fullRepresentation || plasmoidItem.fullRepresentationItem)
            && compactRepresentation && !warmedUp);
    }
    // The panel hands the contents over more than once while it lays out,
    // so the draw waits until that has settled. The longest pause between
    // handovers seen on software rendering is 111 ms; the start-up
    // animation waits on this, so the quiet spell is kept just above it.
    function maybeWarmUp() {
        if (warmUpWanted()) {
            warmUp.restart();
        }
    }
    function finishWarmUp() {
        warming = false;
        warmLimit.stop();
        WildOS.Session.desktopReady();
    }
    Timer {
        id: warmUp
        interval: 150
        onTriggered: {
            if (!root.warmUpWanted() || root.plasmoidItem.expanded) {
                return;
            }
            root.warmedUp = true;
            // After a plasmashell restart nothing covers the screen, so the
            // pop-up would flash up: the first click does the work instead.
            if (!WildOS.Session.splashShowing) {
                return;
            }
            // Plasma hands the preloaded contents to the pop-up only when it
            // opens; the first draw needs them now.
            if (!root.fullRepresentation) {
                root.fullRepresentation = root.plasmoidItem.fullRepresentationItem;
            }
            root.warmFrames = 0;
            root.warming = true;
            warmLimit.start();
        }
    }
    Timer {
        id: warmLimit
        interval: 2000
        onTriggered: root.finishWarmUp()
    }
    Connections {
        target: dialog
        enabled: root.warming
        function onFrameSwapped() {
            // The second frame has every image in it; the first may not.
            if (++root.warmFrames >= 2) {
                root.finishWarmUp();
            }
        }
    }

    FocusScope {
        id: compactRepresentationParent
        anchors.fill: parent
        activeFocusOnTab: true
        onActiveFocusChanged: {
            // When the scope gets the active focus, try to focus its first descendant,
            // if there is on which has activeFocusOnTab
            if (!activeFocus) {
                return;
            }
            let nextItem = nextItemInFocusChain();
            let candidate = nextItem;
            while (candidate.parent) {
                if (candidate === compactRepresentationParent) {
                    nextItem.forceActiveFocus();
                    return;
                }
                candidate = candidate.parent;
            }
        }

        objectName: "expandApplet"
        Accessible.name: root.mainText
        Accessible.description: i18nd("plasma_shell_org.kde.plasma.desktop", "Open %1", root.subText)
        Accessible.role: Accessible.Button
        Accessible.onPressAction: Plasmoid.activated()

        Keys.onPressed: {
            switch (event.key) {
            case Qt.Key_Space:
            case Qt.Key_Enter:
            case Qt.Key_Return:
            case Qt.Key_Select:
                Plasmoid.activated();
                break;
            }
        }
    }

    KSvg.FrameSvgItem {
        id: expandedItem
        z: -100

        property var containerMargins: {
            let item = root;
            while (item.parent) {
                item = item.parent;
                if (item.isAppletContainer) {
                    return item.getMargins;
                }
            }
            return undefined;
        }

        anchors {
            fill: parent
            property bool returnAllMargins: true
            // The above makes sure margin is returned even for side margins, that
            // would be otherwise turned off.
            bottomMargin: !vertical && containerMargins ? -containerMargins('bottom', returnAllMargins) : 0
            topMargin: !vertical && containerMargins ? -containerMargins('top', returnAllMargins) : 0
            leftMargin: vertical && containerMargins ? -containerMargins('left', returnAllMargins) : 0
            rightMargin: vertical && containerMargins ? -containerMargins('right', returnAllMargins) : 0
        }
        imagePath: "widgets/tabbar"
        visible: opacity > 0
        prefix: {
            let prefix;
            switch (Plasmoid.location) {
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
        opacity: plasmoidItem && plasmoidItem.expanded ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.InOutQuad
            }
        }
    }

    Timer {
        id: expandedSync
        interval: 100
        onTriggered: plasmoidItem.expanded = dialog.visible;
    }

    Connections {
        target: Plasmoid.internalAction("configure")
        function onTriggered() {
            if (root.plasmoidItem.hideOnWindowDeactivate) {
                plasmoidItem.expanded = false
            }
        }
    }

    Connections {
        target: root.Plasmoid
        function onContextualActionsAboutToShow() { root.hideImmediately() }
    }

    PlasmaCore.AppletPopup {
        id: dialog
        objectName: "popupWindow"

        popupDirection: {
            switch (Plasmoid.location) {
            case PlasmaCore.Types.TopEdge:
                return Qt.BottomEdge
            case PlasmaCore.Types.LeftEdge:
                return Qt.RightEdge
            case PlasmaCore.Types.RightEdge:
                return Qt.LeftEdge
            default:
                return Qt.TopEdge
            }
        }
        // Wild OS: pop-ups from the taskbar sit 14 px above the pills and
        // 14 px in from the screen edge, as every flyout in the design does
        // (bottom 90 px over a 62 px bar that is 14 px off the edge).
        margin: Plasmoid.formFactor === PlasmaCore.Types.Horizontal ? 14
            : (Plasmoid.containmentDisplayHints & PlasmaCore.Types.ContainmentPrefersFloatingApplets) ? Kirigami.Units.largeSpacing : 0

        Behavior on margin {
            NumberAnimation {
                // Since the panel animation won't be perfectly in sync,
                // using a duration larger than the panel animation results
                // in a better-looking animation.
                duration: Kirigami.Units.veryLongDuration
                easing.type: Easing.OutCubic
            }
        }

        floating: Plasmoid.location === PlasmaCore.Types.Floating
        removeBorderStrategy: Plasmoid.location === PlasmaCore.Types.Floating
            ? PlasmaCore.AppletPopup.AtScreenEdges
            : PlasmaCore.AppletPopup.AtScreenEdges | PlasmaCore.AppletPopup.AtPanelEdges

        hideOnWindowDeactivate: root.plasmoidItem && root.plasmoidItem.hideOnWindowDeactivate
        visible: root.plasmoidItem && (root.plasmoidItem.expanded || root.warming) && fullRepresentation
        visualParent: root.compactRepresentation
        backgroundHints: (Plasmoid.containmentDisplayHints & PlasmaCore.Types.ContainmentPrefersOpaqueBackground) ? PlasmaCore.AppletPopup.SolidBackground : PlasmaCore.AppletPopup.StandardBackground
        appletInterface: root.plasmoidItem

        property var oldStatus: PlasmaCore.Types.UnknownStatus

        // Set while the unseen first draw is on screen, so its showing and
        // hiding don't count as the pop-up opening and closing.
        property bool warmShown: false

        onVisibleChanged: {
            if (visible && root.warming) {
                warmShown = true;
                return;
            }
            if (!visible && warmShown) {
                warmShown = false;
                return;
            }
            if (!visible) {
                expandedSync.restart();
                Plasmoid.status = oldStatus;
            } else {
                oldStatus = Plasmoid.status;
                Plasmoid.status = PlasmaCore.Types.RequiresAttentionStatus;
                // This call currently fails and complains at runtime:
                // QWindow::setWindowState: QWindow::setWindowState does not accept Qt::WindowActive
                dialog.requestActivate();
            }
        }
        //It's a MouseEventListener to get all the events, so the eventfilter will be able to catch them
        mainItem: MouseEventListener {
            id: appletParent

            focus: true

            Keys.onEscapePressed: {
                root.plasmoidItem.expanded = false;
            }

            Layout.minimumWidth: fullRepresentation ? fullRepresentation.Layout.minimumWidth : 0
            Layout.minimumHeight: fullRepresentation ? fullRepresentation.Layout.minimumHeight : 0

            Layout.maximumWidth: fullRepresentation ? fullRepresentation.Layout.maximumWidth : Infinity
            Layout.maximumHeight: fullRepresentation ? fullRepresentation.Layout.maximumHeight : Infinity

            implicitWidth: {
                if (root.fullRepresentation !== null) {
                    /****/ if (root.fullRepresentation.Layout.preferredWidth > 0) {
                        return root.fullRepresentation.Layout.preferredWidth;
                    } else if (root.fullRepresentation.implicitWidth > 0) {
                        return root.fullRepresentation.implicitWidth;
                    }
                }
                return Kirigami.Units.iconSizes.sizeForLabels * 35;
            }
            implicitHeight: {
                if (root.fullRepresentation !== null) {
                    /****/ if (fullRepresentation.Layout.preferredHeight > 0) {
                        return fullRepresentation.Layout.preferredHeight;
                    } else if (fullRepresentation.implicitHeight > 0) {
                        return fullRepresentation.implicitHeight;
                    }
                }
                return Kirigami.Units.iconSizes.sizeForLabels * 25;
            }

            onActiveFocusChanged: {
                if (activeFocus && fullRepresentation) {
                    fullRepresentation.forceActiveFocus()
                }
            }

            // Draws a line between the applet dialog and the panel
            KSvg.SvgItem {
                id: separator
                // Only draw for popups of panel applets, not desktop applets
                visible: [PlasmaCore.Types.TopEdge, PlasmaCore.Types.LeftEdge, PlasmaCore.Types.RightEdge, PlasmaCore.Types.BottomEdge]
                    .includes(Plasmoid.location) && !dialog.margin
                anchors {
                    topMargin: -dialog.topPadding
                    leftMargin: -dialog.leftPadding
                    rightMargin: -dialog.rightPadding
                    bottomMargin: -dialog.bottomPadding
                }
                z: 999 /* Draw the line on top of the applet */
                elementId: (Plasmoid.location === PlasmaCore.Types.TopEdge || Plasmoid.location === PlasmaCore.Types.BottomEdge) ? "horizontal-line" : "vertical-line"
                imagePath: "widgets/line"
                states: [
                    State {
                        when: Plasmoid.location === PlasmaCore.Types.TopEdge
                        AnchorChanges {
                            target: separator
                            anchors {
                                top: separator.parent.top
                                left: separator.parent.left
                                right: separator.parent.right
                            }
                        }
                        PropertyChanges {
                            target: separator
                            height: 1
                        }
                    },
                    State {
                        when: Plasmoid.location === PlasmaCore.Types.LeftEdge
                        AnchorChanges {
                            target: separator
                            anchors {
                                left: separator.parent.left
                                top: separator.parent.top
                                bottom: separator.parent.bottom
                            }
                        }
                        PropertyChanges {
                            target: separator
                            width: 1
                        }
                    },
                    State {
                        when: Plasmoid.location === PlasmaCore.Types.RightEdge
                        AnchorChanges {
                            target: separator
                            anchors {
                                top: separator.parent.top
                                right: separator.parent.right
                                bottom: separator.parent.bottom
                            }
                        }
                        PropertyChanges {
                            target: separator
                            width: 1
                        }
                    },
                    State {
                        when: Plasmoid.location === PlasmaCore.Types.BottomEdge
                        AnchorChanges {
                            target: separator
                            anchors {
                                left: separator.parent.left
                                right: separator.parent.right
                                bottom: separator.parent.bottom
                            }
                        }
                        PropertyChanges {
                            target: separator
                            height: 1
                        }
                    }
                ]
            }

            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true
        }
    }
}
