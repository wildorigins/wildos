import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A choice of a few short values in a row (10.9 "Rotate", 10.10 "Fans /
// Pump"): a radius 7 track at 0.05 with a 0.10 edge holding 26px segments;
// the chosen one is lifted to 0.12 in weight 550, the others muted. Left
// and Right move between them.
Rectangle {
    id: control

    // [{ value, text }]
    property var options: []
    property var value
    property string accessibleName
    readonly property int currentIndex: options.findIndex(o => o.value === value)

    signal chosen(var value)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: row.implicitHeight + 6
    radius: 7
    color: UI.Style.tone(0.05)
    border.width: 1
    border.color: UI.Style.borderField
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTabList
    Accessible.name: accessibleName

    Keys.onLeftPressed: if (currentIndex > 0) chosen(options[currentIndex - 1].value)
    Keys.onRightPressed: if (currentIndex < options.length - 1) chosen(options[currentIndex + 1].value)

    UI.FocusRing {
        visible: control.activeFocus
        targetRadius: control.radius
    }

    Row {
        id: row
        anchors.centerIn: parent

        Repeater {
            model: control.options

            T.AbstractButton {
                id: option

                required property var modelData
                required property int index
                readonly property bool current: control.currentIndex === index

                implicitWidth: label.implicitWidth + 24
                implicitHeight: 26
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.text
                Accessible.selected: current
                onClicked: control.chosen(modelData.value)

                background: Rectangle {
                    radius: 5
                    // Fades in on hover and clears at once, so moving along a list
                    // never lights two items. It comes before the colour so it sees
                    // the change in hover first.
                    Behavior on color {
                        enabled: option.hovered
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                    color: option.current ? UI.Style.surfacePressed : option.down ? UI.Style.surfaceHover : option.hovered ? UI.Style.surfaceControl : "transparent"
                }

                contentItem: Words {
                    id: label
                    horizontalAlignment: Text.AlignHCenter
                    text: option.modelData.text
                    size: 12.5
                    font.weight: option.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: option.current ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }
    }
}
