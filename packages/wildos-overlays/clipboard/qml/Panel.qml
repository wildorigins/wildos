pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.taskmanager as TaskManager
import WildOS.UI as UI
import org.wildos.overlays
import org.wildos.shell as WildOS
import WildOS.Clipboard

// The clipboard and emoji panel (ClipboardEmoji.dc): 380 wide, the flyouts'
// glass and 22px corners, 12px inside, with Emoji, Symbols and Clipboard as
// pill tabs. It opens in the bottom right corner above the taskbar, where
// the flyouts open, and closes on Escape, on the same shortcut again, or
// when another window is clicked.
Window {
    id: panel

    readonly property bool clipboardTab: PanelApp.tab === "clipboard"
    property bool wasActive: false
    // The panel is out of the way for the moment an emoji takes to go into
    // the app, or has just come back. KWin gives the keyboard to the app
    // only once the panel is off the screen, whatever the panel asks for.
    property bool stepping: false

    // The app in front, for "· Files" under what it copied and to paste
    // into terminals with Ctrl+Shift+V.
    readonly property string activeApp: tasks.activeTask.valid
        ? String(tasks.data(tasks.activeTask, TaskManager.AbstractTasksModel.AppName) || "") : ""
    readonly property bool activeIsTerminal: {
        if (!tasks.activeTask.valid) {
            return false;
        }
        const id = String(tasks.data(tasks.activeTask, TaskManager.AbstractTasksModel.AppId) || "").toLowerCase();
        return id.indexOf("terminal") >= 0 || id.indexOf("konsole") >= 0 || id.indexOf("alacritty") >= 0 || id.indexOf("kitty") >= 0;
    }

    width: 380
    height: clipboardTab ? 560 : 460
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | (placement.wayland ? 0 : Qt.Tool)
    title: qsTr("Clipboard and emoji")
    visible: false

    TaskManager.TasksModel {
        id: tasks
        groupMode: TaskManager.TasksModel.GroupDisabled
    }

    OverlayPlacement {
        id: placement
        window: panel
        edges: OverlayPlacement.Bottom | OverlayPlacement.Right
        marginRight: 16
        // 14px above the taskbar, as the flyouts sit: the docked bar is
        // 52px tall, the floating pills 62px and 14px off the screen edge.
        fromScreenEdge: true
        marginBottom: (WildOS.TaskbarStyle.floating ? 76 : 52) + 14
        layer: OverlayPlacement.Overlay
        keyboard: OverlayPlacement.OnDemand
        // Coming back after an emoji went in, it is simply there again.
        slideFrom: UI.Style.reducedMotion || panel.stepping ? OverlayPlacement.NoEdge : OverlayPlacement.Bottom
        scope: "wildos-clipboard"
    }

    UI.WindowFrame {
        window: panel
        radius: UI.Style.radius2xl
        shadowColor: UI.Style.shadowFlyout
    }

    Connections {
        target: PanelApp
        function onShownChanged() {
            if (PanelApp.shown) {
                panel.wasActive = false;
                panel.visible = true;
                panel.requestActivate();
                pages.focusCurrent();
            } else {
                panel.visible = false;
            }
        }
    }

    onActiveChanged: {
        if (active) {
            wasActive = true;
        } else if (wasActive && visible) {
            PanelApp.shown = false;
        }
    }

    Connections {
        target: PanelApp
        function onTypingChanged() {
            if (PanelApp.typing) {
                panel.stepping = true;
                // A picture of the panel takes its place first, so it never
                // seems to blink, however fast emoji are picked.
                const grabbing = panel.contentItem.grabToImage(result => {
                    if (PanelApp.typing) {
                        standIn.show(result);
                    }
                });
                if (grabbing) {
                    standInLate.restart();
                } else {
                    panel.visible = false;
                }
            } else if (PanelApp.shown) {
                // Back where it was, search and place in the list included.
                panel.wasActive = false;
                panel.visible = true;
                panel.requestActivate();
                standIn.dropAfterPanel = true;
                settled.restart();
            } else {
                panel.stepping = false;
                standIn.drop();
            }
        }
    }
    Timer {
        id: settled
        interval: 400
        onTriggered: panel.stepping = false
    }

    onFrameSwapped: {
        if (standIn.dropAfterPanel) {
            standIn.dropAfterPanel = false;
            // The compositor shows the panel a frame after Qt has drawn it.
            dropLater.restart();
        }
    }
    // The app only gets the keyboard once the panel is gone, so it goes
    // in time even if its picture is slow to come.
    Timer {
        id: standInLate
        interval: 80
        onTriggered: panel.visible = false
    }
    Timer {
        id: dropLater
        interval: 50
        onTriggered: standIn.drop()
    }

    // Stands in for the panel while it is off the screen so the app can take
    // the keyboard. It never asks for the keyboard itself, like the volume
    // pop-up, so KWin hands it straight back to the app. Clicks on it go
    // nowhere, rather than into the app underneath.
    readonly property Window standIn: Window {
        id: standIn

        property var shot: null
        property bool hidePanel: false
        property bool dropAfterPanel: false

        function show(result): void {
            shot = result;
            shotImage.source = result.url;
            if (shotImage.status === Image.Ready) {
                reveal();
            }
        }
        function reveal(): void {
            hidePanel = true;
            visible = true;
        }
        function drop(): void {
            standInLate.stop();
            dropLater.stop();
            dropAfterPanel = false;
            hidePanel = false;
            visible = false;
            shotImage.source = "";
            shot = null;
        }

        width: panel.width
        height: panel.height
        color: "transparent"
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | (placement.wayland ? 0 : Qt.Tool)
        title: panel.title
        visible: false

        onFrameSwapped: {
            if (hidePanel) {
                hidePanel = false;
                standInLate.stop();
                panel.visible = false;
            }
        }

        OverlayPlacement {
            window: standIn
            edges: placement.edges
            marginRight: placement.marginRight
            fromScreenEdge: placement.fromScreenEdge
            marginBottom: placement.marginBottom
            layer: OverlayPlacement.Overlay
            keyboard: OverlayPlacement.NoKeyboard
            slideFrom: OverlayPlacement.NoEdge
            scope: "wildos-clipboard-standin"
        }

        UI.WindowFrame {
            window: standIn
            radius: UI.Style.radius2xl
            shadowColor: UI.Style.shadowFlyout
        }

        Image {
            id: shotImage
            anchors.fill: parent
            cache: false
            smooth: true
            onStatusChanged: {
                if (status === Image.Ready && standIn.shot && !standIn.visible) {
                    standIn.reveal();
                }
            }
        }

        MouseArea {
            anchors.fill: parent
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radius2xl
        color: UI.Style.glassPanel
        border.width: 1
        border.color: UI.Style.borderGlass
    }

    Item {
        id: pages
        anchors.fill: parent
        anchors.margins: 12
        focus: true

        function focusCurrent(): void {
            if (PanelApp.tab === "clipboard") {
                clipboardPage.focusList();
            } else if (PanelApp.tab === "emoji") {
                emojiPage.focusSearch();
            } else {
                symbolsPage.focusSearch();
            }
        }

        Keys.onEscapePressed: PanelApp.shown = false

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Row {
                id: tabs
                Layout.fillWidth: true
                spacing: 4

                UI.SegmentButton {
                    text: qsTr("Emoji")
                    checked: PanelApp.tab === "emoji"
                    onClicked: {
                        PanelApp.tab = "emoji";
                        pages.focusCurrent();
                    }
                }
                UI.SegmentButton {
                    text: qsTr("Symbols")
                    checked: PanelApp.tab === "symbols"
                    onClicked: {
                        PanelApp.tab = "symbols";
                        pages.focusCurrent();
                    }
                }
                UI.SegmentButton {
                    text: qsTr("Clipboard")
                    checked: PanelApp.tab === "clipboard"
                    onClicked: {
                        PanelApp.tab = "clipboard";
                        pages.focusCurrent();
                    }
                }
            }

            ClipboardPage {
                id: clipboardPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: PanelApp.tab === "clipboard"
                activeApp: panel.activeApp
                terminal: panel.activeIsTerminal
            }

            // The emoji and symbol pages read the whole emoji list, so each
            // is made the first time its tab opens and then kept. The
            // clipboard page stays loaded: its history records all session.
            Loader {
                id: emojiPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: PanelApp.tab === "emoji"
                active: visible || status === Loader.Ready
                sourceComponent: GridPage {
                    emoji: true
                    terminal: panel.activeIsTerminal
                }
                function focusSearch(): void {
                    if (item)
                        item.focusSearch();
                }
            }

            Loader {
                id: symbolsPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: PanelApp.tab === "symbols"
                active: visible || status === Loader.Ready
                sourceComponent: GridPage {
                    emoji: false
                    terminal: panel.activeIsTerminal
                }
                function focusSearch(): void {
                    if (item)
                        item.focusSearch();
                }
            }
        }
    }
}
