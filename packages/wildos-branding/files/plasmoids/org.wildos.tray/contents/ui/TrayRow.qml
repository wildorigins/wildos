pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.wildos.shell as WildOS

// What sits on the taskbar: the arrow (only when something is behind it),
// the inline app icons, the "safely remove" icon while a drive is in use,
// and a cup while a hot corner keeps the computer awake.
Item {
    id: slot

    required property var tray

    // Every icon is 16 px in a 32 px button, which leaves 16 px between two
    // of them, the same as between Wi-Fi and volume. Left alone, the row put
    // its first icon 9 px from the divider and its last 27 px from Wi-Fi
    // (its own 8 px, the panel's 4 and Quick settings' padding). So the row
    // starts a little further in and takes less room than it draws, its last
    // button's empty edge lying over Quick settings' empty edge, and the gaps
    // at both ends come out at 16 px too. The arrow is narrower than an icon
    // and needs less of a start.
    readonly property int lead: tray.overflowCount > 0 ? 4 : 7
    readonly property int overlap: 11

    Layout.minimumWidth: Math.max(0, row.implicitWidth + lead - overlap)
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: 44

    RowLayout {
        id: row

        readonly property var tray: slot.tray

        x: slot.lead
        height: parent.height
        spacing: 0

        HoverHandler {
            onPointChanged: Nav.keyboard = false
        }

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                Nav.keyboard = true;
            }
            event.accepted = false;
        }

        TrayButton {
            visible: row.tray.overflowCount > 0
            Layout.preferredWidth: 32
            Layout.fillHeight: true
            checked: row.tray.expanded && row.tray.mode === "overflow"
            tip: qsTr("Show hidden icons")
            iconName: "chevron-up"
            onActivated: row.tray.open("overflow")
        }

        Repeater {
            model: row.tray.items

            TrayIcon {
                required property int index
                visible: row.tray.inlineRows.indexOf(index) >= 0
                Layout.preferredWidth: 32
                // The panel gives this row less height than the 44 px the other
                // taskbar buttons ask for, and a taller item would hang from the
                // row's top edge, a few pixels below the shield and Wi-Fi icons.
                // Filling the row keeps every icon on the taskbar's centre line.
                Layout.fillHeight: true
                fillWidth: 32
                fillHeight: 40
            }
        }

        // A USB plug with a tick, Windows' "safely remove hardware" mark,
        // which the shared icon set does not have.
        TrayButton {
            visible: row.tray.mountedDrives.count > 0
            Layout.preferredWidth: 32
            Layout.fillHeight: true
            checked: row.tray.expanded && row.tray.mode === "usb"
            tip: qsTr("Safely remove a drive")
            glyph: "M8 3h6v6H8zM6 9h10v6a3 3 0 0 1-3 3h-4a3 3 0 0 1-3-3zM11 18v3M14 13.5l2 2 4-4"
            onActivated: row.tray.open("usb")
        }

        // The cup quick settings uses for its own Keep awake.
        TrayButton {
            visible: WildOS.KeepAwake.active
            Layout.preferredWidth: 32
            Layout.fillHeight: true
            tip: qsTr("Keep awake is on. Click to turn it off")
            accessibleName: qsTr("Turn off keep awake")
            glyph: "M5 9.5h11V14a5 5 0 0 1-5 5h-1a5 5 0 0 1-5-5V9.5zM16 11h1.5a2.5 2.5 0 0 1 0 5H16M8 3.5v3M10.5 3.5v3M13 3.5v3"
            onActivated: WildOS.KeepAwake.turnOff()
        }
    }
}
