pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.Remote
import WildOS.UI as UI

// A remote desktop's window. Full screen (RemoteSession.dc), the toolbar
// waits at the top edge: latency, the Keys, Clipboard, Sharing and Displays
// menus, back to a window, minimise, disconnect and a pin to keep it. In a
// window (RemoteWindowed.dc) the same tools sit in the title bar. A dropped
// connection blurs the picture under "Connection lost" while it reconnects.
UI.AppWindow {
    id: window

    property var desktop
    property rect sourceRect: Qt.rect(0, 0, 0, 0)
    // With all monitors, the screen this window fills (its connector name).
    property string targetScreen
    // With all monitors, the first window carries the toolbar's choices.
    property bool primary: true
    property bool pinned: false
    property bool toolbarShown: pinned || hoverTop || keysMenu.opened || clipboardMenu.opened || sharingMenu.opened || displaysMenu.opened || summoned
    property bool hoverTop: false
    property bool summoned: false
    readonly property bool full: desktop && desktop.display !== "window"
    readonly property bool lost: desktop && desktop.state === "reconnecting"
    readonly property bool ended: desktop && desktop.state === "ended"

    title: desktop ? qsTr("%1, Remote").arg(desktop.name) : qsTr("Remote")
    color: full ? "#000000" : "transparent"
    rememberSize: false
    // Full screen, a window is its screen's size. Sized from the remote
    // desktop it grew, once connected, to the span of every monitor with All
    // my monitors, and showed its part of the picture shrunk inside it.
    width: full ? Screen.width : desktop && desktop.desktopSize.width > 0 ? Math.min(desktop.desktopSize.width / Screen.devicePixelRatio, Screen.desktopAvailableWidth - 40) : 1280
    height: full ? Screen.height : desktop && desktop.desktopSize.height > 0 ? Math.min(desktop.desktopSize.height / Screen.devicePixelRatio + 40, Screen.desktopAvailableHeight - 40) : 800
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    deep: true

    Component.onCompleted: {
        if (full) {
            showFullScreen();
            if (targetScreen.length > 0) {
                App.fullScreenOn(window, targetScreen);
            }
            hint.show();
        }
        view.forceActiveFocus();
    }
    onFullChanged: {
        // A menu opened from the toolbar would float where it no longer is.
        keysMenu.close();
        clipboardMenu.close();
        sharingMenu.close();
        displaysMenu.close();
        if (full) {
            showFullScreen();
            if (targetScreen.length > 0) {
                App.fullScreenOn(window, targetScreen);
            }
        } else {
            showNormal();
        }
    }
    onClosing: {
        if (desktop && desktop.state !== "ended") {
            desktop.disconnectNow();
        }
    }

    Connections {
        target: window.desktop
        function onStateChanged() {
            if (window.desktop.state === "ended" && window.desktop.endTitle.length === 0) {
                // Ended here (Disconnect, closing): nothing to say.
                window.close();
            }
        }
        function onLost() {
            if (!window.active && window.primary) {
                Notify.send(window.desktop.endTitle, window.desktop.endText, qsTr("Reconnect"), () => window.reconnect());
            }
        }
    }

    function openMenu(name) {
        const menus = { keys: [keysMenu, keysButton], clipboard: [clipboardMenu, clipButton], sharing: [sharingMenu, shareButton], displays: [displaysMenu, displayButton] };
        menus[name][0].openAt(menus[name][1]);
    }

    function reconnect() {
        const h = Hosts.host(desktop.hostId);
        window.close();
        if (h.id) {
            Desktops.start(h, desktop.display);
        }
    }

    ShortcutInhibitor {
        window: window
        enabled: window.desktop !== undefined && window.desktop.sendShortcuts && !window.lost
    }

    // The windowed title bar (RemoteWindowed.dc).
    Item {
        id: titleBar
        visible: !window.full
        width: parent.width
        height: 40

        MouseArea {
            anchors.fill: parent
            property point origin
            onPressed: m => origin = Qt.point(m.x, m.y)
            onPositionChanged: m => {
                if (pressed && Math.abs(m.x - origin.x) + Math.abs(m.y - origin.y) > 4) {
                    window.startSystemMove();
                }
            }
            onDoubleClicked: window.frame.toggleMaximised()
        }
        Row {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                source: "qrc:/qt/qml/WildOS/Remote/data/org.wildos.remote.svg"
                sourceSize: Qt.size(36, 36)
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: window.desktop ? window.desktop.name : ""
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Remote")
                size: 14
                color: UI.Style.inkSubtle
            }
            Latency {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 8
            }
        }
        Row {
            anchors.right: windowButtons.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            ToolIcon { id: keysIcon; glyph: RemoteIcons.keyboard; tip: qsTr("Keys"); onClicked: keysMenu.openAt(keysIcon) }
            ToolIcon { id: clipIcon; glyph: RemoteIcons.clipboard; tip: qsTr("Clipboard"); onClicked: clipboardMenu.openAt(clipIcon) }
            ToolIcon { id: shareIcon; glyph: RemoteIcons.folder; tip: qsTr("Sharing"); onClicked: sharingMenu.openAt(shareIcon) }
            ToolIcon { id: displayIcon; glyph: RemoteIcons.displays; tip: qsTr("Displays"); onClicked: displaysMenu.openAt(displayIcon) }
            Rectangle { width: 1; height: 20; anchors.verticalCenter: parent.verticalCenter; color: UI.Style.divider }
        }
        UI.WindowButtons {
            id: windowButtons
            anchors.right: parent.right
            buttonHeight: 40
            frame: window.frame
        }
    }

    // The desktop itself.
    Rectangle {
        id: stage
        y: window.full ? 0 : titleBar.height
        width: parent.width
        height: parent.height - y
        color: window.full ? "#000000" : "#121317"
        bottomLeftRadius: window.full || window.maximised ? 0 : UI.Style.windowRadius
        bottomRightRadius: bottomLeftRadius
        clip: true

        DesktopView {
            id: view
            anchors.fill: parent
            desktop: window.desktop
            sourceRect: window.sourceRect
            blurred: window.lost || window.ended
            focus: true
            onEscapeRequested: {
                window.desktop.sendShortcuts = false;
                window.summoned = true;
                summonTimer.restart();
            }
            onPointerMoved: (x, y) => window.hoverTop = window.full && (y < 4 || (window.hoverTop && y < 64))
        }

        // Asks Windows for the new size as the window changes, once it
        // settles.
        Timer {
            id: resizeTimer
            interval: 400
            onTriggered: {
                if (window.desktop && window.desktop.session && window.primary) {
                    Desktops.resizeTo(window.desktop, Qt.size(stage.width * Screen.devicePixelRatio, stage.height * Screen.devicePixelRatio), Screen.devicePixelRatio);
                }
            }
        }
        onWidthChanged: if (!window.full) resizeTimer.restart()
        onHeightChanged: if (!window.full) resizeTimer.restart()

        // Connection lost (RemoteWindowed.dc).
        Rectangle {
            visible: window.lost || (window.ended && window.desktop.endTitle.length > 0)
            anchors.centerIn: parent
            width: Math.min(450, parent.width - 40)
            height: lostColumn.implicitHeight + 56
            radius: 16
            color: UI.Style.glassDialog
            border.width: 1
            border.color: UI.Style.borderDialog
            ColumnLayout {
                id: lostColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 28
                spacing: 12
                UI.Spinner {
                    visible: window.lost
                    Layout.alignment: Qt.AlignHCenter
                    size: 36
                    running: window.lost
                }
                UI.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: window.lost ? qsTr("Connection lost") : window.desktop ? window.desktop.endTitle : ""
                    size: 18
                    font.weight: UI.Style.weightBold
                }
                UI.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: window.lost
                        ? qsTr("Reconnecting to %1, attempt %2 of %3. Windows keeps your session running while you're away, so your apps stay open.")
                            .arg(window.desktop.name).arg(window.desktop.attempt).arg(window.desktop.attempts)
                        : window.desktop ? window.desktop.endText : ""
                    size: 14
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    lineHeight: 1.35
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    spacing: 10
                    UI.Button {
                        text: window.lost ? qsTr("Stop trying") : qsTr("Close")
                        onClicked: window.lost ? window.desktop.stopTrying() : window.close()
                    }
                    UI.Button {
                        kind: "primary"
                        text: window.lost ? qsTr("Try now") : qsTr("Reconnect")
                        onClicked: window.lost ? window.desktop.tryNow() : window.reconnect()
                    }
                }
            }
        }
    }

    // The full screen toolbar (RemoteSession.dc).
    Rectangle {
        id: toolbar
        visible: window.full && window.primary
        anchors.horizontalCenter: parent.horizontalCenter
        y: window.toolbarShown ? 0 : -height - 2
        width: toolRow.implicitWidth + 24
        height: 48
        bottomLeftRadius: 14
        bottomRightRadius: 14
        color: UI.Style.glassOnWallpaper
        border.width: 1
        border.color: UI.Style.borderOnWallpaper
        z: 10
        Behavior on y {
            NumberAnimation { duration: UI.Style.motionNormal; easing.type: UI.Style.easeEnter }
        }
        HoverHandler {
            onHoveredChanged: if (hovered) window.hoverTop = true; else if (!window.pinned) leaveTimer.restart()
        }
        Timer {
            id: leaveTimer
            interval: 600
            onTriggered: window.hoverTop = false
        }

        RowLayout {
            id: toolRow
            anchors.centerIn: parent
            spacing: 4
            Image {
                Layout.leftMargin: 4
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: "qrc:/qt/qml/WildOS/Remote/data/org.wildos.remote.svg"
                sourceSize: Qt.size(36, 36)
            }
            UI.Label {
                Layout.leftMargin: 6
                text: window.desktop ? window.desktop.name : ""
                size: 14
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkOnWallpaper
            }
            Latency { onPicture: true; Layout.leftMargin: 8; Layout.rightMargin: 8 }
            Divider {}
            ToolText { id: keysButton; glyph: RemoteIcons.keyboard; text: qsTr("Keys"); on: keysMenu.opened; onClicked: keysMenu.openAt(keysButton) }
            ToolText { id: clipButton; glyph: RemoteIcons.clipboard; text: qsTr("Clipboard"); on: clipboardMenu.opened; onClicked: clipboardMenu.openAt(clipButton) }
            ToolText { id: shareButton; glyph: RemoteIcons.folder; text: qsTr("Sharing"); on: sharingMenu.opened; onClicked: sharingMenu.openAt(shareButton) }
            ToolText { id: displayButton; glyph: RemoteIcons.displays; text: qsTr("Displays"); on: displaysMenu.opened; onClicked: displaysMenu.openAt(displayButton) }
            Divider {}
            ToolText { glyph: RemoteIcons.exitFull; text: qsTr("Window"); onClicked: window.desktop.display = "window" }
            ToolText { iconName: "minus"; text: qsTr("Minimise"); onClicked: window.showMinimized() }
            ToolText {
                glyph: RemoteIcons.power
                text: qsTr("Disconnect")
                colour: "#ff9a8a"
                onClicked: window.desktop.disconnectNow()
            }
            Divider {}
            UI.IconButton {
                width: 32
                height: 32
                iconName: window.pinned ? "unpin" : "pin"
                iconSize: 15
                iconColor: window.pinned ? UI.Style.systemAccent : UI.Style.inkOnWallpaper
                tip: window.pinned ? qsTr("Let the toolbar hide") : qsTr("Keep the toolbar showing")
                focusPolicy: Qt.NoFocus
                onClicked: window.pinned = !window.pinned
            }
        }
    }

    Timer {
        id: summonTimer
        interval: 4000
        onTriggered: window.summoned = false
    }

    // How to get out of full screen, said once.
    Rectangle {
        id: hint
        visible: opacity > 0
        opacity: 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: hintRow.implicitWidth + 32
        height: 36
        radius: 10
        color: UI.Style.glassOnWallpaper
        z: 10
        function show() {
            opacity = 1;
            hintTimer.restart();
        }
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionSlow }
        }
        Timer {
            id: hintTimer
            interval: 6000
            onTriggered: hint.opacity = 0
        }
        Row {
            id: hintRow
            anchors.centerIn: parent
            spacing: 10
            UI.Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "monitor"
                size: 14
                color: UI.Style.inkOnWallpaper
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Full screen. Move to the top edge for the toolbar, or press Ctrl+Alt+Home")
                size: 13
                color: UI.Style.inkOnWallpaper
            }
        }
    }

    // Menus.
    ToolMenu {
        id: keysMenu
        MenuRow { text: qsTr("Send Ctrl+Alt+Del"); shortcut: "Ctrl+Alt+End"; onClicked: window.desktop.sendKeys("ctrl-alt-del") }
        MenuRow { text: qsTr("Send the Windows key"); onClicked: window.desktop.sendKeys("windows") }
        MenuRow { text: qsTr("Send Print Screen"); onClicked: window.desktop.sendKeys("print") }
        MenuRow { text: qsTr("Switch app on the remote PC"); shortcut: "Alt+Tab"; onClicked: window.desktop.sendKeys("alt-tab") }
        MenuLine {}
        MenuToggle {
            text: qsTr("Send all shortcuts there")
            detail: qsTr("Alt+Tab, Super and the rest go to the remote PC while it's in front. Ctrl+Alt+Home always comes back here.")
            checked: window.desktop ? window.desktop.sendShortcuts : false
            onToggled: on => window.desktop.sendShortcuts = on
        }
    }
    ToolMenu {
        id: clipboardMenu
        MenuToggle {
            text: qsTr("Share the clipboard")
            detail: qsTr("Copy here, paste there and the other way round: text and pictures.")
            checked: window.desktop ? window.desktop.sharing.clipboard === true : false
            onToggled: on => window.desktop.setSharing("clipboard", on)
        }
        Reconnect {}
    }
    ToolMenu {
        id: sharingMenu
        MenuToggle {
            text: qsTr("Sound plays here")
            checked: window.desktop ? window.desktop.sharing.sound === true : false
            onToggled: on => window.desktop.setSharing("sound", on)
        }
        MenuToggle {
            text: qsTr("Microphone")
            checked: window.desktop ? window.desktop.sharing.microphone === true : false
            onToggled: on => window.desktop.setSharing("microphone", on)
        }
        MenuToggle {
            text: qsTr("Printers")
            checked: window.desktop ? window.desktop.sharing.printers === true : false
            onToggled: on => window.desktop.setSharing("printers", on)
        }
        MenuToggle {
            text: qsTr("Folders")
            detail: qsTr("The folder chosen for this PC, which the remote PC can change")
            checked: window.desktop ? window.desktop.sharing.folders === true : false
            onToggled: on => window.desktop.setSharing("folders", on)
        }
        Reconnect {}
    }
    ToolMenu {
        id: displaysMenu
        MenuRow {
            text: qsTr("Full screen on this monitor")
            ticked: window.desktop && window.desktop.display === "full"
            onClicked: window.desktop.display = "full"
        }
        MenuRow {
            text: qsTr("In a window")
            ticked: window.desktop && window.desktop.display === "window"
            onClicked: window.desktop.display = "window"
        }
        MenuRow {
            visible: Qt.application.screens.length > 1
            text: qsTr("All my monitors")
            detail: qsTr("Reconnects to lay the desktop out across them")
            ticked: window.desktop && window.desktop.multiMonitor
            onClicked: {
                const h = Hosts.host(window.desktop.hostId);
                window.close();
                if (h.id) {
                    Desktops.start(h, "all");
                }
            }
        }
    }

    component Latency: Row {
        spacing: 6
        // On the full screen toolbar it sits on dark glass whatever the
        // theme, so it keeps the pale colours there.
        property bool onPicture: false
        readonly property color colour: down ? (onPicture ? "#ff9a8a" : UI.Style.danger) : (onPicture ? "#5fd39a" : UI.Style.success)
        readonly property bool down: window.lost
        visible: window.desktop !== undefined && (down || window.desktop.latency > 0)
        UI.Glyph {
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            path: RemoteIcons.signal
            color: parent.colour
        }
        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.down ? qsTr("No connection") : qsTr("%1 ms").arg(window.desktop ? window.desktop.latency : 0)
            size: 13
            color: parent.colour
        }
    }
    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 22
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        color: UI.Style.borderOnWallpaper
    }
    component ToolIcon: UI.IconButton {
        width: 32
        height: 30
        iconSize: 16
        iconColor: UI.Style.iconMuted
        focusPolicy: Qt.NoFocus
    }
    component ToolText: Rectangle {
        id: tool
        property string text
        property string glyph
        property string iconName
        property bool on: false
        property color colour: UI.Style.inkOnWallpaper
        signal clicked()
        implicitWidth: toolLabelRow.implicitWidth + 20
        implicitHeight: 34
        radius: 8
        color: on ? UI.Style.controlOnWallpaperSelected : toolMouse.containsMouse ? UI.Style.controlOnWallpaperHover : "transparent"
        Accessible.role: Accessible.Button
        Accessible.name: text
        Row {
            id: toolLabelRow
            anchors.centerIn: parent
            spacing: 8
            UI.Glyph {
                visible: tool.glyph.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 15
                height: 15
                path: tool.glyph
                color: tool.colour
            }
            UI.Icon {
                visible: tool.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: tool.iconName
                size: 15
                color: tool.colour
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: tool.text
                size: 14
                font.weight: UI.Style.weightSemiBold
                color: tool.colour
            }
        }
        MouseArea {
            id: toolMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: tool.clicked()
        }
    }
    component ToolMenu: T.Popup {
        id: menu
        default property alias rows: menuColumn.data
        function openAt(item) {
            const p = item.mapToItem(null, 0, item.height + 6);
            x = Math.min(p.x, window.width - width - 8);
            y = p.y;
            open();
        }
        parent: T.Overlay.overlay
        width: 314
        padding: 6
        implicitHeight: menuColumn.implicitHeight + 12
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
        onClosed: view.forceActiveFocus()
        background: Rectangle {
            radius: 12
            color: UI.Style.glassMenu
            border.width: 1
            border.color: UI.Style.borderMenu
        }
        contentItem: Column {
            id: menuColumn
            spacing: 0
        }
    }
    component MenuRow: Rectangle {
        id: row
        property string text
        property string shortcut
        property string detail
        property bool ticked: false
        signal clicked()
        width: 302
        height: detail.length > 0 ? 52 : 36
        radius: 7
        color: rowMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
        UI.Icon {
            visible: row.ticked
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            name: "check"
            size: 14
            color: UI.Style.iconInk
        }
        Column {
            x: 12 + (row.ticked ? 20 : 0)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            UI.Label { text: row.text; size: 14 }
            UI.Label { visible: row.detail.length > 0; text: row.detail; size: 12; color: UI.Style.inkMuted }
        }
        UI.Label {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: row.shortcut
            size: 12
            color: UI.Style.inkSubtle
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                row.clicked();
                keysMenu.close();
                displaysMenu.close();
            }
        }
    }
    component MenuLine: Rectangle {
        width: 290
        x: 6
        height: 1
        color: UI.Style.divider
    }
    component MenuToggle: Item {
        id: toggleRow
        property string text
        property string detail
        property bool checked
        signal toggled(bool on)
        width: 302
        height: Math.max(44, toggleText.implicitHeight + 20)
        Column {
            id: toggleText
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 80
            spacing: 3
            UI.Label { width: parent.width; text: toggleRow.text; size: 14; font.weight: UI.Style.weightSemiBold }
            UI.Label {
                visible: toggleRow.detail.length > 0
                width: parent.width
                text: toggleRow.detail
                size: 12
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
        }
        UI.Toggle {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            checked: toggleRow.checked
            focusPolicy: Qt.NoFocus
            Accessible.name: toggleRow.text
            onToggled: toggleRow.toggled(checked)
        }
    }
    component Reconnect: Column {
        visible: window.desktop !== undefined && window.desktop.sharingChanged
        width: 302
        topPadding: 6
        spacing: 8
        Rectangle { width: 290; x: 6; height: 1; color: UI.Style.divider }
        UI.Label {
            x: 12
            width: 278
            text: qsTr("Remote Desktop settles what's shared as it connects, so this takes effect when you reconnect.")
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
        UI.Button {
            x: 12
            size: "small"
            text: qsTr("Reconnect now")
            onClicked: window.reconnect()
        }
    }
}
