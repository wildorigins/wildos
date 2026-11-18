import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI
import org.wildos.shell as WildOS

// The taskbar item from board 4.13: the window-with-shield mark that
// Settings uses for Windows apps, its status dot, and two lines, "Windows
// mode" and the state. A 40 px button with the taskbar's 8 px corners, 10 px in on the
// left and 12 on the right. It sits at the left end of the bar in both
// styles and carries its own gap from the screen edge, so the bar needs no
// spacer before it: 8 px docked; 13 px floating, where its pill starts 5 px
// in, so with the bar's own 12 px margin the pill sits 17 px from the left
// edge, as the status pill does from the right, with 8 px inside. It takes no room at all
// until Windows mode is set up or when it is turned off.
MouseArea {
    id: chip

    required property var winmode

    readonly property int edge: WildOS.TaskbarStyle.floating ? 13 : 8
    property bool wasExpanded: false

    readonly property real fullWidth: edge + button.width
    Layout.minimumWidth: winmode.shown ? fullWidth : 0
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: winmode.shown ? 40 : 0
    // Plasma shows the compact representation itself whatever it says, so
    // hiding is no width and nothing drawn.
    enabled: winmode.shown

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Windows mode: %1").arg(winmode.stateText)
    Accessible.description: winmode.toolTipSubText

    // Remembering the state at press time stops a click that closes the popup
    // (it loses focus first) from opening it again straight away.
    onPressed: mouse => {
        if (mouse.button === Qt.LeftButton) {
            wasExpanded = winmode.expanded;
        }
    }
    onClicked: mouse => {
        if (mouse.button === Qt.RightButton) {
            openMenu(mouse.x);
        } else {
            winmode.expanded = !wasExpanded;
        }
    }

    // A right click gives the one thing the state calls for and the way to
    // its settings; everything else stays in the panel a left click opens.
    // It opens in a window of its own, above the taskbar, as the taskbar is
    // too short to hold it.
    function openMenu(px: real): void {
        const entries = [];
        const base = winmode.settingUp ? "" : winmode.baseLevel;
        const free = !winmode.busyAction;
        if (base === "off") {
            entries.push({ text: qsTr("Start Windows mode"), icon: "play", enabled: free, run: () => winmode.power("start") });
        } else if (base === "paused" || base === "saved") {
            entries.push({ text: qsTr("Resume"), icon: "play", enabled: free, run: () => winmode.power("resume") });
        } else if (base === "running" && winmode.system !== null && winmode.system.vm === "running") {
            entries.push({ text: qsTr("Pause"), icon: "media-pause", enabled: free, run: () => winmode.power("pause") });
        }
        if (entries.length > 0) {
            entries.push({ separator: true });
        }
        entries.push({ text: qsTr("Windows mode settings"), icon: "menu-settings", run: () => winmode.openSettings() });
        menu.entries = entries;
        const scene = chip.mapToItem(null, 0, 0);
        menu.x = Math.round(Math.max(chip.edge, px - menu.implicitWidth / 2));
        menu.y = Math.round(-scene.y - 12 - menu.implicitHeight);
        menu.open();
    }

    UI.Menu {
        id: menu
        popupType: T.Popup.Window
        minimumWidth: 220
    }

    Rectangle {
        id: button
        visible: chip.winmode.shown
        x: chip.edge
        anchors.verticalCenter: parent.verticalCenter
        width: row.implicitWidth + 22
        height: 40
        radius: UI.Style.radiusSm
        color: chip.pressed ? UI.Style.surfacePressed
            : chip.winmode.expanded || chip.containsMouse ? UI.Style.tone(0.12) : UI.Style.tone(0.07)
        border.width: 1
        border.color: chip.winmode.expanded ? UI.Style.tone(0.10) : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        RowLayout {
            id: row
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Item {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20

                UI.Icon {
                    anchors.fill: parent
                    name: "windowsapps"
                    size: 20
                    strokeWidth: 1.8
                    color: UI.Style.ink
                }

                // A ring of the bar's colour around the dot keeps it apart
                // from the mark's stroke underneath.
                Rectangle {
                    visible: chip.winmode.level !== "settingup"
                    x: 20 + 3 - width
                    y: 20 + 2 - height
                    width: 12
                    height: 12
                    radius: 6
                    color: Tones.bar

                    StatusDot {
                        anchors.centerIn: parent
                        level: chip.winmode.level
                    }
                }

                // Setting up (board 9.11): a 14 px ring that fills with
                // setup's progress, on a disc of the bar's colour 1.5 px
                // wider, 4 px out from the mark's corner.
                Rectangle {
                    visible: chip.winmode.level === "settingup"
                    x: 20 + 4 - width
                    y: 20 + 3 - height
                    width: 17
                    height: 17
                    radius: width / 2
                    color: Tones.bar

                    ProgressRing {
                        anchors.centerIn: parent
                        size: 14
                        thickness: 2.4
                        value: chip.winmode.setupPercent / 100
                    }
                }
            }

            ColumnLayout {
                spacing: 1

                UI.Label {
                    text: qsTr("Windows mode")
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                    lineHeight: 1.2
                }

                UI.Label {
                    text: chip.winmode.stateText
                    size: 11
                    lineHeight: 1.2
                    color: chip.winmode.level === "attention" ? Tones.dot("attention") : UI.Style.inkMuted
                }
            }
        }
    }
}
