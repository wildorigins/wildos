pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Layouts
import org.kde.notificationmanager as NotificationManager
import WildOS.UI as UI

// A copy, move or download in progress, as DialogCopy.dc (8.2) draws it:
// "Copying 214 items", where from and where to, "64% complete" with the time
// left, a live speed graph under the part already done, the speed and the
// items left, the file being copied now, then Fewer details, Pause and
// Cancel. Every KIO app reports its jobs here (Files, Dolphin, the desktop,
// downloads in KDE apps), so this is the one copy window of Wild OS. In a
// pop-up it has a title bar whose cross hides it while the copy carries on
// in the notification list.
ColumnLayout {
    id: job

    required property var n
    property bool toast: false

    readonly property var details: n?.jobDetails ?? null
    readonly property bool suspended: n?.jobState === NotificationManager.Notifications.JobStateSuspended
    // KIO counts bytes against the whole copy only when it could size it
    // first; copies from the web count one file at a time, so the share of
    // items done is used when the two disagree.
    readonly property real percent: {
        const own = Math.max(0, Math.min(100, n?.percentage ?? 0));
        if (totalItems > 1) {
            const byItems = 100 * processedItems / totalItems;
            if (Math.abs(own - byItems) > 25) {
                return byItems;
            }
        }
        return own;
    }
    readonly property bool bytesKnown: totalBytes > 0 && processedBytes <= totalBytes
        && Math.abs(100 * processedBytes / totalBytes - percent) < 25
    readonly property real speed: details ? details.speed : 0
    readonly property real totalBytes: details ? details.totalBytes : 0
    readonly property real processedBytes: details ? details.processedBytes : 0
    readonly property int totalItems: details ? Math.max(details.totalFiles, details.totalItems) : 0
    readonly property int processedItems: details ? Math.max(details.processedFiles, details.processedItems) : 0
    readonly property string home: StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace(/^file:\/\//, "")
    property bool expanded: true
    // The folder the copy started in: the source of the first file, kept
    // while later files come from folders inside it.
    property string fromFolder: ""
    property var samples: []

    signal killRequested()

    function stops(): var {
        return [pauseButton, cancelButton].filter(b => b.visible);
    }

    function plainPath(value: string): string {
        // Other places keep their host: smb://nas/Photos reads "nas > Photos".
        let path = decodeURIComponent(String(value || "").replace(/^file:\/\//, "").replace(/^[a-z][a-z0-9+.-]*:\/\//i, "//"));
        return path.replace(/\/+$/, "");
    }

    function folderOf(value: string): string {
        const path = plainPath(value);
        return path.substring(0, path.lastIndexOf("/"));
    }

    function friendly(path: string): string {
        if (path.length === 0) {
            return "";
        }
        if (path === home) {
            return qsTr("Home");
        }
        const parts = path.startsWith(home + "/") ? [qsTr("Home")].concat(path.slice(home.length + 1).split("/"))
            : path.startsWith("//") ? path.split("/").filter(p => p.length > 0)
            : ["Wild OS"].concat(path.split("/").filter(p => p.length > 0));
        return parts.join(" › ");
    }

    function lastName(path: string): string {
        if (path === home) {
            return qsTr("Home");
        }
        const name = path.substring(path.lastIndexOf("/") + 1);
        return name.length > 0 ? name : "Wild OS";
    }

    function size(bytes: real): string {
        if (bytes < 1024 * 1024) {
            return qsTr("%1 KB").arg(Math.max(bytes > 0 ? 1 : 0, Math.ceil(bytes / 1024)));
        }
        const units = [qsTr("%1 MB"), qsTr("%1 GB"), qsTr("%1 TB")];
        let value = bytes / (1024 * 1024);
        let unit = 0;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        const decimals = value < 10 ? 1 : 0;
        const rounded = Number(value.toFixed(decimals));
        return units[unit].arg(rounded.toLocaleString(Qt.locale(), "f", rounded % 1 === 0 ? 0 : decimals));
    }

    function timeLeft(): string {
        if (suspended) {
            return qsTr("Paused");
        }
        let seconds = -1;
        if (bytesKnown && speed > 0) {
            seconds = (totalBytes - processedBytes) / speed;
        } else {
            // Otherwise from how fast the percentage moved over the last
            // ten seconds.
            const recent = samples.slice(-10);
            if (recent.length >= 3) {
                const gained = recent[recent.length - 1].p - recent[0].p;
                if (gained > 0) {
                    seconds = (100 - percent) * (recent.length - 1) / gained;
                }
            }
        }
        if (seconds < 0) {
            return percent > 0 ? qsTr("Working out the time left") : qsTr("Getting ready");
        }
        if (seconds < 50) {
            return qsTr("About %1 seconds left").arg(Math.max(5, Math.round(seconds / 5) * 5));
        }
        if (seconds < 90) {
            return qsTr("About 1 minute left");
        }
        if (seconds < 3600) {
            return qsTr("About %1 minutes left").arg(Math.round(seconds / 60));
        }
        const hours = Math.round(seconds / 3600);
        return hours === 1 ? qsTr("About 1 hour left") : qsTr("About %1 hours left").arg(hours);
    }

    readonly property string verb: {
        const summary = String(n?.summary || "").replace(/[.…]+$/, "").trim();
        return summary.length > 0 ? summary : qsTr("Working");
    }
    readonly property string heading: {
        // KIO names the job with one word ("Copying", "Moving"); the count
        // makes it the design's "Copying 214 items".
        if (totalItems > 1 && verb.indexOf(" ") < 0) {
            return qsTr("%1 %2 items").arg(verb).arg(totalItems.toLocaleString(Qt.locale(), "f", 0));
        }
        return verb;
    }
    readonly property string currentName: {
        const path = plainPath(details ? details.descriptionValue1 : "");
        return path.substring(path.lastIndexOf("/") + 1);
    }
    readonly property string toFolder: details && details.destUrl && details.destUrl.toString().length > 0
        ? plainPath(details.destUrl.toString()) : folderOf(details ? details.descriptionValue2 : "")

    Connections {
        target: job.details
        ignoreUnknownSignals: true
        function onDescriptionValue1Changed() {
            const folder = job.folderOf(job.details.descriptionValue1);
            if (job.fromFolder.length === 0 || !(folder + "/").startsWith(job.fromFolder + "/")) {
                job.fromFolder = folder;
            }
        }
    }
    Component.onCompleted: {
        if (details && details.descriptionValue1) {
            fromFolder = folderOf(details.descriptionValue1);
        }
    }

    // One sample a second: how far along against how fast, so the graph
    // reads left to right with the progress bar under it, as on Windows.
    Timer {
        interval: 1000
        repeat: true
        running: !job.suspended
        triggeredOnStart: true
        onTriggered: {
            const list = job.samples.slice(-240);
            list.push({ p: job.percent, s: job.speed });
            job.samples = list;
        }
    }

    spacing: 0

    // Title bar: only in the pop-up, where the card stands in for the
    // Windows copy window.
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        Layout.bottomMargin: 12
        visible: job.toast
        spacing: 10

        UI.ThemeIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            name: "folder"
        }

        UI.Label {
            Layout.fillWidth: true
            text: job.heading
            font.weight: UI.Style.weightMedium
        }

        UI.IconButton {
            implicitWidth: 40
            implicitHeight: 30
            iconName: "close"
            iconSize: 12
            tip: qsTr("Hide")
            // The copy carries on and stays in the notification list.
            onClicked: job.n.dismissed = true
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: !job.toast
        text: job.heading
        font.weight: UI.Style.weightSemiBold
    }

    UI.Label {
        Layout.fillWidth: true
        Layout.topMargin: job.toast ? 0 : 6
        visible: job.fromFolder.length > 0 || job.toFolder.length > 0
        text: job.fromFolder.length > 0 && job.toFolder.length > 0
            ? qsTr("From <b>%1</b> to <b>%2</b>").arg(job.friendly(job.fromFolder)).arg(job.lastName(job.toFolder))
            : job.fromFolder.length > 0 ? qsTr("From <b>%1</b>").arg(job.friendly(job.fromFolder))
            : job.toFolder.length > 0 ? qsTr("to <b>%1</b>").arg(job.lastName(job.toFolder)) : ""
        textFormat: Text.StyledText
        wrapMode: Text.Wrap
        elide: Text.ElideNone
        lineHeight: 1.5
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 12

        UI.Label {
            text: job.suspended ? qsTr("Paused at %1%").arg(Math.round(job.percent)) : qsTr("%1% complete").arg(Math.round(job.percent))
            size: job.toast ? 22 : 18
            font.weight: UI.Style.weightSemiBold
        }

        Item {
            Layout.fillWidth: true
        }

        UI.Label {
            Layout.alignment: Qt.AlignBaseline
            text: job.timeLeft()
            size: 12
            color: UI.Style.inkMuted
        }
    }

    Rectangle {
        id: graph
        Layout.fillWidth: true
        Layout.preferredHeight: job.expanded ? 64 : 6
        Layout.topMargin: 12
        radius: job.expanded ? 6 : 3
        color: UI.Style.tone(0.04)
        clip: true

        Rectangle {
            width: parent.width * job.percent / 100
            height: parent.height
            radius: job.expanded ? 0 : 3
            color: job.expanded ? UI.Style.accentAlpha(0.12, 0.10) : UI.Style.accent
        }

        Canvas {
            id: canvas
            anchors.fill: parent
            visible: job.expanded
            readonly property color line: UI.Style.accent
            onLineChanged: requestPaint()
            Connections {
                target: job
                function onSamplesChanged() {
                    canvas.requestPaint();
                }
            }
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const points = job.samples.filter(s => s.p > 0);
                if (points.length < 2) {
                    return;
                }
                const top = Math.max(...points.map(s => s.s)) * 1.15 || 1;
                const x = s => width * s.p / 100;
                const y = s => height - (height - 6) * s.s / top;
                ctx.beginPath();
                ctx.moveTo(x(points[0]), height);
                for (const s of points) {
                    ctx.lineTo(x(s), y(s));
                }
                ctx.lineTo(x(points[points.length - 1]), height);
                ctx.closePath();
                ctx.globalAlpha = 0.25;
                ctx.fillStyle = line;
                ctx.fill();
                ctx.globalAlpha = 1;
                ctx.beginPath();
                ctx.moveTo(x(points[0]), y(points[0]));
                for (const s of points) {
                    ctx.lineTo(x(s), y(s));
                }
                ctx.lineWidth = 1.5;
                ctx.strokeStyle = line;
                ctx.stroke();
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 12
        visible: job.expanded && (job.speed > 0 || job.totalItems > 0)

        UI.Label {
            visible: job.speed > 0 && !job.suspended
            text: qsTr("Speed: %1/s").arg(job.size(job.speed))
            size: 12
            color: UI.Style.inkStatus
        }

        Item {
            Layout.fillWidth: true
        }

        UI.Label {
            visible: job.totalItems > 0
            text: job.bytesKnown
                ? qsTr("Items left: %1 (%2)").arg(Math.max(0, job.totalItems - job.processedItems).toLocaleString(Qt.locale(), "f", 0))
                    .arg(job.size(Math.max(0, job.totalBytes - job.processedBytes)))
                : qsTr("Items left: %1").arg(Math.max(0, job.totalItems - job.processedItems).toLocaleString(Qt.locale(), "f", 0))
            size: 12
            color: UI.Style.inkStatus
        }
    }

    UI.Label {
        Layout.fillWidth: true
        Layout.topMargin: 12
        visible: job.expanded && job.currentName.length > 0
        text: job.verb + " " + job.currentName
        size: 12
        color: UI.Style.inkMuted
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        Layout.topMargin: 18
        visible: job.toast
        color: UI.Style.divider
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: job.toast ? 10 : 12
        spacing: 10

        UI.LinkButton {
            text: job.expanded ? qsTr("Fewer details") : qsTr("More details")
            onClicked: job.expanded = !job.expanded
        }

        Item {
            Layout.fillWidth: true
        }

        UI.Button {
            id: pauseButton
            visible: !!job.n?.suspendable && !!job.details
            size: job.toast ? "medium" : "small"
            text: job.suspended ? qsTr("Resume") : qsTr("Pause")
            onClicked: job.suspended ? job.details.resume() : job.details.suspend()
        }

        UI.Button {
            id: cancelButton
            visible: !!job.n?.killable
            size: job.toast ? "medium" : "small"
            text: qsTr("Cancel")
            onClicked: job.killRequested()
        }
    }
}
