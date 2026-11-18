import QtQuick
import WildOS.UI as UI

// A fact about the cooler (10.12 "The cooler"): 44px, what it is in the
// muted ink in a 150px column, then the fact. Rows after the first draw the
// hairline above themselves.
Item {
    id: row

    property string name
    property string value
    property bool divider: y > 0
    readonly property real line: divider ? 1 : 0

    width: parent ? parent.width : implicitWidth
    implicitWidth: 360
    implicitHeight: line + Math.max(44, fact.implicitHeight + 16)
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1: %2").arg(name).arg(value)

    Rectangle {
        visible: row.divider
        width: parent.width
        height: 1
        color: UI.Style.borderSubtle
    }

    Words {
        x: 16
        y: row.line
        width: 150
        height: parent.height - row.line
        text: row.name
        color: UI.Style.inkMuted
    }

    Words {
        id: fact
        x: 178
        y: row.line
        width: parent.width - x - 16
        height: parent.height - row.line
        text: row.value
        size: 13.5
        wrapMode: Text.Wrap
    }
}
