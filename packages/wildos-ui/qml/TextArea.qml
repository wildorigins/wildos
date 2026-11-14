import QtQuick
import QtQuick.Templates as T

// Several lines of input: TextField's sunken field (surface sunken, 1px
// 0.10 edge, radius 8, 13px text) with room to wrap, and the same text
// menu on a right click, the Menu key or Shift+F10. Put it in a ScrollView
// for long text; a field that draws its own frame sets `background: null`.
T.TextArea {
    id: control

    property bool error: false
    property bool previewFocus: false
    // Off for a field that brings its own right-click menu.
    property bool textMenu: true
    // The text menu holds focus while it is open. A field that ends its
    // editing when focus leaves waits while this is true, and finishes on
    // textMenuFocusLeft if a click elsewhere closed the menu.
    readonly property bool textMenuOpen: menu.opened
    signal textMenuFocusLeft()

    readonly property bool editingNow: activeFocus || previewFocus

    implicitWidth: 240
    implicitHeight: Math.max(Style.controlHeight * 2, contentHeight + topPadding + bottomPadding)
    leftPadding: 12
    rightPadding: 12
    topPadding: 9
    bottomPadding: 9
    wrapMode: TextEdit.Wrap
    color: Style.inkField
    selectionColor: Style.textSelection
    selectedTextColor: Style.ink
    placeholderTextColor: Style.inkPlaceholder
    font.family: Style.fontFamily
    font.letterSpacing: Style.tracking(Style.px(13))
    font.pixelSize: Style.px(13)
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

    Keys.onPressed: event => {
        if (textMenu && menu.isMenuKey(event)) {
            menu.openAtCaret();
            event.accepted = true;
        }
    }

    background: Rectangle {
        radius: Style.radiusSm
        color: Style.surfaceSunken
        border.width: 1
        border.color: control.error ? Style.danger : control.editingNow ? Style.accentBorderStrong : Style.borderField
        Behavior on border.color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    Label {
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        visible: control.text.length === 0 && control.placeholderText.length > 0 && control.preeditText.length === 0
        text: control.placeholderText
        size: 13
        wrapMode: Text.Wrap
        elide: Text.ElideNone
        color: Style.inkPlaceholder
    }
}
