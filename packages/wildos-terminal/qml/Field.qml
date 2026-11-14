import QtQuick
import WildOS.Terminal
import WildOS.UI as UI

// A text box bound to a setting. The value is handed over when editing
// finishes, or shortly after typing stops, so a half typed name is never
// saved on each key; Escape puts the saved value back.
UI.TextField {
    id: field

    property string value
    property bool monospace: false

    signal committed(string text)

    implicitWidth: 280
    escapeClears: false
    font.family: monospace ? Config.fontFamily : UI.Style.fontFamily
    Accessible.name: placeholderText

    // The text menu holds focus while it is open; the box is still being
    // edited then.
    onValueChanged: if (!activeFocus && !textMenuOpen) text = value
    Component.onCompleted: text = value

    function commit() {
        settle.stop();
        if (text !== value) {
            committed(text);
        }
    }

    Timer {
        id: settle
        interval: 700
        onTriggered: field.commit()
    }

    onTextEdited: settle.restart()
    onEditingFinished: if (!textMenuOpen) commit()
    onActiveFocusChanged: if (!activeFocus && !textMenuOpen) {
        commit();
        text = value;
    }
    onTextMenuFocusLeft: {
        commit();
        text = value;
    }
    Keys.onEscapePressed: {
        text = value;
        settle.stop();
    }
}
