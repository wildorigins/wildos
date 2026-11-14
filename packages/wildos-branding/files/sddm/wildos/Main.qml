import QtQuick
import QtQuick.Effects
import WildOS.UI as UI

import "components"
import "components/messages.js" as Messages

// Wild OS sign-in screen for SDDM 0.21 (Qt 6). Visual spec: Lock.dc.html.
// SDDM provides the context objects sddm, userModel, sessionModel, keyboard
// and config. The controls are WildOS.UI's; the greeter runs as the sddm
// user, which has no settings of its own, so they take the Glacier accent
// and the dark scheme.
Item {
    id: root

    width: 1440
    height: 900

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    readonly property bool hasUserList: userModel.count > 0
    property int userIndex: hasUserList ? Math.max(0, Math.min(userModel.lastIndex, userModel.count - 1)) : -1
    property int sessionIndex: sessionModel.lastIndex >= 0 ? sessionModel.lastIndex : 0
    property bool optionsOpen: false
    property bool busy: false
    property string failureMessage: ""
    // The hint the person set with their password, shown after a wrong one.
    property string passwordHint: ""
    property string infoMessage: ""
    // Set when PAM explains that signing in is blocked for now, so the
    // failure that follows does not claim the password was wrong.
    property bool blocked: false
    // An empty password and Enter go to the fingerprint reader for an
    // account with a finger enrolled (wildos-desktop's wildos-fingerprint
    // PAM profile). Whose finger is enrolled can't be seen from here, so the
    // hint follows the reader alone.
    property bool fingerprintReader: false
    // Advanced protection can ask for the password once after a restart
    // and again after some hours; until then the finger is turned down, so
    // it isn't offered.
    readonly property bool fingerprintNow: fingerprintReader && UI.System.fingerprintAllowed !== undefined
        && UI.System.fingerprintAllowed(hasUserList ? currentUserName : usernamePill.text.trim())
    property bool triedFingerprint: false

    // Values for the chosen user, read through a Repeater because SDDM's
    // models only expose their roles to delegates.
    property string currentUserName: ""
    property string currentRealName: ""
    property string currentIcon: ""
    property bool currentNeedsPassword: true
    // The guest session's account (Advanced protection > Guest session),
    // made without a password; only it is called guest and has none.
    property bool currentIsGuest: false
    property string currentSessionName: ""

    function signIn() {
        if (busy)
            return;
        const user = hasUserList ? currentUserName : usernamePill.text.trim();
        if (user.length === 0) {
            usernamePill.field.forceActiveFocus();
            return;
        }
        failureMessage = "";
        infoMessage = "";
        busy = true;
        triedFingerprint = fingerprintNow && passwordPill.text.length === 0;
        sddm.login(user, passwordPill.text, sessionIndex);
    }

    function focusPassword() {
        if (!hasUserList && usernamePill.text.length === 0)
            usernamePill.field.forceActiveFocus();
        else if (hasUserList && !currentNeedsPassword)
            passwordlessButton.forceActiveFocus();
        else
            passwordPill.field.forceActiveFocus();
    }

    Connections {
        target: sddm

        function onLoginFailed() {
            root.busy = false;
            root.failureMessage = root.blocked ? Messages.unavailable : root.triedFingerprint ? Messages.fingerprintFailed : Messages.wrongPassword;
            root.passwordHint = root.blocked || root.triedFingerprint ? "" : UI.System.passwordHint(root.hasUserList ? root.currentUserName : usernamePill.text.trim());
            root.infoMessage = "";
            root.blocked = false;
            passwordPill.text = "";
            passwordPill.shake();
            root.focusPassword();
        }

        function onLoginSucceeded() {
            fadeOut.start();
        }

        // PAM text is never shown raw; only a recognised reason that signing
        // in is blocked comes through, as a plain sentence.
        function onInformationMessage(message) {
            const finger = Messages.fingerprint(message);
            if (finger !== null) {
                root.infoMessage = finger;
                return;
            }
            const plain = Messages.translate(message);
            if (plain.length > 0) {
                root.blocked = true;
                root.infoMessage = plain;
            }
        }
    }

    // Plain lists of the users and sessions for the choice menus, filled in
    // by the same delegates.
    property var users: []
    property var sessions: []

    Repeater {
        model: userModel
        delegate: Item {
            required property int index
            required property string name
            required property string realName
            required property string icon
            required property bool needsPassword
            readonly property bool chosen: index === root.userIndex
            readonly property bool guest: name === "guest" && !needsPassword
            visible: false
            onChosenChanged: sync()
            Component.onCompleted: {
                const list = root.users.slice();
                list[index] = { name: name, realName: realName, guest: guest };
                root.users = list;
                // The screen opens on the PC's own people, even after a
                // guest was the last to sign in.
                if (chosen && guest && userModel.count > 1) {
                    root.userIndex = index === 0 ? 1 : 0;
                    return;
                }
                sync();
            }
            function sync() {
                if (!chosen)
                    return;
                root.currentUserName = name;
                root.currentRealName = realName.length > 0 ? realName : name;
                root.currentIcon = icon;
                root.currentNeedsPassword = needsPassword;
                root.currentIsGuest = guest;
            }
        }
    }

    Repeater {
        model: sessionModel
        delegate: Item {
            required property int index
            required property string name
            readonly property bool chosen: index === root.sessionIndex
            visible: false
            onChosenChanged: if (chosen) root.currentSessionName = name
            Component.onCompleted: {
                const list = root.sessions.slice();
                list[index] = name;
                root.sessions = list;
                if (chosen)
                    root.currentSessionName = name;
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: UI.Style.lakeNavy
    }

    Item {
        id: scenery
        anchors.fill: parent

        Image {
            id: wallpaper

            readonly property real coverScale: implicitWidth > 0
                ? Math.max(scenery.width / implicitWidth, scenery.height / implicitHeight) : 1

            // object-fit: cover with object-position: center 45%, so the lake
            // keeps the same framing on every aspect ratio.
            width: implicitWidth * coverScale
            height: implicitHeight * coverScale
            x: (scenery.width - width) / 2
            y: (scenery.height - height) * 0.45
            source: {
                const configured = config.background ? String(config.background) : "";
                if (configured.length === 0)
                    return "";
                return configured.indexOf("/") === 0 ? "file://" + configured : configured;
            }
            asynchronous: false
            cache: false
            smooth: true
            mipmap: true
            opacity: status === Image.Ready ? 1 : 0
        }

        Rectangle {
            anchors.fill: parent
            color: UI.Style.wallpaperDim
        }
    }

    MouseArea {
        // Clicking empty space returns focus to the password field, so typing
        // always lands somewhere useful.
        anchors.fill: parent
        onClicked: {
            root.optionsOpen = false;
            root.focusPassword();
        }
    }

    // Straight after boot, the start-up animation's last moments play here
    // before the controls appear: see components/BootHandoff.qml.
    BootHandoff {
        id: handoff
        anchors.fill: parent
        photoSource: wallpaper.source
        photoItem: wallpaper
        input: passwordPill
        secondInput: usernamePill
    }

    Item {
        id: stage
        anchors.fill: parent
        opacity: handoff.stageOpacity

        LockClock {
            id: clock
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(root.height * 96 / 900)
            transform: Translate { y: handoff.clockShift }
        }

        Column {
            id: signInBlock

            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(clock.y + clock.height + 48, Math.round(root.height * 500 / 900))
            width: 400
            spacing: 14
            opacity: handoff.uiOpacity

            UI.Avatar {
                anchors.horizontalCenter: parent.horizontalCenter
                size: 88
                ring: true
                shadow: true
                // SDDM hands every user without a face the stock .face.icon
                // from its faces directory; that counts as no picture, so the
                // Wild OS default shows instead.
                source: {
                    const icon = root.hasUserList ? root.currentIcon : "";
                    if (icon.length === 0 || /\/sddm\/faces\/\.face\.icon$/.test(icon)) {
                        return "";
                    }
                    return icon.indexOf("/") === 0 ? "file://" + icon : icon;
                }
            }

            UI.Label {
                id: nameLabel
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.hasUserList ? root.currentRealName : "Sign in"
                size: 18
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkOnWallpaper
                bottomPadding: 6
                layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: UI.Style.textShadowOnWallpaper
                    shadowBlur: 0.5
                    blurMax: 24
                    shadowVerticalOffset: 1
                    shadowHorizontalOffset: 0
                }
            }

            PasswordPill {
                id: usernamePill
                visible: !root.hasUserList
                anchors.horizontalCenter: parent.horizontalCenter
                backdrop: scenery
                placeholder: "Username"
                secret: false
                showSubmit: false
                KeyNavigation.tab: passwordPill.field
                onSubmitted: passwordPill.field.forceActiveFocus()
            }

            PasswordPill {
                id: passwordPill
                // An account without a password signs in with one button.
                visible: !root.hasUserList || root.currentNeedsPassword
                anchors.horizontalCenter: parent.horizontalCenter
                backdrop: scenery
                focus: true
                placeholder: "Password"
                submitLabel: "Sign in"
                busy: root.busy
                onSubmitted: root.signIn()
                onTextChanged: if (text.length > 0) root.failureMessage = ""
            }

            UI.Button {
                id: passwordlessButton
                visible: !passwordPill.visible
                anchors.horizontalCenter: parent.horizontalCenter
                width: 300
                kind: "primary"
                size: "large"
                busy: root.busy
                text: "Sign in"
                onClicked: root.signIn()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
            }

            Column {
                id: notices
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                spacing: 6
                visible: failureNotice.shown || hintNotice.shown || capsNotice.shown || infoNotice.shown || fingerprintNotice.shown || guestNotice.shown

                Notice {
                    id: failureNotice
                    shown: root.failureMessage.length > 0
                    text: root.failureMessage
                }
                Notice {
                    id: hintNotice
                    shown: root.failureMessage.length > 0 && root.passwordHint.length > 0
                    text: "Hint: " + root.passwordHint
                    muted: true
                }
                Notice {
                    id: infoNotice
                    shown: root.infoMessage.length > 0 && root.failureMessage.length === 0
                    text: root.infoMessage
                }
                Notice {
                    id: fingerprintNotice
                    shown: root.fingerprintNow && passwordPill.visible && !root.busy && passwordPill.text.length === 0
                        && root.failureMessage.length === 0 && root.infoMessage.length === 0
                    text: "Or press Enter and touch the fingerprint reader"
                    muted: true
                }
                Notice {
                    id: guestNotice
                    shown: root.hasUserList && root.currentIsGuest && root.failureMessage.length === 0
                    text: "A clean desktop, wiped when you sign out."
                    muted: true
                }
                Notice {
                    id: capsNotice
                    shown: keyboard.capsLock
                    text: "Caps Lock is on"
                    iconName: "caps-lock"
                    muted: true
                }
            }

            UI.Button {
                id: optionsLink
                anchors.horizontalCenter: parent.horizontalCenter
                visible: userModel.count > 1 || sessionModel.count > 1 || (keyboard.layouts && keyboard.layouts.length > 1)
                kind: "text"
                textColor: UI.Style.inkOnWallpaper
                text: root.optionsOpen ? "Hide sign-in options" : "Sign-in options"
                onClicked: root.optionsOpen = !root.optionsOpen
            }

            Column {
                id: options
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                visible: opacity > 0
                opacity: root.optionsOpen ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: UI.Style.motionFast; easing.type: Easing.OutCubic } }

                ChoiceRow {
                    id: userChoice
                    visible: userModel.count > 1
                    backdrop: scenery
                    iconName: "account"
                    label: "User"
                    value: root.currentRealName
                    choices: root.users.map((user, index) => ({
                        text: user.realName.length > 0 ? user.realName : user.name,
                        detail: user.guest ? "Wiped when they sign out" : user.realName.length > 0 && user.realName !== user.name ? user.name : "",
                        checked: index === root.userIndex,
                        run: () => {
                            root.userIndex = index;
                            root.failureMessage = "";
                            passwordPill.text = "";
                            root.focusPassword();
                        }
                    }))
                }

                // Only offered when there is more than one way to sign in.
                ChoiceRow {
                    id: sessionChoice
                    visible: sessionModel.count > 1
                    backdrop: scenery
                    iconName: "desktop"
                    label: "Session"
                    value: root.currentSessionName
                    choices: root.sessions.map((name, index) => ({
                        text: name,
                        checked: index === root.sessionIndex,
                        run: () => {
                            root.sessionIndex = index;
                            root.focusPassword();
                        }
                    }))
                }

                ChoiceRow {
                    id: layoutChoice
                    visible: keyboard.layouts && keyboard.layouts.length > 1
                    backdrop: scenery
                    iconName: "keyboard"
                    label: "Keyboard"
                    value: visible && keyboard.layouts[keyboard.currentLayout]
                           ? keyboard.layouts[keyboard.currentLayout].longName : ""
                    choices: {
                        const list = [];
                        const layouts = keyboard.layouts || [];
                        for (let i = 0; i < layouts.length; ++i) {
                            const index = i;
                            list.push({
                                text: layouts[i].longName,
                                checked: index === keyboard.currentLayout,
                                run: () => {
                                    keyboard.currentLayout = index;
                                    root.focusPassword();
                                }
                            });
                        }
                        return list;
                    }
                }
            }
        }

        EncryptedChip {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            backdrop: scenery
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            spacing: 10

            RoundButton {
                id: keyboardButton
                visible: keyboardLoader.status === Loader.Ready
                backdrop: scenery
                checkable: true
                checked: keyboardLoader.item ? keyboardLoader.item.wanted : false
                iconName: "accessibility"
                label: checked ? "Hide on-screen keyboard" : "Show on-screen keyboard"
                onClicked: {
                    keyboardLoader.item.wanted = !keyboardLoader.item.wanted;
                    root.focusPassword();
                    if (keyboardLoader.item.wanted)
                        Qt.inputMethod.show();
                    else
                        Qt.inputMethod.hide();
                }
            }

            RoundButton {
                id: powerButton

                // Only what this computer can do is offered, in the order
                // Windows lists them.
                readonly property var actions: {
                    const list = [];
                    if (sddm.canSuspend)
                        list.push({ text: "Sleep", icon: "moon", run: () => sddm.suspend() });
                    if (sddm.canHibernate)
                        list.push({ text: "Hibernate", icon: "hibernate", run: () => sddm.hibernate() });
                    if (sddm.canPowerOff)
                        list.push({ text: "Shut down", icon: "power", run: () => sddm.powerOff() });
                    if (sddm.canReboot)
                        list.push({ text: "Restart", icon: "restart", run: () => sddm.reboot() });
                    return list;
                }

                visible: actions.length > 0
                backdrop: scenery
                iconName: "power"
                label: "Power"
                checked: powerMenu.opened
                onClicked: powerMenu.opened ? powerMenu.close() : powerMenu.open()

                UI.Menu {
                    id: powerMenu
                    x: powerButton.width - width
                    y: -height - 10
                }

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

    Loader {
        id: keyboardLoader
        anchors.left: parent.left
        anchors.right: parent.right
        height: item ? item.height : 0
        y: parent.height - height
        z: 10
        source: "components/VirtualKeyboard.qml"
    }

    NumberAnimation {
        id: fadeOut
        target: stage
        property: "opacity"
        to: 0
        duration: UI.Style.motionNormal
        easing.type: UI.Style.easeExit
    }

    // The accessibility shortcuts from Settings > Accessibility, here before
    // anyone has signed in. This screen's KWin has no global shortcuts, so
    // the magnifier keys are handled here too. What the last person to sign
    // out left on is copied to this screen's own settings
    // (/usr/libexec/wildos/greeter-access), so it comes back on here.
    UI.AccessShortcuts {
        id: access
        magnifier: true
        onKeyboardRequested: {
            if (keyboardLoader.item)
                keyboardButton.clicked();
        }
    }

    Component.onCompleted: {
        focusPassword();
        access.restore();
        // Asking fprintd starts it, which can take a moment; the screen is
        // up first.
        Qt.callLater(() => root.fingerprintReader = UI.System.fingerprintReader !== undefined && UI.System.fingerprintReader());
        if (access.keyboardOn && keyboardLoader.item) {
            keyboardLoader.item.wanted = true;
            Qt.inputMethod.show();
        }
    }
}
