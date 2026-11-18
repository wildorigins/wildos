import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.plasma.plasmoid
import org.kde.plasma.private.sessions as Sessions
import org.kde.kirigami as Kirigami
import WildOS.UI as UI

MouseArea {
    id: button

    required property PlasmoidItem launcher

    property bool wasExpanded: false

    Layout.minimumWidth: 46
    Layout.preferredWidth: 46
    Layout.minimumHeight: 46
    Layout.preferredHeight: 46

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    Accessible.name: qsTr("Start")
    Accessible.role: Accessible.Button

    // Remembering the state at press time stops a click that closes the popup
    // (it loses focus first) from immediately opening it again.
    onPressed: mouse => {
        if (mouse.button === Qt.LeftButton) {
            wasExpanded = launcher.expanded;
        }
    }
    onClicked: mouse => {
        if (mouse.button === Qt.RightButton) {
            openShortcutMenu();
        } else {
            launcher.expanded = !wasExpanded;
        }
    }

    // A right click gives the power-user list Windows keeps on Win+X
    // (MenusShell.dc "Start button"): every row is reachable elsewhere too,
    // and an app that is not installed is left out.
    function openShortcutMenu(): void {
        const start = launcher;
        const session = start.session;
        const has = id => start.appRow(id) >= 0;
        const entries = [];
        if (start.hasWildSettings) {
            entries.push({ text: qsTr("Apps"), icon: "menu-apps", run: () => start.openSettingsPage("installedapps") });
        }
        if (has("org.wildos.taskmanager.desktop")) {
            entries.push({ text: qsTr("Task Manager"), icon: "menu-task-manager", shortcut: qsTr("Ctrl+Shift+Esc"),
                run: () => start.launchApp("org.wildos.taskmanager.desktop", "") });
        }
        if (start.hasWildSettings) {
            entries.push({ text: qsTr("Settings"), icon: "menu-settings", shortcut: "Win+I", run: () => start.openSettings() });
        }
        if (has("org.wildos.files.desktop")) {
            entries.push({ text: qsTr("Files"), icon: "folder", shortcut: "Win+E", run: () => start.launchApp("org.wildos.files.desktop", "") });
        }
        if (has("org.wildos.terminal.desktop")) {
            entries.push({ text: qsTr("Terminal"), icon: "terminal", run: () => start.launchApp("org.wildos.terminal.desktop", "") });
        }
        if (entries.length > 0) {
            entries.push({ separator: true });
        }
        // Win+S and Win+R open Start, where typing searches and runs.
        entries.push({ text: qsTr("Search"), icon: "search", shortcut: "Win+S", run: () => start.expanded = true });
        entries.push({ text: qsTr("Run"), icon: "menu-run", shortcut: "Win+R", run: () => start.expanded = true });
        entries.push({ separator: true });
        const power = [
            { text: qsTr("Sign out"), icon: "sign-out", run: () => session.requestLogout(Sessions.SessionManagement.ConfirmationMode.Skip) }
        ];
        if (session.canSuspend) {
            power.push({ text: qsTr("Sleep"), icon: "moon", run: () => session.suspend() });
        }
        power.push({ text: qsTr("Shut down"), icon: "power", run: () => session.requestShutdown(Sessions.SessionManagement.ConfirmationMode.Skip) },
            { text: qsTr("Restart"), icon: "refresh", run: () => session.requestReboot(Sessions.SessionManagement.ConfirmationMode.Skip) });
        entries.push({ text: qsTr("Shut down or sign out"), icon: "menu-power", submenu: power, submenuWidth: 200 });

        shortcutMenu.entries = entries;
        const scene = button.mapToItem(null, 0, 0);
        shortcutMenu.x = 0;
        shortcutMenu.y = Math.round(-scene.y - 12 - shortcutMenu.implicitHeight);
        shortcutMenu.open();
    }

    UI.Menu {
        id: shortcutMenu
        popupType: T.Popup.Window
        minimumWidth: 236
        maximumWidth: 320
    }

    // The Taskbar artboard tints the button while Start is open (startBg);
    // hovering gives the same light fill as the app buttons beside it, and
    // holding it down the deeper one the clock and quick settings show.
    Rectangle {
        anchors.centerIn: parent
        width: 40
        height: 40
        radius: 8
        color: button.pressed ? Style.pressed
             : button.launcher.expanded ? Style.accentTint
             : button.containsMouse ? Style.hover
             : "transparent"
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    // The press dip the taskbar's app buttons share (LaunchStates artboard):
    // on the press itself the glyph goes down to 0.86, springs past full size
    // to 1.04 and settles, 100 ms in all. Skipped when KDE's animation speed
    // is set to instant, Wild OS's reduced motion.
    property real logoScale: 1

    onPressedChanged: {
        if (pressed && Kirigami.Units.longDuration > 1) {
            dip.restart();
        }
    }

    SequentialAnimation {
        id: dip
        NumberAnimation { target: button; property: "logoScale"; to: 0.86; duration: 35; easing.type: Easing.OutCubic }
        NumberAnimation { target: button; property: "logoScale"; to: 1.04; duration: 35; easing.type: Easing.OutCubic }
        NumberAnimation { target: button; property: "logoScale"; to: 1; duration: 30; easing.type: Easing.InOutQuad }
    }

    // Plasma centres the popup on its visual parent. This invisible strip has
    // the button's height but sits at the middle of the screen, so Start rises
    // from the centre of the taskbar as the design shows, wherever the button
    // is in the centred group. With the taskbar aligned left it sits on the
    // button instead, and Start opens above it, as on Windows.
    Item {
        id: screenCentre
        y: 0
        width: 2
        height: button.height

        function recentre(): void {
            if (launcher.taskbarLeftAligned()) {
                x = (button.width - width) / 2;
                return;
            }
            const origin = button.mapToGlobal(0, 0);
            x = Screen.virtualX + Screen.width / 2 - origin.x - width / 2;
        }
    }

    Component.onCompleted: launcher.popupAnchor = screenCentre

    // The app grid glyph at 30 px. A custom icon set in the widget's
    // settings still wins; the default follows light and dark mode.
    Kirigami.Icon {
        anchors.centerIn: parent
        width: 30
        height: 30
        roundToIconSize: false
        source: Plasmoid.icon === "wildos-start" && !UI.Style.dark ? "wildos-start-light" : Plasmoid.icon
        scale: button.logoScale
    }
}
