import QtQuick
import QtQuick.Effects
import QtQuick.Window
import WildOS.Welcome
import WildOS.UI as UI

// The first run: a glass sheet over the wallpaper, dimmed, as drawn in the
// First run artboards (3.3 to 3.10). It covers the whole screen so nothing
// else competes for attention until setup is done.
//
// With setupMode it is the first boot setup session (wildos-setup), all
// seven steps before any account exists, with no way out but finishing:
// the account is made at step 3, and steps 4 to 7 are saved for it and
// applied when Finish signs in to it. Without, it is the same four steps
// for an account made later, which apply as they are chosen.
Window {
    id: root

    property bool windowed: false
    property bool setupMode: false
    property int startStep: 1
    property int step: Math.min(startStep, stepNames.length)

    // Step 7 is Windows mode (3.9 and 3.10), only when Windows is on this
    // PC and Windows mode is installed.
    readonly property bool windowsStep: windowsMode.item !== null && windowsMode.item.windowsFound
    // Its two pages: 1 asks (3.9), 2 has the options (3.10).
    property int windowsPage: 1
    // Set up Windows mode was chosen on page 1.
    property bool windowsChosen: false
    property string windowsError

    readonly property var stepNames: (setupMode ? [qsTr("Region and language"), qsTr("Wi-Fi"), qsTr("Your account")] : [])
        .concat([qsTr("Make it feel like home"), qsTr("Bring your files across"), qsTr("Your privacy")])
        .concat(windowsStep ? [qsTr("Your Windows programs")] : [])

    width: 1440
    height: 900
    visible: true
    visibility: windowed ? Window.Windowed : Window.FullScreen
    flags: windowed ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)
    color: UI.Style.lakeNavy
    title: qsTr("Welcome to Wild OS")

    // Nothing may close the setup session but finishing it: the session
    // ends with this window, and SDDM would only start it again.
    onClosing: close => {
        if (setupMode) {
            close.accepted = false;
        }
    }

    // Wi-Fi has nothing to do on a computer a cable already has online.
    readonly property bool skipWifi: setupMode && FirstBoot.wifi.wiredOnline && FirstBoot.wifi.online

    function next() {
        if (step < stepNames.length) {
            step += setupMode && step === 1 && skipWifi ? 2 : 1;
            windowsPage = 1;
        } else {
            finish();
        }
    }

    // Finish goes to the desktop (board 9.11). Windows mode, if chosen,
    // starts setting up in the background: straight away for an account
    // that already exists, or at the new account's first sign-in, which
    // sends the request saved with its plan.
    function finish() {
        const windows = windowsStep && windowsChosen && windowsMode.item ? windowsMode.item.request() : "";
        if (setupMode) {
            try {
                WindowsImport.release();
                FirstBoot.finish({
                    home: Setup.plan(),
                    privacy: Privacy.plan(),
                    copy: WindowsImport.plan(),
                    windowsMode: windows
                });
            } catch (error) {
                console.warn("Finishing setup failed:", error);
                FirstBoot.fail(qsTr("Wild OS could not finish setting up. Restart the computer to try again."));
            }
        } else {
            if (windows.length > 0) {
                // Straight to the desktop (9.11), while the request is
                // passed on; back only to say why it couldn't start.
                root.visible = false;
                windowsError = windowsMode.item.start();
                if (windowsError.length > 0) {
                    root.visible = true;
                    return;
                }
            }
            Setup.finish();
            Setup.quit();
        }
    }

    function back() {
        if (stepNames[step - 1] === qsTr("Your Windows programs") && windowsPage === 2) {
            windowsPage = 1;
        } else if (step > 1) {
            step -= setupMode && step === 3 && skipWifi ? 2 : 1;
            windowsPage = 1;
        }
    }

    // Windows mode's checks start with the welcome, so they are done (and
    // step 7 is known to be there or not) by the time it comes.
    Loader {
        id: windowsMode
        source: "steps/WindowsModeProbe.qml"
    }

    // The photo and the dim over it, drawn once and blurred again for the
    // part the sheet covers, the way the artboard's backdrop filter does.
    Item {
        id: backdrop
        anchors.fill: parent

        Image {
            id: photo

            readonly property real scale: implicitWidth > 0 ? Math.max(parent.width / implicitWidth, parent.height / implicitHeight) : 1

            width: implicitWidth * scale
            height: implicitHeight * scale
            x: (parent.width - width) / 2
            // object-position: center 45%
            y: (parent.height - height) * 0.45
            source: Setup.wallpaper
            asynchronous: true
            smooth: true
        }

        Rectangle {
            anchors.fill: parent
            color: UI.Style.scrim
        }
    }

    Item {
        id: sheetMask
        anchors.fill: parent
        visible: false
        layer.enabled: true

        Rectangle {
            x: sheet.x
            y: sheet.y
            width: sheet.width
            height: sheet.height
            radius: Metrics.radiusSheet
        }
    }

    Rectangle {
        id: shadowSource
        x: sheet.x
        y: sheet.y
        width: sheet.width
        height: sheet.height
        radius: Metrics.radiusSheet
        color: UI.Style.ground
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: shadowSource
        source: shadowSource
        shadowEnabled: true
        shadowColor: UI.Style.shadowFlyout
        shadowBlur: 1.0
        blurMax: 64
        shadowScale: 1.0
        shadowVerticalOffset: 40
        autoPaddingEnabled: true
    }

    MultiEffect {
        anchors.fill: backdrop
        source: backdrop
        blurEnabled: true
        blur: 1.0
        blurMax: 64
        saturation: 0.4
        // Padding would stretch the mask past the sheet's edges.
        autoPaddingEnabled: false
        maskEnabled: true
        maskSource: sheetMask
    }

    Item {
        id: sheet
        width: Metrics.sheetWidth
        // Grows with larger text, up to the screen, then the step scrolls.
        height: Math.min(root.height - 48, Math.max(Metrics.sheetHeight, stepLoader.item ? stepLoader.item.implicitHeight + 2 : 0, sidebar.implicitHeight + 2))
        anchors.centerIn: parent

        Rectangle {
            anchors.fill: parent
            radius: Metrics.radiusSheet
            color: UI.Style.glassWindowDeep
            border.width: 1
            border.color: UI.Style.borderGlass
        }

        Sidebar {
            id: sidebar
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 1
            width: Metrics.sidebarWidth
            steps: root.stepNames
            current: root.step
            showNote: root.stepNames[root.step - 1] === qsTr("Make it feel like home")
            canLeave: !root.setupMode
            onLater: Setup.quit()
        }

        Loader {
            id: stepLoader
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.left: sidebar.right
            anchors.margins: 1
            anchors.leftMargin: 0
            focus: true
            // Next slides in from the right and Back from the left, as the
            // Windows setup pages do.
            transform: UI.Reveal {
                item: stepLoader
                key: root.step
                directional: true
                dx: UI.Style.travelMedium
            }
            sourceComponent: (root.setupMode ? [regionStep, wifiStep, accountStep] : [])
                .concat([homeStep, filesStep, privacyStep, root.windowsPage === 1 ? windowsAskStep : windowsOptionsStep])[root.step - 1]
        }
    }

    Component {
        id: regionStep
        RegionStep {
            onNext: root.next()
        }
    }

    Component {
        id: wifiStep
        WifiStep {
            onNext: root.next()
            onBack: root.back()
        }
    }

    Component {
        id: accountStep
        AccountStep {
            onNext: root.next()
            onBack: root.back()
        }
    }

    Component {
        id: homeStep
        HomeStep {
            onNext: root.next()
            onBack: root.back()
            canGoBack: root.step > 1
        }
    }

    Component {
        id: filesStep
        FilesStep {
            onNext: root.next()
            onBack: root.back()
        }
    }

    Component {
        id: privacyStep
        PrivacyStep {
            // The last step when step 7 isn't there; its checks are still
            // running if this is reached quickly, and Continue waits for them.
            last: !root.windowsStep && !(windowsMode.item && windowsMode.item.checking)
            waiting: windowsMode.item !== null && windowsMode.item.checking
            onNext: root.next()
            onBack: root.back()
        }
    }

    // Step 7's pages import Windows mode's module, so they load by URL: a
    // missing module then costs the step, never the welcome.
    Component {
        id: windowsAskStep
        Loader {
            id: askLoader
            source: "steps/WindowsAskStep.qml"
            focus: true

            Connections {
                target: askLoader.item
                function onBack() { root.back(); }
                function onNext() {
                    root.windowsChosen = true;
                    root.windowsPage = 2;
                }
                function onFinished() {
                    root.windowsChosen = false;
                    root.finish();
                }
            }
        }
    }

    Component {
        id: windowsOptionsStep
        Loader {
            id: optionsLoader
            source: "steps/WindowsOptionsStep.qml"
            focus: true
            onLoaded: item.error = Qt.binding(() => root.windowsError)

            Connections {
                target: optionsLoader.item
                function onBack() { root.back(); }
                function onFinished() { root.finish(); }
            }
        }
    }
}
