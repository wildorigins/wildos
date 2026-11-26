import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// A place on the start screen's rail (WriteStart.dc): 38px, radius 8, a
// 17px line icon and 13.5px text, filled with the accent when current.
T.AbstractButton {
    id: button

    property string iconName
    property bool current: false

    Layout.fillWidth: true
    implicitHeight: 38
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Rectangle {
        radius: 8
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: button.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: button.current ? UI.Style.fillSelected(0.16, 0.12) : button.down ? UI.Style.surfacePressed : button.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        UI.FocusRing {
            visible: button.visualFocus
        }
    }

    contentItem: Row {
        leftPadding: 14
        spacing: 12
        UI.Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: button.iconName
            size: 17
            color: button.current ? UI.Style.iconInk : UI.Style.iconMuted
        }
        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            size: 13.5
            font.weight: button.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
        }
    }
}
