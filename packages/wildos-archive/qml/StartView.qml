import QtQuick
import QtQuick.Layouts
import WildOS.Archive
import WildOS.UI as UI

// Archive opened from Start with nothing to show: open one, make one, or
// pick up a recent one.
Item {
    id: view

    signal openArchive()
    signal newArchive()
    signal openRecent(string path)

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(32, (parent.height - implicitHeight) / 2 - 24)
        width: Math.min(560, parent.width - 64)
        spacing: 0

        Image {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 72
            Layout.preferredHeight: 72
            source: ArchiveStyle.icon
            sourceSize.width: 144
            sourceSize.height: 144
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 14
            text: qsTr("Open an archive or make a new one")
            size: 18
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            text: qsTr("ZIP, 7z, RAR, TAR and GZ archives open here. Nothing leaves this PC.")
            size: 13
            color: UI.Style.inkMuted
        }
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 22
            spacing: 8

            ToolButton {
                accent: true
                iconName: "folder-open"
                text: qsTr("Open an archive")
                onClicked: view.openArchive()
            }
            UI.Button {
                size: "small"
                iconName: ""
                text: qsTr("New archive")
                onClicked: view.newArchive()
            }
        }

        UI.Label {
            Layout.topMargin: 36
            Layout.bottomMargin: 6
            visible: ArchiveApp.recent.length > 0
            text: qsTr("Recent")
            size: 13
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkMuted
        }

        Repeater {
            model: ArchiveApp.recent

            Rectangle {
                id: recentRow
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                radius: 6
                color: rowMouse.pressed ? ArchiveStyle.rowPressed : rowMouse.containsMouse ? ArchiveStyle.rowHover : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name
                Accessible.description: modelData.folder
                Accessible.onPressAction: view.openRecent(modelData.path)
                Keys.onReturnPressed: view.openRecent(modelData.path)
                Keys.onEnterPressed: view.openRecent(modelData.path)
                Keys.onSpacePressed: view.openRecent(modelData.path)
                Keys.onDeletePressed: ArchiveApp.removeRecent(modelData.path)

                Rectangle {
                    anchors.fill: parent
                    visible: recentRow.activeFocus
                    radius: parent.radius
                    color: "transparent"
                    border.width: 2
                    border.color: UI.Style.accent
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: view.openRecent(recentRow.modelData.path)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 6
                    spacing: 12

                    Image {
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        source: ArchiveStyle.icon
                        sourceSize.width: 44
                        sourceSize.height: 44
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        UI.Label {
                            Layout.fillWidth: true
                            text: recentRow.modelData.name
                            size: 13
                            elide: Text.ElideMiddle
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: recentRow.modelData.folder
                            size: 12
                            color: UI.Style.inkSubtle
                            elide: Text.ElideLeft
                        }
                    }
                    UI.Label {
                        text: recentRow.modelData.size
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        Layout.preferredWidth: 130
                        horizontalAlignment: Text.AlignRight
                        text: recentRow.modelData.modified
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                    ToolButton {
                        side: 28
                        iconSize: 14
                        iconName: "x"
                        tip: qsTr("Remove from the list")
                        opacity: rowMouse.containsMouse || hovered || activeFocus || recentRow.activeFocus ? 1 : 0
                        onClicked: ArchiveApp.removeRecent(recentRow.modelData.path)
                    }
                }
            }
        }
    }
}
