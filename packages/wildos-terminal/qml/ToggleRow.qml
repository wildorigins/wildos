import QtQuick
import WildOS.UI as UI

// A setting that is on or off: the row with the state spelt out beside the
// switch, as Settings draws it.
UI.SettingRow {
    id: row

    property bool value
    signal switched(bool value)

    status: value ? qsTr("On") : qsTr("Off")

    UI.Toggle {
        anchors.verticalCenter: parent.verticalCenter
        checked: row.value
        enabled: row.enabled
        Accessible.name: row.title
        onToggled: row.switched(checked)
    }
}
