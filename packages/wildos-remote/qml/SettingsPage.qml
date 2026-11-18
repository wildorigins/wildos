import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Remote
import WildOS.UI as UI

// Remote's settings. Its terminal follows the settings saved in Terminal,
// so this page says so and opens them; the rest are Remote's own: what a
// newly added desktop starts with, and whether ssh keeps an idle
// connection open.
Item {
    id: page

    // Terminal may have been installed or removed since the page was last
    // looked at.
    onVisibleChanged: if (visible) RemoteSettings.checkTerminal()

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: UI.ScrollBar {}

        ColumnLayout {
            id: column
            x: 36
            y: 22
            width: Math.min(flick.width - 72, 900)
            spacing: 14

            UI.Label {
                Layout.preferredHeight: 44
                text: qsTr("Settings")
                size: 26
                font.weight: UI.Style.weightBold
                Accessible.role: Accessible.Heading
            }

            Heading {
                text: qsTr("Terminal")
            }
            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "terminal"
                        title: qsTr("Appearance, and copy and paste")
                        description: RemoteSettings.terminalInstalled
                            ? qsTr("Remote's terminal uses the settings you change in Terminal: colours, font, cursor, history, and how copy and paste work. A change shows straight away, in open tabs too. A setting you haven't changed in Terminal stays as Remote has it.")
                            : qsTr("Remote's terminal follows Terminal's settings, and Terminal isn't installed on this PC.")

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Open Terminal settings")
                            enabled: RemoteSettings.terminalInstalled
                            onClicked: RemoteSettings.openTerminalSettings()
                        }
                    }
                }
            }

            Heading {
                text: qsTr("New desktops")
            }
            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        iconName: "monitor"
                        title: qsTr("Open in")
                        description: qsTr("For desktops you add from now on. Each one can be changed in its own settings.")

                        Segments {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Open in")
                            options: [{ value: "window", text: qsTr("Window") }, { value: "full", text: qsTr("Full screen") }, { value: "all", text: qsTr("All monitors") }]
                            value: RemoteSettings.desktopDisplay
                            onChosen: v => RemoteSettings.desktopDisplay = v
                        }
                    }

                    SwitchRow {
                        iconName: "copy"
                        title: qsTr("Share the clipboard")
                        description: qsTr("Copy and paste both ways. For desktops you add from now on.")
                        value: RemoteSettings.desktopClipboard
                        onSwitched: v => RemoteSettings.desktopClipboard = v
                    }
                }
            }

            Heading {
                text: qsTr("SSH")
            }
            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    SwitchRow {
                        iconName: "plug"
                        title: qsTr("Keep idle connections open")
                        description: qsTr("Remote checks in with the server every 30 seconds, so a router or firewall doesn't drop a tab you've left alone. For tabs opened from now on.")
                        value: RemoteSettings.keepAlive
                        onSwitched: v => RemoteSettings.keepAlive = v
                    }
                }
            }
        }
    }

    component Heading: UI.Label {
        Layout.topMargin: 8
        size: 15
        font.weight: UI.Style.weightSemiBold
        Accessible.role: Accessible.Heading
    }

    // A setting that is on or off, with the state spelt out beside the
    // switch, as Settings and Terminal draw it.
    component SwitchRow: UI.SettingRow {
        id: row

        property bool value
        signal switched(bool value)

        status: value ? qsTr("On") : qsTr("Off")

        UI.Toggle {
            anchors.verticalCenter: parent.verticalCenter
            checked: row.value
            Accessible.name: row.title
            onToggled: row.switched(checked)
        }
    }
}
