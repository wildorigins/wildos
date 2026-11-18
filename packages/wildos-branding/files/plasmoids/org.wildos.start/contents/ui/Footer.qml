import QtQuick
import WildOS.UI as UI

// The darker strip along the bottom: who is signed in on the left, Files,
// Settings and Power on the right.
Item {
    id: footer

    required property var launcher
    property bool powerMenuOpen: false
    property bool accountMenuOpen: false

    property alias userButton: userButton
    property alias filesButton: filesButton
    property alias settingsButton: settingsButton
    property alias powerButton: powerButton

    signal powerRequested()
    signal accountRequested()
    signal leaveUp()

    readonly property string userName: {
        const full = String(launcher.user.fullName ?? "").trim();
        return full.length > 0 ? full : String(launcher.user.loginName ?? "");
    }

    implicitHeight: Style.footerHeight

    function arrows(event, previous, next): void {
        if (event.key === Qt.Key_Left && previous) {
            previous.forceActiveFocus(Qt.BacktabFocusReason);
        } else if (event.key === Qt.Key_Right && next) {
            next.forceActiveFocus(Qt.TabFocusReason);
        } else if (event.key === Qt.Key_Up) {
            footer.leaveUp();
        } else {
            return;
        }
        Nav.keyboard = true;
        event.accepted = true;
    }

    WButton {
        id: userButton
        x: 22
        anchors.verticalCenter: parent.verticalCenter
        width: userRow.implicitWidth + 18
        height: 44
        radius: 22
        accessibleName: qsTr("Account options for %1").arg(footer.userName)
        highlighted: footer.accountMenuOpen
        onClicked: footer.accountRequested()
        Keys.onPressed: event => footer.arrows(event, null, filesButton)

        Row {
            id: userRow
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            UI.Avatar {
                size: 32
                source: footer.launcher.user.faceIconUrl
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 380)
                size: 14
                font.weight: UI.Style.weightMedium
                text: footer.userName
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        UI.IconButton {
            id: filesButton
            width: 44
            height: 44
            radius: 10
            focusPolicy: Qt.NoFocus
            iconName: "folder"
            iconSize: 19
            stroke: 1.8
            tip: qsTr("Files")
            onClicked: footer.launcher.launchApp("org.wildos.files.desktop", footer.launcher.homeUrl)
            Keys.onPressed: event => footer.arrows(event, userButton, settingsButton)
        }

        UI.IconButton {
            id: settingsButton
            width: 44
            height: 44
            radius: 10
            focusPolicy: Qt.NoFocus
            iconName: "cog"
            iconSize: 19
            stroke: 1.6
            tip: qsTr("Settings")
            onClicked: footer.launcher.openSettings()
            Keys.onPressed: event => footer.arrows(event, filesButton, powerButton)
        }

        UI.IconButton {
            id: powerButton
            width: 44
            height: 44
            radius: 10
            focusPolicy: Qt.NoFocus
            iconName: "power"
            iconSize: 19
            stroke: 1.8
            tip: qsTr("Power options")
            checked: footer.powerMenuOpen
            onClicked: footer.powerRequested()
            Keys.onPressed: event => footer.arrows(event, settingsButton, null)
            Keys.onDownPressed: footer.powerRequested()
        }
    }
}
