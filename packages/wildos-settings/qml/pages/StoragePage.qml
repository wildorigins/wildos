import QtQuick
import QtCore
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Storage: how full each disk is, and the way into Free up space.
PageScroll {
    id: page

    property var volumes: System.volumes()

    Repeater {
        model: page.volumes

        UI.Card {
            id: volume
            required property var modelData
            readonly property real used: modelData.total - modelData.free
            Layout.fillWidth: true
            implicitHeight: body.implicitHeight + 36

            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    UI.Icon {
                        name: "storage"
                        size: 22
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        UI.Label {
                            Layout.fillWidth: true
                            text: volume.modelData.name
                            size: 15
                            font.weight: UI.Style.weightSemiBold
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 used of %2").arg(System.formatBytes(volume.used)).arg(System.formatBytes(volume.modelData.total))
                            size: 12
                            color: UI.Style.inkMuted
                        }
                    }

                    UI.Label {
                        text: qsTr("%1 free").arg(System.formatBytes(volume.modelData.free))
                        color: volume.modelData.free / volume.modelData.total < 0.1 ? UI.Style.warning : UI.Style.accentText
                    }
                }

                UI.ProgressBar {
                    Layout.fillWidth: true
                    barHeight: 8
                    value: volume.modelData.total > 0 ? volume.used / volume.modelData.total : 0
                    // Nearly full is worth a glance, so it turns Sand.
                    warning: value > 0.9
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Free up space")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                // Everything Wild OS can clear (SettingsFreeSpace.dc), the
                // Recycle Bin included, on a page of its own.
                UI.SettingRow {
                    iconName: "trash"
                    title: qsTr("Clean up")
                    description: qsTr("Recycle Bin, old restore points, update files, app caches and more")
                    chevron: true
                    onClicked: Navigator.open("freespace")
                }

                UI.SettingRow {
                    visible: System.hasProgram("partitionmanager")
                    iconName: "storage"
                    title: qsTr("Disks and volumes")
                    description: qsTr("Format drives and change partitions")
                    external: true
                    onClicked: System.launch("partitionmanager")
                }

                // Wild OS's own File Explorer; Dolphin is hidden from people.
                UI.SettingRow {
                    visible: System.hasProgram("wildos-files")
                    iconName: "folder"
                    title: qsTr("See what is taking up space")
                    description: qsTr("Open your Home folder in Files")
                    external: true
                    onClicked: System.launch("wildos-files", [StandardPaths.writableLocation(StandardPaths.HomeLocation)])
                }
            }
        }
    }
}
