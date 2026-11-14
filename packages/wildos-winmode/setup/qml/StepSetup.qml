import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.UI as UI
import WildOS.WinMode.Setup

// 9.5, Setting up (WinModeSetup.dc). Everything here is the daemon's own
// progress; closing the window leaves setup running.
Page {
    id: page

    signal closeRequested()

    readonly property var status: Setup.status
    readonly property var stages: status.stages || []
    readonly property var problem: status.problem || ({})
    readonly property bool paused: status.paused === true
    readonly property bool waitingForWindows: problem.kind === "iso" || problem.kind === "iso-refused"
    // Setup is waiting for the internet and carries on by itself.
    readonly property bool offline: problem.kind === "download" && problem.offline === true
    // Start over pressed: the service is stopping setup and clearing up,
    // which can take a little while; the window then goes back to the checks.
    property bool stopping: false
    onVisibleChanged: if (!visible) stopping = false
    readonly property var active: {
        for (const s of stages) {
            if (s.state === "active") {
                return s;
            }
        }
        return null;
    }

    title: "Setting up Windows mode"
    text: "This takes about 25 minutes. Close this window and keep using your PC; we'll let you know when it's ready."
    note: "If your PC restarts or sleeps, setup carries on where it left off."
    spacing: 16

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: progressColumn.implicitHeight + 36
        radius: UI.Style.radiusLg
        color: UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.tone(0.08)

        ColumnLayout {
            id: progressColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                UI.Label {
                    Layout.fillWidth: true
                    text: page.stopping ? "Stopping setup" : page.paused ? "Paused" : page.offline ? "Waiting for the internet"
                        : page.waitingForWindows ? "Waiting for Windows" : page.active ? page.activeLabel(page.active.id) : "Getting ready"
                    size: 13
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    text: page.stopping || page.paused || page.offline || page.waitingForWindows ? "" : (page.status.left || "")
                    size: 13
                    color: UI.Style.inkMuted
                }
            }

            UI.ProgressBar {
                Layout.fillWidth: true
                barHeight: 8
                from: 0
                to: 100
                value: page.status.percent || 0
                Accessible.name: "Windows mode setup"
            }

            UI.Label {
                text: (page.status.percent || 0) + "%"
                size: 12
                color: UI.Style.inkMuted
            }
        }
    }

    function activeLabel(id) {
        switch (id) {
        case "media": return page.status.ownIso ? "Preparing" : "Downloading";
        case "install": return "Installing Windows";
        case "activate": return "Activating";
        case "tune": return "Tuning";
        default: return "Installing Microsoft 365";
        }
    }

    UI.Card {
        Layout.fillWidth: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            Repeater {
                model: page.stages

                CheckRow {
                    required property var modelData
                    title: modelData.title
                    detail: modelData.detail
                    state: modelData.state === "done" ? "done" : modelData.state === "active" ? "active" : modelData.state === "failed" ? "failed" : "pending"
                    value: modelData.state === "done" ? "Done" : modelData.state === "active" && modelData.percent > 0 ? modelData.percent + "%" : ""
                }
            }
        }
    }

    // Windows itself comes from Microsoft's page in the browser: there is
    // no documented way for Wild OS to fetch it.
    Banner {
        visible: page.waitingForWindows
        tone: page.problem.kind === "iso-refused" ? "warning" : "accent"
        iconName: "download"
        title: page.problem.kind === "iso-refused" ? "That file can't be used" : "Download Windows 11 from Microsoft"
        text: page.problem.kind === "iso-refused" ? page.problem.text
            : "On Microsoft's page, choose Windows 11, then your language, then 64-bit Download. Setup carries on by itself once the download finishes."

        UI.Button {
            kind: "primary"
            size: "small"
            text: "Open Microsoft's page"
            onClicked: Setup.openDownloadPage()
        }

        UI.Button {
            size: "small"
            text: "Choose a file"
            onClicked: isoDialog.open()
        }
    }

    // Setup putting a problem right by itself (setup.py, recover): nothing
    // to press, so it says what happened and that it is handling it.
    Banner {
        visible: !!page.status.notice && !page.waitingForWindows
        tone: "warning"
        iconName: "restart"
        title: "Trying again"
        text: page.status.notice || ""
    }

    Banner {
        visible: page.offline && !page.stopping
        iconName: "wifi-off"
        title: "Waiting for the internet"
        text: "This PC is offline. Setup carries on from where it stopped as soon as you're back online, with nothing to press."
    }

    Banner {
        visible: !page.waitingForWindows && !page.status.notice && !page.offline
        iconName: "monitor"
        title: "Carry on while it works"
        text: "Close this window and Wild OS tells you when it's ready. Setup only uses spare capacity."
    }

    FileDialog {
        id: isoDialog
        title: "Choose the Windows 11 download"
        nameFilters: ["Windows installation media (*.iso)"]
        onAccepted: Setup.chooseIso(selectedFile)
    }

    UI.Dialog {
        id: startOverDialog
        title: "Start setting up again?"
        text: page.status.ownIso ? "Windows mode stops this setup and removes what it has made so far, including its copy of your Windows 11 file. Your own file stays as it is."
            : "Windows mode stops this setup and removes what it has made so far. The Windows download is kept, so starting again doesn't fetch it twice."
        buttons: [
            UI.Button {
                kind: "primary"
                text: "Start over"
                onClicked: {
                    startOverDialog.close();
                    page.stopping = true;
                    Setup.cancel();
                }
            },
            UI.Button {
                text: "Keep going"
                onClicked: startOverDialog.close()
            }
        ]
    }

    buttons: [
        UI.Button {
            text: "Start over"
            visible: !page.stopping && (page.status.state === "setup" || page.status.state === "failed")
            onClicked: startOverDialog.open()
        },
        UI.Button {
            text: page.paused ? "Resume" : "Pause"
            visible: !page.stopping && page.status.state === "setup" && (page.problem.kind !== "download" || page.offline) && !page.waitingForWindows
            onClicked: page.paused ? Setup.resume() : Setup.pause()
        },
        UI.Button {
            kind: "primary"
            text: "Close"
            onClicked: page.closeRequested()
        }
    ]
}
