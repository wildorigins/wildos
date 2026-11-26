import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// The Overall / Each core switch (TaskManagerPerf.dc): a 34px pill track
// at 0.06 holding 28px pills; the chosen one is lifted to 0.14 in weight
// 600, the other muted. Arrow keys move between them.
Rectangle {
    id: control

    property var options: []
    property int currentIndex: 0

    signal activated(int index)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: row.implicitHeight + 6
    radius: height / 2
    color: UI.Style.surfaceControl
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTabList

    Keys.onLeftPressed: if (currentIndex > 0) activated(currentIndex - 1)
    Keys.onRightPressed: if (currentIndex < options.length - 1) activated(currentIndex + 1)

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
                required property string modelData
                required property int index
                readonly property bool chosen: control.currentIndex === index
                implicitWidth: label.implicitWidth + 24
                implicitHeight: 28
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData
                Accessible.selected: chosen
                onClicked: control.activated(index)

                background: Rectangle {
                    radius: height / 2
                    // Fades in on hover and clears at once, so moving along a list
                    // never lights two items. It comes before the colour so it sees
                    // the change in hover first.
                    Behavior on color {
                        enabled: option.hovered
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                    color: option.chosen ? UI.Style.tone(0.14) : option.down ? UI.Style.surfacePressed : option.hovered ? UI.Style.surfaceRaised : Qt.alpha(UI.Style.surfaceRaised, 0)
                }

                contentItem: UI.Label {
                    id: label
                    horizontalAlignment: Text.AlignHCenter
                    text: option.modelData
                    size: 12
                    font.weight: option.chosen ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: option.chosen ? UI.Style.ink : UI.Style.inkAlpha(0.75, 0.70)
                }
            }
        }
    }
}
