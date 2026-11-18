import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Everything an app from the Store can reach, app by app: every folder,
// device, socket, service and variable in its sandbox with a switch, ways
// to give it more, and a reset to what it asked for when installed. All of
// it is the user override file "flatpak override --user" writes, so the
// simple permission pages and the Store show the same answers.
PageScroll {
    id: page

    property string selected
    property var apps: []
    property var sections: []
    readonly property var app: {
        for (const a of apps) {
            if (a.id === selected) {
                return a;
            }
        }
        return null;
    }

    readonly property int depth: selected.length > 0 ? 2 : 1
    readonly property var subTitles: app ? [app.name] : []

    function back() {
        if (selected.length > 0) {
            selected = "";
            return true;
        }
        return false;
    }

    function refresh() {
        apps = AppAccess.everyApp();
        sections = selected.length > 0 ? AppAccess.details(selected) : [];
    }

    onSelectedChanged: refresh()

    Connections {
        target: AppAccess
        function onChanged() {
            page.refresh();
        }
    }

    Component.onCompleted: AppAccess.reload()

    UI.Label {
        Layout.fillWidth: true
        visible: page.selected.length === 0
        text: qsTr("Every permission an app from the Store has, including the technical ones the other pages leave out. Changes apply the next time the app opens.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.selected.length === 0

        Column {
            width: parent.width

            Repeater {
                model: page.selected.length === 0 ? page.apps : []

                UI.SettingRow {
                    required property var modelData
                    themeIcon: modelData.icon
                    title: modelData.name
                    description: modelData.changed ? qsTr("Changed from what the app asked for") : qsTr("As the app asked for")
                    chevron: true
                    onClicked: page.selected = modelData.id
                }
            }

            UI.SettingRow {
                visible: page.apps.length === 0
                iconName: "sandbox"
                title: qsTr("No apps from the Store yet")
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.app !== null
        spacing: Metrics.sectionGap

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    themeIcon: page.app ? page.app.icon : ""
                    iconTile: true
                    minHeight: 68
                    title: page.app ? page.app.name : ""
                    description: page.app && page.app.changed ? qsTr("You have changed what this app can reach") : qsTr("This app has what it asked for when installed")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.app !== null && page.app.changed
                        text: qsTr("Reset to the app's defaults")
                        onClicked: resetDialog.open()
                    }
                }
            }
        }

        Repeater {
            model: page.sections

            ColumnLayout {
                id: block
                required property var modelData
                // Lists the app may add to are shown even when empty, so
                // there is somewhere to add from.
                readonly property string addKind: ({ "filesystems": "folder", "session-bus": "bus", "env": "env" })[modelData.key] || ""
                Layout.fillWidth: true
                visible: modelData.items.length > 0 || addKind.length > 0
                spacing: 10

                SectionTitle {
                    text: block.modelData.title
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        Repeater {
                            model: block.modelData.items

                            UI.SettingRow {
                                id: permission
                                required property var modelData
                                title: modelData.label
                                description: [modelData.detail, modelData.changed ? qsTr("Changed by you") : ""].filter(t => t.length > 0).join(". ")
                                status: modelData.allowed ? qsTr("On") : qsTr("Off")

                                UI.Toggle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: permission.modelData.allowed
                                    Accessible.name: permission.modelData.label
                                    onToggled: AppAccess.set(page.selected, permission.modelData.token, checked, permission.modelData.grant)
                                }
                            }
                        }

                        UI.SettingRow {
                            visible: block.addKind.length > 0
                            iconName: "plus"
                            title: block.addKind === "folder" ? qsTr("Give it another folder")
                                : block.addKind === "bus" ? qsTr("Let it talk to another service")
                                : qsTr("Set a variable")
                            description: block.modelData.items.length === 0
                                ? (block.addKind === "folder" ? qsTr("It can only open files you choose for it")
                                    : block.addKind === "bus" ? qsTr("It can only use the desktop's own services")
                                    : qsTr("It has no variables of its own"))
                                : ""

                            UI.Button {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Add")
                                onClicked: addDialog.show(block.addKind)
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Dialog {
        id: addDialog

        property string kind
        property string error

        function show(k) {
            kind = k;
            error = "";
            valueField.text = "";
            extraField.text = "";
            accessBox.currentIndex = 0;
            open();
            valueField.forceActiveFocus();
        }

        title: kind === "folder" ? qsTr("Give it another folder") : kind === "bus" ? qsTr("Let it talk to another service") : qsTr("Set a variable")
        text: kind === "folder" ? qsTr("Type the folder's path. ~ is your home folder.")
            : kind === "bus" ? qsTr("Type the service's name. Only add one the app's makers ask you to.")
            : qsTr("Type the variable's name and the value the app sees.")

        UI.TextField {
            id: valueField
            Layout.fillWidth: true
            placeholderText: addDialog.kind === "folder" ? qsTr("For example ~/Projects") : addDialog.kind === "bus" ? qsTr("For example org.freedesktop.Notifications") : qsTr("Name, for example GTK_THEME")
            onAccepted: addButton.clicked()
        }

        UI.TextField {
            id: extraField
            Layout.fillWidth: true
            visible: addDialog.kind === "env"
            placeholderText: qsTr("Value")
            onAccepted: addButton.clicked()
        }

        UI.ComboBox {
            id: accessBox
            Layout.fillWidth: true
            visible: addDialog.kind !== "env"
            Accessible.name: qsTr("Access")
            model: addDialog.kind === "folder"
                ? [{ value: "rw", text: qsTr("Can open and change files") }, { value: "ro", text: qsTr("Can open files, not change them") }]
                : [{ value: "talk", text: qsTr("Can talk to it") }, { value: "own", text: qsTr("Can talk to it and take its name") }]
        }

        UI.Label {
            Layout.fillWidth: true
            visible: addDialog.error.length > 0
            text: addDialog.error
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: addButton
                text: qsTr("Add")
                kind: "primary"
                enabled: valueField.text.trim().length > 0
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    const extra = addDialog.kind === "env" ? extraField.text : accessBox.currentValue;
                    const why = AppAccess.add(page.selected, addDialog.kind, valueField.text, extra);
                    if (why.length > 0) {
                        addDialog.error = why;
                    } else {
                        addDialog.close();
                    }
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: addDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: resetDialog
        title: page.app ? qsTr("Reset %1?").arg(page.app.name) : ""
        text: qsTr("Every sandbox permission you changed for this app is undone, here and in the Store, and it gets exactly what it asked for when you installed it.")
        buttons: [
            UI.Button {
                text: qsTr("Reset")
                kind: "primary"
                onClicked: {
                    resetDialog.close();
                    AppAccess.reset(page.selected);
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: resetDialog.close()
            }
        ]
    }
}
