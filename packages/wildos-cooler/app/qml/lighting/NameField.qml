import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// A zone's name with a pencil (10.15): click it, or press Enter on it, to
// type another. Enter or clicking away keeps the new name, Escape the old.
// An empty name goes back to the one the part reports.
Rectangle {
    id: field

    property string name
    property string accessibleName
    readonly property bool editing: input.activeFocus

    signal renamed(string name)

    function finish(keep) {
        if (keep && input.text.trim() !== field.name) {
            field.renamed(input.text.trim());
        }
        input.text = Qt.binding(() => field.name);
        input.focus = false;
    }

    implicitWidth: 150
    implicitHeight: 30
    radius: 7
    color: UI.Style.tone(0.04)
    border.width: 1
    border.color: editing ? UI.Style.accent : hover.hovered ? UI.Style.borderField : UI.Style.borderSubtle

    HoverHandler {
        id: hover
        cursorShape: Qt.IBeamCursor
    }

    TextInput {
        id: input
        x: 10
        width: parent.width - 20 - (field.editing ? 0 : 18)
        height: parent.height
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        text: field.name
        maximumLength: 40
        color: UI.Style.ink
        selectionColor: UI.Style.accent
        selectedTextColor: UI.Style.textOnAccent
        font.family: UI.Style.fontFamily
        font.pixelSize: 13.5 * UI.Style.textScale
        font.weight: UI.Style.weightSemiBold
        selectByMouse: true
        activeFocusOnTab: true
        Accessible.role: Accessible.EditableText
        Accessible.name: field.accessibleName
        onActiveFocusChanged: activeFocus ? selectAll() : field.finish(true)
        onAccepted: field.finish(true)
        Keys.onEscapePressed: field.finish(false)
    }

    LightIcon {
        visible: !field.editing
        x: parent.width - 10 - 12
        anchors.verticalCenter: parent.verticalCenter
        name: "pencil"
        size: 12
        strokeWidth: 2
        color: UI.Style.inkSubtle
    }
}
