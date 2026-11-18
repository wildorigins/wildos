pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import WildOS.Files
import WildOS.UI as UI

// The details pane (the command bar's Details switch): a large preview of
// the selection and what Properties would say about it.
Item {
    id: pane

    required property var folder
    readonly property var selection: folder && folder.selectionRevision >= 0 && folder.selectedCount > 0 ? folder.selectedUrls() : (folder && folder.kind !== "computer" ? [folder.url] : [])

    width: 264

    ItemInfo {
        id: info
        urls: pane.selection
    }

    Rectangle {
        width: 1
        height: parent.height
        color: UI.Style.divider
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 1
        contentHeight: column.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: column
            x: 18
            y: 18
            width: parent.width - 36
            spacing: 14

            Item {
                width: parent.width
                height: 150
                visible: info.urls.length > 0

                FileIcon {
                    anchors.centerIn: parent
                    size: info.preview ? 150 : 96
                    iconName: info.iconName
                    url: info.urls.length === 1 ? info.urls[0] : ""
                    preview: info.preview
                    stamp: info.stamp
                }
            }

            UI.Label {
                width: parent.width
                text: info.name
                font.pixelSize: UI.Style.px(15)
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 3
                horizontalAlignment: Text.AlignHCenter
            }

            UI.Label {
                width: parent.width
                visible: info.urls.length === 0
                text: qsTr("Select a file to see its details.")
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                width: parent.width
                height: 1
                color: UI.Style.divider
                visible: info.urls.length > 0
            }

            Repeater {
                model: [
                    { label: qsTr("Type"), value: info.type },
                    { label: qsTr("Size"), value: info.size },
                    { label: qsTr("Contains"), value: info.contains },
                    { label: qsTr("Opens with"), value: info.opensWith },
                    { label: qsTr("Location"), value: info.location },
                    { label: qsTr("Modified"), value: info.modified },
                    { label: qsTr("Created"), value: info.created }
                ]

                delegate: Column {
                    required property var modelData
                    width: column.width
                    spacing: 3
                    visible: modelData.value.length > 0

                    UI.Label {
                        text: parent.modelData.label
                        font.pixelSize: UI.Style.px(12)
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        width: parent.width
                        text: parent.modelData.value
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                    }
                }
            }
        }
    }
}
