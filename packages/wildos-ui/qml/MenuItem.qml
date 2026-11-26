import QtQuick
import QtQuick.Templates as T

// One row of a Wild OS menu (MenusShell.dc, MenusApps.dc): 34px, radius 7,
// a 16px line icon (or theme icon) centred in an 18px column 10px in, 13px
// text 10px after it, the shortcut in 12px muted text on the right, a
// chevron for a submenu. A ticked row with no icon shows its tick in the
// icon column, as Windows 11 does; with an icon the tick moves to the
// right, where it can no longer collide with a shortcut (wildos-files Menu
// drew both at the same anchor).
T.MenuItem {
    id: control

    property string iconName
    property string themeIcon
    // What a model hands over instead of an icon name: a QIcon (Plasma's
    // task and jump list models), a path or URL.
    property var iconSource
    property string shortcutText
    property bool danger: false
    // The row that does what a click on the thing itself would do (Open,
    // the app's own name in a taskbar jump list) is drawn in 600.
    property bool bold: false
    // A chosen option in a group (Sort by, a layout) shows a dot, not a tick.
    property bool radio: false
    // Files' menus keep ticks and dots in a column of their own left of the
    // icons, as File Explorer does; Menu.markColumn turns it on.
    property bool markColumn: false
    property bool previewHover: false

    readonly property bool hasSource: iconSource !== undefined && iconSource !== null && iconSource !== ""
    readonly property bool hasIcon: iconName.length > 0 || themeIcon.length > 0 || hasSource
    readonly property real markWidth: markColumn ? 22 : 0
    readonly property bool tickRight: checked && hasIcon && !markColumn
    readonly property bool lit: (highlighted || previewHover) && enabled

    implicitWidth: 38 + markWidth + label.implicitWidth + (shortcutText.length > 0 ? 16 + shortcut.implicitWidth : 0)
        + (subMenu ? 24 : 0) + (tickRight ? 24 : 0) + 12
    implicitHeight: Math.max(Style.menuItemHeight, label.implicitHeight + 14)
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    Accessible.name: text

    background: Rectangle {
        radius: Style.radiusMenuRow
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.lit
            ColorAnimation { duration: 80 }
        }
        color: control.down ? Style.surfacePressed : control.lit ? Style.surfaceSelected : "transparent"
    }

    contentItem: Item {
        Icon {
            visible: control.iconName.length > 0 && control.themeIcon.length === 0 && !control.hasSource
            x: 11 + control.markWidth
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 16
            strokeWidth: 1.8
            color: control.danger ? Style.dangerMenu : Style.iconInk
        }

        // An app action without an icon of its own (a browser's "New
        // window") hands over an empty icon. With no fallback it stays
        // invalid and the row is left blank, rather than showing the
        // generic program icon.
        ThemeIcon {
            fallback: ""
            visible: (control.themeIcon.length > 0 || control.hasSource) && valid
            x: 11 + control.markWidth
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            name: control.themeIcon
            source: control.hasSource ? control.iconSource : undefined
        }

        Icon {
            visible: control.checked && !control.radio
            x: control.tickRight ? parent.width - 10 - size : control.markColumn ? 9 : 11
            anchors.verticalCenter: parent.verticalCenter
            name: "check"
            size: 14
            strokeWidth: 2.2
            color: Style.accentText
        }

        Rectangle {
            visible: control.checked && control.radio
            x: control.tickRight ? parent.width - 13 - width : control.markColumn ? 14 : 16
            anchors.verticalCenter: parent.verticalCenter
            width: 6
            height: 6
            radius: 3
            color: Style.accent
        }

        Label {
            id: label
            x: 38 + control.markWidth
            width: parent.width - x - (shortcut.visible ? shortcut.implicitWidth + 26 : 12) - (control.subMenu ? 20 : 0)
                - (control.tickRight ? 24 : 0)
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font.weight: control.bold ? Style.weightSemiBold : Style.weightRegular
            color: control.danger ? Style.dangerMenu : Style.ink
        }

        Label {
            id: shortcut
            visible: control.shortcutText.length > 0
            anchors.right: parent.right
            anchors.rightMargin: control.subMenu ? 28 : control.tickRight ? 34 : 10
            anchors.verticalCenter: parent.verticalCenter
            text: control.shortcutText
            size: 12
            color: Style.inkPlaceholder
        }
    }

    arrow: Icon {
        visible: control.subMenu
        x: control.width - width - 10
        y: (control.height - height) / 2
        name: "chevron"
        size: 12
        strokeWidth: 2
        color: Style.iconInk
    }

    indicator: Item {}
}
