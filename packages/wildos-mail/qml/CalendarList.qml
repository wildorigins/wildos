import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// The calendars (CalendarMicrosoft.dc, under the small month): the ones on
// this PC first, then each account's, each with a box in its own colour
// that shows or hides it. Right-click one to rename it, change its colour,
// make new events go to it, or remove it.
QQC.ScrollView {
    id: list

    signal addCalendar()

    property var collapsed: ({})
    readonly property var groups: {
        const out = [];
        for (const c of CalendarEngine.calendars) {
            let g = out.find(x => x.name === c.group);
            if (!g) {
                g = { name: c.group, uid: c.groupUid, items: [] };
                out.push(g);
            }
            g.items.push(c);
        }
        return out;
    }
    readonly property var palette: [
        { c: "#6f9bff", n: qsTr("Blue") }, { c: "#4cc38a", n: qsTr("Green") }, { c: "#f0a04b", n: qsTr("Orange") },
        { c: "#ee6f9a", n: qsTr("Pink") }, { c: "#a98bf5", n: qsTr("Purple") }, { c: "#46c2c9", n: qsTr("Teal") },
        { c: "#e5c55a", n: qsTr("Yellow") }, { c: "#9aa3b5", n: qsTr("Grey") }
    ]

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: list.availableWidth
        spacing: 2

        Repeater {
            model: list.groups
            delegate: ColumnLayout {
                id: group
                required property var modelData
                readonly property bool open: !list.collapsed[modelData.name]
                Layout.fillWidth: true
                spacing: 2

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    RowLayout {
                        anchors.fill: parent
                        spacing: 6
                        MailIcon {
                            name: group.open ? "chevron-down" : "chevron-right"
                            size: 12
                            color: UI.Style.iconMuted
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: group.modelData.name
                            size: 13
                            font.weight: UI.Style.weightSemiBold
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const c = Object.assign({}, list.collapsed);
                            c[group.modelData.name] = group.open;
                            list.collapsed = c;
                        }
                    }
                }

                Repeater {
                    model: group.open ? group.modelData.items : []
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 30
                        radius: UI.Style.radiusXs
                        color: rowMouse.containsMouse ? UI.Style.surfaceHover : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 6
                            spacing: 10
                            Rectangle {
                                width: 16
                                height: 16
                                radius: 4
                                color: row.modelData.selected ? row.modelData.color : "transparent"
                                border.width: 1.5
                                border.color: row.modelData.color
                                MailIcon {
                                    anchors.centerIn: parent
                                    visible: row.modelData.selected
                                    name: "check"
                                    size: 12
                                    strokeWidth: 2.4
                                    // Dark on every calendar colour, which are light in both modes.
                                    color: Qt.rgba(0, 0, 0, 0.8)
                                }
                            }
                            UI.Label {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                size: 13
                                elide: Text.ElideRight
                            }
                            UI.Label {
                                visible: row.modelData.uid === CalendarEngine.defaultCalendar && CalendarEngine.calendars.length > 1
                                text: qsTr("Default")
                                size: 11
                                color: UI.Style.inkSubtle
                            }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: row.modelData.name
                            onClicked: function (event) {
                                if (event.button === Qt.RightButton) {
                                    menu.calendar = row.modelData;
                                    menu.popup();
                                } else {
                                    CalendarEngine.setSelected(row.modelData.uid, !row.modelData.selected);
                                }
                            }
                        }
                    }
                }
            }
        }

        UI.LinkButton {
            Layout.topMargin: 6
            Layout.leftMargin: 2
            text: "+  " + qsTr("Add calendar")
            onClicked: list.addCalendar()
        }
    }

    UI.Menu {
        id: menu
        property var calendar: ({})
        entries: [
            { text: qsTr("Rename"), enabled: !calendar.readOnly || calendar.kind === "webcal", run: () => renameDialog.ask(menu.calendar) },
            { text: qsTr("Colour"), submenu: list.palette.map(p => ({ text: p.n, radio: true, checked: p.c === menu.calendar.color, run: () => CalendarEngine.setColor(menu.calendar.uid, p.c) })) },
            { text: qsTr("New events go here"), enabled: !calendar.readOnly, checked: calendar.uid === CalendarEngine.defaultCalendar, run: () => CalendarEngine.defaultCalendar = menu.calendar.uid },
            { separator: true },
            { text: calendar.kind === "system" ? qsTr("This PC's calendar can't be removed") : calendar.groupUid ? qsTr("Hide from Calendar") : qsTr("Remove"),
              icon: "delete", danger: calendar.kind !== "system", enabled: calendar.kind !== "system",
              run: () => removeConfirm.ask(menu.calendar) }
        ]
    }

    UI.Dialog {
        id: renameDialog
        property var calendar: ({})
        function ask(c) {
            calendar = c;
            renameField.text = c.name;
            open();
            renameField.forceActiveFocus();
            renameField.selectAll();
        }
        title: qsTr("Rename calendar")
        UI.TextField {
            id: renameField
            Layout.fillWidth: true
            onAccepted: renameButton.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: renameDialog.close()
            },
            UI.Button {
                id: renameButton
                kind: "primary"
                text: qsTr("Rename")
                enabled: renameField.text.trim().length > 0
                onClicked: {
                    CalendarEngine.renameCalendar(renameDialog.calendar.uid, renameField.text.trim());
                    renameDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: removeConfirm
        property var calendar: ({})
        function ask(c) {
            calendar = c;
            open();
        }
        title: calendar.groupUid ? qsTr("Hide %1?").arg(calendar.name) : qsTr("Remove %1?").arg(calendar.name)
        text: calendar.groupUid ? qsTr("It stays in your account; Calendar stops showing it. Add the account again to bring it back.")
            : calendar.kind === "local" ? qsTr("The calendar and every event in it are deleted from this PC. This can't be undone.")
            : qsTr("Calendar stops showing it.")
        iconName: "warning"
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: removeConfirm.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: removeConfirm.calendar.groupUid ? qsTr("Hide") : qsTr("Remove")
                onClicked: {
                    CalendarEngine.removeCalendar(removeConfirm.calendar.uid);
                    removeConfirm.close();
                }
            }
        ]
    }
}
