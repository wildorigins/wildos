import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI
import WildOS.SystemDialogs.Backend

// What the Ctrl+Alt+Del screen shows (CtrlAltDel.dc): the security options
// in a 300px column with Cancel below, and the network, accessibility and
// power buttons at the bottom right. Options the computer cannot offer
// (no display manager to switch user, no Task Manager) are left out
// rather than shown dead. Up and Down move between them, Escape cancels.
FocusScope {
    id: content

    property bool previewHover: false

    function takeFocus() {
        lockLink.forceActiveFocus();
    }

    Keys.onEscapePressed: Security.close()

    Column {
        id: options
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(24, Math.round((content.height - height) / 2 - content.height * 0.03))
        spacing: 6

        SecurityLink {
            id: lockLink
            text: qsTr("Lock")
            onClicked: Security.lock()
            KeyNavigation.down: switchLink.visible ? switchLink : signOutLink
        }

        SecurityLink {
            id: switchLink
            visible: Security.canSwitchUser
            text: qsTr("Switch user")
            onClicked: Security.switchUser()
            KeyNavigation.up: lockLink
            KeyNavigation.down: signOutLink
        }

        SecurityLink {
            id: signOutLink
            visible: Security.canSignOut
            text: qsTr("Sign out")
            onClicked: Security.signOut()
            KeyNavigation.up: switchLink.visible ? switchLink : lockLink
            KeyNavigation.down: passwordLink.visible ? passwordLink : taskLink
        }

        SecurityLink {
            id: passwordLink
            visible: Security.canChangePassword
            text: qsTr("Change password")
            onClicked: Security.changePassword()
            KeyNavigation.up: signOutLink
            KeyNavigation.down: taskLink.visible ? taskLink : cancelButton
        }

        SecurityLink {
            id: taskLink
            visible: Security.canTaskManager
            text: qsTr("Task Manager")
            previewHover: content.previewHover
            onClicked: Security.taskManager()
            KeyNavigation.up: passwordLink.visible ? passwordLink : signOutLink
            KeyNavigation.down: cancelButton
        }

        Item {
            width: 1
            height: 14
        }

        UI.Button {
            id: cancelButton
            anchors.horizontalCenter: parent.horizontalCenter
            onWallpaper: true
            size: "large"
            leftPadding: 28
            text: qsTr("Cancel")
            onClicked: Security.close()
            KeyNavigation.up: taskLink.visible ? taskLink : passwordLink.visible ? passwordLink : signOutLink
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 26
        spacing: 10

        CornerButton {
            visible: Security.networkAvailable
            iconName: Security.networkConnected ? "wifi" : "wifi-off"
            label: qsTr("Network")
            entries: [
                { text: Security.networkText, enabled: false },
                { separator: true },
                { text: qsTr("Network settings"), icon: "cog", run: () => Security.networkSettings() }
            ]
        }

        CornerButton {
            iconName: "accessibility"
            label: qsTr("Accessibility")
            entries: {
                const list = [
                    { text: qsTr("Magnifier"), checked: Security.magnifier, run: () => Security.setMagnifier(!Security.magnifier) },
                    { text: qsTr("Sticky keys"), checked: Security.stickyKeys, run: () => Security.setStickyKeys(!Security.stickyKeys) }
                ];
                if (Security.screenReaderAvailable) {
                    list.push({ text: qsTr("Screen reader"), checked: Security.screenReader, run: () => Security.setScreenReader(!Security.screenReader) });
                }
                return list;
            }
        }

        CornerButton {
            iconName: "power"
            label: qsTr("Power")
            entries: {
                const list = [];
                if (Security.canSleep) {
                    list.push({ text: qsTr("Sleep"), icon: "moon", run: () => Security.sleep() });
                }
                list.push({ text: qsTr("Shut down"), icon: "power", run: () => Security.shutDown() });
                list.push({ text: qsTr("Restart"), icon: "restart", run: () => Security.restart() });
                return list;
            }
        }
    }
}
