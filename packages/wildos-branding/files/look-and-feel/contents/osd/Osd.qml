import QtQuick
import QtQuick.Window
import WildOS.UI as UI
import org.wildos.overlays
import org.wildos.shell as WildOS

// The on-screen display for the volume, brightness and other special keys
// (Overlays.dc): a slim 300 by 52 pill, radius 26, centred 28px above the
// taskbar (104px from the bottom of the screen over the floating taskbar the
// board draws, 80px over the docked one). A 20px glyph, the level as a
// 4px accent bar and the number at the right; switches such as Wi-Fi or a
// keyboard layout show a short line of words instead. It fades 1.5 seconds
// after the last key press and lets clicks through.
//
// Plasma loads this file for its OSD service (org.kde.osdService, called
// by the audio shortcuts, power management, KWin and the network and
// Bluetooth services) and sets the properties below.
Window {
    id: osd

    // How long Plasma keeps it up: the 1.5 second hold plus the fade.
    property int timeout: 1700
    property var osdValue: 0
    property int osdMaxValue: 100
    property string osdAdditionalText
    property string icon
    property bool showingProgress: false

    readonly property int level: showingProgress ? Math.round(Number(osdValue) || 0) : 0

    width: Math.max(300, words.visible ? words.implicitWidth + 70 : 300)
    height: 52
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
        | (placement.wayland ? 0 : Qt.ToolTip)
    title: words.visible ? words.text : qsTr("Level %1").arg(level)
    visible: false

    OverlayPlacement {
        id: placement
        window: osd
        edges: OverlayPlacement.Bottom
        fromScreenEdge: true
        marginBottom: (WildOS.TaskbarStyle.floating ? 76 : 52) + 28
        layer: OverlayPlacement.Overlay
        keyboard: OverlayPlacement.NoKeyboard
        // Rises from the bottom edge, as the Windows volume flyout does.
        slideFrom: UI.Style.reducedMotion ? OverlayPlacement.NoEdge : OverlayPlacement.Bottom
        scope: "wildos-osd"
    }

    UI.WindowFrame {
        id: frame
        window: osd
        radius: 26
        shadowColor: UI.Style.shadowWindow
    }

    // Plasma's own words for its switches are title case and say "Wifi";
    // these are the Wild OS ones.
    function wordsFor(icon: string, text: string): string {
        const known = {
            "audio-volume-muted": qsTr("Muted"),
            "microphone-sensitivity-muted": qsTr("Microphone muted"),
            "input-touchpad-on": qsTr("Touchpad on"),
            "input-touchpad-off": qsTr("Touchpad off"),
            "network-wireless-on": qsTr("Wi-Fi on"),
            "network-wireless-off": qsTr("Wi-Fi off"),
            "preferences-system-bluetooth": qsTr("Bluetooth on"),
            "preferences-system-bluetooth-inactive": qsTr("Bluetooth off"),
            "network-mobile-on": qsTr("Mobile data on"),
            "network-mobile-off": qsTr("Mobile data off"),
            "input-keyboard-virtual-on": qsTr("On-screen keyboard on"),
            "input-keyboard-virtual-off": qsTr("On-screen keyboard off"),
            "system-suspend-inhibited": qsTr("Sleep and locking paused"),
            "system-suspend-uninhibited": qsTr("Sleep and locking back on"),
            "battery-profile-powersave": qsTr("Power saver"),
            "speedometer": qsTr("Balanced"),
            "battery-profile-performance": qsTr("Best performance")
        };
        return known[icon] || String(text || "");
    }

    // The design's glyphs on the 24px grid; speaker waves follow the level.
    function glyphFor(icon: string, level: int): string {
        const speaker = "M4 9.5h4l5-4v13l-5-4H4z";
        if (icon.indexOf("audio-volume") === 0 || icon === "") {
            if (icon === "audio-volume-muted") {
                return speaker + "M16.5 9.5l5 5M21.5 9.5l-5 5";
            }
            if (level <= 33) {
                return speaker + "M16.5 9a4.5 4.5 0 0 1 0 6";
            }
            return speaker + "M16.5 9a4.5 4.5 0 0 1 0 6M19 6.5a8 8 0 0 1 0 11";
        }
        return "";
    }

    readonly property string iconName: {
        if (icon.indexOf("brightness") >= 0) {
            return icon.indexOf("keyboard") >= 0 ? "keyboard" : "sun";
        }
        if (icon.indexOf("microphone") === 0) {
            return "mic";
        }
        if (icon.indexOf("touchpad") >= 0) {
            return "touchpad";
        }
        if (icon.indexOf("wireless") >= 0) {
            return icon.indexOf("off") >= 0 ? "wifi-off" : "wifi";
        }
        if (icon.indexOf("bluetooth") >= 0) {
            return "bluetooth";
        }
        if (icon.indexOf("keyboard") >= 0) {
            return "keyboard";
        }
        if (icon.indexOf("battery") === 0 || icon === "speedometer") {
            return "bolt";
        }
        if (icon.indexOf("suspend") >= 0) {
            return "moon";
        }
        if (icon.indexOf("network-mobile") === 0) {
            return "phone";
        }
        if (icon.indexOf("audio-volume") === 0 || icon === "") {
            return "";
        }
        // A media player's own volume: a speaker stands for it.
        return showingProgress ? "" : "info";
    }

    function wake(): void {
        if (!visible) {
            return;
        }
        fade.stop();
        if (card.opacity < 1 && !appear.running) {
            appear.restart();
        }
        frame.blur = true;
        hold.restart();
    }

    onVisibleChanged: {
        if (visible) {
            card.opacity = 0;
        }
        wake();
    }
    onOsdValueChanged: wake()
    onIconChanged: wake()
    onShowingProgressChanged: wake()

    Timer {
        id: hold
        interval: 1500
        onTriggered: {
            if (!UI.Style.reducedMotion) {
                // The compositor's blur cannot fade, so it goes first.
                frame.blur = false;
                fade.restart();
            }
        }
    }

    NumberAnimation {
        id: appear
        target: card
        property: "opacity"
        to: 1
        duration: UI.Style.motionFast
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id: fade
        target: card
        property: "opacity"
        to: 0
        duration: UI.Style.motionNormal
        easing.type: UI.Style.easeExit
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 26
        color: UI.Style.glassFloat
        border.width: 1
        border.color: UI.Style.borderGlass

        Row {
            id: row
            x: 18
            width: parent.width - 36
            height: parent.height
            spacing: 14

            UI.Glyph {
                anchors.verticalCenter: parent.verticalCenter
                visible: osd.iconName === ""
                width: 20
                height: 20
                strokeWidth: 2
                path: osd.glyphFor(osd.icon, osd.level)
                color: UI.Style.iconInk
            }

            UI.Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: osd.iconName !== ""
                name: osd.iconName
                size: 20
                color: UI.Style.iconInk
            }

            // The level.
            Item {
                anchors.verticalCenter: parent.verticalCenter
                visible: osd.showingProgress
                width: row.width - 20 - 26 - 2 * row.spacing
                height: 4

                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: UI.Style.borderControl
                }

                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, osd.level / Math.max(1, osd.osdMaxValue)))
                    height: parent.height
                    radius: 2
                    color: UI.Style.accent
                }
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: osd.showingProgress
                width: 26
                horizontalAlignment: Text.AlignRight
                size: 13
                font.features: { "tnum": 1 }
                text: osd.level
                // Past 100% the sound may distort; the number says so.
                color: osd.level > 100 ? UI.Style.warning : UI.Style.ink
            }

            UI.Label {
                id: words
                anchors.verticalCenter: parent.verticalCenter
                visible: !osd.showingProgress
                size: 13
                text: osd.wordsFor(osd.icon, osd.osdValue)
            }
        }
    }
}
