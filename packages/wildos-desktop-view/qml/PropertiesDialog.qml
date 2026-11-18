pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.Desktop
import WildOS.UI as UI

// Properties (Alt+Enter): what the item is, where it lives, how big it is
// and when it changed, on one sheet rather than Windows' tabs.
DeskDialog {
    id: props

    required property var view
    property var urls: []

    title: info.name
    defaultAction: () => props.close()
    text: ""

    function show(list) {
        urls = list;
        present();
    }

    ItemInfo {
        id: info
        urls: props.urls
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 14

        ItemIcon {
            size: 48
            iconName: info.iconName
            url: info.single ? info.urls[0] : ""
            preview: info.preview
            stamp: info.stamp
        }

        UI.Label {
            Layout.fillWidth: true
            text: info.type
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 4
        height: 1
        color: UI.Style.divider
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 18
        rowSpacing: 10

        Repeater {
            model: [
                { label: qsTr("Location"), value: info.location },
                { label: qsTr("Size"), value: info.size },
                { label: qsTr("Contains"), value: info.contains },
                { label: qsTr("Created"), value: info.created },
                { label: qsTr("Modified"), value: info.modified },
                { label: qsTr("Opened"), value: info.accessed }
            ]

            delegate: UI.Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 0
                Layout.alignment: Qt.AlignTop
                visible: modelData.value.length > 0
                text: modelData.label
                color: UI.Style.inkMuted
            }
        }

        Repeater {
            model: [info.location, info.size, info.contains, info.created, info.modified, info.accessed]

            delegate: UI.Label {
                required property string modelData
                required property int index
                Layout.row: index
                Layout.column: 1
                Layout.fillWidth: true
                visible: modelData.length > 0
                text: modelData
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }

    RowLayout {
        visible: info.single && !info.isDir
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 12

        UI.Label {
            text: qsTr("Opens with")
            color: UI.Style.inkMuted
        }
        UI.Label {
            Layout.fillWidth: true
            text: info.opensWith.length > 0 ? info.opensWith : qsTr("No app chosen yet")
        }
        UI.Button {
            size: "small"
            text: qsTr("Change")
            onClicked: {
                props.close();
                props.view.chooseApp(props.urls);
            }
        }
    }

    UI.CheckBox {
        visible: info.single && info.canChangeReadOnly
        text: qsTr("Read only")
        checked: info.readOnly
        onToggled: info.readOnly = checked
    }

    buttons: [
        UI.Button {
            kind: "primary"
            text: qsTr("Close")
            onClicked: props.close()
        }
    ]
}
