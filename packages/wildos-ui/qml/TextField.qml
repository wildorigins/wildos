import QtQuick
import QtQuick.Templates as T

// A single line of input: the sunken field from the artboards (surface
// sunken, 1px 0.10 edge, radius 8, 36px, 13px text) whose edge turns
// accent while typing, as wildos-settings SearchField does. `size: "large"`
// is the 42px, radius 10, 14px field of the account and installer forms
// (Account.dc). A password field gets a show and hide button with
// `revealable`. A right click, the Menu key or Shift+F10 opens the text
// menu (TextMenu.qml). A single line field holds plain text only, so
// Paste already pastes plain text and the board's "Paste as plain
// text" has nothing to add here; spelling suggestions need a spell
// checked text box, which this is not.
T.TextField {
    id: control

    property string size: "medium"
    property bool revealable: false
    property bool revealed: false
    property bool error: false
    property bool escapeClears: true
    property bool previewFocus: false
    property alias trailing: trailingRow.data
    // Off for a field that brings its own right-click menu.
    property bool textMenu: true
    // The text menu holds focus while it is open. A field that ends its
    // editing when focus leaves waits while this is true, and finishes on
    // textMenuFocusLeft if a click elsewhere closed the menu.
    readonly property bool textMenuOpen: menu.opened
    signal textMenuFocusLeft()

    readonly property bool large: size === "large"
    readonly property bool editingNow: activeFocus || previewFocus

    implicitWidth: 240
    implicitHeight: Math.max(large ? Style.fieldHeightLarge : Style.controlHeight, contentHeight + 16)
    leftPadding: large ? 14 : 12
    rightPadding: (large ? 14 : 12) + (trailingRow.width > 0 ? trailingRow.width + 6 : 0)
    verticalAlignment: TextInput.AlignVCenter
    color: Style.inkField
    selectionColor: Style.textSelection
    selectedTextColor: Style.ink
    placeholderTextColor: Style.inkPlaceholder
    font.family: Style.fontFamily
    font.letterSpacing: Style.tracking(Style.px(large ? 14 : 13))
    font.pixelSize: Style.px(large ? 14 : 13)
    echoMode: revealable && !revealed ? TextInput.Password : TextInput.Normal
    passwordCharacter: "•"
    opacity: enabled ? 1 : 0.5
    Accessible.name: placeholderText

    function openTextMenu(px: real, py: real): void {
        menu.open(px, py);
    }

    TextMenu {
        id: menu
        enabled: control.textMenu && control.enabled
        onFocusLeft: control.textMenuFocusLeft()
    }

    // A password shown as text is copied marked as a secret.
    SecretCopy {
        enabled: control.revealable
    }

    Keys.onPressed: event => {
        if (textMenu && menu.isMenuKey(event)) {
            menu.openAtCaret();
            event.accepted = true;
        }
    }

    Keys.onEscapePressed: event => {
        // A type that handles Escape itself (SearchField) turns this off;
        // both handlers run, so this one must then leave the event alone.
        if (!escapeClears) {
            return;
        }
        if (text.length > 0 && echoMode !== TextInput.Password) {
            clear();
            event.accepted = true;
        } else {
            event.accepted = false;
        }
    }

    background: Rectangle {
        radius: control.large ? 10 : Style.radiusSm
        color: Style.surfaceSunken
        border.width: 1
        border.color: control.error ? Style.danger : control.editingNow ? Style.accentBorderStrong : control.large ? Style.borderControl : Style.borderField
        Behavior on border.color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    Label {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height
        visible: control.text.length === 0 && control.placeholderText.length > 0 && control.preeditText.length === 0
        text: control.placeholderText
        size: control.large ? 14 : 13
        color: Style.inkPlaceholder
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.rightMargin: control.large ? 8 : 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        IconButton {
            visible: control.revealable
            width: 28
            height: 28
            radius: Style.radiusXs
            iconName: control.revealed ? "eye-off" : "eye"
            iconSize: 15
            tip: control.revealed ? "Hide password" : "Show password"
            focusPolicy: Qt.NoFocus
            onClicked: control.revealed = !control.revealed
        }
    }
}
