import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasma5support as P5Support
import org.kde.kirigami as Kirigami
import WildOS.UI as UI

// The Task view button beside Start: opens KWin's Overview, which shows every
// window and the virtual desktops, as Win+Tab does. It shares Start's 46 px
// square, hover fill and press dip so the two read as one pair.
PlasmoidItem {
    id: root

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: UI.Style.dark ? "wildos-task-view" : "wildos-task-view-light"
    preferredRepresentation: fullRepresentation
    toolTipMainText: qsTr("Task view")
    toolTipSubText: "Win+Tab"

    Layout.minimumWidth: 46
    Layout.maximumWidth: 46

    // KWin's Overview is a global shortcut; invoking it through the shortcut
    // service toggles it exactly as the keys would.
    function toggleOverview(): void {
        runner.connectSource("dbus-send --session --type=method_call --dest=org.kde.kglobalaccel "
            + "/component/kwin org.kde.kglobalaccel.Component.invokeShortcut string:Overview");
    }

    function openSettings(): void {
        runner.connectSource("kstart --desktopfile org.wildos.settings -- wildos-settings --page multitasking");
    }

    P5Support.DataSource {
        id: runner
        engine: "executable"
        onNewData: source => disconnectSource(source)
    }

    fullRepresentation: MouseArea {
        id: button

        Layout.minimumWidth: 46
        Layout.preferredWidth: 46
        Layout.minimumHeight: 46
        Layout.preferredHeight: 46

        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        activeFocusOnTab: true
        Accessible.name: qsTr("Task view")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: root.toggleOverview()
        onPressed: label.hideImmediately()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                openMenu(mouse.x);
            } else {
                root.toggleOverview();
            }
        }

        Keys.onReturnPressed: root.toggleOverview()
        Keys.onEnterPressed: root.toggleOverview()
        Keys.onSpacePressed: root.toggleOverview()
        Keys.onMenuPressed: openMenu(width / 2)

        // It opens in a window of its own, above the taskbar, as the
        // taskbar is too short to hold it.
        function openMenu(px: real): void {
            menu.entries = [
                { text: qsTr("Task view"), icon: "multitasking", shortcut: "Win+Tab", run: () => root.toggleOverview() },
                { text: qsTr("Multitasking settings"), icon: "menu-settings", run: () => root.openSettings() }
            ];
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

        // This widget is its own button, with no compact form for the
        // taskbar to hang a hover label on, so it brings its own: the same
        // plain label every other taskbar item shows, in Plasma's tooltip
        // because only that can leave the taskbar's window.
        PlasmaCore.ToolTipArea {
            id: label
            anchors.fill: parent
            mainText: root.toolTipMainText
            textFormat: Text.PlainText
            location: Plasmoid.location
            // Rounded up and never shortened, or the last letters are cut.
            mainItem: Item {
                implicitWidth: Math.ceil(tipLabel.implicitWidth)
                implicitHeight: Math.ceil(tipLabel.implicitHeight)

                UI.Label {
                    id: tipLabel
                    leftPadding: 8
                    rightPadding: 8
                    topPadding: 6
                    bottomPadding: 6
                    size: 12
                    elide: Text.ElideNone
                    text: root.toolTipMainText
                }
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 40
            height: 40
            radius: 8
            // Fades in on hover and clears at once, so moving along a list
            // never lights two items. It comes before the colour so it sees
            // the change in hover first.
            Behavior on color {
                enabled: button.containsMouse
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
            color: button.pressed ? UI.Style.surfacePressed : button.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

            // Clicking never focuses the button, so this only shows when
            // the keyboard reached it.
            UI.FocusRing {
                visible: button.activeFocus
            }
        }

        property real glyphScale: 1

        onPressedChanged: {
            if (pressed && Kirigami.Units.longDuration > 1) {
                dip.restart();
            }
        }

        SequentialAnimation {
            id: dip
            NumberAnimation { target: button; property: "glyphScale"; to: 0.86; duration: 35; easing.type: Easing.OutCubic }
            NumberAnimation { target: button; property: "glyphScale"; to: 1.04; duration: 35; easing.type: Easing.OutCubic }
            NumberAnimation { target: button; property: "glyphScale"; to: 1; duration: 30; easing.type: Easing.InOutQuad }
        }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 26
            height: 26
            roundToIconSize: false
            source: Plasmoid.icon
            scale: button.glyphScale
        }
    }
}
