import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// The taskbar button: a 16 px shield in a 32 px cell with a 32 by 40 hover,
// the same as the tray's icons and the Updates button, so the gaps between
// them all match. No label. The
// shield carries the state: accent with a tick when all is well, sand with
// "!" when something needs doing, and plain while the first check runs (so
// login never flashes a warning) or while updates have not been checked.
MouseArea {
    id: chip

    required property var security

    property bool wasExpanded: false

    // The panel puts about 5 px between widgets, where the tray's own icons
    // sit edge to edge. 27 px here keeps the icons 32 px apart, the same as
    // Wi-Fi, volume and battery; the hover stays the tray's full 32 px.
    Layout.minimumWidth: 27
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: 44

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Security: %1").arg(security.chipText.toLowerCase())
    Accessible.description: security.toolTipSubText

    // Remembering the state at press time stops a click that closes the popup
    // (it loses focus first) from opening it again straight away.
    onPressed: mouse => {
        if (mouse.button === Qt.LeftButton) {
            wasExpanded = security.expanded;
        }
    }
    onClicked: mouse => {
        if (mouse.button === Qt.RightButton) {
            openMenu(mouse.x);
        } else {
            security.expanded = !wasExpanded;
        }
    }

    // A right click gives the short list Windows gives its corner icons.
    // It opens in a window of its own, above the taskbar, as the taskbar is
    // too short to hold it.
    function openMenu(px: real): void {
        menu.entries = [
            { text: qsTr("Privacy and security settings"), icon: "menu-settings", run: () => security.openPrivacy() },
            { text: qsTr("Firewall settings"), icon: "firewall", run: () => security.openSettings("firewall") }
        ];
        const scene = chip.mapToItem(null, 0, 0);
        menu.x = Math.round(px - menu.implicitWidth / 2);
        menu.y = Math.round(-scene.y - 12 - menu.implicitHeight);
        menu.open();
    }

    UI.Menu {
        id: menu
        popupType: T.Popup.Window
        minimumWidth: 264
    }

    Rectangle {
        anchors.centerIn: parent
        width: 32
        height: 40
        radius: 8
        color: chip.pressed ? UI.Style.surfacePressed : chip.security.expanded ? UI.Style.surfaceSelected : chip.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
    }

    ShieldGlyph {
        anchors.centerIn: parent
        width: 16
        height: 16
        mark: chip.security.level === "protected" ? "tick"
            : chip.security.level === "attention" ? "bang" : ""
        color: chip.security.level === "attention" ? UI.Style.warning : UI.Style.accent
    }
}
