import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Adding an account (MailMsAccount.dc, MailMsSetupManual.dc,
// MailMsSetupSync.dc, MailMsSetupDone.dc): the address, then a password
// (an app password for Gmail, iCloud, Yahoo and Fastmail), or the server
// settings for anything Mail can't find by itself, then the name people
// see. Shown over the window.
Item {
    id: dialog

    // email, password, manual, details, done
    property string step: "email"
    property var settings: ({})
    property bool testing: false
    property string error
    property bool guessed: false

    // The password, app password or (for Sign in with Google) refresh token
    // that passed the test, kept for saving.
    property string secret

    signal finished()

    function open() {
        step = "email";
        settings = {};
        error = "";
        email.text = "";
        password.text = "";
        visible = true;
        email.forceActiveFocus();
    }
    function close() {
        GoogleSignIn.cancel();
        visible = false;
        finished();
    }
    function choose(provider) {
        error = "";
        if (provider === "other") {
            settings = Object.assign({ provider: "other", email: email.text.trim(), user: email.text.trim() }, settings.imapHost ? settings : {});
            fillManual();
            step = "manual";
            return;
        }
        settings = autoConfig.preset(provider, email.text.trim());
        if (provider === "gmail") {
            startGoogle();
            return;
        }
        step = "password";
        password.forceActiveFocus();
    }
    // Gmail and Google Workspace: Google's own sign-in page in the browser.
    function startGoogle() {
        settings = Object.assign({}, autoConfig.preset("gmail", email.text.trim()), { auth: "google" });
        step = "google";
        GoogleSignIn.start(email.text.trim());
    }
    function useAppPassword() {
        GoogleSignIn.cancel();
        settings = Object.assign({}, settings, { auth: "password" });
        step = "password";
        password.forceActiveFocus();
    }
    function fillManual() {
        imapHost.text = settings.imapHost || "";
        imapPort.text = settings.imapPort || 993;
        imapSecurity.currentIndex = ["ssl", "starttls", "none"].indexOf(settings.imapSecurity || "ssl");
        smtpHost.text = settings.smtpHost || "";
        smtpPort.text = settings.smtpPort || 465;
        smtpSecurity.currentIndex = ["ssl", "starttls", "none"].indexOf(settings.smtpSecurity || "ssl");
        user.text = settings.user || email.text.trim();
    }
    function test(pw) {
        error = "";
        secret = pw;
        testing = true;
        MailEngine.testAccount(settings, pw);
    }
    readonly property var appPasswordHelp: ({
        gmail: { name: "Gmail", url: "https://myaccount.google.com/apppasswords", how: qsTr("Gmail needs an app password: a separate password just for Mail, which you can take back any time. Turn on 2-Step Verification in your Google account first, then create one.") },
        icloud: { name: "iCloud Mail", url: "https://account.apple.com", how: qsTr("iCloud needs an app-specific password. In your Apple Account, open Sign-In and Security, then App-Specific Passwords.") },
        yahoo: { name: "Yahoo Mail", url: "https://login.yahoo.com/myaccount/security/app-password", how: qsTr("Yahoo needs an app password, made in your Yahoo account's security settings.") },
        fastmail: { name: "Fastmail", url: "https://app.fastmail.com/settings/security/apps", how: qsTr("Fastmail needs an app password. In Fastmail, open Settings, Privacy and security, then Manage app passwords, and give it access to mail.") },
        aol: { name: "AOL Mail", url: "https://login.aol.com/account/security", how: qsTr("AOL needs an app password, made in your AOL account's security settings.") },
        other: { name: "", url: "", how: "" }
    })

    visible: false
    anchors.fill: parent
    z: 50

    Connections {
        target: MailEngine
        function onAccountTested(ok, message) {
            if (!dialog.visible) {
                return;
            }
            dialog.testing = false;
            if (ok) {
                fullName.text = dialog.settings.fullName || MailEngine.systemFullName();
                accountName.text = MailEngine.accounts.length === 0 ? qsTr("Personal") : dialog.settings.email;
                dialog.step = "details";
            } else {
                dialog.error = message;
            }
        }
    }

    Connections {
        target: GoogleSignIn
        function onSignedIn(address, refreshToken) {
            if (dialog.visible && dialog.step === "google") {
                dialog.test(refreshToken);
            }
        }
        function onFailed(message) {
            if (dialog.visible && dialog.step === "google") {
                dialog.error = message;
            }
        }
    }

    AutoConfig {
        id: autoConfig
        onFound: function (found, wasGuessed) {
            dialog.guessed = wasGuessed;
            if (found.provider === "outlook") {
                dialog.error = qsTr("Outlook and Microsoft 365 accounts need Microsoft's own sign-in, which is coming in an update.");
                return;
            }
            if (found.provider === "proton") {
                dialog.error = qsTr("Proton Mail only lets other mail apps in through Proton Mail Bridge, which needs a paid Proton plan. Mail can't use Bridge yet, so read Proton mail in your browser for now.");
                return;
            }
            dialog.settings = found;
            if (found.provider === "gmail") {
                dialog.startGoogle();
                return;
            }
            if (found.provider !== "other" || !wasGuessed) {
                if (found.provider === "other") {
                    dialog.settings.provider = "other";
                }
                dialog.step = "password";
                password.forceActiveFocus();
            } else {
                dialog.fillManual();
                dialog.step = "manual";
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: UI.Style.scrim
        MouseArea {
            anchors.fill: parent
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(600, parent.width - 48)
        // Always the size of what is in it: every step is laid out to fit
        // Mail's smallest window, so nothing here ever scrolls.
        height: content.implicitHeight + 48
        radius: UI.Style.radiusDialog
        // Solid: the compositor's blur can't reach what is inside this same
        // window, so glass would show the mail underneath.
        color: UI.Style.windowOpaque
        border.width: 1
        border.color: UI.Style.borderDialog

        Item {
            anchors.fill: parent
            anchors.margins: 24

            ColumnLayout {
                id: content
                width: card.width - 48
                spacing: 12

                UI.Label {
                    visible: text.length > 0
                    text: dialog.step === "email" || dialog.step === "done" ? "" : dialog.step === "details" ? qsTr("Step 3 of 3") : qsTr("Step 2 of 3")
                    size: 12
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: dialog.step === "email" ? qsTr("Add an account")
                        : dialog.step === "google" ? qsTr("Sign in with Google")
                        : dialog.step === "password" ? (dialog.appPasswordHelp[dialog.settings.provider] || dialog.appPasswordHelp.other).name || qsTr("Sign in")
                        : dialog.step === "manual" ? qsTr("Other account")
                        : dialog.step === "details" ? qsTr("You're signed in")
                        : qsTr("You're all set")
                    size: 22
                    font.weight: UI.Style.weightSemiBold
                    wrapMode: Text.WordWrap
                }

                // Step 1: the address, or a provider.
                ColumnLayout {
                    visible: dialog.step === "email"
                    Layout.fillWidth: true
                    spacing: 12

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Your mail, and for Gmail your calendar too.")
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                    UI.Label {
                        text: qsTr("Email address")
                        size: 13
                        font.weight: UI.Style.weightMedium
                    }
                    UI.TextField {
                        id: email
                        Layout.fillWidth: true
                        placeholderText: "name@example.com"
                        inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                        onAccepted: if (continueButton.enabled) continueButton.clicked()
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("We'll find the right settings for you. No server names needed.")
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                    UI.Label {
                        Layout.topMargin: 6
                        text: qsTr("or choose your provider")
                        size: 12
                        color: UI.Style.inkMuted
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8

                    Repeater {
                        model: [
                            { id: "gmail", name: "Gmail", note: qsTr("Sign in with Google") },
                            { id: "outlook", name: qsTr("Outlook and Microsoft 365"), note: qsTr("Coming in an update") },
                            { id: "icloud", name: qsTr("iCloud Mail"), note: qsTr("Uses an app-specific password") },
                            { id: "yahoo", name: qsTr("Yahoo Mail"), note: qsTr("Uses an app password") },
                            { id: "fastmail", name: "Fastmail", note: qsTr("Uses an app password") },
                            { id: "other", name: qsTr("Other account"), note: qsTr("Any IMAP and SMTP mail") }
                        ]

                        // A compact tile per provider, two to a row, so the
                        // first step fits without scrolling.
                        delegate: Rectangle {
                            id: tile
                            required property var modelData
                            readonly property bool available: modelData.id !== "outlook"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: 54
                            radius: UI.Style.radiusSm
                            color: tileMouse.containsMouse && available ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                            border.width: 1
                            border.color: UI.Style.borderSubtle
                            opacity: available ? 1 : 0.55

                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: tile.available
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (email.text.trim().indexOf("@") < 1) {
                                        dialog.error = qsTr("Type your email address first.");
                                        email.forceActiveFocus();
                                        return;
                                    }
                                    dialog.choose(tile.modelData.id);
                                }
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 10
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                spacing: 1
                                UI.Label {
                                    Layout.fillWidth: true
                                    text: tile.modelData.name
                                    size: 13
                                    font.weight: UI.Style.weightSemiBold
                                    elide: Text.ElideRight
                                }
                                UI.Label {
                                    Layout.fillWidth: true
                                    text: tile.modelData.note
                                    size: 11.5
                                    color: UI.Style.inkMuted
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    }
                }

                // Step 2 for Gmail: Google's page in the browser
                // (MailMsSetupSignin.dc).
                ColumnLayout {
                    visible: dialog.step === "google"
                    Layout.fillWidth: true
                    spacing: 12

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("We've opened Google's sign-in page in your browser. Sign in there and allow access to your mail and calendar, then come back here.")
                        size: 13
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 64
                        radius: UI.Style.radiusSm
                        color: UI.Style.surfaceRaised
                        border.width: 1
                        border.color: UI.Style.borderSubtle

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12
                            SenderBadge {
                                size: 32
                                initials: MailEngine.initialsOf(email.text.trim())
                                colourIndex: MailEngine.colourOf(email.text.trim())
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                UI.Label {
                                    Layout.fillWidth: true
                                    text: email.text.trim()
                                    size: 13
                                    font.weight: UI.Style.weightSemiBold
                                    elide: Text.ElideRight
                                }
                                UI.Label {
                                    text: dialog.testing ? qsTr("Checking your mail") : qsTr("Waiting for you to sign in")
                                    size: 12
                                    color: UI.Style.inkMuted
                                }
                            }
                            UI.Spinner {
                                size: 20
                            }
                        }
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Google's page names Evolution: that's the mail engine Wild OS Mail is built on. Mail asks for your mail and your calendar, and you can take its access back in your Google account.")
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        spacing: 18
                        UI.LinkButton {
                            text: qsTr("Open the page again")
                            onClicked: GoogleSignIn.reopen()
                        }
                        UI.LinkButton {
                            text: qsTr("Use an app password instead")
                            onClicked: dialog.useAppPassword()
                        }
                    }
                }

                // Step 2: the password.
                ColumnLayout {
                    visible: dialog.step === "password"
                    Layout.fillWidth: true
                    spacing: 12

                    UI.Label {
                        Layout.fillWidth: true
                        text: email.text.trim()
                        size: 13
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: (dialog.appPasswordHelp[dialog.settings.provider] || dialog.appPasswordHelp.other).how
                        size: 13
                        wrapMode: Text.WordWrap
                    }
                    UI.LinkButton {
                        visible: ((dialog.appPasswordHelp[dialog.settings.provider] || dialog.appPasswordHelp.other).url || "").length > 0
                        text: qsTr("Open the page to make one")
                        onClicked: Qt.openUrlExternally(dialog.appPasswordHelp[dialog.settings.provider].url)
                    }
                    UI.Label {
                        text: dialog.settings.provider === "other" ? qsTr("Password") : qsTr("App password")
                        size: 13
                        font.weight: UI.Style.weightMedium
                    }
                    UI.TextField {
                        id: password
                        Layout.fillWidth: true
                        revealable: true
                        onAccepted: if (password.text.length > 0) dialog.test(password.text)
                    }
                    RowLayout {
                        spacing: 8
                        MailIcon {
                            name: "lock"
                            size: 14
                            color: UI.Style.success
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: MailEngine.passwordInWallet ? qsTr("Kept in this PC's password store, KWallet, not in Mail's own files.") : qsTr("Kept in a file only your account on this PC can read.")
                            size: 12
                            color: UI.Style.inkMuted
                            wrapMode: Text.WordWrap
                        }
                    }
                    UI.LinkButton {
                        visible: dialog.settings.provider === "other"
                        text: qsTr("Change server settings")
                        onClicked: {
                            dialog.fillManual();
                            dialog.step = "manual";
                        }
                    }
                }

                // Step 2 for anything else: the servers.
                GridLayout {
                    visible: dialog.step === "manual"
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10
                    rowSpacing: 8

                    UI.Label {
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        text: dialog.guessed ? qsTr("We couldn't find settings for this address, so these are a guess. Check them against what your email provider gives.") : qsTr("Enter the settings from your email provider. We fill in what we can find.")
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                    UI.Label {
                        Layout.columnSpan: 3
                        Layout.topMargin: 6
                        text: qsTr("Incoming mail (IMAP)")
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.TextField {
                        id: imapHost
                        Layout.fillWidth: true
                        placeholderText: qsTr("Server")
                    }
                    UI.TextField {
                        id: imapPort
                        Layout.preferredWidth: 80
                        placeholderText: qsTr("Port")
                        validator: IntValidator { bottom: 1; top: 65535 }
                    }
                    UI.ComboBox {
                        id: imapSecurity
                        Layout.preferredWidth: 130
                        model: [{ value: "ssl", text: "SSL/TLS" }, { value: "starttls", text: "STARTTLS" }, { value: "none", text: qsTr("None") }]
                    }
                    UI.Label {
                        Layout.columnSpan: 3
                        Layout.topMargin: 6
                        text: qsTr("Outgoing mail (SMTP)")
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.TextField {
                        id: smtpHost
                        Layout.fillWidth: true
                        placeholderText: qsTr("Server")
                    }
                    UI.TextField {
                        id: smtpPort
                        Layout.preferredWidth: 80
                        placeholderText: qsTr("Port")
                        validator: IntValidator { bottom: 1; top: 65535 }
                    }
                    UI.ComboBox {
                        id: smtpSecurity
                        Layout.preferredWidth: 130
                        model: [{ value: "ssl", text: "SSL/TLS" }, { value: "starttls", text: "STARTTLS" }, { value: "none", text: qsTr("None") }]
                    }
                    // Username and password side by side, to keep the step
                    // short enough never to scroll.
                    GridLayout {
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 8

                        UI.Label {
                            text: qsTr("Username")
                            size: 13
                            font.weight: UI.Style.weightSemiBold
                        }
                        UI.Label {
                            text: qsTr("Password")
                            size: 13
                            font.weight: UI.Style.weightSemiBold
                        }
                        UI.TextField {
                            id: user
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                        }
                        UI.TextField {
                            id: manualPassword
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            revealable: true
                        }
                    }
                    UI.Label {
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        visible: imapSecurity.currentIndex === 2 || smtpSecurity.currentIndex === 2
                        text: qsTr("Without SSL/TLS or STARTTLS your password and mail cross the network unprotected.")
                        size: 12
                        color: UI.Style.warning
                        wrapMode: Text.WordWrap
                    }
                }

                // Step 3: the name people see.
                ColumnLayout {
                    visible: dialog.step === "details"
                    Layout.fillWidth: true
                    spacing: 10

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("You can change any of this later in Settings.")
                        size: 13
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        text: qsTr("Your name, as people see it")
                        size: 13
                        font.weight: UI.Style.weightMedium
                    }
                    UI.TextField {
                        id: fullName
                        Layout.fillWidth: true
                    }
                    UI.Label {
                        text: qsTr("What to call this account in Mail")
                        size: 13
                        font.weight: UI.Style.weightMedium
                    }
                    UI.TextField {
                        id: accountName
                        Layout.fillWidth: true
                    }
                    UI.SettingRow {
                        Layout.fillWidth: true
                        title: qsTr("Keep all mail on this PC")
                        description: qsTr("Read and search everything offline. Off keeps only what you open.")

                        UI.Toggle {
                            id: keepAll
                            checked: true
                        }
                    }
                }

                // Done.
                ColumnLayout {
                    visible: dialog.step === "done"
                    Layout.fillWidth: true
                    spacing: 10

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("You can start using Mail now. Older messages keep arriving in the background.")
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        visible: MailEngine.syncStatus.length > 0
                        spacing: 8
                        UI.Spinner {
                            size: 14
                        }
                        UI.Label {
                            text: MailEngine.syncStatus
                            size: 13
                        }
                    }
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: dialog.error.length > 0
                    text: dialog.error
                    size: 13
                    color: UI.Style.danger
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8

                    UI.Button {
                        visible: dialog.step === "done"
                        text: qsTr("Add another account")
                        onClicked: dialog.open()
                    }
                    UI.Button {
                        visible: dialog.step === "password" || dialog.step === "manual" || dialog.step === "google"
                        text: qsTr("Back")
                        enabled: !dialog.testing
                        onClicked: {
                            GoogleSignIn.cancel();
                            dialog.error = "";
                            dialog.step = "email";
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    UI.Button {
                        visible: dialog.step !== "done"
                        text: qsTr("Cancel")
                        enabled: !dialog.testing
                        onClicked: dialog.close()
                    }
                    UI.Button {
                        id: continueButton
                        visible: dialog.step !== "google"
                        kind: "primary"
                        busy: dialog.testing || autoConfig.busy
                        enabled: !dialog.testing && !autoConfig.busy
                            && (dialog.step !== "email" || email.text.trim().indexOf("@") > 0)
                            && (dialog.step !== "password" || password.text.length > 0)
                            && (dialog.step !== "manual" || (imapHost.text.length > 0 && smtpHost.text.length > 0 && manualPassword.text.length > 0))
                        text: dialog.step === "email" ? qsTr("Continue") : dialog.step === "details" ? qsTr("Finish")
                            : dialog.step === "done" ? qsTr("Go to Inbox") : dialog.testing ? qsTr("Checking") : qsTr("Sign in")
                        onClicked: {
                            dialog.error = "";
                            if (dialog.step === "email") {
                                autoConfig.lookup(email.text.trim());
                            } else if (dialog.step === "password") {
                                dialog.test(password.text);
                            } else if (dialog.step === "manual") {
                                const sec = ["ssl", "starttls", "none"];
                                dialog.settings = Object.assign({}, dialog.settings, {
                                    email: email.text.trim(), provider: "other",
                                    imapHost: imapHost.text.trim(), imapPort: parseInt(imapPort.text), imapSecurity: sec[imapSecurity.currentIndex],
                                    smtpHost: smtpHost.text.trim(), smtpPort: parseInt(smtpPort.text), smtpSecurity: sec[smtpSecurity.currentIndex],
                                    user: user.text.trim()
                                });
                                password.text = manualPassword.text;
                                dialog.test(manualPassword.text);
                            } else if (dialog.step === "details") {
                                const s = Object.assign({}, dialog.settings, { fullName: fullName.text.trim(), name: accountName.text.trim() || dialog.settings.email, keepAll: keepAll.checked });
                                MailEngine.saveAccount(s, dialog.secret);
                                // Google's sign-in allowed calendars and contacts too.
                                if (s.auth === "google") {
                                    CalendarEngine.addGoogleAccount(s.email, dialog.secret);
                                }
                                dialog.secret = "";
                                password.text = "";
                                manualPassword.text = "";
                                dialog.step = "done";
                            } else {
                                dialog.close();
                            }
                        }
                    }
                }
            }
        }
    }
}
