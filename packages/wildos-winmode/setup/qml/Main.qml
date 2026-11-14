import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI
import WildOS.WinMode.Setup

// Windows mode setup, boards 9.1 to 9.7: a 1040 by 720 sheet with the steps
// at the left. It opens on whatever the daemon is doing: the checks when
// Windows mode is not set up, progress while it sets up, Ready once done.
UI.AppWindow {
    id: window

    property string backdrop
    property string board

    // check, options, setup or ready
    property string page: "check"
    property string problem
    property string startError
    // The setup page has seen a setup under way, so the service saying
    // there is none means Start over has finished.
    property bool following: false

    readonly property int step: page === "check" ? 1 : page === "options" ? 2 : page === "setup" ? 3 : 4

    width: board === "problems" ? 1440 : 1040
    height: board === "problems" ? 900 : 720
    minimumWidth: 1040
    minimumHeight: 720
    visible: true
    deep: true
    title: "Windows mode"

    Component.onCompleted: {
        Setup.begin(board.length > 0 ? "preview" : "setup", board);
        switch (board) {
        case "home":
        case "nolicence":
        case "options": page = "options"; break;
        case "setup": page = "setup"; break;
        case "download": page = "setup"; problem = "download"; break;
        case "ready": page = "ready"; break;
        }
    }

    // Follow the daemon: a setup already under way, or done, is shown as such.
    Connections {
        target: Setup
        function onStatusChanged() {
            const s = Setup.status.state;
            if (Setup.preview) {
                return;
            }
            if (s === "setup" && window.page !== "setup") {
                window.page = "setup";
            } else if (s === "ready" && window.page === "setup") {
                window.page = "ready";
            } else if (s === "ready" && window.page === "check") {
                window.page = "ready";
            }
            if (window.page === "setup" && (s === "setup" || s === "failed")) {
                window.following = true;
            } else if (window.page === "setup" && window.following && (s === "none" || s === "")) {
                // Start over removed what setup had made: begin again from
                // the checks, which run afresh.
                window.following = false;
                window.problem = "";
                window.startError = "";
                window.page = "check";
                Setup.recheck();
                return;
            }
            const kind = (Setup.status.problem || {}).kind || "";
            if (s === "failed" && window.page === "setup") {
                window.problem = "failed";
            } else if ((kind === "download" || kind === "space") && window.page === "setup") {
                window.problem = kind;
            } else if ((window.problem === "download" || window.problem === "space") && kind !== window.problem) {
                // Back online, or the space was freed: setup has carried on.
                window.problem = "";
            }
        }
    }

    function startSetup() {
        startError = Setup.startSetup(false);
        if (startError.length === 0) {
            page = "setup";
        } else {
            problem = "failed";
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            current: window.step
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ["check", "options", "setup", "ready"].indexOf(window.page)

            StepCheck {
                onNext: window.page = "options"
                onLater: window.close()
                onProblem: kind => window.problem = kind
            }

            // 9.4, the licence among the options since 3.10 shares the page.
            OptionsPage {
                mainText: "Set up"
                onBack: window.page = "check"
                onConfirmed: window.startSetup()
            }

            StepSetup {
                onCloseRequested: window.close()
            }

            StepReady {
                onDone: window.close()
            }
        }
    }

    // Closing is always possible until setup starts; from then on the
    // setup and ready steps have their own buttons, since closing no longer
    // stops what is under way.
    UI.IconButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 14
        anchors.rightMargin: 14
        visible: window.page === "check" || window.page === "options"
        iconName: "close"
        tip: "Close"
        onClicked: window.close()
    }

    T.Popup {
        id: problemPopup
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        visible: window.problem.length > 0
        padding: 0
        width: 420
        height: contentItem.implicitHeight
        closePolicy: T.Popup.CloseOnEscape
        onClosed: window.problem = ""
        T.Overlay.modal: Rectangle {
            radius: UI.Style.windowRadius
            color: UI.Style.scrim
        }
        background: null
        contentItem: ProblemCard {
            solid: true
            kind: window.problem
            received: (Setup.status.problem || {}).received || 0
            offline: (Setup.status.problem || {}).offline === true
            failure: window.startError.length > 0 ? window.startError : ((Setup.status.problem || {}).text || "")
            detail: window.startError.length > 0 ? "" : ((Setup.status.problem || {}).detail || "")
            onDismissed: problemPopup.close()
            onRetry: {
                if (window.startError.length > 0 || Setup.status.state === "failed") {
                    window.startError = "";
                    window.page = "options";
                }
            }
        }
        onOpened: contentItem.takeFocus()
    }

    Loader {
        anchors.fill: parent
        active: window.board === "problems"
        sourceComponent: ProblemsBoard {
            backdrop: window.backdrop
        }
    }
}
