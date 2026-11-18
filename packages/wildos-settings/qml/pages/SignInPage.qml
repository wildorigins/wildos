import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Sign-in options (SignInOptions.dc): the ways to sign in (fingerprint,
// password, PIN, security key), where a fingerprint is used, then signing in
// automatically when the computer starts, restarting apps after signing
// in, and the way to the screen lock. Turning automatic sign-in on asks for
// your own password, so the system can keep the key that opens your saved
// passwords (KWallet) when nobody types one.
//
// With no fingerprint reader the Fingerprint row and "Use your fingerprint
// for" are left out rather than greyed, and come back when one is plugged
// in. The disk at start-up always takes the password: it is unlocked before
// any reader or account exists.
PageScroll {
    id: page

    readonly property bool autoOn: Users.autoLogin.length > 0 && Users.autoLogin === Users.me.userName
    readonly property bool autoOther: Users.autoLogin.length > 0 && Users.autoLogin !== Users.me.userName

    // ksmserver's loginMode, as KDE's session settings write it. It saves
    // the open apps at sign-out only in restorePreviousLogout; a session
    // someone saved by hand (restoreSavedSession) also counts as on.
    property int sessionRevision: 0
    readonly property bool restoreApps: {
        sessionRevision;
        return System.config("ksmserverrc", "General", "loginMode", "restorePreviousLogout") !== "emptySession";
    }

    function setRestoreApps(on) {
        System.setConfig("ksmserverrc", "General", "loginMode", on ? "restorePreviousLogout" : "emptySession");
        sessionRevision++;
    }

    Connections {
        target: Users
        function onDone(what, message) {
            if (what === "password") {
                if (message.length > 0) {
                    passwordDialog.close();
                    notice.show(qsTr("Password changed"), message, false);
                }
            } else if (what === "autologin") {
                autoDialog.close();
                notice.show(qsTr("Automatic sign-in"), message, false);
            } else if (what === "wallet") {
                walletDialog.close();
                notice.show(qsTr("Saved passwords"), message, false);
            }
        }
        function onFailed(what, reason) {
            if (what === "wallet" || what === "wallet-other") {
                walletDialog.needsOther = walletDialog.needsOther || what === "wallet-other";
                walletDialog.error = reason;
            } else if (what === "password") {
                passwordDialog.error = reason;
            } else if (what === "autologin") {
                autoToggle.checked = Qt.binding(() => page.autoOn);
                if (autoDialog.visible) {
                    autoDialog.error = reason;
                } else {
                    notice.show(qsTr("Automatic sign-in was not changed"), reason, true);
                }
            }
        }
    }

    Notice {
        id: notice
    }

    Connections {
        target: Pin
        function onDone(what) {
            pinDialog.close();
            if (what === "set") {
                notice.show(qsTr("PIN saved"), qsTr("Type it in the password box to sign in or unlock."), false);
            } else {
                notice.show(qsTr("PIN removed"), qsTr("Signing in and unlocking take your password."), false);
            }
        }
        function onFailed(reason) {
            if (pinDialog.visible) {
                pinDialog.error = reason;
            } else {
                notice.show(qsTr("The PIN was not changed"), reason, true);
            }
        }
    }

    Connections {
        target: Fingerprint
        function onFailed(what, reason) {
            if (what === "remove") {
                notice.show(qsTr("The fingerprint wasn't removed"), reason, true);
            } else if (what === "use") {
                notice.show(qsTr("Fingerprint use wasn't changed"), reason, true);
            } else if (!setupDialog.visible && what !== "password") {
                notice.show(qsTr("The fingerprint wasn't saved"), reason, true);
            }
        }
    }

    function changedText(iso) {
        if (!iso) {
            return qsTr("Sign in with your account's password");
        }
        const parts = iso.split("-");
        const changed = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
        const now = new Date();
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        if (changed.getTime() === today.getTime()) {
            return qsTr("Last changed today");
        }
        //: The locale whose month names this line uses: the code for your language, such as fr_FR
        const month = Qt.locale(qsTr("en_GB")).monthName(changed.getMonth(), Locale.LongFormat);
        return changed.getFullYear() === now.getFullYear() ? qsTr("Last changed in %1").arg(month) : qsTr("Last changed in %1 %2").arg(month).arg(changed.getFullYear());
    }

    function fingerprintText() {
        if (!Fingerprint.available) {
            return qsTr("This PC's fingerprint reader doesn't work with Wild OS yet");
        }
        if (Fingerprint.windowsKey) {
            return qsTr("This reader still holds a key from Windows Hello, so Wild OS can't use it");
        }
        const count = Fingerprint.fingers.length;
        return (Fingerprint.place.length > 0 ? qsTr("Reader found: %1.").arg(Fingerprint.place) : qsTr("Reader found.")) + " "
            + (count === 0 ? qsTr("No fingers added yet.") : count === 1 ? qsTr("1 finger added.") : qsTr("%1 fingers added.").arg(count));
    }

    // The saved passwords notification (wildos-setup-wallet) opens this
    // page with Navigator.request "wallet".
    function takeRequest() {
        if (Navigator.request === "wallet") {
            Navigator.request = "";
            walletDialog.open();
        }
    }

    Component.onCompleted: {
        takeRequest();
        // A reader can be plugged in, or fingers changed elsewhere, while
        // Settings is open.
        Fingerprint.reload();
    }

    Connections {
        target: Navigator
        function onRequestChanged() {
            page.takeRequest();
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Ways to sign in")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                // A reader that libfprint can't drive still gets the row,
                // so nobody wonders where the setting went.
                UI.SettingRow {
                    visible: Fingerprint.available || Fingerprint.unsupported
                    leading: PathIcon {
                        name: "fingerprint"
                        size: 20
                    }
                    title: qsTr("Fingerprint")
                    description: page.fingerprintText()

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Fingerprint.available && !Fingerprint.windowsKey
                        text: Fingerprint.fingers.length === 0 ? qsTr("Set up") : qsTr("Manage")
                        enabled: Fingerprint.busy === ""
                        onClicked: Fingerprint.fingers.length === 0 ? setupDialog.open() : manageDialog.open()
                    }
                }

                UI.SettingRow {
                    leading: PathIcon {
                        name: "key-round"
                        size: 20
                    }
                    title: qsTr("Password")
                    description: page.changedText(Users.me.passwordChanged)

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Change")
                        onClicked: passwordDialog.open()
                    }
                }

                UI.SettingRow {
                    visible: Pin.available
                    leading: PathIcon {
                        name: "pin-pad"
                        size: 20
                    }
                    title: qsTr("PIN")
                    description: Pin.isSet ? qsTr("Signs you in and unlocks the lock screen. Your password still works.")
                        : qsTr("A short number that signs you in and unlocks the lock screen instead of your password")

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        UI.Button {
                            visible: Pin.isSet
                            text: qsTr("Remove")
                            enabled: !Pin.busy
                            onClicked: Pin.remove()
                        }

                        UI.Button {
                            text: Pin.isSet ? qsTr("Change") : qsTr("Set up")
                            enabled: !Pin.busy
                            onClicked: pinDialog.open()
                        }
                    }
                }

                UI.SettingRow {
                    leading: PathIcon {
                        name: "security-key"
                        size: 20
                    }
                    title: qsTr("Security key")
                    description: qsTr("Sign in with a USB or NFC key, like a YubiKey")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Add")
                        onClicked: keyDialog.open()
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        visible: Fingerprint.available && !Fingerprint.windowsKey

        SectionTitle {
            text: qsTr("Use your fingerprint for")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    leading: PathIcon {
                        name: "log-in"
                        size: 20
                    }
                    title: qsTr("Sign in and unlock")
                    description: qsTr("At the sign-in screen and when the screen is locked")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Fingerprint.signIn
                        enabled: Fingerprint.switching === ""
                        Accessible.name: qsTr("Use your fingerprint to sign in and unlock")
                        onToggled: {
                            Fingerprint.setUse("signin", checked);
                            checked = Qt.binding(() => Fingerprint.signIn);
                        }
                    }
                }

                UI.SettingRow {
                    leading: PathIcon {
                        name: "shield-round"
                        size: 20
                    }
                    title: qsTr("Admin prompts")
                    description: qsTr("When an app asks to change system settings")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Fingerprint.admin
                        enabled: Fingerprint.switching === ""
                        Accessible.name: qsTr("Use your fingerprint for admin prompts")
                        onToggled: {
                            Fingerprint.setUse("admin", checked);
                            checked = Qt.binding(() => Fingerprint.admin);
                        }
                    }
                }

                UI.SettingRow {
                    leading: PathIcon {
                        name: "hard-drive"
                        size: 20
                    }
                    title: qsTr("Disk unlock at start-up")
                    description: qsTr("Always needs your password, so the disk stays encrypted until you type it")
                    status: qsTr("Password only")
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: Fingerprint.available && !Fingerprint.windowsKey
        spacing: 10

        PathIcon {
            name: "lock-round"
            size: 14
            strokeWidth: 2
            color: UI.Style.inkMuted
        }

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Your fingerprint is stored as a pattern on this PC (or on the reader itself), never as a picture, and never leaves it.")
            size: 12.5
            color: UI.Style.inkMuted
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Additional settings")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "power"
                    minHeight: 72
                    title: qsTr("Sign in automatically when this computer starts")
                    description: page.autoOther ? qsTr("Set up for %1. Turning it on here moves it to you.").arg(Users.autoLogin)
                        : qsTr("Anyone who turns the computer on gets straight to your files and saved passwords, so only use it on a computer that stays at home.")
                    status: page.autoOn ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        id: autoToggle
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.autoOn
                        enabled: Users.busy !== "autologin"
                        Accessible.name: qsTr("Sign in automatically when this computer starts")
                        onToggled: {
                            if (checked) {
                                checked = Qt.binding(() => page.autoOn);
                                autoDialog.open();
                            } else {
                                Users.setAutoLogin(false);
                            }
                        }
                    }
                }

                UI.SettingRow {
                    iconName: "restart"
                    minHeight: 72
                    title: qsTr("Restart my apps when I sign back in")
                    description: qsTr("Apps that were open when you signed out or restarted open again, where the app supports it")
                    status: page.restoreApps ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.restoreApps
                        Accessible.name: qsTr("Restart my apps when I sign back in")
                        onToggled: page.setRestoreApps(checked)
                    }
                }

                UI.SettingRow {
                    iconName: "clock"
                    title: qsTr("Screen lock")
                    description: qsTr("Lock after a while and ask for your password on wake")
                    chevron: true
                    onClicked: Navigator.open("screenlock")
                }
            }
        }
    }

    FingerprintSetup {
        id: setupDialog
    }

    // The fingers set up so far. fprintd files each under a finger's name,
    // but nobody was asked which finger it was, so they are numbered.
    UI.Dialog {
        id: manageDialog

        title: qsTr("Your fingerprints")
        text: qsTr("Each one signs you in on its own. Remove any you no longer use; your password always works.")
        onAboutToShow: Fingerprint.reload()

        Repeater {
            model: Fingerprint.fingers

            RowLayout {
                required property string modelData
                required property int index

                Layout.fillWidth: true
                spacing: 12

                PathIcon {
                    name: "fingerprint"
                    size: 20
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Fingerprint %1").arg(index + 1)
                    size: 14
                }

                UI.Button {
                    text: qsTr("Remove")
                    busy: Fingerprint.busy === modelData
                    enabled: Fingerprint.busy === ""
                    Accessible.name: qsTr("Remove fingerprint %1").arg(index + 1)
                    onClicked: Fingerprint.remove(modelData)
                }
            }
        }

        Connections {
            target: Fingerprint
            function onChanged() {
                if (manageDialog.visible && Fingerprint.fingers.length === 0) {
                    manageDialog.close();
                }
            }
        }

        buttons: [
            UI.Button {
                text: qsTr("Add another finger")
                visible: Fingerprint.fingers.length < 10
                enabled: Fingerprint.busy === ""
                onClicked: {
                    manageDialog.close();
                    setupDialog.open();
                }
            },
            UI.Button {
                text: qsTr("Close")
                kind: "primary"
                onClicked: manageDialog.close()
            }
        ]
    }

    // pam_u2f isn't part of Wild OS yet, so there is nothing to add a key
    // to; the row is here so the page reads as the design does.
    UI.Dialog {
        id: keyDialog

        title: qsTr("Security keys aren't ready yet")
        text: qsTr("Wild OS can't sign in with a USB or NFC security key yet. Your password, and your fingerprint if you set one up, keep working as they do now.")

        buttons: [
            UI.Button {
                text: qsTr("OK")
                kind: "primary"
                onClicked: keyDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: walletDialog

        property string error
        property bool needsOther: false

        title: qsTr("Open your saved passwords")
        text: needsOther
            ? qsTr("Your saved passwords use a different password from your account. Type both, and Wild OS moves them to your account password so they open when you sign in.")
            : page.autoOn
                ? qsTr("This computer signs you in automatically, so your saved passwords need your password once. After this they open by themselves at every start.")
                : qsTr("Type your account password to open your saved passwords. They open by themselves whenever you sign in with your password.")
        onAboutToShow: {
            error = "";
            needsOther = false;
            walletPasswordField.text = "";
            walletOtherField.text = "";
            walletPasswordField.forceActiveFocus();
        }

        UI.TextField {
            id: walletPasswordField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: qsTr("Your account password")
            revealable: true
            onTextChanged: walletDialog.error = ""
            onAccepted: walletDialog.needsOther ? walletOtherField.forceActiveFocus() : walletButton.clicked()
        }

        UI.TextField {
            id: walletOtherField
            UI.NoPaste {}
            Layout.fillWidth: true
            visible: walletDialog.needsOther
            placeholderText: qsTr("The password your saved passwords use")
            revealable: true
            onTextChanged: walletDialog.error = ""
            onAccepted: walletButton.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: walletDialog.error
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: walletButton
                text: qsTr("Continue")
                kind: "primary"
                busy: Users.busy === "wallet"
                enabled: walletPasswordField.text.length > 0 && (!walletDialog.needsOther || walletOtherField.text.length > 0)
                    && Users.busy !== "wallet"
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    walletDialog.error = "";
                    Users.openWallet(walletPasswordField.text, walletDialog.needsOther ? walletOtherField.text : "");
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                enabled: Users.busy !== "wallet"
                onClicked: walletDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: autoDialog

        property string error

        title: qsTr("Sign in automatically")
        text: Protection.status.encryption === "on"
            ? qsTr("Anyone who gets past the disk password when this computer starts will reach your files and your saved passwords, without your account password. Type your password so Wild OS can open your saved passwords for you.")
            : qsTr("Anyone who turns this computer on will reach your files and your saved passwords, without a password. Type your password so Wild OS can open your saved passwords for you.")
        onAboutToShow: {
            error = "";
            autoPasswordField.text = "";
            autoPasswordField.forceActiveFocus();
        }

        UI.TextField {
            id: autoPasswordField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: qsTr("Your password")
            revealable: true
            onTextChanged: autoDialog.error = ""
            onAccepted: turnOnButton.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: autoDialog.error
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: turnOnButton
                text: qsTr("Turn on")
                kind: "primary"
                busy: Users.busy === "autologin"
                enabled: autoPasswordField.text.length > 0 && Users.busy !== "autologin"
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    autoDialog.error = "";
                    Users.setAutoLogin(true, autoPasswordField.text);
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                enabled: Users.busy !== "autologin"
                onClicked: autoDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: pinDialog

        property string error
        readonly property string problem: {
            if (pinField.text.length > 0 && !Pin.valid(pinField.text)) {
                return qsTr("A PIN is 4 to 12 digits.");
            }
            if (pinConfirmField.text.length > 0 && pinConfirmField.text !== pinField.text) {
                return qsTr("The two PINs don't match.");
            }
            if (pinField.text.length > 0 && pinField.text === pinPasswordField.text) {
                return qsTr("The PIN is the same as your password.");
            }
            return "";
        }
        readonly property bool ready: pinPasswordField.text.length > 0 && Pin.valid(pinField.text)
            && pinConfirmField.text === pinField.text && problem.length === 0

        title: Pin.isSet ? qsTr("Change your PIN") : qsTr("Set up a PIN")
        text: qsTr("Type your password, then a PIN of 4 to 12 digits twice. The PIN signs you in and unlocks the lock screen. Administrator prompts and the encrypted disk still take your password.")
        onAboutToShow: {
            error = "";
            pinPasswordField.text = "";
            pinField.text = "";
            pinConfirmField.text = "";
            pinPasswordField.forceActiveFocus();
        }

        UI.TextField {
            id: pinPasswordField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: qsTr("Your password")
            revealable: true
            onTextChanged: pinDialog.error = ""
            onAccepted: pinField.forceActiveFocus()
        }

        UI.TextField {
            id: pinField
            UI.NoPaste {}
            Layout.fillWidth: true
            Layout.topMargin: 6
            placeholderText: qsTr("New PIN")
            revealable: true
            inputMethodHints: Qt.ImhDigitsOnly
            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,12}/ }
            error: text.length > 0 && !Pin.valid(text)
            onTextChanged: pinDialog.error = ""
            onAccepted: pinConfirmField.forceActiveFocus()
        }

        UI.TextField {
            id: pinConfirmField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: qsTr("Confirm the PIN")
            revealable: true
            inputMethodHints: Qt.ImhDigitsOnly
            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,12}/ }
            error: text.length > 0 && text !== pinField.text
            onAccepted: savePinButton.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: pinDialog.error.length > 0 ? pinDialog.error : pinDialog.problem
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: savePinButton
                text: qsTr("Save")
                kind: "primary"
                busy: Pin.busy
                enabled: pinDialog.ready && !Pin.busy
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    pinDialog.error = "";
                    Pin.save(pinPasswordField.text, pinField.text);
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                enabled: !Pin.busy
                onClicked: pinDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: passwordDialog

        property string error
        readonly property string problem: {
            if (confirmField.text.length > 0 && confirmField.text !== newField.text) {
                return qsTr("The two new passwords don't match.");
            }
            if (newField.text.length > 0 && newField.text === currentField.text) {
                return qsTr("The new password is the same as the current one.");
            }
            return "";
        }
        readonly property bool ready: currentField.text.length > 0 && newField.text.length > 0
            && confirmField.text === newField.text && problem.length === 0

        title: qsTr("Change your password")
        text: Protection.status.encryption === "on"
            ? qsTr("Type your current password, then the new one twice. The disk itself still unlocks with the password it was given when Wild OS was installed.")
            : qsTr("Type your current password, then the new one twice.")
        onAboutToShow: {
            error = "";
            currentField.text = "";
            newField.text = "";
            confirmField.text = "";
            hintField.text = Users.me.hint || "";
            currentField.forceActiveFocus();
        }

        UI.TextField {
            id: currentField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: qsTr("Current password")
            revealable: true
            onTextChanged: passwordDialog.error = ""
            onAccepted: newField.forceActiveFocus()
        }

        UI.TextField {
            id: newField
            UI.NoPaste {}
            Layout.fillWidth: true
            Layout.topMargin: 6
            placeholderText: qsTr("New password")
            revealable: true
            onTextChanged: passwordDialog.error = ""
            onAccepted: confirmField.forceActiveFocus()
        }

        StrengthLine {
            Layout.fillWidth: true
            password: newField.text
            oldPassword: currentField.text
            names: [Users.me.userName || "", Users.me.realName || ""]
        }

        UI.TextField {
            id: confirmField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: qsTr("Confirm the new password")
            revealable: true
            error: text.length > 0 && text !== newField.text
            onAccepted: hintField.forceActiveFocus()
        }

        UI.TextField {
            id: hintField
            Layout.fillWidth: true
            Layout.topMargin: 6
            placeholderText: qsTr("Password hint (optional)")
            onAccepted: changeButton.clicked()
        }

        UI.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: passwordDialog.error.length > 0 ? passwordDialog.error : passwordDialog.problem
            size: 12
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        buttons: [
            UI.Button {
                id: changeButton
                text: qsTr("Change")
                kind: "primary"
                busy: Users.busy === "password"
                enabled: passwordDialog.ready && Users.busy !== "password"
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    passwordDialog.error = "";
                    Users.changePassword(currentField.text, newField.text, hintField.text);
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                enabled: Users.busy !== "password"
                onClicked: passwordDialog.close()
            }
        ]
    }
}
