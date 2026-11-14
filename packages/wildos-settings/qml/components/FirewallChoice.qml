import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// One choice of a set in the firewall's dialog (FirewallAllow.dc): a 46px
// row with an 18px radio, a 13px title over a 12px muted line, and an
// optional mark at the right. Chosen, it takes the accent edge on a 0.10
// accent wash; otherwise a 0.04 fill with a 0.07 edge. Radius 8.
T.AbstractButton {
    id: control

    property string title
    property string description
    default property alias trailing: trailingRow.data
    property alias below: belowColumn.data
    property bool showBelow: false

    // The dialog binds `checked` and sets its own choice on click.
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: 300
    implicitHeight: Math.max(46, body.implicitHeight + topPadding + bottomPadding)
    topPadding: 6
    bottomPadding: 6
    leftPadding: 14
    rightPadding: 14
    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.description: description

    background: Rectangle {
        radius: 8
        color: control.checked ? Qt.rgba(UI.Style.accent.r, UI.Style.accent.g, UI.Style.accent.b, 0.10)
            : control.hovered && control.enabled ? UI.Style.tone(0.07) : UI.Style.tone(0.04)
        border.width: 1
        border.color: control.checked ? UI.Style.accent : UI.Style.tone(0.07)

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: 8
        }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                radius: 9
                color: control.checked ? UI.Style.accent : "transparent"
                border.width: control.checked ? 0 : 1.5
                border.color: UI.Style.toggleOffBorder

                Rectangle {
                    visible: control.checked
                    anchors.centerIn: parent
                    width: 7
                    height: 7
                    radius: 3.5
                    color: UI.Style.textOnAccent
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                UI.Label {
                    Layout.fillWidth: true
                    text: control.title
                    size: 13
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: control.description.length > 0
                    text: control.description
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            Row {
                id: trailingRow
                visible: children.length > 0
                Layout.alignment: Qt.AlignVCenter
                spacing: 8
            }
        }

        ColumnLayout {
            id: belowColumn
            Layout.fillWidth: true
            Layout.leftMargin: 30
            Layout.bottomMargin: 4
            visible: control.showBelow
        }
    }
}
