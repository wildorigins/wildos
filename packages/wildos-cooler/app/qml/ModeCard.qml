import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// One of the four cooling modes (10.10): its name in 14px weight 600 over
// a line saying what it is like to live with. The chosen one takes the
// accent: a 0.10 fill inside an accent edge.
T.AbstractButton {
    id: card

    property string description
    property bool current: false

    implicitHeight: 13 + title.implicitHeight + 3 + note.implicitHeight + 13
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.description: description
    Accessible.checked: current

    background: Rectangle {
        radius: UI.Style.radiusPanel
        color: card.current ? UI.Style.accentAlpha(0.10, 0.09) : card.down ? UI.Style.surfacePressed : card.hovered ? UI.Style.surfaceHover : UI.Style.surfaceRaised
        border.width: 1
        border.color: card.current ? UI.Style.accent : UI.Style.borderSubtle
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: card.visualFocus
            targetRadius: UI.Style.radiusPanel
        }
    }

    contentItem: Item {
        Words {
            id: title
            x: 15
            y: 13
            width: parent.width - 30
            text: card.text
            size: 14
            font.weight: UI.Style.weightBold
        }

        Words {
            id: note
            x: 15
            y: 13 + title.implicitHeight + 3
            width: parent.width - 30
            text: card.description
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }
}
