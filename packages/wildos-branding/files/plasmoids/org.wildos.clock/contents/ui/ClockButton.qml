import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI
import org.wildos.shell as WildOS
import "Formats.js" as Formats

// The clock button from the design's taskbar: the time over the date, right
// aligned, in a 44 px tall button with 12 px padding. A small moon shows when
// do not disturb is on, so muted notifications are never a surprise.
MouseArea {
    id: button

    required property var clock

    property bool wasExpanded: false

    Layout.minimumWidth: row.implicitWidth + 24
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: 44

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Date, time and notifications")
    Accessible.description: clock.toolTipMainText + ", " + time.text
        + (clock.toolTipSubText.length > 0 ? ". " + clock.toolTipSubText.replace(/\n/g, ". ") : "")

    // Remembering the state at press time stops a click that closes the popup
    // (it loses focus first) from opening it again straight away.
    onPressed: mouse => {
        if (mouse.button === Qt.LeftButton) {
            wasExpanded = clock.expanded;
        }
    }
    onClicked: mouse => {
        if (mouse.button === Qt.RightButton) {
            openMenu(mouse.x);
        } else {
            clock.expanded = !wasExpanded;
        }
    }

    // A right click gives what Windows' clock does: where to set the date
    // and time, and where to choose what may notify.
    function openMenu(px: real): void {
        menu.entries = [
            { text: qsTr("Adjust date and time"), icon: "clock", run: () => clock.openSettings("datetime") },
            { text: qsTr("Notification settings"), icon: "bell", run: () => clock.openSettings("notifications") }
        ];
        const scene = button.mapToItem(null, 0, 0);
        // The clock sits at the end of the taskbar, so the menu never
        // reaches past the button's right edge.
        menu.x = Math.round(Math.min(px - menu.implicitWidth / 2, button.width - menu.implicitWidth));
        menu.y = Math.round(-scene.y - 12 - menu.implicitHeight);
        menu.open();
    }

    UI.Menu {
        id: menu
        popupType: T.Popup.Window
        minimumWidth: 220
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: 44
        radius: 12
        color: button.pressed ? UI.Style.surfacePressed : button.clock.expanded ? UI.Style.surfaceSelected : button.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 8

        UI.Glyph {
            visible: button.clock.doNotDisturb
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            path: "M20 14.5A8.5 8.5 0 0 1 9.5 4 8.5 8.5 0 1 0 20 14.5z"
            color: UI.Style.inkStatus
        }

        ColumnLayout {
            spacing: 1

            UI.Label {
                id: time
                Layout.alignment: Qt.AlignRight
                text: Formats.time(button.clock.now, WildOS.TaskbarStyle.clockSeconds)
                font.weight: UI.Style.weightMedium
                font.features: { "tnum": 1 }
            }

            UI.Label {
                Layout.alignment: Qt.AlignRight
                text: WildOS.TaskbarStyle.clockLongDate ? Formats.longDate(button.clock.now) : Formats.shortDate(button.clock.now)
                size: 12
                font.features: { "tnum": 1 }
                color: UI.Style.inkStatus
            }
        }
    }
}
