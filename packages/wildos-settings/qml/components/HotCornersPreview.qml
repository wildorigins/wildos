pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

// The screen with the taskbar drawn in, and each corner's choices in that
// corner of it, as the Mac lays out its hot corners: the action, a chip for
// the key it waits for, and the Hold drop-down that sets the key.
Item {
    id: preview

    // wildosrc [HotCorners], from System.hotCorners.
    required property var settings

    readonly property var actions: [
        { value: "none", text: qsTr("Nothing") },
        { value: "taskview", text: qsTr("Task view (all windows)"), divider: true },
        { value: "appwindows", text: qsTr("This app's windows") },
        { value: "desktop", text: qsTr("Show desktop") },
        { value: "appgrid", text: qsTr("App grid") },
        { value: "notifications", text: qsTr("Notifications"), divider: true },
        { value: "quicknote", text: qsTr("Quick note") },
        { value: "lock", text: qsTr("Lock"), divider: true },
        { value: "screenoff", text: qsTr("Turn screen off") },
        { value: "keepawake", text: qsTr("Keep awake"), divider: true },
        { value: "dnd", text: qsTr("Do not disturb") }
    ]

    readonly property var keys: [
        { value: "none", text: qsTr("None") },
        { value: "ctrl", text: "Ctrl" },
        { value: "alt", text: "Alt" },
        { value: "shift", text: qsTr("Shift") },
        { value: "super", text: "Super" },
        { value: "ctrl+alt", text: "Ctrl + Alt" },
        { value: "ctrl+shift", text: qsTr("Ctrl + Shift") }
    ]

    readonly property int inset: 12
    readonly property int barHeight: 22

    implicitHeight: Math.max(topLeft.height, topRight.height) + Math.max(bottomLeft.height, bottomRight.height) + barHeight + inset * 3 + 24

    DesktopPreview {
        anchors.fill: parent
        sketch: false
    }

    // The docked taskbar across the bottom: Start and a few apps in the
    // middle, the clock at the right end.
    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
        height: preview.barHeight
        bottomLeftRadius: UI.Style.radiusMd - 1
        bottomRightRadius: UI.Style.radiusMd - 1
        color: UI.Style.glassBar

        Row {
            anchors.centerIn: parent
            spacing: 6

            Repeater {
                model: 5

                Rectangle {
                    required property int index
                    width: 10
                    height: 10
                    radius: 3
                    color: index === 0 ? UI.Style.accent : UI.Style.tone(0.28)
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            height: 8
            radius: 2
            color: UI.Style.tone(0.28)
        }
    }

    component Corner: Rectangle {
        id: corner

        required property string name
        required property string title
        property bool alignRight: false

        readonly property string action: preview.settings[name] ?? "none"
        readonly property string key: preview.settings[name + "Key"] ?? "none"
        readonly property int keyIndex: preview.keys.findIndex(o => o.value === key)

        width: column.width + 16
        height: column.height + 16
        radius: UI.Style.radiusMd
        color: UI.Style.glassMenu
        border.width: 1
        border.color: UI.Style.borderMenu

        Column {
            id: column
            x: 8
            y: 8
            spacing: 6
            width: Math.max(actionBox.implicitWidth, holdRow.implicitWidth)

            UI.ComboBox {
                id: actionBox
                width: parent.width
                Accessible.name: corner.title
                model: preview.actions
                currentIndex: Math.max(0, model.findIndex(o => o.value === corner.action))
                onActivated: index => System.setHotCorner(corner.name, model[index].value)
            }

            UI.Chip {
                anchors.right: corner.alignRight ? parent.right : undefined
                visible: corner.key !== "none" && corner.keyIndex > 0
                text: visible ? qsTr("Hold %1").arg(preview.keys[corner.keyIndex].text) : ""
            }

            Row {
                id: holdRow
                anchors.right: corner.alignRight ? parent.right : undefined
                spacing: 8

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Hold")
                    size: 13
                    color: UI.Style.inkMuted
                }

                UI.ComboBox {
                    Accessible.name: qsTr("%1, key to hold").arg(corner.title)
                    model: preview.keys
                    currentIndex: Math.max(0, corner.keyIndex)
                    onActivated: index => System.setHotCorner(corner.name + "Key", model[index].value)
                }
            }
        }
    }

    Corner {
        id: topLeft
        name: "TopLeft"
        title: qsTr("Top left corner")
        x: preview.inset
        y: preview.inset
    }

    Corner {
        id: topRight
        name: "TopRight"
        title: qsTr("Top right corner")
        alignRight: true
        x: preview.width - width - preview.inset
        y: preview.inset
    }

    Corner {
        id: bottomLeft
        name: "BottomLeft"
        title: qsTr("Bottom left corner")
        x: preview.inset
        y: bar.y - height - preview.inset
    }

    Corner {
        id: bottomRight
        name: "BottomRight"
        title: qsTr("Bottom right corner")
        alignRight: true
        x: preview.width - width - preview.inset
        y: bar.y - height - preview.inset
    }
}
