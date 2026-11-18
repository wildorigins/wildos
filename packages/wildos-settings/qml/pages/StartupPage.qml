import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Startup, after Windows 11's Settings > Apps > Startup: apps that start
// when you sign in, each with its switch. Parts of the desktop itself are
// not listed, so nothing here can break the desktop.
PageScroll {
    id: page

    Component.onCompleted: StartupApps.reload()

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Apps can start when you sign in. Fewer apps at the start means the desktop is ready sooner.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            Repeater {
                model: StartupApps.apps

                UI.SettingRow {
                    required property var modelData
                    themeIcon: modelData.icon.length > 0 ? modelData.icon : "application-x-executable"
                    title: modelData.name
                    description: modelData.description
                    status: modelData.enabled ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: modelData.enabled
                        Accessible.name: modelData.name
                        onToggled: StartupApps.setEnabled(modelData.file, checked)
                    }
                }
            }

            UI.SettingRow {
                visible: StartupApps.apps.length === 0
                iconName: "startup"
                title: qsTr("No apps start when you sign in")
                description: qsTr("Apps that offer to start with Wild OS appear here, each with a switch.")
            }
        }
    }
}
