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
        return when.length > 0 ? qsTr("Last checked %1.").arg(when) : "";
    }

    // Updates are shown by what they are to a person, never by package:
    // apps by their own names, everything else by the part of the system it
    // belongs to, as Windows Update words its entries.
    readonly property var groupNames: ({
        "wildos": "Wild OS", "core": qsTr("Core system"), "browser": qsTr("Web browser"), "desktop": qsTr("Desktop and graphics"),
        "library": qsTr("System libraries"), "other": qsTr("System tools")
    })

    function groupTitle(group, security) {
        switch (group) {
        case "wildos": return qsTr("Wild OS update");
        case "core": return security ? qsTr("Core system security fixes") : qsTr("Core system updates");
        case "browser": return security ? qsTr("Security fixes for the browser") : qsTr("Browser update");
        case "desktop": return security ? qsTr("Desktop security fixes") : qsTr("Desktop updates");
        case "library": return security ? qsTr("Security fixes for system libraries") : qsTr("System library updates");
        default: return security ? qsTr("Security fixes for system tools") : qsTr("System tool updates");
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
        return more > 0 ? qsTr("%1 and %2 more").arg(shown.join(", ")).arg(more) : qsTr("%1 and %2").arg(shown.slice(0, -1).join(", ")).arg(shown[shown.length - 1]);
    }

    readonly property var pending: gather(Updates.updates.map(u => u.name), Updates.updates.map(u => u.security))

    function historyTitle(entry) {
        const groups = gather(entry.packages);
        const apps = groups.filter(g => g.group === "app").map(g => g.app);
        if (entry.kind === "update") {
            if (!entry.succeeded) {
                return qsTr("Updates that did not finish");
            }
            if (groups.some(g => g.group === "wildos")) {
                return qsTr("Wild OS update");
            }
            if (groups.length === 1) {
                return groups[0].group === "app" ? qsTr("Update to %1").arg(groups[0].app) : groupTitle(groups[0].group, false);
            }
            return entry.packages.length === 1 ? qsTr("1 update") : qsTr("%1 updates").arg(entry.packages.length);
        }
        if (!entry.succeeded) {
            return entry.kind === "install" ? qsTr("An install that did not finish") : qsTr("A removal that did not finish");
        }
        const what = apps.length > 0 ? listOf(apps)
            : entry.packages.length === 1 ? qsTr("1 system part") : qsTr("%1 system parts").arg(entry.packages.length);
        return entry.kind === "install" ? qsTr("Installed %1").arg(what) : qsTr("Removed %1").arg(what);
    }

    function historyDetail(entry) {
        page.now;
        const when = Format.dayText(entry.when);
        const removed = entry.kind === "remove";
        const prefix = when.length > 0 ? (removed ? qsTr("Removed %1").arg(when) : qsTr("Installed %1").arg(when))
            : (removed ? qsTr("Removed") : qsTr("Installed"));
        const groups = gather(entry.packages);
        return groups.length > 1 ? qsTr("%1: %2").arg(prefix).arg(listOf(groups.map(label))) : prefix;
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
            case "checking": return qsTr("Checking for updates");
            case "installing": return qsTr("Installing updates");
            case "available":
                return Updates.securityCount > 0
                    ? (Updates.securityCount === 1 ? qsTr("1 security fix is ready to install") : qsTr("%1 security fixes are ready to install").arg(Updates.securityCount))
                    : (Updates.updates.length === 1 ? qsTr("1 update is ready to install") : qsTr("%1 updates are ready to install").arg(Updates.updates.length));
            case "installed": return Updates.restartNeeded ? qsTr("Restart to finish updating") : qsTr("Updates installed");
            case "uptodate": return unchecked ? qsTr("Not checked for updates yet") : qsTr("You're up to date");
            case "error": return Updates.updates.length > 0 && !Updates.checkFailed ? qsTr("The updates were not installed") : qsTr("Wild OS could not check for updates");
            default: return Updates.available ? qsTr("Updates") : qsTr("Updates are not available");
            }
        }
        description: {
            switch (Updates.state) {
            case "checking":
                if (Updates.clockWaiting)
                    return (Updates.clockBehind.length > 0 ? qsTr("The clock on this computer is at least %1 behind.").arg(Updates.clockBehind) + " " : "")
                        + qsTr("Wild OS is setting it through Tor, which can take a few minutes after start-up, and checks again as soon as it has.");
                return Updates.waiting ? qsTr("Waiting for another install to finish first, such as the automatic security updates.")
                                       : qsTr("This can take a minute on a slow connection.");
            case "installing":
                if (Updates.waiting)
                    return qsTr("Waiting for another install to finish first, such as the automatic security updates. Your updates start straight after.");
                return (Updates.progress > 0 ? qsTr("%1% done.").arg(Updates.progress) : qsTr("Getting ready."))
                    + (Updates.detail.length > 0 ? " " + qsTr("Now: %1.").arg(Updates.detail) : "");
            case "available":
                return (Updates.securityCount > 0 ? qsTr("Install them now to stay protected.") + " " : "") + page.lastChecked;
            case "installed":
                return Updates.restartNeeded
                    ? (page.restartAt.length > 0 ? qsTr("Wild OS will restart %1, as you chose.").arg(page.restartAt.charAt(0).toLowerCase() + page.restartAt.slice(1)) : qsTr("Some updates only take effect after a restart."))
                    : page.version + " " + page.lastChecked;
            case "error": return Updates.error;
            case "uptodate": return unchecked ? page.version + " " + qsTr("Check now to see what's new.") : page.version + " " + page.lastChecked;
            default:
                return Updates.available ? page.version + " " + page.lastChecked : qsTr("The update service is not installed on this computer.");
            }
        }

        UI.Button {
            visible: Updates.restartNeeded && !page.busy
            text: qsTr("Restart now")
            kind: "primary"
            onClicked: Updates.restart()
        }

        UI.Button {
            visible: Updates.restartNeeded && !page.busy && System.hasProgram(page.restartPrompt)
            text: page.restartAt.length > 0 ? qsTr("Change time") : qsTr("Schedule restart")
            onClicked: System.launch(page.restartPrompt, ["restart"])
        }

        UI.Button {
            visible: Updates.state === "available"
            text: qsTr("Install now")
            kind: !Updates.restartNeeded ? "primary" : "secondary"
            onClicked: Updates.install()
        }

        UI.Button {
            visible: Updates.state !== "available" && Updates.state !== "installing"
            enabled: Updates.available && !page.busy
            text: Updates.state === "checking" ? (Updates.clockWaiting ? qsTr("Waiting for the clock") : qsTr("Checking"))
                : Updates.state === "error" && Updates.checkFailed ? qsTr("Try again") : qsTr("Check now")
            onClicked: Updates.check()
        }

        UI.Button {
            visible: Updates.state === "error" && Updates.clockProblem && Pages.exists("datetime")
            text: qsTr("Date and time")
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
        text: copied ? qsTr("Technical details copied") : qsTr("Copy technical details")
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
        title: qsTr("Not every software source could be checked")
        description: Updates.skippedSources
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: Updates.updates.length > 0
        spacing: 10

        SectionTitle {
            text: qsTr("Ready to install")
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
                        title: wildos && Updates.pendingBuild > 0 ? qsTr("Wild OS build %1").arg(Updates.pendingBuild)
                            : modelData.group === "app" ? modelData.app : page.groupTitle(modelData.group, modelData.security)
                        description: wildos && page.pendingSummary.length > 0 ? page.sentence(page.pendingSummary)
                            : modelData.group === "app" ? qsTr("App update") : (modelData.count === 1 ? qsTr("1 update") : qsTr("%1 updates").arg(modelData.count))
                        status: modelData.security ? qsTr("Security fix") : ""
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
                    title: qsTr("Show all %1 groups of updates").arg(page.pending.length)
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
                title: qsTr("Install security updates automatically")
                description: page.policyError.length > 0 ? page.policyError
                    : page.policy.available === false ? qsTr("The automatic updates service is not installed on this computer.")
                    : qsTr("Small fixes install quietly in the background.")
                enabled: page.policy.available === true
                status: (page.policy.auto === true) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    id: autoToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.policy.auto === true
                    Accessible.name: qsTr("Install security updates automatically")
                    onToggled: System.setUpdatePolicy("auto", checked ? "on" : "off")
                }
            }

            UI.SettingRow {
                title: qsTr("When a restart is needed")
                description: qsTr("Wild OS will never restart in the middle of your work")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("When a restart is needed")
                    enabled: page.policy.available === true
                    model: [
                        { value: "ask", text: qsTr("Ask me first") },
                        { value: "idle", text: qsTr("Restart when no one is signed in") }
                    ]
                    readonly property var choice: page.policy.restart || "ask"
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => System.setUpdatePolicy("restart", model[index].value)
                }
            }

            // Beta gets each new build as soon as it is made, before it is
            // promoted to Stable.
            UI.SettingRow {
                title: qsTr("Update channel")
                description: qsTr("Stable gets tested releases. Recommended for most people.")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Update channel")
                    enabled: page.policy.channelAvailable === true
                    model: [
                        { value: "stable", text: qsTr("Stable") },
                        { value: "beta", text: qsTr("Beta") }
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
            text: qsTr("Recently installed")
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
                            text: qsTr("Go back")
                            onClicked: goBackDialog.openFor(page.goBackPoint)
                        }

                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: index === page.newestRelease && page.installedNotes.length > 0
                            text: qsTr("What's new")
                            onClicked: notesDialog.open()
                        }
                    }
                }

                UI.SettingRow {
                    visible: page.installedNotes.length > 0
                    title: qsTr("What's new in Wild OS")
                    description: page.installedNotes.length > 0 ? qsTr("You have build %1. See what recent builds changed.").arg(Updates.installedBuild) : ""
                    chevron: true
                    onClicked: notesDialog.open()
                }

                UI.SettingRow {
                    visible: Updates.history.length === 0
                    title: qsTr("Nothing installed yet")
                    description: qsTr("Updates appear here once they are installed.")
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
                title: qsTr("App updates")
                description: System.appUpdates > 0
                    ? (System.appUpdates === 1 ? qsTr("1 app has an update waiting in the Store") : qsTr("%1 apps have updates waiting in the Store").arg(System.appUpdates))
                    : System.appUpdates === 0 ? qsTr("Apps from the Store are up to date")
                    : qsTr("Apps you installed from the Store update there")
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
        title: qsTr("What's new in Wild OS")
        text: qsTr("You have build %1. Newest first.").arg(Updates.installedBuild)

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
                            text: Format.isValid(buildNotes.modelData.date)
                                ? qsTr("Build %1, %2").arg(buildNotes.modelData.build).arg(Format.dayText(buildNotes.modelData.date))
                                : qsTr("Build %1").arg(buildNotes.modelData.build)
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
                            text: qsTr("Small changes under the surface.")
                            size: 13
                            color: UI.Style.inkStatus
                        }
                    }
                }
            }
        }

        buttons: [
            UI.Button {
                text: qsTr("Close")
                kind: "primary"
                onClicked: notesDialog.close()
            }
        ]
    }
}
