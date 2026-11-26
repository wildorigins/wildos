import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A day on the Schedule page: a 40 by 32 pill with the day's first
// letters, accent filled when backups run that day.
T.AbstractButton {
    id: chip

    // 1 Monday to 7 Sunday.
    property int day: 1
    readonly property string longName: Qt.locale().dayName(day % 7, Locale.LongFormat)

    implicitWidth: 40
    implicitHeight: 32
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.CheckBox
    Accessible.name: longName
    Accessible.checked: checked
    Accessible.checkable: true

    background: Rectangle {
        radius: 16
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: chip.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: chip.checked ? (chip.hovered ? UI.Style.accentHover : UI.Style.accent) : chip.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        border.width: chip.checked ? 0 : 1
        border.color: UI.Style.borderControl

        UI.FocusRing {
            visible: chip.visualFocus
            targetRadius: 16
        }
    }

    contentItem: UI.Label {
        text: Qt.locale().dayName(chip.day % 7, Locale.ShortFormat).slice(0, 2)
        size: 12.5
        font.weight: chip.checked ? UI.Style.weightSemiBold : UI.Style.weightRegular
        color: chip.checked ? UI.Style.textOnAccent : UI.Style.ink
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    UI.Tooltip {
        text: chip.longName
        visible: chip.hovered
    }
}
