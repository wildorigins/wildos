import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// A small status tile on a raised card: icon, what it is, and its state in
// accent text (sand when it needs a look). A tile that opens something gets
// the hover and press fills; the rest stay still.
T.AbstractButton {
    id: tile

    property string iconName
    property string title
    property string value
    property bool warning: false
    property bool interactive: false

    readonly property bool lit: interactive && hovered && enabled

    Layout.fillWidth: true
    implicitHeight: Math.max(66, body.implicitHeight + 32)
    padding: 0
    hoverEnabled: interactive
    focusPolicy: interactive ? Qt.StrongFocus : Qt.NoFocus
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: qsTr("%1, %2").arg(title).arg(value)

    background: Rectangle {
        radius: UI.Style.radiusMd
        color: tile.interactive && tile.down ? UI.Style.surfacePressed : tile.lit ? UI.Style.surfaceHover : UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.borderSubtle
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: tile.visualFocus
            targetRadius: UI.Style.radiusMd
        }
    }

    contentItem: RowLayout {
        id: body
        spacing: 14

        Item {
            Layout.preferredWidth: 18 - body.spacing
        }

        UI.Icon {
            name: tile.iconName
            size: 22
            strokeWidth: 1.8
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            spacing: 2

            UI.Label {
                Layout.fillWidth: true
                text: tile.title
                font.weight: UI.Style.weightMedium
            }

            UI.Label {
                Layout.fillWidth: true
                text: tile.value
                size: 12
                color: tile.warning ? UI.Style.warning : UI.Style.accentText
            }
        }

        Item {
            Layout.preferredWidth: 18 - body.spacing
        }
    }
}
