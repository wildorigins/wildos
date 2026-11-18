import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Activity history, after Windows 11: whether this computer remembers the
// files you open, so Start and Files can offer them again, for how long,
// apps it never remembers, and Clear history. Nothing here leaves this
// computer.
PageScroll {
    id: page

    Connections {
        target: ActivityHistory
        function onCleared() {
            notice.show(qsTr("History cleared"), qsTr("The files you opened are no longer listed as recent."), false);
        }
        function onFailed(reason) {
            notice.show(qsTr("History was not cleared"), reason, true);
        }
    }

    Component.onCompleted: ActivityHistory.reload()

    Notice {
        id: notice
    }

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("This computer can remember the files you open so you can get back to them from Start and Files. The history stays on this computer.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "clock"
                title: qsTr("Remember the files I open")
                description: qsTr("Shows recent files in Start and Files")
                status: ActivityHistory.remember ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: ActivityHistory.remember
                    Accessible.name: qsTr("Remember the files I open")
                    onToggled: ActivityHistory.remember = checked
                }
            }

            UI.SettingRow {
                iconName: "restore"
                title: qsTr("Keep history for")
                description: qsTr("Older activity is deleted automatically")
                enabled: ActivityHistory.remember

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Keep history for")
                    model: [
                        { value: 1, text: qsTr("1 month") },
                        { value: 3, text: qsTr("3 months") },
                        { value: 6, text: qsTr("6 months") },
                        { value: 12, text: qsTr("1 year") },
                        { value: 0, text: qsTr("Forever") }
                    ]
                    readonly property int choice: ActivityHistory.keepMonths
                    // A length set elsewhere shows as the nearest longer one
                    // until it is changed here.
                    currentIndex: {
                        if (choice === 0) {
                            return 4;
                        }
                        const i = model.findIndex(o => o.value !== 0 && o.value >= choice);
                        return i >= 0 ? i : 4;
                    }
                    onActivated: index => ActivityHistory.keepMonths = model[index].value
                }
            }

            UI.SettingRow {
                iconName: "delete"
                title: qsTr("Clear activity history")
                description: qsTr("Forgets every file this computer remembers you opening")

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Clear history")
                    busy: ActivityHistory.clearing
                    enabled: !ActivityHistory.clearing
                    onClicked: clearDialog.open()
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Apps that are never remembered")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: ActivityHistory.blocked

                    UI.SettingRow {
                        id: blockedRow
                        required property var modelData
                        themeIcon: modelData.icon
                        title: modelData.name
                        enabled: ActivityHistory.remember

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Remove")
                            onClicked: ActivityHistory.unblock(blockedRow.modelData.id)
                        }
                    }
                }

                UI.SettingRow {
                    iconName: "plus"
                    title: qsTr("Add an app")
                    description: qsTr("Files you open in it are never added to your history")
                    enabled: ActivityHistory.remember

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Choose an app")
                        onClicked: appDialog.show()
                    }
                }
            }
        }
    }

    UI.Dialog {
        id: clearDialog
        title: qsTr("Clear activity history?")
        text: qsTr("Recent files disappear from Start and Files. The files themselves are not touched.")
        buttons: [
            UI.Button {
                text: qsTr("Clear")
                kind: "primary"
                onClicked: {
                    clearDialog.close();
                    notice.hide();
                    ActivityHistory.clear();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: clearDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: appDialog

        function show() {
            appBox.model = ActivityHistory.apps();
            appBox.currentIndex = 0;
            open();
        }

        title: qsTr("Never remember files from")
        text: qsTr("Files you open in this app are left out of your history from now on.")

        UI.ComboBox {
            id: appBox
            Layout.fillWidth: true
            Accessible.name: qsTr("App")
        }

        buttons: [
            UI.Button {
                text: qsTr("Add")
                kind: "primary"
                enabled: appBox.count > 0
                onClicked: {
                    appDialog.close();
                    ActivityHistory.block(appBox.currentValue);
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: appDialog.close()
            }
        ]
    }
}
