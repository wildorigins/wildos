import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// Updates, laid out as Updates.dc.html: the status card, how updates are
// installed, what was installed recently and the way to app updates. System
// updates go through PackageKit, the service the Store uses, so both always
// agree on what is waiting; the automatic ones are apt's unattended
// upgrades, set through a small polkit helper.
PageScroll {
    id: page

    property bool showAll: false
    property string policyError
    property date now: new Date()
    // The restart picked in the Wild OS restart prompt (wildos-dialogs), read
    // again each time the clock above ticks.
    readonly property string restartAt: page.now, System.config("wildos-restartrc", "Restart", "Label", "")
    readonly property string restartPrompt: "/usr/libexec/wildos/wildos-dialogs"

    Timer {
        interval: 30000
        running: true
        repeat: true
        onTriggered: page.now = new Date()
    }

    Component.onCompleted: {
        System.refreshUpdatePolicy();
        System.refreshAppUpdates();
        takeRequest();
    }

    // Install on the taskbar's Updates icon opens this page with
    // Navigator.request "install". Settings may have only just started and
    // still be reading the list, so the install waits for it; when the list
    // turns out empty there is nothing to do.
    property bool installWanted: false

    function takeRequest() {
        if (Navigator.request !== "install") {
            return;
        }
        Navigator.request = "";
        installWanted = true;
        installIfWanted();
    }

    function installIfWanted() {
        if (!installWanted) {
            return;
        }
        if (Updates.state === "available" || (Updates.state === "error" && Updates.updates.length > 0)) {
            installWanted = false;
            Updates.install();
        } else if (Updates.state !== "checking" && Updates.state !== "idle") {
            installWanted = false;
        }
    }

    Connections {
        target: Navigator
        function onRequestChanged() {
            page.takeRequest();
        }
    }

    Connections {
        target: Updates
        function onStateChanged() {
            page.installIfWanted();
        }
    }

    Connections {
        target: System
        function onUpdatePolicyFailed(reason) {
            page.policyError = reason;
            autoToggle.checked = Qt.binding(() => page.policy.auto === true);
        }
        function onUpdatePolicyChanged() {
            page.policyError = "";
        }
    }

    readonly property bool working: Updates.state === "checking" || Updates.state === "installing"
    readonly property var policy: System.updatePolicy
    readonly property string version: System.osName + (System.osVersion.length > 0 ? " " + System.osVersion : "") + "."
    readonly property string lastChecked: {
        page.now;
        // PackageKit may not have refreshed yet when apt was refreshed some
        // other way; the status tool reads apt's own timestamps.
        const fromApt = parseInt(Protection.status.updates_checked || "0");
        const when = Format.isValid(Updates.lastChecked) ? Format.ago(Updates.lastChecked)
            : fromApt > 0 ? Format.ago(new Date(fromApt * 1000)) : "";
        return when.length > 0 ? "Last checked " + when + "." : "";
    }

    // Updates are shown by what they are to a person, never by package:
    // apps by their own names, everything else by the part of the system it
    // belongs to, as Windows Update words its entries.
    readonly property var groupNames: ({
        "wildos": "Wild OS", "core": "Core system", "browser": "Web browser", "desktop": "Desktop and graphics",
        "library": "System libraries", "other": "System tools"
    })

    function groupTitle(group, security) {
        switch (group) {
        case "wildos": return "Wild OS update";
        case "core": return security ? "Core system security fixes" : "Core system updates";
        case "browser": return security ? "Security fixes for the browser" : "Browser update";
        case "desktop": return security ? "Desktop security fixes" : "Desktop updates";
        case "library": return security ? "Security fixes for system libraries" : "System library updates";
        default: return security ? "Security fixes for system tools" : "System tool updates";
        }
    }

    // Packages gathered into the groups a person reads, security first.
    function gather(packages, security) {
        const groups = [];
        const byKey = {};
        packages.forEach((name, i) => {
            const d = Updates.describe(name);
            const key = d.group === "app" ? "app:" + d.title : d.group;
            let g = byKey[key];
            if (!g) {
                g = { group: d.group, app: d.title, count: 0, security: false };
                byKey[key] = g;
                groups.push(g);
            }
            g.count += 1;
            g.security = g.security || (security ? security[i] === true : false);
        });
        return groups.sort((a, b) => (b.security ? 1 : 0) - (a.security ? 1 : 0));
    }

    function label(g) {
        return g.group === "app" ? g.app : groupNames[g.group];
    }

    function listOf(names) {
        if (names.length <= 1) {
            return names.join("");
        }
        const shown = names.slice(0, 3);
        const more = names.length - shown.length;
        return more > 0 ? shown.join(", ") + " and " + more + " more" : shown.slice(0, -1).join(", ") + " and " + shown[shown.length - 1];
    }

    readonly property var pending: gather(Updates.updates.map(u => u.name), Updates.updates.map(u => u.security))

    function historyTitle(entry) {
        const groups = gather(entry.packages);
        const apps = groups.filter(g => g.group === "app").map(g => g.app);
        if (entry.kind === "update") {
            if (!entry.succeeded) {
                return "Updates that did not finish";
            }
            if (groups.some(g => g.group === "wildos")) {
                return "Wild OS update";
            }
            if (groups.length === 1) {
                return groups[0].group === "app" ? "Update to " + groups[0].app : groupTitle(groups[0].group, false);
            }
            return Format.plural(entry.packages.length, "update", "updates");
        }
        const verb = entry.kind === "install" ? "Installed " : "Removed ";
        if (!entry.succeeded) {
            return entry.kind === "install" ? "An install that did not finish" : "A removal that did not finish";
        }
        return verb + (apps.length > 0 ? listOf(apps) : Format.plural(entry.packages.length, "system part", "system parts"));
    }

    function historyDetail(entry) {
        page.now;
        const when = Format.day(entry.when);
        const verb = entry.kind === "remove" ? "Removed " : "Installed ";
        const prefix = when.length > 0 ? verb + when : verb.trim();
        const groups = gather(entry.packages);
        return groups.length > 1 ? prefix + ": " + listOf(groups.map(label)) : prefix;
    }

    // What the waiting Wild OS build brings, from the update site's notes:
    // every build since the one installed, or just the waiting one when the
    // installed build is not known. Empty when the notes could not be
    // fetched, and the page reads as it did before there were notes.
    readonly property var pendingNotes: Updates.notes, Updates.pendingBuild > 0
        ? Updates.notesBetween(Updates.installedBuild > 0 ? Updates.installedBuild : Updates.pendingBuild - 1, Updates.pendingBuild) : []
    readonly property string pendingSummary: Updates.notes, Updates.pendingBuild > 0
        ? Updates.summary(Updates.installedBuild > 0 ? Updates.installedBuild : Updates.pendingBuild - 1, Updates.pendingBuild) : ""
    // The builds this computer has had, newest first, for "What's new".
    readonly property var installedNotes: Updates.notes.filter(b => b.build <= Updates.installedBuild).slice(0, 10)
    readonly property int newestRelease: Updates.history.findIndex(e => e.kind === "update" && e.succeeded && e.packages.some(n => n.startsWith("wildos")))

    function sentence(text) {
        return text.length > 0 ? text.charAt(0).toUpperCase() + text.slice(1) + "." : "";
    }

    StatusCard {
        flat: true
        iconCircle: 56
        iconSize: 28
        // A list read before the first check is empty, which is not the same
        // as up to date.
        readonly property bool unchecked: Updates.state === "uptodate" && page.lastChecked.length === 0
        readonly property bool working: Updates.state === "checking" || Updates.state === "installing"
        readonly property bool restartWaiting: Updates.restartNeeded && !working && Updates.state !== "error"
        readonly property bool done: !restartWaiting && ((Updates.state === "uptodate" && !unchecked) || Updates.state === "installed")
        iconStroke: done ? 2.6 : 2.2
        spinning: Updates.state === "checking"
        tone: done ? "success"
            : restartWaiting || Updates.state === "error" || (Updates.state === "available" && Updates.securityCount > 0) ? "warning"
            : "accent"
        iconName: Updates.state === "error" ? "warning"
                : done ? "checkmark"
                : restartWaiting ? "restart"
                : "update"
        title: {
            switch (Updates.state) {
            case "checking": return "Checking for updates";
            case "installing": return "Installing updates";
            case "available":
                return Updates.securityCount > 0
                    ? Format.plural(Updates.securityCount, "security fix is", "security fixes are") + " ready to install"
                    : Format.plural(Updates.updates.length, "update is", "updates are") + " ready to install";
            case "installed": return Updates.restartNeeded ? "Restart to finish updating" : "Updates installed";
            case "uptodate": return unchecked ? "Not checked for updates yet" : "You're up to date";
            case "error": return Updates.updates.length > 0 && !Updates.checkFailed ? "The updates were not installed" : "Wild OS could not check for updates";
            default: return Updates.available ? "Updates" : "Updates are not available";
            }
        }
        description: {
            switch (Updates.state) {
            case "checking":
                if (Updates.clockWaiting)
                    return (Updates.clockBehind.length > 0 ? "The clock on this computer is at least " + Updates.clockBehind + " behind. " : "")
                        + "Wild OS is setting it through Tor, which can take a few minutes after start-up, and checks again as soon as it has.";
                return Updates.waiting ? "Waiting for another install to finish first, such as the automatic security updates."
                                       : "This can take a minute on a slow connection.";
            case "installing":
                if (Updates.waiting)
                    return "Waiting for another install to finish first, such as the automatic security updates. Your updates start straight after.";
                return (Updates.progress > 0 ? Updates.progress + "% done." : "Getting ready.")
                    + (Updates.detail.length > 0 ? " Now: " + Updates.detail + "." : "");
            case "available":
                return (Updates.securityCount > 0 ? "Install them now to stay protected. " : "") + page.lastChecked;
            case "installed":
                return Updates.restartNeeded
                    ? (page.restartAt.length > 0 ? "Wild OS will restart " + page.restartAt.charAt(0).toLowerCase() + page.restartAt.slice(1) + ", as you chose." : "Some updates only take effect after a restart.")
                    : page.version + " " + page.lastChecked;
            case "error": return Updates.error;
            case "uptodate": return unchecked ? page.version + " Check now to see what's new." : page.version + " " + page.lastChecked;
            default:
                return Updates.available ? page.version + " " + page.lastChecked : "The update service is not installed on this computer.";
            }
        }

        UI.Button {
            visible: Updates.restartNeeded && !page.busy
            text: "Restart now"
            kind: "primary"
            onClicked: Updates.restart()
        }

        UI.Button {
            visible: Updates.restartNeeded && !page.busy && System.hasProgram(page.restartPrompt)
            text: page.restartAt.length > 0 ? "Change time" : "Schedule restart"
            onClicked: System.launch(page.restartPrompt, ["restart"])
        }

        UI.Button {
            visible: Updates.state === "available"
            text: "Install now"
            kind: !Updates.restartNeeded ? "primary" : "secondary"
            onClicked: Updates.install()
        }

        UI.Button {
            visible: Updates.state !== "available" && Updates.state !== "installing"
            enabled: Updates.available && !page.busy
            text: Updates.state === "checking" ? (Updates.clockWaiting ? "Waiting for the clock" : "Checking")
                : Updates.state === "error" && Updates.checkFailed ? "Try again" : "Check now"
            onClicked: Updates.check()
        }

        UI.Button {
            visible: Updates.state === "error" && Updates.clockProblem && Pages.exists("datetime")
            text: "Date and time"
            onClicked: Navigator.open("datetime")
        }
    }

    // PackageKit's error and apt's lines, for anyone helping to fix a check
    // that failed; the card above says why in plain words.
    UI.LinkButton {
        id: copyDetails
        property bool copied: false
        Layout.leftMargin: -4
        Layout.topMargin: -8
        visible: Updates.state === "error" && Updates.errorDetails.length > 0
        text: copied ? "Technical details copied" : "Copy technical details"
        onVisibleChanged: copied = false
        onClicked: {
            detailsClipboard.text = Updates.errorDetails;
            detailsClipboard.selectAll();
            detailsClipboard.copy();
            copied = true;
        }
    }

    TextEdit {
        id: detailsClipboard
        visible: false
    }

    UI.ProgressBar {
        Layout.fillWidth: true
        visible: Updates.state === "installing"
        barHeight: 8
        value: Updates.progress / 100
        indeterminate: Updates.progress <= 0
    }

    StatusCard {
        visible: Updates.skippedSources.length > 0 && Updates.state !== "checking" && Updates.state !== "error"
        iconName: "warning"
        warning: true
        title: "Not every software source could be checked"
        description: Updates.skippedSources
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: Updates.updates.length > 0
        spacing: 10

        SectionTitle {
            text: "Ready to install"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.showAll ? page.pending : page.pending.slice(0, 6)

                    UI.SettingRow {
                        id: pendingRow
                        required property var modelData
                        readonly property bool wildos: modelData.group === "wildos"
                        title: wildos && Updates.pendingBuild > 0 ? "Wild OS build " + Updates.pendingBuild
                            : modelData.group === "app" ? modelData.app : page.groupTitle(modelData.group, modelData.security)
                        description: wildos && page.pendingSummary.length > 0 ? page.sentence(page.pendingSummary)
                            : modelData.group === "app" ? "App update" : Format.plural(modelData.count, "update", "updates")
                        status: modelData.security ? "Security fix" : ""
                        below: BuildNotes {
                            width: parent ? parent.width : 0
                            visible: pendingRow.wildos && page.pendingNotes.length > 0
                            builds: page.pendingNotes
                            limit: 5
                        }
                    }
                }

                UI.SettingRow {
                    visible: !page.showAll && page.pending.length > 6
                    title: "Show all " + page.pending.length + " groups of updates"
                    chevron: true
                    onClicked: page.showAll = true
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                title: "Install security updates automatically"
                description: page.policyError.length > 0 ? page.policyError
                    : page.policy.available === false ? "The automatic updates service is not installed on this computer."
                    : "Small fixes install quietly in the background."
                enabled: page.policy.available === true
                status: (page.policy.auto === true) ? "On" : "Off"

                UI.Toggle {
                    id: autoToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.policy.auto === true
                    Accessible.name: "Install security updates automatically"
                    onToggled: System.setUpdatePolicy("auto", checked ? "on" : "off")
                }
            }

            UI.SettingRow {
                title: "When a restart is needed"
                description: "Wild OS will never restart in the middle of your work"

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: "When a restart is needed"
                    enabled: page.policy.available === true
                    model: [
                        { value: "ask", text: "Ask me first" },
                        { value: "idle", text: "Restart when no one is signed in" }
                    ]
                    readonly property var choice: page.policy.restart || "ask"
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => System.setUpdatePolicy("restart", model[index].value)
                }
            }

            // Beta gets each new build as soon as it is made, before it is
            // promoted to Stable.
            UI.SettingRow {
                title: "Update channel"
                description: "Stable gets tested releases. Recommended for most people."

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: "Update channel"
                    enabled: page.policy.channelAvailable === true
                    model: [
                        { value: "stable", text: "Stable" },
                        { value: "beta", text: "Beta" }
                    ]
                    readonly property var choice: page.policy.channel || "stable"
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => System.setUpdatePolicy("channel", model[index].value)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Recently installed"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: Updates.history.slice(0, 5)

                    UI.SettingRow {
                        required property var modelData
                        required property int index
                        title: page.historyTitle(modelData)
                        description: page.historyDetail(modelData)

                        // The restore point taken just before this build
                        // installed (System > Recovery's go-back dialog).
                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: index === page.newestRelease && page.goBackPoint >= 0
                            text: "Go back"
                            onClicked: goBackDialog.openFor(page.goBackPoint)
                        }

                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: index === page.newestRelease && page.installedNotes.length > 0
                            text: "What's new"
                            onClicked: notesDialog.open()
                        }
                    }
                }

                UI.SettingRow {
                    visible: page.installedNotes.length > 0
                    title: "What's new in Wild OS"
                    description: page.installedNotes.length > 0 ? "You have build " + Updates.installedBuild + ". See what recent builds changed." : ""
                    chevron: true
                    onClicked: notesDialog.open()
                }

                UI.SettingRow {
                    visible: Updates.history.length === 0
                    title: "Nothing installed yet"
                    description: "Updates appear here once they are installed."
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: System.storeApp.length > 0

        Column {
            width: parent.width

            UI.SettingRow {
                title: "App updates"
                description: System.appUpdates > 0
                    ? Format.plural(System.appUpdates, "app has an update", "apps have updates") + " waiting in the Store"
                    : System.appUpdates === 0 ? "Apps from the Store are up to date"
                    : "Apps you installed from the Store update there"
                chevron: true
                onClicked: System.openStore("updates")
            }
        }
    }

    readonly property int goBackPoint: Recovery.points, Recovery.pointForBuild(Updates.installedBuild)

    GoBackDialog {
        id: goBackDialog
    }

    UI.Dialog {
        id: notesDialog
        title: "What's new in Wild OS"
        text: "You have build " + Updates.installedBuild + ". Newest first."

        Flickable {
            id: notesScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 440)
            contentHeight: notesColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: UI.ScrollBar {}

            ColumnLayout {
                id: notesColumn
                width: notesScroll.width - 12
                spacing: 20

                Repeater {
                    model: notesDialog.visible ? page.installedNotes : []

                    ColumnLayout {
                        id: buildNotes
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8

                        UI.Label {
                            Layout.fillWidth: true
                            text: "Build " + buildNotes.modelData.build
                                + (Format.isValid(buildNotes.modelData.date) ? ", " + Format.day(buildNotes.modelData.date) : "")
                            size: 15
                            font.weight: UI.Style.weightSemiBold
                            Accessible.role: Accessible.Heading
                        }

                        BuildNotes {
                            Layout.fillWidth: true
                            visible: buildNotes.modelData.items.length > 0
                            builds: [buildNotes.modelData]
                            limit: 4
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            visible: buildNotes.modelData.items.length === 0
                            text: "Small changes under the surface."
                            size: 13
                            color: UI.Style.inkStatus
                        }
                    }
                }
            }
        }

        buttons: [
            UI.Button {
                text: "Close"
                kind: "primary"
                onClicked: notesDialog.close()
            }
        ]
    }
}
