import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// One status row inside a Home or Security card: a small icon, the name,
// its state on the right and a chevron, under a hairline. The whole row
// opens what it names. A row with `link` set is a card's "N more" line:
// accent words where the name goes, no icon and no state.
T.AbstractButton {
    id: row

    property string iconName
    property color iconColor: UI.Style.iconMuted
    property real iconSize: 16
    property string title
    property string value
    property color valueColor: UI.Style.inkMuted
    property color titleColor: link ? UI.Style.accentText : UI.Style.ink
    property bool link: false
    property bool chevron: true
    property int rowHeight: 30
    property real titleSize: 13
    property real valueSize: 12.5

    Layout.fillWidth: true
    implicitHeight: rowHeight
    padding: 0
    leftPadding: 18
    rightPadding: 18
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: link ? Accessible.Link : Accessible.Button
    Accessible.name: value.length > 0 ? qsTr("%1, %2").arg(title).arg(value) : title

    background: Rectangle {
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: row.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.down ? UI.Style.surfacePressed : row.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        Rectangle {
            width: parent.width
            height: 1
            color: UI.Style.divider
        }

        UI.FocusRing {
            visible: row.visualFocus
            targetRadius: UI.Style.radiusXs
        }
    }

    contentItem: RowLayout {
        spacing: 12

        Item {
            Layout.preferredWidth: row.iconSize
            Layout.preferredHeight: row.iconSize

            PathIcon {
                visible: !row.link && row.iconName.length > 0
                name: row.iconName
                size: row.iconSize
                strokeWidth: row.iconSize <= 16 ? 2 : 1.8
                color: row.iconColor
            }
        }

        UI.Label {
            Layout.fillWidth: true
            text: row.title
            size: row.titleSize
            color: row.titleColor
        }

        UI.Label {
            visible: row.value.length > 0
            Layout.maximumWidth: row.width * 0.55
            text: row.value
            size: row.valueSize
            color: row.valueColor
        }

        UI.Icon {
            visible: row.chevron
            name: "chevron-right"
            size: 14
            strokeWidth: 2
            color: UI.Style.inkSubtle
        }
    }
}
