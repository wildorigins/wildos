import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Sign-in options (SignInOptions.dc): the ways to sign in (fingerprint,
// password, security key), where a fingerprint is used, then signing in
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
                    notice.show("Password changed", message, false);
                }
            } else if (what === "autologin") {
                autoDialog.close();
                notice.show("Automatic sign-in", message, false);
            } else if (what === "wallet") {
                walletDialog.close();
                notice.show("Saved passwords", message, false);
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
                    notice.show("Automatic sign-in was not changed", reason, true);
                }
            }
        }
    }

    Notice {
        id: notice
    }

    Connections {
        target: Fingerprint
        function onFailed(what, reason) {
            if (what === "remove") {
                notice.show("The fingerprint wasn't removed", reason, true);
            } else if (what === "use") {
                notice.show("Fingerprint use wasn't changed", reason, true);
            } else if (!setupDialog.visible && what !== "password") {
                notice.show("The fingerprint wasn't saved", reason, true);
            }
        }
    }

    function changedText(iso) {
        if (!iso) {
            return "Sign in with your account's password";
        }
        const parts = iso.split("-");
        const changed = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
        const now = new Date();
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        if (changed.getTime() === today.getTime()) {
            return "Last changed today";
        }
        const month = Qt.locale("en_GB").monthName(changed.getMonth(), Locale.LongFormat);
        return changed.getFullYear() === now.getFullYear() ? "Last changed in " + month : "Last changed in " + month + " " + changed.getFullYear();
    }

    function fingerprintText() {
        if (!Fingerprint.available) {
            return "This PC's fingerprint reader doesn't work with Wild OS yet";
        }
        if (Fingerprint.windowsKey) {
            return "This reader still holds a key from Windows Hello, so Wild OS can't use it";
        }
        const count = Fingerprint.fingers.length;
        return "Reader found" + (Fingerprint.place.length > 0 ? ": " + Fingerprint.place : "") + ". "
            + (count === 0 ? "No fingers added yet." : count === 1 ? "1 finger added." : count + " fingers added.");
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
            text: "Ways to sign in"
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
                    title: "Fingerprint"
                    description: page.fingerprintText()

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Fingerprint.available && !Fingerprint.windowsKey
                        text: Fingerprint.fingers.length === 0 ? "Set up" : "Manage"
                        enabled: Fingerprint.busy === ""
                        onClicked: Fingerprint.fingers.length === 0 ? setupDialog.open() : manageDialog.open()
                    }
                }

                UI.SettingRow {
                    leading: PathIcon {
                        name: "key-round"
                        size: 20
                    }
                    title: "Password"
                    description: page.changedText(Users.me.passwordChanged)

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Change"
                        onClicked: passwordDialog.open()
                    }
                }

                UI.SettingRow {
                    leading: PathIcon {
                        name: "security-key"
                        size: 20
                    }
                    title: "Security key"
                    description: "Sign in with a USB or NFC key, like a YubiKey"

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Add"
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
            text: "Use your fingerprint for"
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
                    title: "Sign in and unlock"
                    description: "At the sign-in screen and when the screen is locked"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Fingerprint.signIn
                        enabled: Fingerprint.switching === ""
                        Accessible.name: "Use your fingerprint to sign in and unlock"
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
                    title: "Admin prompts"
                    description: "When an app asks to change system settings"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Fingerprint.admin
                        enabled: Fingerprint.switching === ""
                        Accessible.name: "Use your fingerprint for admin prompts"
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
                    title: "Disk unlock at start-up"
                    description: "Always needs your password, so the disk stays encrypted until you type it"
                    status: "Password only"
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
            text: "Your fingerprint is stored as a pattern on this PC (or on the reader itself), never as a picture, and never leaves it."
            size: 12.5
            color: UI.Style.inkMuted
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Additional settings"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "power"
                    minHeight: 72
                    title: "Sign in automatically when this computer starts"
                    description: page.autoOther ? "Set up for " + Users.autoLogin + ". Turning it on here moves it to you."
                        : "Anyone who turns the computer on gets straight to your files and saved passwords, so only use it on a computer that stays at home."
                    status: page.autoOn ? "On" : "Off"

                    UI.Toggle {
                        id: autoToggle
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.autoOn
                        enabled: Users.busy !== "autologin"
                        Accessible.name: "Sign in automatically when this computer starts"
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
                    title: "Restart my apps when I sign back in"
                    description: "Apps that were open when you signed out or restarted open again, where the app supports it"
                    status: page.restoreApps ? "On" : "Off"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.restoreApps
                        Accessible.name: "Restart my apps when I sign back in"
                        onToggled: page.setRestoreApps(checked)
                    }
                }

                UI.SettingRow {
                    iconName: "clock"
                    title: "Screen lock"
                    description: "Lock after a while and ask for your password on wake"
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

        title: "Your fingerprints"
        text: "Each one signs you in on its own. Remove any you no longer use; your password always works."
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
                    text: "Fingerprint " + (index + 1)
                    size: 14
                }

                UI.Button {
                    text: "Remove"
                    busy: Fingerprint.busy === modelData
                    enabled: Fingerprint.busy === ""
                    Accessible.name: "Remove fingerprint " + (index + 1)
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
                text: "Add another finger"
                visible: Fingerprint.fingers.length < 10
                enabled: Fingerprint.busy === ""
                onClicked: {
                    manageDialog.close();
                    setupDialog.open();
                }
            },
            UI.Button {
                text: "Close"
                kind: "primary"
                onClicked: manageDialog.close()
            }
        ]
    }

    // pam_u2f isn't part of Wild OS yet, so there is nothing to add a key
    // to; the row is here so the page reads as the design does.
    UI.Dialog {
        id: keyDialog

        title: "Security keys aren't ready yet"
        text: "Wild OS can't sign in with a USB or NFC security key yet. Your password, and your fingerprint if you set one up, keep working as they do now."

        buttons: [
            UI.Button {
                text: "OK"
                kind: "primary"
                onClicked: keyDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: walletDialog

        property string error
        property bool needsOther: false

        title: "Open your saved passwords"
        text: needsOther
            ? "Your saved passwords use a different password from your account. Type both, and Wild OS moves them to your account password so they open when you sign in."
            : page.autoOn
                ? "This computer signs you in automatically, so your saved passwords need your password once. After this they open by themselves at every start."
                : "Type your account password to open your saved passwords. They open by themselves whenever you sign in with your password."
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
            placeholderText: "Your account password"
            revealable: true
            onTextChanged: walletDialog.error = ""
            onAccepted: walletDialog.needsOther ? walletOtherField.forceActiveFocus() : walletButton.clicked()
        }

        UI.TextField {
            id: walletOtherField
            UI.NoPaste {}
            Layout.fillWidth: true
            visible: walletDialog.needsOther
            placeholderText: "The password your saved passwords use"
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
                text: "Continue"
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
                text: "Cancel"
                enabled: Users.busy !== "wallet"
                onClicked: walletDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: autoDialog

        property string error

        title: "Sign in automatically"
        text: (Protection.status.encryption === "on"
            ? "Anyone who gets past the disk password when this computer starts will reach your files and your saved passwords, without your account password."
            : "Anyone who turns this computer on will reach your files and your saved passwords, without a password.")
            + " Type your password so Wild OS can open your saved passwords for you."
        onAboutToShow: {
            error = "";
            autoPasswordField.text = "";
            autoPasswordField.forceActiveFocus();
        }

        UI.TextField {
            id: autoPasswordField
            UI.NoPaste {}
            Layout.fillWidth: true
            placeholderText: "Your password"
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
                text: "Turn on"
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
                text: "Cancel"
                enabled: Users.busy !== "autologin"
                onClicked: autoDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: passwordDialog

        property string error
        readonly property string problem: {
            if (newField.text.length > 0 && newField.text.length < 8) {
                return "Use at least 8 characters.";
            }
            if (confirmField.text.length > 0 && confirmField.text !== newField.text) {
                return "The two new passwords don't match.";
            }
            if (newField.text.length > 0 && newField.text === currentField.text) {
                return "The new password is the same as the current one.";
            }
            return "";
        }
        readonly property bool ready: currentField.text.length > 0 && newField.text.length >= 8
            && confirmField.text === newField.text && problem.length === 0

        title: "Change your password"
        text: Protection.status.encryption === "on"
            ? "Type your current password, then the new one twice. The disk itself still unlocks with the password it was given when Wild OS was installed."
            : "Type your current password, then the new one twice."
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
            placeholderText: "Current password"
            revealable: true
            onTextChanged: passwordDialog.error = ""
            onAccepted: newField.forceActiveFocus()
        }

        UI.TextField {
            id: newField
            UI.NoPaste {}
            Layout.fillWidth: true
            Layout.topMargin: 6
            placeholderText: "New password"
            revealable: true
            error: text.length > 0 && text.length < 8
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
            placeholderText: "Confirm the new password"
            revealable: true
            error: text.length > 0 && text !== newField.text
            onAccepted: hintField.forceActiveFocus()
        }

        UI.TextField {
            id: hintField
            Layout.fillWidth: true
            Layout.topMargin: 6
            placeholderText: "Password hint (optional)"
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
                text: "Change"
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
                text: "Cancel"
                enabled: Users.busy !== "password"
                onClicked: passwordDialog.close()
            }
        ]
    }
}
