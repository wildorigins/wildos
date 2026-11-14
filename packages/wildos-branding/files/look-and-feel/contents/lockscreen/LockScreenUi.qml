import QtQml
import QtQuick
import QtQuick.Controls

import org.kde.plasma.workspace.components as PW
import org.kde.plasma.private.keyboardindicator as KeyboardIndicator
import org.kde.plasma.private.sessions
import org.kde.plasma.private.battery
import org.kde.breeze.components as Breeze
import WildOS.UI as UI

import "components"
import "components/messages.js" as Messages

// Wild OS lock screen. The behaviour follows Plasma's own lock screen line for
// line (authenticator handling, grace lock, fade out when idle, virtual
// keyboard, media controls, sleep, hibernate, switch user, battery and the
// on-screen display); only the look is Wild OS. Visual spec: Lock.dc.html.
Item {
    id: lockScreenUi

    property Item lockRoot

    readonly property bool hasConfig: typeof config !== "undefined" && config !== null
    readonly property Item backdrop: typeof wallpaper !== "undefined" ? wallpaper : null
    readonly property string userImageUrl: kscreenlocker_userImage !== ""
        ? "file://" + kscreenlocker_userImage.split("/").map(encodeURIComponent).join("/")
        : ""

    // Set when PAM explains that signing in is blocked for now, so the
    // failure that follows does not claim the password was wrong.
    property bool blocked: false

    // kscreenlocker runs the fingerprint (/etc/pam.d/kde-fingerprint) beside
    // the password and lists it in authenticatorTypes (Fingerprint is bit
    // 1) while pam_fprintd waits for a finger. fprintd turns it down at
    // once when there is no reader or no finger enrolled, so the hint waits
    // a second rather than flash on every lock.
    readonly property bool fingerprintOffered: (authenticator.authenticatorTypes & 1) !== 0
    property bool fingerprintReady: false
    onFingerprintOfferedChanged: {
        if (fingerprintOffered) {
            fingerprintTimer.restart();
        } else {
            fingerprintTimer.stop();
            fingerprintReady = false;
        }
    }

    Timer {
        id: fingerprintTimer
        interval: 1000
        onTriggered: lockScreenUi.fingerprintReady = lockScreenUi.fingerprintOffered
    }

    function showMessage(msg) {
        if (!msg)
            return;
        if (lockRoot.notification === msg) {
            lockRoot.notificationRepeated();
        } else {
            lockRoot.notification = msg;
        }
    }

    function handlePamMessage(msg) {
        const plain = Messages.translate(msg);
        if (plain.length === 0)
            return;
        blocked = true;
        showMessage(plain);
        notificationRemoveTimer.restart();
    }

    Connections {
        target: authenticator

        function onFailed(kind) {
            // A finger that doesn't match is retried by pam_fprintd itself,
            // and smartcards are not part of Wild OS, so only the password's
            // failures reach the screen.
            if (kind != 0)
                return;
            lockScreenUi.showMessage(lockScreenUi.blocked ? Messages.unavailable : Messages.wrongPassword);
            mainBlock.passwordHint = lockScreenUi.blocked ? "" : UI.System.passwordHint();
            graceLockTimer.restart();
            notificationRemoveTimer.restart();
            mainBlock.shake();
        }

        function onSucceeded() {
            if (authenticator.hadPrompt) {
                Qt.quit();
            } else {
                mainStack.replace(null, Qt.resolvedUrl("NoPasswordUnlock.qml"), {
                    backdrop: Qt.binding(() => lockScreenUi.backdrop),
                    screenHeight: Qt.binding(() => lockScreenRoot.height),
                    topEdge: Qt.binding(() => clock.y + clock.height + 48),
                    userName: kscreenlocker_userName,
                    userImage: lockScreenUi.userImageUrl,
                }, StackView.Immediate);
                mainStack.forceActiveFocus();
            }
        }

        function onInfoMessageChanged() {
            lockScreenUi.handlePamMessage(authenticator.infoMessage);
        }

        function onErrorMessageChanged() {
            lockScreenUi.handlePamMessage(authenticator.errorMessage);
        }

        // The prompt text itself ("Password: ") is already said by the
        // placeholder, so it is not repeated under the pill.

        function onPromptForSecretChanged(msg) {
            mainBlock.mainPasswordBox.forceActiveFocus();
        }
    }

    SessionManagement {
        id: sessionManagement
    }

    Connections {
        target: sessionManagement
        function onAboutToSuspend() {
            lockRoot.clearPassword();
        }
    }

    KeyboardIndicator.KeyState {
        id: capsLockState
        key: Qt.Key_CapsLock
    }

    BatteryControlModel {
        id: batteryControl
    }

    PW.KeyboardLayoutSwitcher {
        id: keyboardLayoutSwitcher
        width: 0
        height: 0
        acceptedButtons: Qt.NoButton
    }

    Rectangle {
        anchors.fill: parent
        color: UI.Style.wallpaperDim
    }

    MouseArea {
        id: lockScreenRoot

        property bool uiVisible: false
        property bool seenPositionChange: false
        property bool blockUI: containsMouse && (mainStack.depth > 1 || mainBlock.mainPasswordBox.text.length > 0 || inputPanel.keyboardActive)

        x: parent.x
        y: parent.y
        width: parent.width
        height: parent.height
        hoverEnabled: true
        cursorShape: uiVisible ? Qt.ArrowCursor : Qt.BlankCursor
        drag.filterChildren: true
        onPressed: uiVisible = true
        onPositionChanged: {
            uiVisible = seenPositionChange;
            seenPositionChange = true;
        }
        onUiVisibleChanged: {
            if (blockUI) {
                fadeoutTimer.running = false;
            } else if (uiVisible) {
                fadeoutTimer.restart();
            }
            authenticator.startAuthenticating();
        }
        onBlockUIChanged: {
            if (blockUI) {
                fadeoutTimer.running = false;
                uiVisible = true;
            } else {
                fadeoutTimer.restart();
            }
        }
        onExited: {
            uiVisible = false;
        }
        Keys.onEscapePressed: {
            // Escape asks kscreenlocker to turn the screen off, so the prompt
            // should not appear for it.
            if (uiVisible) {
                uiVisible = false;
                if (inputPanel.keyboardActive) {
                    inputPanel.showHide();
                }
                lockRoot.clearPassword();
            }
        }
        Keys.onPressed: event => {
            uiVisible = true;
            event.accepted = false;
        }

        Timer {
            id: fadeoutTimer
            interval: 10000
            onTriggered: {
                if (!lockScreenRoot.blockUI) {
                    lockScreenRoot.uiVisible = false;
                    mainBlock.optionsOpen = false;
                }
            }
        }
        Timer {
            id: notificationRemoveTimer
            interval: 3000
            onTriggered: {
                lockRoot.notification = "";
                lockScreenUi.blocked = false;
            }
        }
        Timer {
            id: graceLockTimer
            interval: 3000
            onTriggered: {
                lockRoot.clearPassword();
                authenticator.startAuthenticating();
            }
        }

        opacity: 0
        Component.onCompleted: launchAnimation.start()
        NumberAnimation {
            id: launchAnimation
            target: lockScreenRoot
            property: "opacity"
            from: 0
            to: 1
            duration: UI.Style.motionSlow
            easing.type: Easing.OutCubic
        }

        LockClock {
            id: clock
            visible: !lockScreenUi.hasConfig || config.alwaysShowClock !== false
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(lockScreenRoot.height * 96 / 900) + mainStack.y
        }

        LockNotices {
            anchors.horizontalCenter: parent.horizontalCenter
            y: clock.y + clock.height + 20
            backdrop: lockScreenUi.backdrop
            availableHeight: mainStack.y + mainBlock.contentTop - y - 20
        }

        StackView {
            id: mainStack

            anchors.left: parent.left
            anchors.right: parent.right
            height: lockScreenRoot.height
            focus: true
            visible: opacity > 0
            opacity: lockScreenRoot.uiVisible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: UI.Style.motionSlow; easing.type: UI.Style.easeStandard } }
            // The sign-in block rises the last few pixels as it fades in, and
            // sinks as it fades away again.
            transform: Translate {
                y: (1 - mainStack.opacity) * UI.Style.travelMedium
            }

            initialItem: MainBlock {
                id: mainBlock

                backdrop: lockScreenUi.backdrop
                screenHeight: lockScreenRoot.height
                topEdge: Math.round(lockScreenRoot.height * 96 / 900) + clock.height + 48
                lockScreenUiVisible: lockScreenRoot.uiVisible
                enabled: !graceLockTimer.running

                userName: kscreenlocker_userName
                userImage: lockScreenUi.userImageUrl
                capsLockOn: capsLockState.locked
                fingerprintReady: lockScreenUi.fingerprintReady
                notificationMessage: lockRoot.notification
                canSwitchUser: sessionManagement.canSwitchUser
                layoutSwitcher: keyboardLayoutSwitcher
                showMediaControls: lockScreenUi.hasConfig && config.showMediaControls === true

                StackView.onStatusChanged: {
                    // Prepare for being shown again after another page.
                    if (StackView.status === StackView.Activating) {
                        mainPasswordBox.clear();
                        mainPasswordBox.focus = true;
                        lockRoot.notification = "";
                    }
                }

                onPasswordResult: password => authenticator.respond(password)
                onSwitchUserRequested: sessionManagement.switchUser()

                Connections {
                    target: lockRoot
                    function onClearPassword() {
                        mainBlock.mainPasswordBox.forceActiveFocus();
                        mainBlock.mainPasswordBox.text = "";
                        mainBlock.mainPasswordBox.text = Qt.binding(() => PasswordSync.password);
                    }
                    function onNotificationRepeated() {
                        mainBlock.playHighlightAnimation();
                    }
                }
            }
        }

        Breeze.VirtualKeyboardLoader {
            id: inputPanel

            z: 1

            screenRoot: lockScreenRoot
            mainStack: mainStack
            mainBlock: mainBlock
            passwordField: mainBlock.mainPasswordBox
        }

        Loader {
            z: 2
            active: lockRoot.viewVisible
            source: "LockOsd.qml"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 96
        }

        EncryptedChip {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            backdrop: lockScreenUi.backdrop
        }

        Row {
            id: footer

            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            spacing: 10
            visible: opacity > 0
            opacity: lockScreenRoot.uiVisible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: UI.Style.motionSlow; easing.type: UI.Style.easeStandard } }

            Item {
                id: battery

                visible: batteryControl.hasInternalBatteries
                width: batteryRow.implicitWidth + 28
                height: 44
                Accessible.role: Accessible.StaticText
                Accessible.name: "Battery at " + batteryControl.percent + "%"

                Glass {
                    anchors.fill: parent
                    backdrop: lockScreenUi.backdrop
                    tint: UI.Style.glassOnWallpaper
                    blurRadius: 64
                }

                Row {
                    id: batteryRow
                    anchors.centerIn: parent
                    spacing: 6

                    PW.BatteryIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        pluggedIn: batteryControl.pluggedIn
                        hasBattery: batteryControl.hasCumulative
                        percent: batteryControl.percent
                    }

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: batteryControl.percent + "%"
                        color: UI.Style.inkOnWallpaper
                    }
                }
            }

            RoundButton {
                id: keyboardButton
                visible: inputPanel.status === Loader.Ready
                backdrop: lockScreenUi.backdrop
                checked: inputPanel.keyboardActive
                iconName: "accessibility"
                label: inputPanel.keyboardActive ? "Hide on-screen keyboard" : "Show on-screen keyboard"
                focusPolicy: Qt.TabFocus
                onClicked: {
                    // Keep focus in the password field, otherwise keys typed on
                    // the virtual keyboard go nowhere.
                    mainBlock.mainPasswordBox.forceActiveFocus();
                    inputPanel.showHide();
                }
            }

            RoundButton {
                id: powerButton

                // As on Windows: sleep, shut down and restart from the lock
                // screen. Shut down and restart go through the session
                // manager, as from Start, so apps are closed properly.
                readonly property var actions: {
                    const list = [];
                    if (lockRoot.suspendToRamSupported) {
                        list.push({ text: "Sleep", icon: "moon", run: () => lockRoot.suspendToRam() });
                    }
                    if (lockRoot.suspendToDiskSupported) {
                        list.push({ text: "Hibernate", icon: "hibernate", run: () => lockRoot.suspendToDisk() });
                    }
                    if (sessionManagement.canShutdown) {
                        list.push({ text: "Shut down", icon: "power", run: () => sessionManagement.requestShutdown(SessionManagement.ConfirmationMode.Skip) });
                    }
                    if (sessionManagement.canReboot) {
                        list.push({ text: "Restart", icon: "restart", run: () => sessionManagement.requestReboot(SessionManagement.ConfirmationMode.Skip) });
                    }
                    return list;
                }

                visible: actions.length > 0
                backdrop: lockScreenUi.backdrop
                iconName: "power"
                label: "Power"
                checked: powerMenu.opened
                focusPolicy: Qt.TabFocus
                onClicked: powerMenu.opened ? powerMenu.close() : powerMenu.open()

                UI.Menu {
                    id: powerMenu
                    x: powerButton.width - width
                    y: -height - 10
                }

                // Support for each action is reported after start-up, so the
                // rows follow a live list rather than being fixed items.
                Instantiator {
                    model: powerButton.actions
                    delegate: UI.MenuItem {
                        required property var modelData
                        // The power menu's rows are 40px (PowerMenu.dc).
                        implicitHeight: 40
                        text: modelData.text
                        iconName: modelData.icon
                        onTriggered: modelData.run()
                    }
                    onObjectAdded: (index, object) => powerMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => powerMenu.removeItem(object)
                }
            }
        }
    }

    // The accessibility shortcuts from Settings > Accessibility. The lock
    // screen lets only KWin's magnifier keys through as global shortcuts, so
    // the rest are caught here and run the same switches as on the desktop.
    UI.AccessShortcuts {
        id: access
        onKeyboardRequested: {
            if (inputPanel.status !== Loader.Ready)
                return;
            mainBlock.mainPasswordBox.forceActiveFocus();
            inputPanel.showHide();
        }
    }

    // Someone who uses the on-screen keyboard gets it here straight away.
    Timer {
        running: access.keyboardOn
        interval: 300
        onTriggered: {
            if (inputPanel.status === Loader.Ready && !inputPanel.keyboardActive) {
                mainBlock.mainPasswordBox.forceActiveFocus();
                inputPanel.showHide();
            }
        }
    }
}
