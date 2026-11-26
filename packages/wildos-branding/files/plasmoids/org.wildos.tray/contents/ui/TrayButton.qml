import QtQuick
import WildOS.UI as UI

// One of the tray's own buttons on the taskbar (the arrow, safely remove,
// keep awake): a 16 px line icon in a 32 px cell, drawn and centred exactly
// as the app icons beside it are, so they share a centre line and a hover
// shape whatever height the panel gives the row.
MouseArea {
    id: button

    property string tip
    property string accessibleName: tip
    property string iconName
    property string glyph
    property bool checked: false
    readonly property bool showFocus: activeFocus && Nav.keyboard

    signal activated()

    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: activated()

    Keys.onReturnPressed: activated()
    Keys.onEnterPressed: activated()
    Keys.onSpacePressed: activated()

    onPressed: label.hideImmediately()
    onClicked: activated()

    Rectangle {
        anchors.centerIn: parent
        width: 32
        height: 40
        radius: UI.Style.radiusSm
        color: button.pressed ? UI.Style.surfacePressed
            : button.checked ? UI.Style.surfaceSelected
            : button.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: button.showFocus
            targetRadius: UI.Style.radiusSm
        }
    }

    UI.Icon {
        visible: button.iconName.length > 0
        anchors.centerIn: parent
        name: button.iconName
        size: 16
        strokeWidth: 2
        color: UI.Style.iconInk
    }

    UI.Glyph {
        visible: button.glyph.length > 0
        anchors.centerIn: parent
        width: 16
        height: 16
        path: button.glyph
        color: UI.Style.iconInk
    }

    // Not over the popup the button has just opened.
    TaskbarTip {
        id: label
        text: button.checked ? "" : button.tip
    }
}
