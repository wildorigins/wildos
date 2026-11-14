import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Window
import QtQuick.Controls.Basic as C
import WildOS.Recovery

// Start-up recovery (RecoveryStartup.dc, RecoveryStartupMore.dc). Black,
// like the start-up screen: what happened, three choices and More options,
// the keyboard layout bottom left, accessibility and power bottom right.
// Arrow keys move, Enter chooses, Esc goes back. Anything that changes the
// disk asks for the disk password first; the one exception is the Btrfs
// countdown, which starts the restore point taken before the last update
// (undoable, and the disk was unlocked at this start).
Window {
    id: win

    visible: true
    visibility: Window.FullScreen
    width: Screen.width
    height: Screen.height
    color: "#000000"
    title: "Start-up recovery"

    readonly property var info: Env.info
    readonly property bool btrfs: info.backend === "btrfs"
    readonly property var points: info.points || []
    // The newest point taken before a change (an update, a driver, by hand).
    // An undo point is the version someone went back from, so after failed
    // starts it is never the one to recommend.
    readonly property var latest: points.find(p => p.kind !== "undo") || null
    readonly property string reason: info.reason || ""
    readonly property bool failed: reason === "failed"
    property string screen: "home"
    property string working: ""
    property string problem: ""
    property real textScale: 1
    property int countdown: 20
    property bool counting: btrfs && failed && latest !== null && screen === "home"
    property var pendingAction: null
    property int resetStep: 0
    property string resetHow: "keep"
    property var sticks: []

    readonly property var icons: ({
        "history": "M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5M12 7v5l4 2",
        "again": "M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8M21 3v5h-5M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16M8 16H3v5",
        "wrench": "M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z",
        "shield": "M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z",
        "reset": "M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5",
        "terminal": "M4 17 10 11 4 5M12 19h8",
        "report": "M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2zM12 7v2M12 13h.01",
        "disk": "M22 12H2M5.45 5.11 2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11zM6 16h.01M10 16h.01",
        "usb": "M10 7h4v4h-4zM9 11h6v6a3 3 0 0 1-6 0zM12 20v2",
        "keep": "M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2ZM9 13l2 2 4-4",
        "trash": "M3 6h18M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2M10 11v6M14 11v6"
    })

    readonly property var months: ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
    readonly property var days: ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]

    function pad(n) {
        return n < 10 ? "0" + n : "" + n;
    }

    function when(seconds) {
        const d = new Date(seconds * 1000);
        const now = new Date();
        const diff = Math.round((new Date(now.getFullYear(), now.getMonth(), now.getDate()) - new Date(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000);
        const day = diff <= 0 ? "today" : diff === 1 ? "yesterday" : diff < 7 ? days[d.getDay()] : d.getDate() + " " + months[d.getMonth()];
        return day + " at " + pad(d.getHours()) + ":" + pad(d.getMinutes());
    }

    function lowerFirst(text) {
        return text.length > 0 ? text.charAt(0).toLowerCase() + text.slice(1) : text;
    }

    function pointDetail(p) {
        if (!p) {
            return "";
        }
        const from = p.label.startsWith("Before ") ? "From " + lowerFirst(p.label) + ", " : p.label + ", ";
        return (btrfs ? "" : "Takes a few minutes. ") + from + when(p.time);
    }

    function go(name) {
        countdown = 20;
        screen = name;
        problem = "";
        Qt.callLater(() => {
            const first = pages.children[pages.currentIndex];
            if (first && first.focusFirst) {
                first.focusFirst();
            }
        });
    }

    // Asks for the disk password, then runs `action`.
    function withPassword(text, action) {
        pendingAction = action;
        passwordDialog.purpose = text;
        passwordDialog.open();
    }

    function start(tag, args, text) {
        working = text;
        screen = "working";
        Env.call(tag, args);
    }

    function goBack(point) {
        start("go-back", ["go-back", String(point.id)], btrfs ? "Going back to the earlier version" : "Restoring the earlier version. This takes a few minutes.");
    }

    Timer {
        interval: 1000
        repeat: true
        running: win.counting
        onTriggered: {
            win.countdown -= 1;
            if (win.countdown <= 0) {
                win.goBack(win.latest);
            }
        }
    }

    Connections {
        target: Env
        function onReplied(tag, answer) {
            if (tag === "info") {
                // The reply's own reason: win.info may not have caught up yet.
                if (answer.reason === "reset-everything" && answer.in_ram) {
                    win.go("wipe");
                } else if (answer.reason === "reset-keep") {
                    // Settings > Recovery > Reset already asked; this is
                    // the last step, which asks for the disk password.
                    win.resetHow = "keep";
                    win.resetStep = 2;
                    win.go("reset");
                } else {
                    win.go("home");
                }
                return;
            }
            if (tag === "usb") {
                win.sticks = answer.sticks || [];
                return;
            }
            if (answer.ok) {
                if (tag === "go-back" || tag === "reset-keep" || tag === "repair-startup") {
                    win.working = tag === "repair-startup" ? "Start-up repaired. Restarting" : "Done. Restarting";
                    Env.call("reboot", ["reboot"]);
                } else if (tag === "check-disk") {
                    win.go("repair");
                    win.problem = "No problems found on the disk.";
                } else if (tag === "report") {
                    win.go("report");
                    win.problem = "The report is on the USB stick. Send it from another device.";
                } else if (tag === "prepare-wipe") {
                    win.working = "Opening the installer";
                    Env.openInstaller();
                }
            } else if (tag !== "reboot" && tag !== "poweroff" && tag !== "try-again" && tag !== "safe-mode") {
                // go() moves the keyboard to the screen's first choice.
                win.go(tag === "check-disk" || tag === "repair-startup" ? "repair" : tag === "report" ? "report" : "problem");
                win.problem = answer.why || "That didn't work. Nothing was changed.";
            }
        }
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            win.counting = false;
            if (event.key === Qt.Key_Escape && win.screen !== "home" && win.screen !== "working") {
                win.go(win.screen === "versions" || win.screen === "repair" || win.screen === "report" || win.screen === "reset" ? "more" : "home");
                event.accepted = true;
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            onPositionChanged: win.counting = false
        }

        StackLayout {
            id: pages
            anchors.horizontalCenter: parent.horizontalCenter
            width: 560 * win.textScale
            y: win.screen === "home" ? (parent.height - 900) / 2 + 150 : (parent.height - 900) / 2 + 110
            height: parent.height - y - 90
            currentIndex: ["home", "more", "versions", "repair", "report", "reset", "working", "problem", "wipe"].indexOf(win.screen)

            // 17.3: what happened, and three choices.
            ColumnLayout {
                id: home
                spacing: 14
                function focusFirst() {
                    (earlier.visible ? earlier : again).forceActiveFocus();
                }

                Logo {
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    text: win.failed ? "Wild OS didn't start properly" : "Start-up options"
                    color: "#ffffff"
                    font.family: "Albert Sans"
                    font.pixelSize: 24 * win.textScale
                    font.weight: 600
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 470 * win.textScale
                    Layout.bottomMargin: 14
                    horizontalAlignment: Text.AlignHCenter
                    text: win.failed ? "It stopped twice while starting, which usually means the last update didn't agree with this PC. Your files are safe."
                        : "Choose how Wild OS starts. Your files are safe."
                    color: Qt.rgba(1, 1, 1, 0.75)
                    font.family: "Albert Sans"
                    font.pixelSize: 14 * win.textScale
                    lineHeight: 1.2
                    wrapMode: Text.Wrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    OptionRow {
                        id: earlier
                        Layout.fillWidth: true
                        visible: win.latest !== null
                        scale_: win.textScale
                        iconPath: win.icons.history
                        text: win.btrfs ? "Start the earlier version" : "Restore the earlier version"
                        badge: "Recommended"
                        detail: win.pointDetail(win.latest)
                        KeyNavigation.down: again
                        onClicked: win.withPassword("to go back to the earlier version", () => win.goBack(win.latest))
                    }

                    OptionRow {
                        id: again
                        Layout.fillWidth: true
                        scale_: win.textScale
                        iconPath: win.icons.again
                        text: "Try again"
                        detail: "Start normally, as if nothing happened"
                        KeyNavigation.up: earlier.visible ? earlier : null
                        KeyNavigation.down: more
                        // The rows take the arrow keys before the page's
                        // own key handler sees them, so moving off the
                        // recommended choice is what stops the countdown.
                        onActiveFocusChanged: if (activeFocus) win.counting = false
                        onClicked: {
                            win.working = "Restarting";
                            win.screen = "working";
                            Env.call("try-again", ["try-again"]);
                        }
                    }

                    OptionRow {
                        id: more
                        Layout.fillWidth: true
                        scale_: win.textScale
                        iconPath: win.icons.wrench
                        text: "More options"
                        detail: "Safe mode, reset Wild OS, repair tools, report a problem"
                        chevron: true
                        KeyNavigation.up: again
                        onActiveFocusChanged: if (activeFocus) win.counting = false
                        onClicked: win.go("more")
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 18
                    spacing: 8

                    Shape {
                        Layout.preferredWidth: 15
                        Layout.preferredHeight: 15
                        scale: 15 / 24
                        transformOrigin: Item.TopLeft
                        ShapePath {
                            strokeColor: Qt.rgba(1, 1, 1, 0.6)
                            strokeWidth: 2
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: "M4 4h16a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2ZM6 8h.01M10 8h.01M14 8h.01M18 8h.01M8 12h.01M12 12h.01M16 12h.01M7 16h10" }
                        }
                    }

                Hint {
                    text: win.counting ? "Arrow keys to choose, Enter to start. Starting the earlier version in " + win.countdown + " seconds."
                        : "Arrow keys to choose, Enter to start."
                }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // 17.4: More options.
            ColumnLayout {
                spacing: 10
                function focusFirst() {
                    choose.forceActiveFocus();
                }

                BackLink {
                    onClicked: win.go("home")
                }

                Heading {
                    text: "More options"
                }

                OptionRow {
                    id: choose
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    enabled: win.points.length > 0
                    iconPath: win.icons.history
                    text: "Choose an earlier version"
                    detail: win.points.length > 1 ? "Pick from all " + win.points.length + " restore points" : win.points.length === 1 ? "Pick the one restore point on this PC" : "There are no restore points on this PC"
                    KeyNavigation.down: safe
                    onClicked: win.go("versions")
                }

                OptionRow {
                    id: safe
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.shield
                    text: "Safe mode"
                    detail: "Start with basic graphics and no extra drivers or startup apps"
                    KeyNavigation.up: choose
                    KeyNavigation.down: reset
                    onClicked: {
                        win.working = "Starting in safe mode";
                        win.screen = "working";
                        Env.call("safe-mode", ["safe-mode"]);
                    }
                }

                OptionRow {
                    id: reset
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.reset
                    text: "Reset Wild OS"
                    detail: "Back to how it was when installed, with or without your files"
                    KeyNavigation.up: safe
                    KeyNavigation.down: repair
                    onClicked: {
                        win.resetStep = 0;
                        win.resetHow = "keep";
                        win.go("reset");
                    }
                }

                OptionRow {
                    id: repair
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.terminal
                    text: "Repair tools"
                    detail: "Check the disk, repair start-up, open a terminal"
                    KeyNavigation.up: reset
                    KeyNavigation.down: report
                    onClicked: win.go("repair")
                }

                OptionRow {
                    id: report
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.report
                    text: "Report a problem"
                    detail: win.info.report ? "Save a report to a USB stick to send from another device" : "Report a problem isn't installed on this PC"
                    enabled: win.info.report === true
                    KeyNavigation.up: repair
                    onClicked: {
                        win.sticks = [];
                        Env.call("usb", ["usb-list"]);
                        win.go("report");
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Choose an earlier version.
            ColumnLayout {
                spacing: 10
                function focusFirst() {
                    if (versionList.count > 0) {
                        versionList.itemAt(0).forceActiveFocus();
                    }
                }

                BackLink {
                    onClicked: win.go("more")
                }

                Heading {
                    text: "Choose an earlier version"
                }

                Repeater {
                    id: versionList
                    model: win.points

                    OptionRow {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        rowHeight: 64
                        scale_: win.textScale
                        iconPath: win.icons.history
                        text: modelData.label
                        detail: win.when(modelData.time).charAt(0).toUpperCase() + win.when(modelData.time).slice(1) + (modelData.summary ? " · " + modelData.summary : "")
                        Keys.onUpPressed: if (index > 0) versionList.itemAt(index - 1).forceActiveFocus()
                        Keys.onDownPressed: if (index < versionList.count - 1) versionList.itemAt(index + 1).forceActiveFocus()
                        onClicked: win.withPassword("to go back to this version", () => win.goBack(modelData))
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Repair tools.
            ColumnLayout {
                spacing: 10
                function focusFirst() {
                    checkDisk.forceActiveFocus();
                }

                BackLink {
                    onClicked: win.go("more")
                }

                Heading {
                    text: "Repair tools"
                }

                OptionRow {
                    id: checkDisk
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.disk
                    text: "Check the disk"
                    detail: "Looks for damage and fixes the start-up partitions"
                    KeyNavigation.down: repairStart
                    onClicked: win.withPassword("to check the disk", () => win.start("check-disk", ["check-disk"], "Checking the disk"))
                }

                OptionRow {
                    id: repairStart
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.wrench
                    text: "Repair start-up"
                    detail: "Puts the start-up menu and start-up files back"
                    KeyNavigation.up: checkDisk
                    KeyNavigation.down: terminal
                    onClicked: win.withPassword("to repair start-up", () => win.start("repair-startup", ["repair-startup"], "Repairing start-up"))
                }

                OptionRow {
                    id: terminal
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.terminal
                    text: "Open a terminal"
                    detail: "For people who know what they are doing"
                    KeyNavigation.up: repairStart
                    onClicked: win.withPassword("to open a terminal", () => Env.openTerminal())
                }

                Hint {
                    Layout.topMargin: 8
                    visible: win.problem.length > 0
                    text: win.problem
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Report a problem: save it to a USB stick.
            ColumnLayout {
                spacing: 10
                function focusFirst() {
                    if (stickList.count > 0) {
                        stickList.itemAt(0).forceActiveFocus();
                    } else {
                        refreshSticks.forceActiveFocus();
                    }
                }

                BackLink {
                    onClicked: win.go("more")
                }

                Heading {
                    text: "Report a problem"
                }

                Hint {
                    Layout.fillWidth: true
                    text: win.sticks.length > 0 ? "Choose the USB stick to save the report on. Nothing else on it changes."
                        : "Put in a USB stick to save the report on, then choose Look again."
                }

                Repeater {
                    id: stickList
                    model: win.sticks

                    OptionRow {
                        required property var modelData
                        Layout.fillWidth: true
                        rowHeight: 64
                        scale_: win.textScale
                        iconPath: win.icons.usb
                        text: modelData.label
                        detail: modelData.size + " · " + modelData.device
                        onClicked: win.start("report", ["report", modelData.device], "Saving the report")
                    }
                }

                OptionRow {
                    id: refreshSticks
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.again
                    text: "Look again"
                    detail: "Find USB sticks put in since"
                    onClicked: Env.call("usb", ["usb-list"])
                }

                Hint {
                    Layout.topMargin: 8
                    visible: win.problem.length > 0
                    text: win.problem
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Reset Wild OS (RecoveryReset.dc), the same three steps as
            // Settings.
            ColumnLayout {
                spacing: 12
                function focusFirst() {
                    (win.resetStep === 0 ? keepChoice : resetNext).forceActiveFocus();
                }

                BackLink {
                    onClicked: win.resetStep > 0 ? win.resetStep -= 1 : win.go("more")
                }

                Heading {
                    text: win.resetStep === 2 ? (win.resetHow === "keep" ? "Ready to reset" : "Remove everything?") : "Reset Wild OS"
                }

                Hint {
                    Layout.fillWidth: true
                    text: win.resetStep === 0 ? "Choose what happens to your files."
                        : win.resetStep === 1 ? (win.resetHow === "keep"
                            ? "Removed: apps you installed (a list goes on your Desktop so you can get them back) and system settings, back to default. Kept: your home folder, Mail, Calendar and Notes, Windows mode and its apps, Wi-Fi and your password."
                            : "Removed: everything on this PC, your files, apps, accounts and Windows mode. You choose a new disk password and get a new recovery key.")
                        : (win.resetHow === "keep"
                            ? "Takes about 15 minutes and the PC restarts once. It uses the copy of Wild OS kept on this PC, so it works without the internet."
                            : "Every file on this PC is deleted for good. The PC restarts, then Wild OS sets up again from the copy kept on this PC.")
                }

                OptionRow {
                    id: keepChoice
                    Layout.fillWidth: true
                    visible: win.resetStep === 0
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.keep
                    text: "Keep my files"
                    detail: "The system and apps go back to how they were when installed. Your home folder, Mail and Windows mode stay."
                    selected: activeFocus || win.resetHow === "keep"
                    KeyNavigation.down: everythingChoice
                    onClicked: {
                        win.resetHow = "keep";
                        win.resetStep = 1;
                        resetNext.forceActiveFocus();
                    }
                }

                OptionRow {
                    id: everythingChoice
                    Layout.fillWidth: true
                    visible: win.resetStep === 0
                    enabled: win.info.wipe_fits === true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.trash
                    text: "Remove everything"
                    detail: win.info.wipe_fits === true ? "Wipes the whole disk and sets up encryption again with a new key. For selling or giving the PC away."
                        : "Needs at least 4 GB of memory, because Wild OS runs from memory while the disk is wiped."
                    selected: activeFocus || win.resetHow === "everything"
                    KeyNavigation.up: keepChoice
                    onClicked: {
                        win.resetHow = "everything";
                        win.resetStep = 1;
                        resetNext.forceActiveFocus();
                    }
                }

                OptionRow {
                    id: resetNext
                    Layout.fillWidth: true
                    visible: win.resetStep > 0
                    rowHeight: 56
                    scale_: win.textScale
                    iconPath: win.resetHow === "keep" ? win.icons.reset : win.icons.trash
                    text: win.resetStep === 1 ? "Next" : win.resetHow === "keep" ? "Reset" : "Remove everything"
                    onClicked: {
                        if (win.resetStep === 1) {
                            win.resetStep = 2;
                        } else if (win.resetHow === "keep") {
                            win.withPassword("to reset Wild OS", () => win.start("reset-keep", ["reset-keep"], "Resetting Wild OS. This takes about 15 minutes."));
                        } else {
                            win.withPassword("to remove everything", () => win.start("reset-everything", ["reset-everything"], "Restarting to remove everything"));
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Working.
            ColumnLayout {
                spacing: 18
                function focusFirst() {}

                Item {
                    Layout.preferredHeight: 120
                }

                C.BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: win.screen === "working"
                    palette.dark: "#8fb4ff"
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 480
                    horizontalAlignment: Text.AlignHCenter
                    text: win.working
                    color: "#ffffff"
                    font.family: "Albert Sans"
                    font.pixelSize: 17 * win.textScale
                    wrapMode: Text.Wrap
                }

                Hint {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Keep the PC switched on."
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Something went wrong.
            ColumnLayout {
                spacing: 14
                function focusFirst() {
                    problemBack.forceActiveFocus();
                }

                Heading {
                    text: "That didn't work"
                }

                Hint {
                    Layout.fillWidth: true
                    text: win.problem
                }

                OptionRow {
                    id: problemBack
                    Layout.fillWidth: true
                    rowHeight: 56
                    scale_: win.textScale
                    iconPath: win.icons.again
                    text: "Back to start-up options"
                    onClicked: win.go("home")
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // Remove everything, second start: the image is in memory, so the
            // disk can be wiped and set up again by the installer.
            ColumnLayout {
                spacing: 14
                function focusFirst() {
                    wipeGo.forceActiveFocus();
                }

                Heading {
                    text: "Remove everything"
                }

                Hint {
                    Layout.fillWidth: true
                    text: "Wild OS is running from memory, so the disk can be wiped. The installer opens next: choose the disk, set a new disk password and write down the new recovery key. Every file on this PC is deleted."
                }

                OptionRow {
                    id: wipeGo
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.trash
                    text: "Wipe the disk and set up Wild OS"
                    detail: "Asks for the disk password one last time"
                    KeyNavigation.down: wipeCancel
                    onClicked: win.withPassword("to wipe the disk", () => win.start("prepare-wipe", ["prepare-wipe"], "Getting the disk ready"))
                }

                OptionRow {
                    id: wipeCancel
                    Layout.fillWidth: true
                    rowHeight: 64
                    scale_: win.textScale
                    iconPath: win.icons.again
                    text: "Don't remove anything"
                    detail: "Restart and start Wild OS as it is"
                    KeyNavigation.up: wipeGo
                    onClicked: {
                        win.working = "Restarting";
                        win.screen = "working";
                        Env.call("try-again", ["try-again"]);
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        // Bottom left: the keyboard layout, as on the start-up screen.
        Rectangle {
            id: layoutChip
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 28
            anchors.bottomMargin: 26
            height: 36
            width: chipRow.implicitWidth + 28
            radius: 18
            color: chipArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
            property string name: Env.layoutName() || "English (UK)"
            Accessible.role: Accessible.Button
            Accessible.name: "Keyboard: " + name

            Connections {
                target: Env
                function onLayoutChanged() {
                    layoutChip.name = Env.layoutName() || layoutChip.name;
                }
            }

            RowLayout {
                id: chipRow
                anchors.centerIn: parent
                spacing: 8

                Shape {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    scale: 16 / 24
                    transformOrigin: Item.TopLeft
                    ShapePath {
                        strokeColor: "#ffffff"
                        strokeWidth: 1.8
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        PathSvg { path: "M4 6h16a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2ZM6 10h0M10 10h0M14 10h0M18 10h0M7 14h10" }
                    }
                }

                Text {
                    text: "Keyboard: " + layoutChip.name
                    color: Qt.rgba(1, 1, 1, 0.82)
                    font.family: "Albert Sans"
                    font.pixelSize: 12 * win.textScale
                }
            }

            MouseArea {
                id: chipArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: Env.nextLayout()
            }
        }

        // Bottom right: accessibility and power.
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 28
            anchors.bottomMargin: 26
            spacing: 10

            RoundButton {
                label: "Accessibility"
                iconPath: "M12 4.5m-1.8 0a1.8 1.8 0 1 0 3.6 0a1.8 1.8 0 1 0 -3.6 0M5 8.5l7 1.5 7-1.5M12 10v5M9 21l3-6 3 6"
                onClicked: accessMenu.open()

                C.Menu {
                    id: accessMenu
                    y: -height - 8
                    C.MenuItem {
                        text: win.textScale > 1 ? "Normal text size" : "Larger text"
                        onTriggered: win.textScale = win.textScale > 1 ? 1 : 1.3
                    }
                }
            }

            RoundButton {
                label: "Power"
                iconPath: "M12 3v9M6.3 6.8a8 8 0 1 0 11.4 0"
                onClicked: powerMenu.open()

                C.Menu {
                    id: powerMenu
                    y: -height - 8
                    x: -width + 40
                    C.MenuItem {
                        text: "Restart"
                        onTriggered: Env.call("reboot", ["reboot"])
                    }
                    C.MenuItem {
                        text: "Shut down"
                        onTriggered: Env.call("poweroff", ["poweroff"])
                    }
                }
            }
        }
    }

    // The disk password, before anything that changes the disk.
    C.Popup {
        id: passwordDialog

        property string purpose
        property bool wrong: false

        anchors.centerIn: parent
        width: 420
        modal: true
        focus: true
        padding: 24
        onOpened: {
            wrong = false;
            field.text = "";
            field.forceActiveFocus();
        }

        background: Rectangle {
            radius: 12
            color: "#1b1d22"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }

        C.Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: "Enter your disk password"
                color: "#ffffff"
                font.family: "Albert Sans"
                font.pixelSize: 17
                font.weight: 600
            }

            Text {
                Layout.fillWidth: true
                text: passwordDialog.wrong ? "That isn't the disk password. Try again." : "It's the password you type when the PC starts, needed " + passwordDialog.purpose + "."
                color: passwordDialog.wrong ? "#ffc46a" : Qt.rgba(1, 1, 1, 0.75)
                font.family: "Albert Sans"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            C.TextField {
                id: field
                Layout.fillWidth: true
                echoMode: TextInput.Password
                color: "#ffffff"
                font.family: "Albert Sans"
                font.pixelSize: 14
                placeholderText: "Disk password"
                // Only what is typed: no Ctrl+V, Shift+Insert or middle click.
                Keys.onPressed: event => {
                    if (event.matches(StandardKey.Paste) || (event.key === Qt.Key_Insert && (event.modifiers & Qt.ShiftModifier)))
                        event.accepted = true;
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.MiddleButton
                }
                placeholderTextColor: Qt.rgba(1, 1, 1, 0.5)
                background: Rectangle {
                    implicitHeight: 42
                    radius: 21
                    color: Qt.rgba(1, 1, 1, 0.07)
                    border.width: 1
                    border.color: field.activeFocus ? "#8fb4ff" : Qt.rgba(1, 1, 1, 0.18)
                }
                leftPadding: 16
                Keys.onReturnPressed: check()
                Keys.onEnterPressed: check()
                Keys.onEscapePressed: passwordDialog.close()
                function check() {
                    Env.call("password", ["check-password"], field.text);
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: 4
                spacing: 8

                C.Button {
                    text: "Cancel"
                    onClicked: passwordDialog.close()
                    contentItem: Text { text: parent.text; color: "#ffffff"; font.family: "Albert Sans"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { implicitWidth: 84; implicitHeight: 32; radius: 6; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06); border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.10) }
                }

                C.Button {
                    text: "Continue"
                    onClicked: field.check()
                    contentItem: Text { text: parent.text; color: "#0d1530"; font.family: "Albert Sans"; font.pixelSize: 13; font.weight: 600; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { implicitWidth: 96; implicitHeight: 32; radius: 6; color: parent.hovered ? "#a3c1ff" : "#8fb4ff" }
                }
            }

            Connections {
                target: Env
                function onReplied(tag, answer) {
                    if (tag !== "password") {
                        return;
                    }
                    if (answer.ok) {
                        passwordDialog.close();
                        const action = win.pendingAction;
                        win.pendingAction = null;
                        if (action) {
                            action();
                        }
                    } else {
                        passwordDialog.wrong = true;
                        field.selectAll();
                    }
                }
            }
        }
    }

    component Heading: Text {
        Layout.bottomMargin: 4
        color: "#ffffff"
        font.family: "Albert Sans"
        font.pixelSize: 24 * win.textScale
        font.weight: 600
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }

    component Hint: Text {
        color: Qt.rgba(1, 1, 1, 0.6)
        font.family: "Albert Sans"
        font.pixelSize: 12.5 * win.textScale
        wrapMode: Text.Wrap
        lineHeight: 1.2
    }

    component BackLink: MouseArea {
        Layout.bottomMargin: 4
        implicitWidth: backRow.implicitWidth
        implicitHeight: 24
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: "Back"

        RowLayout {
            id: backRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Shape {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                scale: 16 / 24
                transformOrigin: Item.TopLeft
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.7)
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: "m12 19-7-7 7-7M19 12H5" }
                }
            }

            Text {
                text: "Back"
                color: Qt.rgba(1, 1, 1, 0.7)
                font.family: "Albert Sans"
                font.pixelSize: 13 * win.textScale
            }
        }
    }

    component RoundButton: Rectangle {
        id: rb
        property string label
        property string iconPath
        signal clicked
        width: 40
        height: 40
        radius: 20
        color: rbArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
        Accessible.role: Accessible.Button
        Accessible.name: label

        Shape {
            anchors.centerIn: parent
            width: 17
            height: 17
            scale: 17 / 24
            ShapePath {
                strokeColor: "#ffffff"
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: rb.iconPath }
            }
        }

        MouseArea {
            id: rbArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: rb.clicked()
        }
    }
}
