import QtQuick
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI

// The one button every app has, in each state StoreStates.dc.html draws:
// Get, the waiting, downloading and installing rings, Open, Update,
// Paused, Retry and the greyed Get when space is short. `big` is the app
// page's 34px size, otherwise the 28px list size. In a list the button
// keeps one width whatever its state and sits at the right of it, so rows
// never move while apps install; `showStatus` adds the board's short line
// underneath ("Needs 720 MB free", "Download failed").
T.AbstractButton {
    id: control

    property var app
    property bool big: false
    property bool alignRight: !big
    property bool showStatus: !big
    // A curated line for when there is no status to give ("Opens PSD").
    property string note
    property color noteColor: Metrics.inkTertiary
    // What Get says: "Reinstall" for one of Wild OS's own apps.
    property string getLabel: "Get"

    // Turned into Open (or Update, or Installed) from a ring: the app page
    // gives its icon a small bounce.
    signal landed()

    readonly property string realState: app ? app.buttonState : "get"
    // Get answers at once: the ring shows before the Store has queued the
    // job, and the real state takes over as soon as it changes.
    property bool asked: false
    readonly property string shown: asked && (realState === "get" || realState === "retry" || realState === "paused") ? "waiting" : realState
    readonly property bool ring: ["waiting", "preparing", "downloading", "installing", "removing", "paused"].indexOf(shown) >= 0
    readonly property string label: shown === "open" ? "Open"
        : shown === "installed" ? "Installed"
        : shown === "update" ? "Update"
        : shown === "retry" ? "Retry"
        : getLabel

    readonly property int buttonHeight: big ? 34 : 28
    readonly property int minWidth: big ? 84 : 66
    readonly property int sidePadding: big ? 22 : 16
    readonly property real fontSize: big ? 14 : 13
    readonly property real pillWidth: Math.max(minWidth, Math.ceil(labelText.implicitWidth) + 2 * sidePadding + (retryIcon.visible ? retryIcon.width + 6 : 0))
    // The widest the pill gets in a list ("Installed"), so every state
    // fits the same slot.
    readonly property real slotWidth: Math.max(minWidth, Math.ceil(widest.advanceWidth) + 2 * sidePadding)

    implicitWidth: alignRight ? Math.max(slotWidth, pillWidth, statusLabel.visible ? Math.ceil(statusLabel.implicitWidth) : 0) : (ring ? minWidth : pillWidth)
    implicitHeight: buttonHeight + (statusLabel.visible ? statusLabel.implicitHeight + 3 : 0)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    enabled: !!app && ["nospace", "installed", "installing", "removing"].indexOf(shown) < 0
    Accessible.role: ring ? Accessible.ProgressBar : Accessible.Button
    Accessible.name: {
        const name = app ? app.name : "";
        switch (shown) {
        case "waiting": return name + ", waiting to download. Press to cancel.";
        case "preparing": return name + ", getting ready";
        case "downloading": return name + ", downloading, " + app.progress + "%. Press to pause.";
        case "installing": return name + ", installing";
        case "removing": return name + ", uninstalling";
        case "paused": return name + ", paused. Press to carry on.";
        case "nospace": return "Get " + name + ". " + (app ? app.statusLine : "");
        }
        return label + " " + name;
    }

    onRealStateChanged: asked = false

    Timer {
        // Nothing came back (the request was refused at once): show the
        // real state again rather than a ring that never fills.
        running: control.asked
        interval: 5000
        onTriggered: control.asked = false
    }

    property string previous: shown
    onShownChanged: {
        const wasRing = ["waiting", "preparing", "downloading", "installing"].indexOf(previous) >= 0;
        previous = shown;
        // Cancelling back to Get is not an arrival.
        if (wasRing && ["open", "installed", "update"].indexOf(shown) >= 0) {
            if (!UI.Style.reducedMotion) {
                spring.restart();
            }
            landed();
        }
    }

    function act() {
        if (!app) {
            return;
        }
        switch (shown) {
        case "get": {
            const w = control.Window.window;
            if (w && w.requestInstall && w.requestInstall(app)) {
                return;
            }
            asked = true;
            app.install();
            return;
        }
        case "retry":
        case "paused":
            asked = true;
            app.install();
            return;
        case "waiting":
            app.cancel();
            return;
        case "preparing":
            if (app.cancellable) {
                app.cancel();
            }
            return;
        case "downloading":
            app.pause();
            return;
        case "update":
            app.update();
            return;
        case "open":
            app.open();
            return;
        }
    }

    onClicked: act()

    TextMetrics {
        id: widest
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(UI.Style.px(control.fontSize))
        font.pixelSize: UI.Style.px(control.fontSize)
        font.weight: UI.Style.weightBold
        text: control.getLabel.length > "Installed".length ? control.getLabel : "Installed"
    }

    contentItem: Item {}

    background: Item {
        Item {
            id: pillLayer
            readonly property real centreX: control.alignRight ? control.width - control.pillWidth / 2 : control.pillWidth / 2
            x: control.alignRight ? control.width - control.pillWidth : 0
            width: control.pillWidth
            height: control.buttonHeight
            opacity: control.ring ? 0 : 1
            transformOrigin: Item.Center
            Behavior on opacity {
                NumberAnimation { duration: UI.Style.hoverDuration }
            }

            NumberAnimation {
                id: spring
                target: pillLayer
                property: "scale"
                from: 0.72
                to: 1
                duration: 420
                easing.type: Easing.OutBack
                easing.overshoot: 2.4
            }

            Rectangle {
                id: pill
                anchors.fill: parent
                radius: height / 2
                readonly property bool filled: control.big && control.shown === "get"
                readonly property bool live: ["get", "open", "update", "retry"].indexOf(control.shown) >= 0
                color: filled ? (control.down ? UI.Style.accentPressed : control.hovered ? UI.Style.accentHover : UI.Style.accent)
                    : live && control.down ? Metrics.buttonPressed
                    : live && control.hovered ? Metrics.buttonHover
                    : Metrics.buttonFill
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                UI.FocusRing {
                    visible: control.visualFocus && !control.ring
                    targetRadius: pill.radius
                }
            }

            Row {
                anchors.centerIn: parent
                spacing: 6

                StoreIcon {
                    id: retryIcon
                    visible: control.shown === "retry"
                    anchors.verticalCenter: parent.verticalCenter
                    name: "retry"
                    size: control.big ? 15 : 13
                    strokeWidth: 2.2
                    color: Metrics.danger
                }

                UI.Label {
                    id: labelText
                    anchors.verticalCenter: parent.verticalCenter
                    text: control.label
                    size: control.fontSize
                    font.weight: UI.Style.weightBold
                    color: pill.filled ? UI.Style.textOnAccent
                        : control.shown === "retry" ? Metrics.danger
                        : control.shown === "nospace" ? Metrics.inkDim
                        : control.shown === "installed" ? Metrics.inkTertiary
                        : UI.Style.accent
                }
            }
        }

        Item {
            id: ringLayer
            x: control.alignRight ? control.width - control.minWidth : 0
            width: control.minWidth
            height: control.buttonHeight
            opacity: control.ring ? 1 : 0
            visible: opacity > 0
            Behavior on opacity {
                NumberAnimation { duration: UI.Style.hoverDuration }
            }

            Rectangle {
                anchors.centerIn: parent
                width: control.buttonHeight + 6
                height: width
                radius: width / 2
                readonly property bool live: ["waiting", "preparing", "downloading", "paused"].indexOf(control.shown) >= 0
                color: live && control.down ? Metrics.buttonFill : live && control.hovered ? Metrics.rowHover : "transparent"

                UI.FocusRing {
                    visible: control.visualFocus && control.ring
                    targetRadius: parent.radius
                }
            }

            Ring {
                anchors.centerIn: parent
                size: control.buttonHeight
                mode: control.shown === "waiting" ? "dashed"
                    : control.shown === "downloading" ? "progress"
                    : control.shown === "paused" ? "paused"
                    : "spin"
                progress: control.app ? Math.max(0, control.app.progress) : 0
                centre: control.shown === "downloading" || control.shown === "preparing" ? "stop"
                    : control.shown === "paused" ? "play" : "none"
            }
        }

        UI.Label {
            id: statusLabel
            visible: control.showStatus && text.length > 0
            y: control.buttonHeight + 3
            x: Math.min(control.width - width, pillLayer.centreX - width / 2)
            readonly property bool status: control.app && control.app.statusLine.length > 0
            text: status ? control.app.statusLine : control.note
            size: 11
            color: status ? Metrics.inkTertiary : control.noteColor
        }
    }
}
