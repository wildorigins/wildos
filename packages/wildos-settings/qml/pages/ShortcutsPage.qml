import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import WildOS.Settings
import WildOS.UI as UI

// Keyboard shortcuts that work everywhere: Wild OS's own under the names a
// person would use, and the person's own for starting an app or running a
// command. Every change goes straight to the desktop's shortcut service
// (Shortcuts), so it counts as soon as it is saved.
PageScroll {
    id: page

    // The headings, in the order their first shortcut comes.
    readonly property var groups: {
        const names = [];
        for (const entry of Shortcuts.system) {
            if (!names.includes(entry.group)) {
                names.push(entry.group);
            }
        }
        return names;
    }

    Component.onCompleted: Shortcuts.reload()

    // What the keys being recorded are for: { id, name, replace } for a
    // shortcut that exists, or { app } or { name, command } for a new one.
    property var target: null

    function change(entry, replace) {
        target = { id: entry.id, name: entry.name, replace: replace };
        recordDialog.open();
    }

    function openMenu(button, entry, own) {
        const entries = [];
        const several = entry.keys.length > 1;
        entries.push({ text: entry.keys.length > 0 ? qsTr("Change the shortcut") : qsTr("Set a shortcut"), icon: "keyboard", run: () => page.change(entry, true) });
        if (entry.keys.length > 0) {
            entries.push({ text: qsTr("Add another shortcut"), icon: "plus", run: () => page.change(entry, false) });
        }
        // One of your own with a single set of keys is deleted, not left
        // with none.
        for (const one of own && !several ? [] : entry.keys) {
            entries.push({
                text: several ? qsTr("Remove %1").arg(Shortcuts.display(one.keys)) : qsTr("Remove the shortcut"),
                icon: "delete",
                run: () => Shortcuts.removeKeys(entry.id, one.keys)
            });
        }
        if (own) {
            entries.push({ separator: true });
            entries.push({ text: qsTr("Delete"), icon: "delete", danger: true, run: () => Shortcuts.removeOwn(entry.id) });
        } else if (entry.changed) {
            entries.push({ separator: true });
            entries.push({ text: qsTr("Put back the usual shortcut"), icon: "undo", run: () => Shortcuts.reset(entry.id) });
        }
        rowMenu.entries = entries;
        rowMenu.popup(button, 0, button.height + 4);
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("These work in every app. Choose a shortcut's menu to change it, give it a second set of keys or take its keys away.")
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }

        UI.Button {
            text: qsTr("Put all back")
            enabled: Shortcuts.anyChanged
            onClicked: resetDialog.open()
        }
    }

    Repeater {
        model: page.groups

        ColumnLayout {
            id: section
            required property string modelData
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: section.modelData
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: Shortcuts.system.filter(entry => entry.group === section.modelData)

                        ShortcutRow {
                            required property var modelData
                            entry: modelData
                            onMenuRequested: button => page.openMenu(button, modelData, false)
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            SectionTitle {
                Layout.fillWidth: true
                text: qsTr("Your shortcuts")
            }

            UI.Button {
                text: qsTr("Add a shortcut")
                iconName: "plus"
                onClicked: addDialog.open()
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: Shortcuts.own

                    ShortcutRow {
                        required property var modelData
                        entry: modelData
                        themeIcon: modelData.icon
                        description: modelData.command !== modelData.name ? modelData.command : ""
                        onMenuRequested: button => page.openMenu(button, modelData, true)
                    }
                }

                UI.SettingRow {
                    visible: Shortcuts.own.length === 0
                    iconName: "keyboard"
                    title: qsTr("None yet")
                    description: qsTr("Add one to open an app or run a command with keys of your choice")
                }
            }
        }
    }

    component ShortcutRow: UI.SettingRow {
        id: row
        property var entry
        signal menuRequested(Item button)

        title: entry.name

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Repeater {
                model: row.entry.keys

                Row {
                    required property var modelData
                    required property int index
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: parent.index > 0
                        text: qsTr("or")
                        size: 12
                        color: UI.Style.inkMuted
                    }

                    UI.ShortcutKeys {
                        anchors.verticalCenter: parent.verticalCenter
                        keys: parent.modelData.parts
                    }
                }
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.entry.keys.length === 0
                text: qsTr("No shortcut")
                color: UI.Style.inkMuted
            }
        }

        UI.IconButton {
            id: more
            anchors.verticalCenter: parent.verticalCenter
            iconName: "more"
            tip: qsTr("Options for %1").arg(row.entry.name)
            onClicked: row.menuRequested(more)
        }
    }

    UI.Menu {
        id: rowMenu
    }

    // Waits for the keys, then says what else uses them before saving.
    UI.Dialog {
        id: recordDialog

        property string keys
        readonly property string clash: keys.length > 0 ? Shortcuts.usedBy(keys, page.target && page.target.id ? page.target.id : "") : ""

        title: page.target && page.target.name ? qsTr("Shortcut for %1").arg(page.target.name) : qsTr("Choose the keys")
        text: keys.length > 0 ? qsTr("Save it, or press Try again for other keys.")
            : qsTr("Hold Ctrl, Alt, Shift or the Windows key and press another key. A key such as F9 works by itself.")

        function listen() {
            keys = "";
            Shortcuts.record(page.Window.window);
        }

        onAboutToShow: listen()
        onClosed: Shortcuts.stopRecording()

        Connections {
            target: Shortcuts
            enabled: recordDialog.opened
            function onRecorded(keys) {
                recordDialog.keys = keys;
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            radius: UI.Style.radiusSm
            color: UI.Style.surfaceSunken
            border.width: 1
            border.color: Shortcuts.recording ? UI.Style.accent : UI.Style.borderField

            UI.ShortcutKeys {
                anchors.centerIn: parent
                visible: keys.length > 0
                keys: recordDialog.keys.length > 0 ? Shortcuts.parts(recordDialog.keys) : Shortcuts.shown
            }

            UI.Label {
                anchors.centerIn: parent
                visible: recordDialog.keys.length === 0 && Shortcuts.shown.length === 0
                text: qsTr("Press the keys now")
                color: UI.Style.inkMuted
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: recordDialog.clash.length > 0
            text: qsTr("%1 uses these keys now. Saving takes them from it.").arg(recordDialog.clash)
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }

        buttons: [
            UI.Button {
                text: qsTr("Save")
                kind: "primary"
                enabled: recordDialog.keys.length > 0
                onClicked: {
                    const target = page.target;
                    if (target.id) {
                        Shortcuts.setKeys(target.id, recordDialog.keys, target.replace);
                    } else if (target.app) {
                        Shortcuts.addApp(target.app, recordDialog.keys);
                    } else {
                        Shortcuts.addCommand(target.name, target.command, recordDialog.keys);
                    }
                    recordDialog.close();
                }
            },
            UI.Button {
                text: qsTr("Try again")
                enabled: recordDialog.keys.length > 0
                onClicked: recordDialog.listen()
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: recordDialog.close()
            }
        ]
    }

    // What the new shortcut does; its keys are asked for next.
    UI.Dialog {
        id: addDialog

        property string kind: "app"
        property var all: []
        property int chosen: -1
        readonly property var shown: {
            const query = filter.text.trim().toLowerCase();
            return all.filter(app => query.length === 0 || app.name.toLowerCase().includes(query));
        }
        readonly property bool ready: kind === "app" ? chosen >= 0 && chosen < shown.length : commandField.text.trim().length > 0

        title: qsTr("Add a shortcut")
        text: qsTr("Choose what the keys will do.")
        onAboutToShow: {
            all = Shortcuts.apps();
            kind = "app";
            filter.text = "";
            nameField.text = "";
            commandField.text = "";
            chosen = -1;
            filter.forceActiveFocus();
        }

        Segmented {
            options: [
                { value: "app", text: qsTr("Open an app") },
                { value: "command", text: qsTr("Run a command") }
            ]
            value: addDialog.kind
            onPicked: value => {
                addDialog.kind = value;
                if (value === "app") {
                    filter.forceActiveFocus();
                } else {
                    commandField.forceActiveFocus();
                }
            }
        }

        UI.SearchField {
            id: filter
            Layout.fillWidth: true
            visible: addDialog.kind === "app"
            placeholderText: qsTr("Search apps")
            onTextChanged: addDialog.chosen = -1
            onDownPressed: {
                addDialog.chosen = Math.max(0, addDialog.chosen);
                list.forceActiveFocus();
            }
            onAccepted: {
                if (addDialog.shown.length === 1) {
                    addDialog.chosen = 0;
                }
                nextButton.clicked();
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            visible: addDialog.kind === "app"
            radius: UI.Style.radiusSm
            color: UI.Style.surfaceSunken
            border.width: 1
            border.color: UI.Style.borderField

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: addDialog.shown
                currentIndex: addDialog.chosen
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                onCurrentIndexChanged: addDialog.chosen = currentIndex
                Keys.onReturnPressed: nextButton.clicked()

                delegate: UI.ListRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    minHeight: 36
                    divider: false
                    radius: UI.Style.radiusXs
                    interactive: true
                    themeIcon: modelData.icon
                    title: modelData.name
                    checked: index === addDialog.chosen
                    background: Rectangle {
                        radius: UI.Style.radiusXs
                        color: parent.checked ? UI.Style.accentSelected : parent.hovered ? UI.Style.surfaceHover : "transparent"
                    }
                    onClicked: addDialog.chosen = index
                    onDoubleClicked: {
                        addDialog.chosen = index;
                        nextButton.clicked();
                    }
                }

                UI.Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No apps match")
                    color: UI.Style.inkMuted
                }
            }
        }

        UI.TextField {
            id: commandField
            Layout.fillWidth: true
            visible: addDialog.kind === "command"
            placeholderText: qsTr("The command, as you would type it in Terminal")
            accessibleName: qsTr("Command")
            onAccepted: nextButton.clicked()
        }

        UI.TextField {
            id: nameField
            Layout.fillWidth: true
            visible: addDialog.kind === "command"
            placeholderText: qsTr("A name for it (optional)")
            accessibleName: qsTr("Name")
            onAccepted: nextButton.clicked()
        }

        buttons: [
            UI.Button {
                id: nextButton
                text: qsTr("Choose the keys")
                kind: "primary"
                enabled: addDialog.ready
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    if (addDialog.kind === "app") {
                        const app = addDialog.shown[addDialog.chosen];
                        page.target = { app: app.id, name: app.name };
                    } else {
                        const command = commandField.text.trim();
                        page.target = { command: command, name: nameField.text.trim().length > 0 ? nameField.text.trim() : command };
                    }
                    addDialog.close();
                    recordDialog.open();
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
        title: qsTr("Put every shortcut back?")
        text: qsTr("Wild OS's shortcuts go back to the keys they came with. Your own shortcuts stay.")

        buttons: [
            UI.Button {
                text: qsTr("Put all back")
                kind: "primary"
                onClicked: {
                    Shortcuts.resetAll();
                    resetDialog.close();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: resetDialog.close()
            }
        ]
    }
}
