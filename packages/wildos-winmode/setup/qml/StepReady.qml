import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI
import WildOS.WinMode.Setup

// 9.6, Ready (WinModeReady.dc): the Start entries setup made, and what
// Windows reports about itself.
Page {
    id: page

    signal done()

    readonly property var status: Setup.status
    readonly property var apps: status.apps || []
    readonly property var licence: status.licence || ({})
    readonly property var options: status.options || ({})
    readonly property var word: apps.find(a => a.id === "word")
    readonly property bool home: licence.edition === "home"

    title: qsTr("Windows mode is ready")
    text: home ? qsTr("Windows 11 Home is set up. Upgrade to Pro in the Microsoft Store window that opened, and your apps will open in their own windows.")
               : qsTr("Your Windows apps are in Start, and open in their own windows like everything else.")
    note: qsTr("Change how Windows mode works in Settings, under Windows apps.")

    leading: Rectangle {
        width: 64
        height: 64
        radius: 32
        color: UI.Style.accent

        UI.Icon {
            anchors.centerIn: parent
            name: "checkmark"
            size: 32
            strokeWidth: 2.6
            color: UI.Style.textOnAccent
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 12
        rowSpacing: 12

        Repeater {
            model: page.apps.slice(0, 5).concat([{ id: "", name: qsTr("Install a program"), icon: "" }])

            Rectangle {
                id: tile
                required property var modelData
                readonly property bool program: modelData.id.length === 0

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 64
                radius: UI.Style.radiusMd
                color: tileArea.containsMouse && !program ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                border.width: 1
                border.color: UI.Style.tone(0.08)
                Accessible.role: program ? Accessible.StaticText : Accessible.Button
                Accessible.name: modelData.name

                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    enabled: !tile.program
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: Setup.openApp(tile.modelData.id)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12

                    Rectangle {
                        implicitWidth: 40
                        implicitHeight: 40
                        radius: 10
                        color: UI.Style.surfaceTile

                        UI.ThemeIcon {
                            anchors.centerIn: parent
                            visible: !tile.program
                            width: 28
                            height: 28
                            name: tile.program ? "" : tile.modelData.icon
                            fallback: "wildos-windows-app"
                        }

                        UI.Icon {
                            anchors.centerIn: parent
                            visible: tile.program
                            name: "plus"
                            size: 20
                            color: UI.Style.accentText
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        UI.Label {
                            Layout.fillWidth: true
                            text: tile.modelData.name
                            size: 14
                            font.weight: UI.Style.weightSemiBold
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: tile.program ? qsTr("Double-click any .exe") : qsTr("Windows mode")
                            size: 12
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }
        }
    }

    Banner {
        visible: Setup.hyperThreadingNeedsRestart
        iconName: "restart"
        title: qsTr("Restart to use every processor thread")
        text: qsTr("Windows apps get the extra speed after the PC restarts.")
    }

    Banner {
        visible: page.options.office === true
        iconName: "account"
        title: qsTr("Sign in to Microsoft 365")
        text: qsTr("Open any of the apps and sign in once with your Microsoft account.")
    }

    UI.Card {
        Layout.fillWidth: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            CheckRow {
                readonly property bool none: page.licence.source === "none"
                state: page.licence.activated === true ? "ok" : "warning"
                title: page.licence.activated === true ? qsTr("Activated") : none ? qsTr("Not activated") : qsTr("Not activated yet")
                detail: page.licence.activated === true
                    ? (page.licence.source === "key" ? qsTr("%1, using your product key") : qsTr("%1, using this PC's licence"))
                      .arg(page.licence.edition === "home" ? qsTr("Windows 11 Home") : page.licence.edition === "pro" ? "Windows 11 Pro" : "Windows 11")
                    : none ? qsTr("Add a product key any time in Settings") : qsTr("Windows tries again by itself when it's online")
            }

            CheckRow {
                readonly property string how: page.options.whenIdle || "automatic"
                state: "ok"
                title: how === "disk" ? qsTr("Ready in about 5 seconds") : how === "shutdown" ? qsTr("Ready in about 30 seconds") : qsTr("Ready in about a second")
                detail: how === "disk" ? qsTr("Saved to disk while no Windows app is open")
                      : how === "shutdown" ? qsTr("Shut down while no Windows app is open")
                      : how === "memory" ? qsTr("Paused in memory while no Windows app is open")
                      : qsTr("Paused in memory while no Windows app is open, saved to disk on battery")
            }
        }
    }

    buttons: [
        UI.Button {
            visible: page.word !== undefined && !page.home
            text: qsTr("Open Word")
            onClicked: Setup.openApp("word")
        },
        UI.Button {
            kind: "primary"
            text: qsTr("Done")
            onClicked: page.done()
        }
    ]
}
