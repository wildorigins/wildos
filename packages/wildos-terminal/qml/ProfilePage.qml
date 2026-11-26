pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Terminal
import WildOS.UI as UI

// One profile: its name, what it runs, where it starts and its icon.
PageScroll {
    id: page

    required property string profileId

    signal openProfile(string id)

    readonly property var profile: Config.profile(profileId)
    readonly property bool isDefault: Config.defaultProfile === profileId
    readonly property var icons: ["terminal", "code", "server", "shield", "box", "globe", "bolt", "star", "folder"]

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                title: qsTr("Name")
                description: qsTr("Shown on the tab and in the new tab menu.")

                Field {
                    anchors.verticalCenter: parent.verticalCenter
                    value: page.profile.name || ""
                    placeholderText: qsTr("Profile name")
                    onCommitted: text => {
                        if (text.trim().length > 0) {
                            Config.setProfileValue(page.profileId, "name", text.trim());
                        }
                    }
                }
            }

            UI.SettingRow {
                title: qsTr("Command")
                description: qsTr("The program to run, with its options. Leave it empty for your usual shell.")

                Field {
                    anchors.verticalCenter: parent.verticalCenter
                    monospace: true
                    value: page.profile.command || ""
                    placeholderText: Config.defaultShell
                    onCommitted: text => Config.setProfileValue(page.profileId, "command", text.trim())
                }
            }

            UI.SettingRow {
                title: qsTr("Starting folder")
                description: qsTr("Where new tabs with this profile open. Leave it empty for your home folder.")

                Field {
                    anchors.verticalCenter: parent.verticalCenter
                    monospace: true
                    value: page.profile.directory || ""
                    placeholderText: "~"
                    onCommitted: text => Config.setProfileValue(page.profileId, "directory", text.trim())
                }
            }

            UI.SettingRow {
                title: qsTr("Icon")
                description: qsTr("Shown on the tab.")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Repeater {
                        model: page.icons

                        // One icon of a set, chosen like a choice card: accent
                        // edge and fill when it is the profile's.
                        T.AbstractButton {
                            id: choice
                            required property string modelData
                            readonly property bool selected: page.profile.icon === modelData
                            width: 34
                            height: 34
                            padding: 0
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData
                            Accessible.checked: selected
                            onClicked: Config.setProfileValue(page.profileId, "icon", modelData)

                            background: Rectangle {
                                radius: UI.Style.radiusSm
                                // Fades in on hover and clears at once, so moving along a list
                                // never lights two items. It comes before the colour so it sees
                                // the change in hover first.
                                Behavior on color {
                                    enabled: choice.hovered
                                    ColorAnimation { duration: UI.Style.hoverDuration }
                                }
                                color: choice.selected ? UI.Style.accentSelected : choice.down ? UI.Style.surfacePressed : choice.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                                border.width: choice.selected ? 1.5 : 0
                                border.color: UI.Style.accent

                                UI.FocusRing {
                                    visible: choice.visualFocus
                                    targetRadius: UI.Style.radiusSm
                                }
                            }

                            contentItem: Item {
                                UI.Icon {
                                    anchors.centerIn: parent
                                    name: choice.modelData
                                    size: 18
                                    strokeWidth: 1.8
                                    color: choice.selected ? UI.Style.accent : UI.Style.iconInk
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "star"
                title: page.isDefault ? qsTr("This is the default profile") : qsTr("Make this the default profile")
                description: qsTr("The default profile opens in new windows and with Ctrl+Shift+T.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !page.isDefault
                    text: qsTr("Make default")
                    onClicked: Config.defaultProfile = page.profileId
                }
            }

            UI.SettingRow {
                iconName: "plus"
                title: qsTr("Open a tab with this profile")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Open")
                    onClicked: host.newTab(page.profileId)
                }
            }

            UI.SettingRow {
                iconName: "duplicate"
                title: qsTr("Duplicate this profile")
                description: qsTr("A copy to change without losing this one.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Duplicate")
                    onClicked: {
                        const copy = Config.duplicateProfile(page.profileId);
                        if (copy.length > 0) {
                            page.openProfile(copy);
                        }
                    }
                }
            }

            UI.SettingRow {
                iconName: "trash"
                title: qsTr("Delete this profile")
                description: Config.profiles.length > 1 ? qsTr("Tabs already open with it keep running.")
                                                        : qsTr("Terminal needs at least one profile, so the last one stays.")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Delete")
                    danger: true
                    enabled: Config.profiles.length > 1
                    onClicked: Config.removeProfile(page.profileId)
                }
            }
        }
    }
}
