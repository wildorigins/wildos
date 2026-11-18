import QtQuick
import WildOS.UI as UI

import "components"

// Shown when authentication succeeded without a password prompt, for example
// with a fingerprint or for an account with no password: one clear button.
FocusScope {
    id: screen

    property Item backdrop: null
    property real screenHeight: height
    property real topEdge: 0
    property string userName: ""
    property string userImage: ""

    focus: true

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(screen.topEdge, Math.round(screen.screenHeight * 500 / 900))
        width: 400
        spacing: 14

        UI.Avatar {
            anchors.horizontalCenter: parent.horizontalCenter
            size: 88
            ring: true
            shadow: true
            source: screen.userImage
        }

        UI.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: screen.userName
            size: 18
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkOnWallpaper
            bottomPadding: 6
        }

        UI.Button {
            id: unlockButton

            anchors.horizontalCenter: parent.horizontalCenter
            width: 300
            kind: "primary"
            size: "large"
            focus: true
            text: qsTr("Unlock")

            onClicked: Qt.quit()
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
    }

    Component.onCompleted: unlockButton.forceActiveFocus()
}
