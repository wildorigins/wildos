/*
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    A button for an app that is still opening but that Plasma's task model
    has let go of. KWin drops its launch feedback, and with it Plasma's
    starting button, as soon as another window is activated, while a slow
    app may still be on its way. wildos-launchd is still waiting for the
    app's window, so the button stays, with
    the same starting line and glow as Task.qml, until the window turns up
    and takes its place.
*/

import QtQuick
import WildOS.UI as UI

import org.kde.kirigami as Kirigami

Item {
    id: starting

    required property var modelData

    width: Style.button
    height: Style.button

    readonly property bool reducedMotion: Kirigami.Units.longDuration <= 1

    Accessible.role: Accessible.Button
    Accessible.name: modelData.name || modelData.desktopId
    Accessible.description: "Starting"

    HoverHandler {
        id: hover
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: Style.highlightInset
        radius: Style.highlightRadius
        color: hover.hovered ? Style.hover : "transparent"
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    Rectangle {
        id: glow
        anchors.fill: parent
        anchors.margins: Style.highlightInset
        radius: Style.highlightRadius
        color: Style.startingGlow
        opacity: 0
        visible: !starting.reducedMotion

        SequentialAnimation on opacity {
            running: glow.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 1; duration: Style.startingCycle / 2; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1; to: 0; duration: Style.startingCycle / 2; easing.type: Easing.InOutSine }
        }
    }

    UI.ThemeIcon {
        trim: true
        anchors.centerIn: parent
        width: Style.icon
        height: Style.icon
        source: starting.modelData.icon || "application-x-executable"
    }

    Rectangle {
        id: mark
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Style.markBottom
        height: Style.markHeight
        radius: 2
        width: Style.markWidth
        color: Style.startingMark
    }

    SequentialAnimation {
        running: starting.visible && !starting.reducedMotion
        loops: Animation.Infinite
        ParallelAnimation {
            NumberAnimation { target: mark; property: "width"; from: Style.markWidth; to: Style.startingWidth; duration: Style.startingCycle * 0.6; easing.type: Easing.InOutSine }
            NumberAnimation { target: mark; property: "opacity"; from: 0.5; to: 1; duration: Style.startingCycle * 0.6; easing.type: Easing.InOutSine }
        }
        ParallelAnimation {
            NumberAnimation { target: mark; property: "width"; from: Style.startingWidth; to: Style.markWidth; duration: Style.startingCycle * 0.4; easing.type: Easing.InOutSine }
            NumberAnimation { target: mark; property: "opacity"; from: 1; to: 0.5; duration: Style.startingCycle * 0.4; easing.type: Easing.InOutSine }
        }
    }
}
