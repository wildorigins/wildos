import QtQuick
import QtQuick.Layouts
import WildOS.Backup
import WildOS.UI as UI

// The card at the top of the Overview: how the backups stand and what to
// do about it. It reads one of five ways:
//   running   a backup is going: what it's on, how far, Cancel
//   failed    the last one didn't finish: why, and Try again
//   missing   a backup is waiting for the drive to be plugged in
//   never     set up, nothing backed up yet
//   ok        backed up: when, how much changed, how long it took
Panel {
    id: card

    property var app

    readonly property string plugged: Drives.pluggedIn
    readonly property var last: Backup.last
    readonly property var done: Backup.lastDone
    readonly property bool waiting: !isNaN(Backup.pendingSince)
    readonly property string variant: Backup.running ? "running"
        : last.result === "failed" ? "failed"
        : waiting && plugged.length === 0 ? "missing"
        : done.at === undefined ? "never"
        : "ok"

    // The run finds and opens the drive itself, mounting it if need be,
    // and says so in Recent backups if it isn't there; asking UDisks again
    // here only saves a pointless start.
    function backUp() {
        if (plugged.length === 0) {
            Drives.refresh();
        }
        if (Drives.pluggedIn.length === 0) {
            app.say(qsTr("Plug in your backup drive first"));
            return;
        }
        Backup.backUpNow();
    }

    function count(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0);
    }

    function progressText() {
        if (Backup.stopping) {
            return qsTr("Stopping. What's backed up so far is kept for next time.");
        }
        const parts = [];
        if (Backup.phase === "files" && Backup.totalFiles > 0) {
            parts.push(qsTr("%1 of %2 files").arg(count(Backup.filesDone)).arg(count(Backup.totalFiles)));
        }
        if (Backup.phase !== "tidying") {
            if (Backup.totalBytes > 0) {
                parts.push(qsTr("%1 of %2").arg(BackupStyle.size(Backup.bytesDone)).arg(BackupStyle.size(Backup.totalBytes)));
            } else if (Backup.bytesDone > 0) {
                parts.push(qsTr("%1 read").arg(BackupStyle.size(Backup.bytesDone)));
            }
        }
        let text = parts.join(", ");
        const left = Backup.phase === "files" ? BackupStyle.timeLeft(Backup.secondsLeft) : "";
        if (left.length > 0) {
            text = text.length > 0 ? qsTr("%1. %2.").arg(text).arg(left) : left + ".";
        } else if (text.length > 0) {
            text += ".";
        }
        if (Backup.phase === "tidying") {
            return qsTr("Your files are saved. Old backups are being thinned out by your Keep choices.");
        }
        if (text.length === 0) {
            return Backup.runDrive.length > 0 ? qsTr("To %1.").arg(Backup.runDrive) : qsTr("Opening the backup drive.");
        }
        return text;
    }

    function detail() {
        switch (variant) {
        case "running":
            return progressText();
        case "failed":
            return last.reason || qsTr("Something went wrong.");
        case "missing": {
            const drive = Settings.drives.find(d => d.uuid === Settings.defaultDrive);
            return qsTr("A backup has been waiting since %1. It starts by itself when you plug in %2.")
                .arg(BackupStyle.whenInSentence(Backup.pendingSince)).arg(drive ? drive.name : qsTr("the drive"));
        }
        case "never": {
            const next = Backup.nextRun;
            return isNaN(next) ? qsTr("Back up now to make the first one.")
                : qsTr("Back up now, or the first backup starts %1.").arg(BackupStyle.whenInSentence(next));
        }
        default: {
            let s = qsTr("Last backup %1.").arg(BackupStyle.whenInSentence(new Date(done.at)));
            if (done.dataAdded !== undefined) {
                s += " " + qsTr("%1 changed, took %2.").arg(BackupStyle.size(done.dataAdded)).arg(BackupStyle.duration(done.seconds || 0));
            }
            if (done.warning) {
                s += " " + done.warning;
            }
            return s;
        }
        }
    }

    implicitHeight: column.implicitHeight + padding * 2

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 18

            StatusCircle {
                busy: card.variant === "running"
                iconName: card.variant === "failed" ? "alert" : card.variant === "missing" ? "drive" : card.variant === "never" ? "shield-check" : "check"
                tone: card.variant === "failed" ? BackupStyle.bad
                    : card.variant === "missing" ? BackupStyle.late
                    : card.variant === "running" || card.variant === "never" ? UI.Style.accent
                    : BackupStyle.good
                fill: card.variant === "failed" ? BackupStyle.badSoft
                    : card.variant === "missing" ? BackupStyle.lateSoft
                    : card.variant === "running" || card.variant === "never" ? UI.Style.accentSoft
                    : BackupStyle.goodSoft
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                UI.Label {
                    Layout.fillWidth: true
                    size: 20
                    font.weight: UI.Style.weightBold
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                    text: {
                        switch (card.variant) {
                        case "running":
                            return Backup.stopping ? qsTr("Stopping the backup")
                                : Backup.phase === "windows" ? qsTr("Backing up Windows mode")
                                : Backup.phase === "tidying" ? qsTr("Tidying up old backups")
                                : Backup.phase === "files" ? qsTr("Backing up your files")
                                : qsTr("Getting ready to back up");
                        case "failed":
                            return qsTr("The last backup didn't finish");
                        case "missing":
                            return qsTr("Plug in your backup drive");
                        case "never":
                            return qsTr("Ready for your first backup");
                        default:
                            return qsTr("Your files are backed up");
                        }
                    }
                }
                UI.Label {
                    Layout.fillWidth: true
                    size: 13
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    text: card.detail()
                }
                ErrorDetails {
                    Layout.fillWidth: true
                    text: card.variant === "failed" ? (card.last.detail || "") : ""
                }
            }

            BoardButton {
                visible: card.variant !== "running"
                text: qsTr("Restore files")
                iconName: "restore"
                onClicked: card.app.navigate("restore")
            }
            BoardButton {
                visible: card.variant !== "running" && card.variant !== "missing"
                kind: "primary"
                text: card.variant === "failed" ? qsTr("Try again") : qsTr("Back up now")
                iconName: "refresh"
                onClicked: card.backUp()
            }
            BoardButton {
                visible: card.variant === "running"
                text: qsTr("Cancel")
                enabled: !Backup.stopping
                onClicked: Backup.cancel()
            }
        }

        UI.ProgressBar {
            Layout.fillWidth: true
            visible: card.variant === "running" && !Backup.stopping
            from: 0
            to: 1
            value: Backup.percent >= 0 ? Backup.percent : 0
            indeterminate: Backup.percent < 0 || Backup.phase === "tidying" || Backup.phase === "preparing"
        }
    }
}
