import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI
import org.wildos.shell as WildOS

// The tray button from the design's taskbar: network, volume and battery
// glyphs together in one 44 px tall button with 12 px padding and gaps.
MouseArea {
    id: button

    required property var quick

    property bool wasExpanded: false

    readonly property bool showVolume: quick.audio !== null && quick.audio.available
    readonly property bool showBattery: quick.power !== null && quick.power.hasBattery

    Layout.minimumWidth: row.implicitWidth + 24
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: 44

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
    Accessible.role: Accessible.Button
    Accessible.name: "Network, volume and battery"
    Accessible.description: quick.tooltipLines.join(". ")

    // Remembering the state at press time stops a click that closes the popup
    // (it loses focus first) from opening it again straight away.
    onPressed: wasExpanded = quick.expanded
    onClicked: mouse => {
        if (mouse.button === Qt.MiddleButton) {
            quick.audio?.toggleMute();
        } else if (mouse.button === Qt.RightButton) {
            openMenu(mouse.x);
        } else {
            quick.expanded = !wasExpanded;
        }
    }

    // MenusShell.dc "Tray icon": the sound outputs and Sound settings. A left
    // click still opens quick settings. Wild OS has no per-app volume mixer
    // yet, so the board's Volume mixer row is left out.
    function openMenu(px: real): void {
        const entries = [];
        const outputs = showVolume ? quick.audio.outputs() : [];
        if (outputs.length > 0) {
            entries.push({ text: "Output", icon: "menu-output", submenuWidth: 220, submenu: outputs.map(output => ({
                text: output.name,
                checked: output.current,
                run: () => output.device.default = true
            })) });
            entries.push({ separator: true });
        }
        entries.push({ text: "Sound settings", icon: "menu-settings", run: () => quick.openSettings("sound") });
        menu.entries = entries;
        const scene = button.mapToItem(null, 0, 0);
        menu.x = Math.round(px - menu.implicitWidth / 2);
        menu.y = Math.round(-scene.y - 12 - menu.implicitHeight);
        menu.open();
    }

    UI.Menu {
        id: menu
        popupType: T.Popup.Window
        minimumWidth: 220
    }

    // Scrolling over the button changes the volume, as it does over the stock
    // volume icon.
    onWheel: wheel => {
        if (!showVolume) {
            return;
        }
        const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : -wheel.angleDelta.x;
        const step = Math.abs(delta) >= 120 ? 5 * Math.round(delta / 120) : delta / 24;
        quick.audio.setPercent(Math.max(0, Math.min(100, Math.round(quick.audio.percent + step))));
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: 44
        radius: 12
        color: button.pressed ? UI.Style.surfacePressed : quick.expanded ? UI.Style.surfaceSelected : button.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 12

        Item {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18

            WifiGlyph {
                anchors.fill: parent
                visible: button.quick.networkKind === "wifi"
                bars: button.quick.wifiBars
                opacity: button.quick.network && button.quick.network.connecting ? 0.6 : 1
            }
            UI.Glyph {
                anchors.fill: parent
                visible: button.quick.networkKind === "wired"
                path: Icons.ethernet
                color: UI.Style.iconInk
            }
            UI.Glyph {
                anchors.fill: parent
                visible: button.quick.networkKind === "flight"
                path: Icons.flight
                color: UI.Style.iconInk
            }
            // No connection: a globe struck through, as Windows 11 shows.
            UI.Glyph {
                anchors.fill: parent
                visible: button.quick.networkKind === "offline"
                path: Icons.globe + Icons.slash
                color: UI.Style.iconInk
                strokeWidth: 1.8
            }
        }

        // Always there, with a cross when there is nothing to play sound on.
        VolumeGlyph {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            percent: button.quick.audio ? button.quick.audio.percent : 0
            muted: !button.showVolume || button.quick.audio.muted
            color: UI.Style.iconInk
        }

        BatteryGlyph {
            visible: button.showBattery
            Layout.preferredWidth: 22
            Layout.preferredHeight: 18
            percent: button.quick.power ? button.quick.power.percent : 0
            // The bolt shows whenever the charger is in, as on Windows: a full
            // battery, or one held at a charge limit, isn't charging but is
            // still plugged in.
            charging: button.quick.power ? button.quick.power.pluggedIn : false
            low: button.quick.batteryLow
        }

        UI.Label {
            visible: button.showBattery && WildOS.TaskbarStyle.batteryPercent
            Layout.leftMargin: -4
            text: (button.quick.power ? button.quick.power.percent : 0) + "%"
            size: 12
            font.features: { "tnum": 1 }
            color: UI.Style.iconInk
        }
    }
}
