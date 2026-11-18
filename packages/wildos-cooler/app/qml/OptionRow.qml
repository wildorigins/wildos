import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// One row of a card on the Cooler boards (10.9 "Show the Wild OS mark"):
// 50px, 16px side padding, a 17px line icon, the title over a 12px muted
// line, and the row's control at the right. Rows after the first draw the
// hairline above themselves. A switch, slider or drop-down on the right is
// announced by the row's title unless it has a name of its own.
//
// `stacked` puts the controls under the text, for a row whose controls do
// not fit beside it in a narrow window.
Item {
    id: row

    property string iconName
    property color iconColor: UI.Style.inkMuted
    property string title
    property string description
    property bool divider: y > 0
    property bool stacked: false
    default property alias trailing: controls.data
    readonly property real line: divider ? 1 : 0

    width: parent ? parent.width : implicitWidth
    implicitWidth: 360
    implicitHeight: line + main.height + (stacked ? controls.height + 10 : 0)

    Component.onCompleted: {
        for (const control of Array.from(controls.children)) {
            if (control.accessibleName === "") {
                control.accessibleName = Qt.binding(() => row.title);
            }
        }
    }

    Rectangle {
        visible: row.divider
        width: parent.width
        height: 1
        color: UI.Style.borderSubtle
    }

    Item {
        id: main
        y: row.line
        width: parent.width
        height: Math.max(50, text.height + 16)

        CoolerIcon {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            name: row.iconName
            color: row.iconColor
        }

        Column {
            id: text
            x: 46
            width: Math.max(0, (row.stacked || controls.width === 0 ? parent.width - 16 : controls.x - 6) - x)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Words {
                width: parent.width
                text: row.title
                // The board's 13.5 is not a size a font can be set to; 14
                // is too wide for the board's narrowest row.
                size: 13
                wrapMode: Text.Wrap
            }

            Words {
                visible: row.description.length > 0
                width: parent.width
                text: row.description
                size: 12
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
        }
    }

    Row {
        id: controls
        x: row.stacked ? 46 : row.width - 16 - width
        y: row.stacked ? main.y + main.height - 2 : main.y + (main.height - height) / 2
        spacing: 8
    }
}
