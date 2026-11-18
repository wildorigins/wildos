pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// Accessibility, opened from its tile: the switches Wild OS has for seeing
// the screen more easily, each one live.
FocusScope {
    id: page

    required property var quick
    readonly property var access: quick.access

    signal back()

    function focusFirst(): void {
        header.backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    Component.onCompleted: access.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        PageHeader {
            id: header
            Layout.fillWidth: true
            title: qsTr("Accessibility")
            onBack: page.back()
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.access.features

                    UI.SettingRow {
                        id: row
                        required property var modelData
                        title: modelData.title
                        description: modelData.description

                        UI.Toggle {
                            checked: page.access.loaded[row.modelData.id] === true
                            Accessible.name: row.modelData.title
                            onToggled: page.access.setOn(row.modelData.id, checked)
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
