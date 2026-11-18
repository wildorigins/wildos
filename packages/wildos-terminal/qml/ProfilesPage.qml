pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.Terminal
import WildOS.UI as UI

// Profiles: each is a shell or program a tab can open with, its own name,
// icon and starting folder. They are listed in the order the tab menu
// shows them.
PageScroll {
    id: page

    signal openProfile(string id)

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            Repeater {
                model: Config.profiles

                UI.SettingRow {
                    required property var modelData
                    iconName: modelData.icon
                    title: modelData.name
                    description: qsTr("%1, starts in %2", "a command, then a folder").arg(modelData.command.length > 0 ? modelData.command : Config.defaultShell)
                                 .arg(modelData.directory.length > 0 ? modelData.directory : qsTr("your home folder"))
                    status: modelData.id === Config.defaultProfile ? qsTr("Default") : ""
                    chevron: true
                    onClicked: page.openProfile(modelData.id)
                }
            }
        }
    }

    RowLayout {
        spacing: 8

        UI.Button {
            text: qsTr("New profile")
            iconName: "plus"
            onClicked: page.openProfile(Config.addProfile())
        }
    }
}
