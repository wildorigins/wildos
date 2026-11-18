import QtQuick
import WildOS.UI as UI

// The top of a detail page: back, the page title in the design's 15 px
// heading, and the page's on and off switch when it has one.
Item {
    id: header

    property string title
    property bool hasSwitch: false
    property bool switchOn: false
    property alias backButton: back
    property alias toggle: toggle

    signal back()
    signal switchToggled()

    implicitHeight: 44

    UI.IconButton {
        id: back
        // EmptyStates.dc: a 32 px back button at the content edge, the
        // title 10 px after it.
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        width: 32
        height: 32
        iconName: "chevron-left"
        iconSize: 16
        stroke: 2
        tip: qsTr("Back to quick settings")
        onClicked: header.back()
    }

    UI.Label {
        anchors.left: back.right
        anchors.leftMargin: 10
        anchors.right: toggle.visible ? toggle.left : parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: header.title
        size: 15
        font.weight: UI.Style.weightSemiBold
        Accessible.role: Accessible.Heading
    }

    UI.Toggle {
        id: toggle
        visible: header.hasSwitch
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        checked: header.switchOn
        Accessible.name: header.title
        onToggled: header.switchToggled()
    }
}
