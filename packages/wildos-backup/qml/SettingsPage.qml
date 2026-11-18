import QtCore as Core
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Backup
import WildOS.UI as UI

// Settings: the backup password, the folders left out, notifications, and
// checking that the backups can still be read.
Item {
    id: page

    property var app

    component Section: Panel {
        id: section

        property string iconName
        property string title
        property string text
        default property alias body: extra.data

        Layout.fillWidth: true
        implicitHeight: row.implicitHeight + padding * 2

        RowLayout {
            id: row
            width: parent.width
            spacing: 12

            BackupIcon {
                Layout.alignment: Qt.AlignTop
                name: section.iconName
                size: 18
                color: UI.Style.inkMuted
            }
            ColumnLayout {
                id: extra
                Layout.fillWidth: true
                spacing: 6

                UI.Label {
                    Layout.fillWidth: true
                    text: section.title
                    size: 14
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }
                UI.Label {
                    Layout.fillWidth: true
                    visible: section.text.length > 0
                    text: section.text
                    size: 12.5
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + 44
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        C.ScrollBar.vertical: UI.ScrollBar {}

        ColumnLayout {
            id: column
            x: 24
            y: 22
            width: Math.min(flick.width - 48, 760)
            spacing: 14

            PageHeading {
                Layout.fillWidth: true
                title: qsTr("Settings")
            }

            Section {
                iconName: "key"
                title: qsTr("Backup password")
                text: qsTr("Your backups are encrypted with a password kept in your keyring. Write it down and keep it away from this PC: if your keyring is ever lost, it's the only way to open your backups.")

                UI.Button {
                    Layout.topMargin: 4
                    implicitHeight: 32
                    text: qsTr("Show password")
                    iconName: "eye"
                    enabled: Settings.configured
                    onClicked: {
                        Setup.reveal();
                        showDialog.open();
                    }
                }
            }

            Section {
                iconName: "folder"
                title: qsTr("Folders left out")
                text: qsTr("Downloads, caches and the bin are always left out. Add any other folder you don't need backed up.")

                Repeater {
                    model: Settings.excludes
                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 8

                        UI.Label {
                            Layout.fillWidth: true
                            text: modelData
                            size: 13
                            elide: Text.ElideMiddle
                        }
                        UI.IconButton {
                            iconName: "close"
                            iconSize: 14
                            tip: qsTr("Back it up again")
                            onClicked: Settings.removeExclude(modelData)
                        }
                    }
                }

                UI.Button {
                    Layout.topMargin: 4
                    implicitHeight: 32
                    text: qsTr("Exclude a folder")
                    iconName: "plus"
                    onClicked: folderDialog.open()
                }
            }

            Section {
                iconName: "circle-check"
                title: qsTr("Notifications")
                text: qsTr("You're always told when a backup doesn't finish.")

                UI.CheckBox {
                    Layout.topMargin: 4
                    text: qsTr("Also tell me when a backup finishes")
                    checked: Settings.notifyFinished
                    onToggled: Settings.notifyFinished = checked
                }
            }

            Section {
                iconName: "shield-check"
                title: qsTr("Check your backups")
                text: qsTr("Reads part of the backups on the drive to make sure they can be opened and nothing is damaged. Each check reads a different part.")

                RowLayout {
                    Layout.topMargin: 4
                    spacing: 12

                    UI.Button {
                        implicitHeight: 32
                        text: qsTr("Check now")
                        busy: Setup.checking
                        enabled: Settings.configured && !Setup.checking
                        onClicked: Setup.check()
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        visible: Setup.checkResult.length > 0
                        text: Setup.checkResult
                        size: 13
                        color: Setup.checkOk ? BackupStyle.good : BackupStyle.bad
                        wrapMode: Text.Wrap
                    }
                }
                ErrorDetails {
                    Layout.fillWidth: true
                    text: Setup.checkDetail
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Exclude a folder")
        currentFolder: Core.StandardPaths.writableLocation(Core.StandardPaths.HomeLocation)
        onAccepted: Settings.addExclude(decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, "")))
    }

    PasswordDialog {
        id: showDialog
        mode: "show"
    }
}
