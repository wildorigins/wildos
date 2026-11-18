/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import org.kde.kwin.decoration
import org.kde.ksvg 1.0 as KSvg

Decoration {
    id: root
    property bool animate: false
    // Wild OS: as in Windows, a dialog that cannot be resized to fill the
    // screen (a message box, a modal prompt) shows only its close button.
    // Other windows keep minimise and maximise, a disabled one greyed out.
    function shownButtons(buttons) {
        var client = decoration.client;
        if (client.maximizeable || (client.minimizeable && !client.modal)) {
            return buttons;
        }
        var shown = [];
        for (var i = 0; i < buttons.length; i++) {
            if (buttons[i] !== DecorationOptions.DecorationButtonMinimize
                    && buttons[i] !== DecorationOptions.DecorationButtonMaximizeRestore) {
                shown.push(buttons[i]);
            }
        }
        return shown;
    }
    property alias decorationMask: maskItem.mask
    property alias supportsMask: backgroundSvg.supportsMask
    Component.onCompleted: {
        borders.left   = Qt.binding(function() { return Math.max(0, auroraeTheme.borderLeft);});
        borders.right  = Qt.binding(function() { return Math.max(0, auroraeTheme.borderRight);});
        borders.top    = Qt.binding(function() { return Math.max(0, auroraeTheme.borderTop);});
        borders.bottom = Qt.binding(function() { return Math.max(0, auroraeTheme.borderBottom);});
        maximizedBorders.left   = Qt.binding(function() { return Math.max(0, auroraeTheme.borderLeftMaximized);});
        maximizedBorders.right  = Qt.binding(function() { return Math.max(0, auroraeTheme.borderRightMaximized);});
        maximizedBorders.bottom = Qt.binding(function() { return Math.max(0, auroraeTheme.borderBottomMaximized);});
        maximizedBorders.top    = Qt.binding(function() { return Math.max(0, auroraeTheme.borderTopMaximized);});
        // Wild OS: the frame is one pixel, too thin to catch with the
        // pointer. As on Windows 11, a band just outside the sides and the
        // bottom of the window resizes it and draws nothing. Not on a
        // maximised edge, where the band would lie on the next screen.
        extendedBorders.left   = Qt.binding(function() { return decoration.client.maximizedHorizontally ? 0 : 8; });
        extendedBorders.right  = Qt.binding(function() { return decoration.client.maximizedHorizontally ? 0 : 8; });
        extendedBorders.bottom = Qt.binding(function() { return decoration.client.maximizedVertically ? 0 : 8; });
        padding.left   = auroraeTheme.paddingLeft;
        padding.right  = auroraeTheme.paddingRight;
        padding.bottom = auroraeTheme.paddingBottom;
        padding.top    = auroraeTheme.paddingTop;
        root.animate = true;
        root.addMaximizedCloseFills();
    }
    // Wild OS: a maximised window has square corners, so as on Windows 11 its
    // close button's hover fills the corner. Aurorae buttons have no
    // maximised state, so when the theme has a close-maximized.svg beside
    // close.svg with square "hover" and "pressed" fills (Wild OS's does),
    // they go under the close button of a maximised window and show through
    // outside its rounded hover. Other themes are left as they are.
    KSvg.FrameSvg {
        id: closeSvg
        property bool supportsMaximized: imagePath !== "" && hasElementPrefix("hover") && hasElementPrefix("pressed")
        imagePath: auroraeTheme.closeButtonPath.replace(/close\.svgz?$/, "close-maximized.svg")
    }
    Component {
        id: maximizedCloseFill
        Item {
            id: fill
            // Set to the close button, which is also the parent.
            property Item button
            objectName: "wildosMaximizedCloseFill"
            anchors.fill: parent
            z: -1
            visible: decoration.client.maximized
            KSvg.FrameSvgItem {
                anchors.fill: parent
                imagePath: closeSvg.imagePath
                prefix: "hover"
                // The same states and fade as the button's own hover.
                opacity: (fill.button.hovered && !fill.button.pressed && !fill.button.toggled) ? 1 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: auroraeTheme.animationTime
                    }
                }
            }
            KSvg.FrameSvgItem {
                anchors.fill: parent
                imagePath: closeSvg.imagePath
                prefix: "pressed"
                opacity: (fill.button.pressed || fill.button.toggled) ? 1 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: auroraeTheme.animationTime
                    }
                }
            }
        }
    }
    function addMaximizedCloseFills() {
        if (!closeSvg.supportsMaximized) {
            return;
        }
        // Each group holds one Row of buttons, rebuilt when the buttons change.
        var groups = [leftButtonGroup, rightButtonGroup];
        for (var g = 0; g < groups.length; g++) {
            var buttons = groups[g].children[0].children;
            for (var i = 0; i < buttons.length; i++) {
                var button = buttons[i];
                if (button.buttonType !== DecorationOptions.DecorationButtonClose) {
                    continue;
                }
                var present = false;
                for (var c = 0; c < button.children.length; c++) {
                    present = present || button.children[c].objectName === "wildosMaximizedCloseFill";
                }
                if (!present) {
                    maximizedCloseFill.createObject(button, {button: button});
                }
            }
        }
    }
    Connections {
        target: leftButtonGroup.children[0]
        function onChildrenChanged() {
            Qt.callLater(root.addMaximizedCloseFills);
        }
    }
    Connections {
        target: rightButtonGroup.children[0]
        function onChildrenChanged() {
            Qt.callLater(root.addMaximizedCloseFills);
        }
    }
    DecorationOptions {
        id: options
        deco: decoration
    }
    Item {
        id: titleRect
        x: decoration.client.maximized ? maximizedBorders.left : borders.left
        y: decoration.client.maximized ? 0 : root.borders.bottom
        width: decoration.client.width//parent.width - x - (decoration.client.maximized ? maximizedBorders.right : borders.right)
        height: decoration.client.maximized ? maximizedBorders.top : borders.top
        Component.onCompleted: {
            decoration.installTitleItem(titleRect);
        }
    }
    KSvg.FrameSvg {
        property bool supportsInactive: hasElementPrefix("decoration-inactive")
        property bool supportsMask: hasElementPrefix("mask")
        property bool supportsMaximized: hasElementPrefix("decoration-maximized")
        property bool supportsMaximizedInactive: hasElementPrefix("decoration-maximized-inactive")
        property bool supportsInnerBorder: hasElementPrefix("innerborder")
        property bool supportsInnerBorderInactive: hasElementPrefix("innerborder-inactive")
        id: backgroundSvg
        imagePath: auroraeTheme.decorationPath
    }
    KSvg.FrameSvgItem {
        id: decorationActive
        property bool shown: (!decoration.client.maximized || !backgroundSvg.supportsMaximized) && (decoration.client.active || !backgroundSvg.supportsInactive)
        anchors.fill: parent
        imagePath: backgroundSvg.imagePath
        prefix: "decoration"
        opacity: shown ? 1 : 0
        enabledBorders: decoration.client.maximized ? KSvg.FrameSvg.NoBorder : KSvg.FrameSvg.TopBorder | KSvg.FrameSvg.BottomBorder | KSvg.FrameSvg.LeftBorder | KSvg.FrameSvg.RightBorder
        Behavior on opacity {
            enabled: root.animate
            NumberAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    KSvg.FrameSvgItem {
        id: decorationInactive
        anchors.fill: parent
        imagePath: backgroundSvg.imagePath
        prefix: "decoration-inactive"
        opacity: (!decoration.client.active && backgroundSvg.supportsInactive) ? 1 : 0
        enabledBorders: decoration.client.maximized ? KSvg.FrameSvg.NoBorder : KSvg.FrameSvg.TopBorder | KSvg.FrameSvg.BottomBorder | KSvg.FrameSvg.LeftBorder | KSvg.FrameSvg.RightBorder
        Behavior on opacity {
            enabled: root.animate
            NumberAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    KSvg.FrameSvgItem {
        id: decorationMaximized
        property bool shown: decoration.client.maximized && backgroundSvg.supportsMaximized && (decoration.client.active || !backgroundSvg.supportsMaximizedInactive)
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            leftMargin: 0
            rightMargin: 0
            topMargin: 0
        }
        imagePath: backgroundSvg.imagePath
        prefix: "decoration-maximized"
        height: parent.maximizedBorders.top
        opacity: shown ? 1 : 0
        enabledBorders: KSvg.FrameSvg.NoBorder
        Behavior on opacity {
            enabled: root.animate
            NumberAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    KSvg.FrameSvgItem {
        id: decorationMaximizedInactive
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            leftMargin: 0
            rightMargin: 0
            topMargin: 0
        }
        imagePath: backgroundSvg.imagePath
        prefix: "decoration-maximized-inactive"
        height: parent.maximizedBorders.top
        opacity: (!decoration.client.active && decoration.client.maximized && backgroundSvg.supportsMaximizedInactive) ? 1 : 0
        enabledBorders: KSvg.FrameSvg.NoBorder
        Behavior on opacity {
            enabled: root.animate
            NumberAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    AuroraeButtonGroup {
        id: leftButtonGroup
        buttons: root.shownButtons(options.titleButtonsLeft)
        width: childrenRect.width
        animate: root.animate
        anchors {
            left: root.left
            leftMargin: decoration.client.maximized ? auroraeTheme.titleEdgeLeftMaximized : (auroraeTheme.titleEdgeLeft + root.padding.left)
        }
    }
    AuroraeButtonGroup {
        id: rightButtonGroup
        buttons: root.shownButtons(options.titleButtonsRight)
        width: childrenRect.width
        animate: root.animate
        anchors {
            right: root.right
            rightMargin: decoration.client.maximized ? auroraeTheme.titleEdgeRightMaximized : (auroraeTheme.titleEdgeRight + root.padding.right)
        }
    }
    Text {
        id: caption
        // Wild OS: Qt and KDE apps join "document" and "app" with a spaced
        // em dash. Windows uses a hyphen ("Documents - File Explorer"), so the
        // title bar shows that instead.
        text: decoration.client.caption.replace(/ [–—] /g, " - ")
        textFormat: Text.PlainText
        horizontalAlignment: auroraeTheme.horizontalAlignment
        verticalAlignment: auroraeTheme.verticalAlignment
        elide: Text.ElideRight
        height: Math.max(auroraeTheme.titleHeight, auroraeTheme.buttonHeight * auroraeTheme.buttonSizeFactor)
        color: decoration.client.active ? auroraeTheme.activeTextColor : auroraeTheme.inactiveTextColor
        font: options.titleFont
        renderType: Text.NativeRendering
        anchors {
            left: leftButtonGroup.right
            right: rightButtonGroup.left
            top: root.top
            topMargin: decoration.client.maximized ? auroraeTheme.titleEdgeTopMaximized : (auroraeTheme.titleEdgeTop + root.padding.top)
            leftMargin: auroraeTheme.titleBorderLeft
            rightMargin: auroraeTheme.titleBorderRight
        }
        Behavior on color {
            enabled: root.animate
            ColorAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    KSvg.FrameSvgItem {
        id: innerBorder
        anchors {
            fill: parent
            leftMargin: parent.padding.left + parent.borders.left - margins.left
            rightMargin: parent.padding.right + parent.borders.right - margins.right
            topMargin: parent.padding.top + parent.borders.top - margins.top
            bottomMargin: parent.padding.bottom + parent.borders.bottom - margins.bottom
        }
        visible: parent.borders.left > fixedMargins.left
            && parent.borders.right > fixedMargins.right
            && parent.borders.top > fixedMargins.top
            && parent.borders.bottom > fixedMargins.bottom

        imagePath: backgroundSvg.imagePath
        prefix: "innerborder"
        opacity: (decoration.client.active && !decoration.client.maximized && backgroundSvg.supportsInnerBorder) ? 1 : 0
        Behavior on opacity {
            enabled: root.animate
            NumberAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    KSvg.FrameSvgItem {
        id: innerBorderInactive
        anchors {
            fill: parent
            leftMargin: parent.padding.left + parent.borders.left - margins.left
            rightMargin: parent.padding.right + parent.borders.right - margins.right
            topMargin: parent.padding.top + parent.borders.top - margins.top
            bottomMargin: parent.padding.bottom + parent.borders.bottom - margins.bottom
        }

        visible: parent.borders.left > fixedMargins.left
            && parent.borders.right > fixedMargins.right
            && parent.borders.top > fixedMargins.top
            && parent.borders.bottom > fixedMargins.bottom

        imagePath: backgroundSvg.imagePath
        prefix: "innerborder-inactive"
        opacity: (!decoration.client.active && !decoration.client.maximized && backgroundSvg.supportsInnerBorderInactive) ? 1 : 0
        Behavior on opacity {
            enabled: root.animate
            NumberAnimation {
                duration: auroraeTheme.animationTime
            }
        }
    }
    KSvg.FrameSvgItem {
        id: maskItem
        anchors.fill: parent
        // This makes the mask slightly smaller than the frame. Since the svg will have antialiasing and the mask not,
        // there will be artifacts at the corners, if they go under the svg they're less evident
        anchors.margins: 1
        imagePath: backgroundSvg.imagePath
        opacity: 0
        enabledBorders: KSvg.FrameSvg.TopBorder | KSvg.FrameSvg.BottomBorder | KSvg.FrameSvg.LeftBorder | KSvg.FrameSvg.RightBorder
    }
}
