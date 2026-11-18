pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

// Start's search pill: the shared search field in its pill shape, 46 px
// tall with the 17 px magnifier of the Start artboard. Start opens with the
// field focused so typing goes straight in, so the edge only takes the
// accent, with a soft glow around it, once there is something in it (the
// searching design); until then the caret is hidden too and the pill looks
// exactly like the resting design.
FocusScope {
    id: box

    property alias text: field.text
    property alias input: field
    readonly property bool active: field.text.length > 0

    signal accepted()
    signal moveDown()
    signal moveUp()
    signal stepSelection(int step)
    // Escape with nothing typed: the first Escape is the field's, to clear.
    signal escapePressed()

    implicitHeight: 46

    function type(text: string): void {
        field.forceActiveFocus();
        field.insert(field.cursorPosition, text);
    }

    function clear(): void {
        field.clear();
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: height / 2
        color: "transparent"
        border.width: 3
        border.color: UI.Style.accentChip
        visible: box.active
    }

    UI.SearchField {
        id: field
        anchors.fill: parent
        focus: true
        pill: true
        // Start handles Tab itself (StartMenu tabStops).
        focusPolicy: Qt.ClickFocus
        size: "large"
        iconSize: 17
        iconInset: 16
        leftPadding: 45
        placeholderText: qsTr("Search apps, files and settings")
        color: UI.Style.inkStrong
        font.pixelSize: UI.Style.px(box.active ? 15 : 14)
        Accessible.name: qsTr("Search apps, files and settings")

        background: Rectangle {
            radius: height / 2
            color: UI.Style.surfaceSunken
            border.width: 1
            border.color: box.active ? UI.Style.accentBorderStrong : UI.Style.borderGlass
            Behavior on border.color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }

            UI.FocusRing {
                targetRadius: parent.radius
                visible: field.activeFocus && Nav.keyboard && !box.active
            }
        }

        cursorDelegate: Rectangle {
            width: 1.5
            color: UI.Style.inkStrong
            visible: field.cursorVisible && box.active
        }

        onAccepted: box.accepted()
        onEscapePressed: box.escapePressed()
        onDownPressed: {
            Nav.keyboard = true;
            if (box.active) {
                box.stepSelection(1);
            } else {
                box.moveDown();
            }
        }
        Keys.onUpPressed: {
            Nav.keyboard = true;
            if (box.active) {
                box.stepSelection(-1);
            } else {
                box.moveUp();
            }
        }
    }
}
