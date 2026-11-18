import QtQuick
import QtQuick.Templates as T

// A hover label for icon-only buttons and anything else that needs a name:
// one line of 12px text in the menu glass, 600ms after the pointer stops,
// below the control. From wildos-files Tip. Plasmoids use it in place of
// PlasmaCore.ToolTipArea, whose bold two-line Plasma tooltip is not the
// Windows look.
//
// It moves above the control when there is no room for it below. A control
// in a window too small to hold the label at all (the taskbar) sets
// `ownWindow`, and the label opens in a window of its own.
T.ToolTip {
    id: control

    property bool ownWindow: false
    // "auto", or "above" or "below" to keep it on one side whatever the room.
    property string side: "auto"
    property bool above: false

    function fitsBelow(): bool {
        if (!parent) {
            return true;
        }
        const need = implicitHeight + 6 + margins;
        // A label in its own window has the screen to fit in, any other the
        // window it is drawn in. Where it fits on neither side it stays below.
        const top = ownWindow ? parent.mapToGlobal(0, 0).y - parent.Screen.virtualY : parent.mapToItem(null, 0, 0).y;
        const room = ownWindow ? parent.Screen.height : parent.Window.height;
        return room - top - parent.height >= need || top < need;
    }

    // Positions are not bindable, so the room is measured each time it opens.
    onAboutToShow: above = side === "above" || (side === "auto" && !fitsBelow())

    popupType: ownWindow ? T.Popup.Window : T.Popup.Item
    delay: 600
    timeout: 6000
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: !parent ? 0 : above ? -implicitHeight - 6 : parent.height + 6
    margins: 6
    leftPadding: 10
    rightPadding: 10
    topPadding: 6
    bottomPadding: 6
    implicitWidth: Math.min(360, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Label {
        text: control.text
        size: 12
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    background: Rectangle {
        radius: Style.radiusXs
        color: Style.glassMenu
        border.width: 1
        border.color: Style.borderMenu
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.motionFast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Math.round(Style.motionFast / 2) }
    }
}
