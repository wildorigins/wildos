import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI

// The dialog of the recovery boards (RecoveryGoBack.dc, RecoveryReset.dc):
// a 15px title beside an accent icon, the body, and a footer band with the
// buttons at the right. Narrower than UI.Dialog and with 12px corners, as
// the boards draw it, on the same dialog glass and edge.
T.Dialog {
    id: control

    property string iconName: "history"
    property int dialogWidth: 560
    default property alias body: bodyColumn.data
    property alias buttons: buttonRow.data

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(dialogWidth, parent ? parent.width - 48 : dialogWidth)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    modal: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape

    T.Overlay.modal: Rectangle {
        color: UI.Style.scrim
    }

    background: Rectangle {
        radius: UI.Style.radiusMd
        color: UI.Style.glassDialog
        border.width: 1
        border.color: UI.Style.borderDialog
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.5)
            shadowVerticalOffset: 30
            shadowBlur: 1.0
            blurMax: 64
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        focus: true

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 20
            Layout.bottomMargin: 4
            spacing: 10

            PathIcon {
                name: control.iconName
                size: 18
                strokeWidth: 1.8
                color: UI.Style.accent
            }

            UI.Label {
                Layout.fillWidth: true
                text: control.title
                size: 15
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                Accessible.role: Accessible.Heading
            }
        }

        ColumnLayout {
            id: bodyColumn
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 12
            Layout.bottomMargin: 20
            spacing: 14
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            bottomLeftRadius: 12
            bottomRightRadius: 12
            color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.03)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)
            }

            RowLayout {
                id: buttonRow
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
            }
        }
    }
}
