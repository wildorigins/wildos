import QtQuick

// A keyboard shortcut as a row of KeyChips 4px apart ("Super", "Alt",
// "S"). Read out as one phrase, the keys joined by "plus".
Row {
    id: shortcut

    property var keys: []

    spacing: 4
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Shortcut: %1").arg(shortcut.keys.join(qsTr(" plus ")))

    Repeater {
        model: shortcut.keys

        KeyChip {
            required property string modelData
            text: modelData
        }
    }
}
