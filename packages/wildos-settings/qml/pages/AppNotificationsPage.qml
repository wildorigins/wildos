import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Notifications from apps, after Windows 11's "Notifications from apps and
// other senders": every app that can notify, by name, with its switch.
// Selecting an app shows its own choices underneath it.
PageScroll {
    id: page

    property string expanded: ""

    Component.onCompleted: AppNotifications.reload()

    function summary(app) {
        if (!app.enabled) {
            return qsTr("Off");
        }
        const parts = [];
        if (app.banners) {
            parts.push(qsTr("Banners"));
        }
        if (app.hasSound && app.sound) {
            parts.push(qsTr("Sounds"));
        }
        if (!app.banners && app.history) {
            parts.push(qsTr("Notification list only"));
        }
        return parts.join(", ");
    }

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Choose which apps can send you notifications. Select an app for banners, sounds and the notification list.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            Repeater {
                model: AppNotifications.apps

                Column {
                    id: entry
                    required property var modelData
                    readonly property bool open: page.expanded === modelData.id
                    width: parent.width

                    Rectangle {
                        visible: entry.y > 0
                        width: parent.width
                        height: 1
                        color: UI.Style.divider
                    }

                    UI.SettingRow {
                        themeIcon: entry.modelData.icon.length > 0 ? entry.modelData.icon : "application-x-executable"
                        title: entry.modelData.name
                        description: page.summary(entry.modelData)
                        divider: false
                        interactive: true
                        Accessible.description: entry.open ? qsTr("Hide this app's notification options") : qsTr("Show this app's notification options")
                        onClicked: page.expanded = entry.open ? "" : entry.modelData.id

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: entry.modelData.enabled
                            Accessible.name: qsTr("Notifications from %1").arg(entry.modelData.name)
                            onToggled: AppNotifications.setEnabled(entry.modelData.id, checked)
                        }

                        UI.Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: entry.open ? "chevron-up" : "chevron-down"
                            size: 14
                            strokeWidth: 2
                            color: UI.Style.iconMuted
                        }
                    }

                    UI.SettingRow {
                        visible: entry.open
                        enabled: entry.modelData.enabled
                        leading: Item { width: 24; height: 1 }
                        title: qsTr("Show notification banners")
                        description: qsTr("Notifications pop up in the corner of the screen")
                        status: entry.modelData.banners ? qsTr("On") : qsTr("Off")

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: entry.modelData.banners
                            Accessible.name: qsTr("Show notification banners from %1").arg(entry.modelData.name)
                            onToggled: AppNotifications.setBanners(entry.modelData.id, checked)
                        }
                    }

                    UI.SettingRow {
                        visible: entry.open && entry.modelData.hasSound
                        enabled: entry.modelData.enabled
                        leading: Item { width: 24; height: 1 }
                        title: qsTr("Play a sound when a notification arrives")
                        status: entry.modelData.sound ? qsTr("On") : qsTr("Off")

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: entry.modelData.sound
                            Accessible.name: qsTr("Play a sound for notifications from %1").arg(entry.modelData.name)
                            onToggled: AppNotifications.setSound(entry.modelData.id, checked)
                        }
                    }

                    UI.SettingRow {
                        visible: entry.open
                        enabled: entry.modelData.enabled
                        leading: Item { width: 24; height: 1 }
                        title: qsTr("Show notifications in the notification list")
                        description: qsTr("Notifications stay in the list after their banner goes")
                        status: entry.modelData.history ? qsTr("On") : qsTr("Off")

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: entry.modelData.history
                            Accessible.name: qsTr("Show notifications from %1 in the notification list").arg(entry.modelData.name)
                            onToggled: AppNotifications.setHistory(entry.modelData.id, checked)
                        }
                    }
                }
            }

            UI.SettingRow {
                visible: AppNotifications.apps.length === 0
                iconName: "bell"
                title: qsTr("No apps have sent notifications yet")
                description: qsTr("Apps appear here once they can notify you, each with a switch.")
            }
        }
    }
}
