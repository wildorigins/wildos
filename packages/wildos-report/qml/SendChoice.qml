import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// One way of sending it (ReportSend.dc): an 18px radio, a 20px icon, the
// name with an optional chip, and a line on what happens. Chosen: a 2px
// accent edge over an accent tint.
Rectangle {
    id: card

    property string title
    property string description
    property string iconName
    property string note
    property bool checked
    signal clicked()

    implicitHeight: content.implicitHeight + 28
    radius: UI.Style.radiusPanel
    color: checked ? UI.Style.accentAlpha(0.08, 0.06) : mouse.containsMouse ? UI.Style.tone(0.05) : UI.Style.tone(0.03)
    border.width: checked ? 2 : 1
    border.color: checked ? UI.Style.accent : Look.hairline
    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.checked: checked
    activeFocusOnTab: true
    Keys.onSpacePressed: card.clicked()

    RowLayout {
        id: content
        x: 16
        y: 14
        width: parent.width - 32
        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            width: 18
            height: 18
            radius: 9
            color: card.checked ? UI.Style.accent : "transparent"
            border.width: card.checked ? 0 : 1.5
            border.color: UI.Style.tone(0.55)

            Rectangle {
                anchors.centerIn: parent
                visible: card.checked
                width: 7
                height: 7
                radius: 3.5
                color: UI.Style.textOnAccent
            }
        }

        ReportIcon {
            Layout.alignment: Qt.AlignTop
            name: card.iconName
            size: 20
            color: UI.Style.ink
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Row {
                spacing: 10

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.title
                    size: 14
                    font.weight: UI.Style.weightSemiBold
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: card.note.length > 0
                    width: noteLabel.implicitWidth + 16
                    height: 20
                    radius: 10
                    color: UI.Style.accentAlpha(0.14, 0.10)

                    UI.Label {
                        id: noteLabel
                        anchors.centerIn: parent
                        text: card.note
                        size: 11.5
                        font.weight: UI.Style.weightSemiBold
                        color: UI.Style.accentText
                    }
                }
            }
            UI.Label {
                Layout.fillWidth: true
                text: card.description
                size: 12.5
                wrapMode: Text.WordWrap
                lineHeight: 1.2
                color: UI.Style.inkMuted
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.clicked()
    }

    UI.FocusRing {
        visible: card.activeFocus
        targetRadius: 10
    }
}
