import QtQuick
import QtQuick.Templates as T

// A tab in a title bar, as in Windows 11 File Explorer (Main.dc): 34px
// tall and up to 220 wide, rounded 10px on top, the current tab a 0.07
// sheet joined to the bar below, others muted with a soft hover. A 16px
// icon, the title in 13px (500 when current) and a 24px close button.
// Middle click closes; holding a dragged file over a tab for half a second
// switches to it. A title too long for the tab shows in full in a tooltip.
// A right click or the Menu key emits contextMenuRequested, here with the
// place in the tab and on a WildOS.UI TabBar with the tab's index. From
// wildos-files TitleBar.
T.TabButton {
    id: control

    property string iconName
    property string themeIcon
    property bool closable: true
    property bool previewHover: false

    signal closeRequested()
    signal contextMenuRequested(real x, real y)

    function requestContextMenu(x: real, y: real): void {
        contextMenuRequested(x, y);
        const bar = T.TabBar.tabBar;
        if (bar && typeof bar.contextMenuRequested === "function") {
            const place = mapToItem(bar, x, y);
            bar.contextMenuRequested(T.TabBar.index, place.x, place.y);
        }
    }

    Keys.onMenuPressed: requestContextMenu(width / 2, height)

    readonly property bool lit: (hovered || previewHover) && !checked

    width: T.TabBar.tabBar && T.TabBar.tabBar.tabWidth > 0 ? T.TabBar.tabBar.tabWidth : implicitWidth
    implicitWidth: 220
    implicitHeight: 34
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text

    background: Item {
        Rectangle {
            anchors.fill: parent
            visible: control.checked
            topLeftRadius: 10
            topRightRadius: 10
            color: Style.tone(0.07)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            anchors.bottomMargin: 4
            visible: !control.checked
            radius: Style.radiusSm
            color: control.lit ? Style.tone(0.05) : "transparent"
            Behavior on color {
                ColorAnimation { duration: Style.hoverDuration }
            }
        }

        FocusRing {
            visible: control.visualFocus
            anchors.margins: -2
            targetRadius: 10
        }
    }

    contentItem: Item {
        ThemeIcon {
            id: theme
            visible: control.themeIcon.length > 0
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            name: control.themeIcon
        }

        Icon {
            visible: control.themeIcon.length === 0 && control.iconName.length > 0
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 16
        }

        Label {
            id: title
            x: 37
            width: (close.visible ? close.x - 4 : parent.width - 8) - x
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font.weight: control.checked ? Style.weightMedium : Style.weightRegular
            color: control.checked ? Style.ink : Style.inkMuted
        }

        IconButton {
            id: close
            visible: control.closable && (control.checked || control.hovered || control.width >= 140)
            x: parent.width - width - 6
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            radius: Style.radiusXs
            // The window frame's close glyph, as Files' approved tabs draw it;
            // the general "close" glyph reads too small at 10px.
            iconName: "win-close"
            iconSize: 10
            stroke: 3
            tip: qsTr("Close tab")
            focusPolicy: Qt.NoFocus
            onClicked: control.closeRequested()
        }
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        onTapped: if (control.closable) control.closeRequested()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => control.requestContextMenu(point.position.x, point.position.y)
    }

    Tooltip {
        text: control.text
        visible: control.hovered && title.truncated && !close.hovered && !control.down
    }

    DropArea {
        anchors.fill: parent
        onEntered: switchTimer.restart()
        onExited: switchTimer.stop()

        Timer {
            id: switchTimer
            interval: 500
            onTriggered: control.checked = true
        }
    }
}
