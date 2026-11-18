import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// About: the device and Wild OS specifications, as Windows 11 lays them out,
// with renaming the PC done for real through systemd's hostnamed.
PageScroll {
    id: page

    readonly property var deviceSpecs: [
        [qsTr("Device name"), System.hostName],
        [qsTr("Processor"), System.processor],
        [qsTr("Installed memory"), System.memory],
        [qsTr("System type"), System.architecture],
        [qsTr("Secure Boot"), System.secureBoot === "on" ? qsTr("On") : System.secureBoot === "off" ? qsTr("Off") : qsTr("Not supported by this computer")],
        [qsTr("Disk encryption"), Protection.status.encryption === "on" ? qsTr("On")
            : Protection.status.encryption === "off" ? qsTr("Off")
            : Protection.status.encryption === "na" ? qsTr("Not used in the live session") : qsTr("Unknown")]
    ].filter(row => row[1] && row[1].length > 0)

    readonly property var osSpecs: [
        [qsTr("Edition"), System.osName],
        [qsTr("Version"), System.osVersion],
        [qsTr("Kernel"), System.kernel]
    ].filter(row => row[1] && row[1].length > 0)

    function copyText(rows) {
        return rows.map(row => row[0] + "\t" + row[1]).join("\n");
    }

    function copy(rows) {
        clipboard.text = copyText(rows);
        clipboard.selectAll();
        clipboard.copy();
    }

    TextEdit {
        id: clipboard
        visible: false
    }

    UI.Card {
        Layout.fillWidth: true
        implicitHeight: 96

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 18

            Rectangle {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 60
                radius: UI.Style.radiusSm
                color: UI.Style.surfaceSunken
                border.width: 1
                border.color: UI.Style.borderGlass
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: System.thumbnail(System.wallpaper)
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 192
                    asynchronous: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: System.hostName
                    size: 18
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: System.deviceModel.length > 0 ? System.deviceModel : System.osName
                    color: UI.Style.inkMuted
                }
            }

            UI.Button {
                text: qsTr("Rename this PC")
                onClicked: renameDialog.open()
            }
        }
    }

    Repeater {
        model: [
            { title: qsTr("Device specifications"), rows: page.deviceSpecs },
            { title: qsTr("Wild OS specifications"), rows: page.osSpecs }
        ]

        ColumnLayout {
            id: section
            required property var modelData
            Layout.fillWidth: true
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                SectionTitle {
                    Layout.fillWidth: true
                    text: section.modelData.title
                }

                UI.Button {
                    id: copyButton
                    property bool done: false
                    text: done ? qsTr("Copied") : qsTr("Copy")
                    iconName: done ? "check" : "copy"
                    onClicked: {
                        page.copy(section.modelData.rows);
                        done = true;
                        copyReset.restart();
                    }
                    Timer {
                        id: copyReset
                        interval: 2000
                        onTriggered: copyButton.done = false
                    }
                }
            }

            UI.Card {
                Layout.fillWidth: true
                implicitHeight: specs.implicitHeight + 28

                GridLayout {
                    id: specs
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    anchors.topMargin: 14
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 10

                    Repeater {
                        model: section.modelData.rows

                        UI.Label {
                            required property var modelData
                            required property int index
                            Layout.preferredWidth: 180
                            Layout.row: index
                            Layout.column: 0
                            text: modelData[0]
                            color: UI.Style.inkMuted
                        }
                    }

                    Repeater {
                        model: section.modelData.rows

                        UI.Label {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.row: index
                            Layout.column: 1
                            text: modelData[1]
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
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
                iconName: "update"
                title: qsTr("Updates")
                description: qsTr("Check for updates and see what was installed")
                chevron: true
                onClicked: Navigator.open("update")
            }

            UI.SettingRow {
                iconName: "shield"
                title: qsTr("Privacy and security")
                description: qsTr("Encryption, Secure Boot and app protection")
                chevron: true
                onClicked: Navigator.open("privacy")
            }
        }
    }

    SectionTitle {
        Layout.fillWidth: true
        text: qsTr("Credits")
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            // GPL-3.0 asks for the licence to travel with the icons, so the
            // row opens the copy the theme package installs.
            UI.SettingRow {
                iconName: "folder"
                title: qsTr("Fluent icon theme")
                description: qsTr("Folders, This computer and the Recycle Bin. By vinceliuice, under the GPL-3.0 licence.")
                external: true
                onClicked: Qt.openUrlExternally("file:///usr/share/icons/Fluent-yellow/COPYING")
            }

            UI.SettingRow {
                leading: PathIcon {
                    name: "fingerprint"
                    size: 20
                }
                title: "Fluent UI System Icons"
                description: qsTr("The fingerprint in fingerprint setup. By Microsoft, under the MIT licence.")
                external: true
                onClicked: Qt.openUrlExternally("file:///usr/share/wildos-settings/licences/fluent-ui-system-icons.txt")
            }
        }
    }

    UI.Dialog {
        id: renameDialog
        title: qsTr("Rename your PC")
        text: qsTr("Use letters, numbers and hyphens, up to 63 characters. Other devices on your network see this name.")

        property string error

        onOpened: {
            nameField.text = System.hostName;
            error = "";
            nameField.forceActiveFocus();
        }

        function valid(name) {
            return /^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$/.test(name);
        }

        UI.TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("New name")
            error: text.length > 0 && !renameDialog.valid(text)
            onAccepted: if (renameDialog.valid(text)) renameButton.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: nameField.text.length > 0 && !renameDialog.valid(nameField.text)
                ? qsTr("Only letters, numbers and hyphens, and no hyphen at the start or end.")
                : renameDialog.error
            color: UI.Style.warning
            size: 12
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: renameButton
                text: qsTr("Rename")
                kind: "primary"
                enabled: renameDialog.valid(nameField.text) && nameField.text !== System.hostName
                onClicked: {
                    renameDialog.error = "";
                    System.renameHost(nameField.text);
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: renameDialog.close()
            }
        ]

        Connections {
            target: System
            function onHostNameChanged() {
                renameDialog.close();
            }
            function onRenameFailed(reason) {
                renameDialog.error = reason;
            }
        }
    }
}
