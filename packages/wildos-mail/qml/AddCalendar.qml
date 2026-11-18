import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Adding a calendar: a Google account (Google's own sign-in page), iCloud
// with an app-specific password, any other CalDAV server, a calendar from
// the web, or a new one on this PC. Shown over the window and sized to
// what's in it, like adding a mail account, so it never scrolls.
Item {
    id: dialog

    // choose, google, icloud, caldav, web, local, done
    property string step: "choose"
    property string error
    property bool working: false

    function start() {
        step = "choose";
        error = "";
        working = false;
        for (const f of [googleEmail, icloudEmail, icloudPassword, server, serverUser, serverPassword, serverName, webAddress, webName, localName]) {
            f.text = "";
        }
        visible = true;
    }
    function close() {
        GoogleSignIn.cancel();
        visible = false;
    }
    function go(next) {
        error = "";
        step = next;
        const first = { google: googleEmail, icloud: icloudEmail, caldav: server, web: webAddress, local: localName }[next];
        if (first) {
            first.forceActiveFocus();
        }
    }

    visible: false
    anchors.fill: parent
    z: 50

    Connections {
        target: GoogleSignIn
        function onSignedIn(address, refreshToken) {
            if (dialog.visible && dialog.step === "google") {
                dialog.working = true;
                CalendarEngine.addGoogleAccount(googleEmail.text.trim(), refreshToken);
            }
        }
        function onFailed(message) {
            if (dialog.visible && dialog.step === "google") {
                dialog.error = message;
            }
        }
    }
    Connections {
        target: CalendarEngine
        function onAccountAdded(uid) {
            if (dialog.visible) {
                dialog.working = false;
                dialog.step = "done";
            }
        }
        function onAccountFailed(message) {
            if (dialog.visible) {
                dialog.working = false;
                dialog.error = message;
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
        width: Math.min(560, parent.width - 48)
        height: content.implicitHeight + 48
        radius: UI.Style.radiusDialog
        color: UI.Style.windowOpaque
        border.width: 1
        border.color: UI.Style.borderDialog

        ColumnLayout {
            id: content
            x: 24
            y: 24
            width: card.width - 48
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: dialog.step === "choose" ? qsTr("Add a calendar")
                    : dialog.step === "google" ? qsTr("Google Calendar")
                    : dialog.step === "icloud" ? qsTr("iCloud Calendar")
                    : dialog.step === "caldav" ? qsTr("Another calendar account")
                    : dialog.step === "web" ? qsTr("A calendar from the web")
                    : dialog.step === "local" ? qsTr("A new calendar on this PC")
                    : qsTr("You're all set")
                size: 22
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.WordWrap
            }

            // What kind.
            ColumnLayout {
                visible: dialog.step === "choose"
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        { id: "google", name: qsTr("Google"), note: qsTr("Gmail and Google Workspace calendars, with Google's sign-in") },
                        { id: "icloud", name: qsTr("iCloud"), note: qsTr("Your Apple calendars, with an app-specific password") },
                        { id: "caldav", name: qsTr("Another calendar account"), note: qsTr("Fastmail, Nextcloud, Proton Bridge and any CalDAV server") },
                        { id: "web", name: qsTr("Subscribe from the web"), note: qsTr("A calendar address (webcal or .ics), such as fixtures or term dates") },
                        { id: "local", name: qsTr("New calendar on this PC"), note: qsTr("Kept only on this computer") }
                    ]
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 54
                        radius: UI.Style.radiusSm
                        color: tileMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                        border.width: 1
                        border.color: UI.Style.borderSubtle
                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.go(tile.modelData.id)
                        }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 1
                            UI.Label {
                                text: tile.modelData.name
                                size: 13
                                font.weight: UI.Style.weightSemiBold
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

            // Google.
            ColumnLayout {
                visible: dialog.step === "google"
                Layout.fillWidth: true
                spacing: 10
                UI.Label {
                    Layout.fillWidth: true
                    text: GoogleSignIn.waiting ? qsTr("We've opened Google's sign-in page in your browser. Sign in there and allow access to your calendars and contacts, then come back here.")
                                               : qsTr("Your Google address. Google's own page opens in your browser to sign in; Wild OS never sees your password.")
                    size: 13
                    wrapMode: Text.WordWrap
                }
                UI.TextField {
                    id: googleEmail
                    Layout.fillWidth: true
                    enabled: !GoogleSignIn.waiting
                    placeholderText: "name@gmail.com"
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                    onAccepted: googleButton.clicked()
                }
                UI.LinkButton {
                    visible: GoogleSignIn.waiting
                    text: qsTr("Open Google's page again")
                    onClicked: GoogleSignIn.reopen()
                }
            }

            // iCloud.
            ColumnLayout {
                visible: dialog.step === "icloud"
                Layout.fillWidth: true
                spacing: 10
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("iCloud needs an app-specific password, not your Apple Account password. In your Apple Account, open Sign-In and Security, then App-Specific Passwords.")
                    size: 13
                    wrapMode: Text.WordWrap
                }
                UI.LinkButton {
                    text: qsTr("Open Apple Account")
                    onClicked: Qt.openUrlExternally("https://account.apple.com")
                }
                UI.TextField {
                    id: icloudEmail
                    Layout.fillWidth: true
                    placeholderText: qsTr("Apple Account email")
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                }
                UI.TextField {
                    id: icloudPassword
                    Layout.fillWidth: true
                    placeholderText: qsTr("App-specific password")
                    revealable: true
                    onAccepted: icloudButton.clicked()
                }
            }

            // Any CalDAV server.
            ColumnLayout {
                visible: dialog.step === "caldav"
                Layout.fillWidth: true
                spacing: 10
                UI.TextField {
                    id: server
                    Layout.fillWidth: true
                    placeholderText: qsTr("Server address, for example https://caldav.fastmail.com")
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                }
                UI.TextField {
                    id: serverUser
                    Layout.fillWidth: true
                    placeholderText: qsTr("User name, often your email address")
                    inputMethodHints: Qt.ImhNoAutoUppercase
                }
                UI.TextField {
                    id: serverPassword
                    Layout.fillWidth: true
                    placeholderText: qsTr("Password or app password")
                    revealable: true
                }
                UI.TextField {
                    id: serverName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name in the list (optional)")
                    onAccepted: caldavButton.clicked()
                }
            }

            // A calendar from the web.
            ColumnLayout {
                visible: dialog.step === "web"
                Layout.fillWidth: true
                spacing: 10
                UI.TextField {
                    id: webAddress
                    Layout.fillWidth: true
                    placeholderText: qsTr("webcal://… or https://….ics")
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                }
                UI.TextField {
                    id: webName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name in the list")
                    onAccepted: webButton.clicked()
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Calendar checks it twice a day. You can see these events but not change them.")
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.WordWrap
                }
            }

            // On this PC.
            ColumnLayout {
                visible: dialog.step === "local"
                Layout.fillWidth: true
                spacing: 10
                UI.TextField {
                    id: localName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name, for example Family")
                    onAccepted: localButton.clicked()
                }
            }

            UI.Label {
                visible: dialog.step === "done"
                Layout.fillWidth: true
                text: qsTr("Its calendars appear in the list as soon as they've been fetched, usually within a minute.")
                size: 13
                wrapMode: Text.WordWrap
            }

            UI.Label {
                visible: dialog.error.length > 0
                Layout.fillWidth: true
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
                    visible: dialog.step !== "choose" && dialog.step !== "done"
                    text: qsTr("Back")
                    onClicked: {
                        GoogleSignIn.cancel();
                        dialog.go("choose");
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                UI.Button {
                    visible: dialog.step !== "done"
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
                UI.Button {
                    id: googleButton
                    visible: dialog.step === "google"
                    kind: "primary"
                    busy: GoogleSignIn.waiting || dialog.working
                    enabled: googleEmail.text.indexOf("@") > 0 && !busy
                    text: qsTr("Sign in with Google")
                    onClicked: {
                        dialog.error = "";
                        GoogleSignIn.startCalendar(googleEmail.text.trim());
                    }
                }
                UI.Button {
                    id: icloudButton
                    visible: dialog.step === "icloud"
                    kind: "primary"
                    busy: dialog.working
                    enabled: icloudEmail.text.indexOf("@") > 0 && icloudPassword.text.length > 0 && !busy
                    text: qsTr("Add")
                    onClicked: {
                        dialog.working = true;
                        CalendarEngine.addCalDavAccount("iCloud", "https://caldav.icloud.com/", icloudEmail.text.trim(), icloudPassword.text);
                    }
                }
                UI.Button {
                    id: caldavButton
                    visible: dialog.step === "caldav"
                    kind: "primary"
                    busy: dialog.working
                    enabled: server.text.trim().length > 3 && serverUser.text.trim().length > 0 && serverPassword.text.length > 0 && !busy
                    text: qsTr("Add")
                    onClicked: {
                        dialog.working = true;
                        CalendarEngine.addCalDavAccount(serverName.text.trim(), server.text.trim(), serverUser.text.trim(), serverPassword.text);
                    }
                }
                UI.Button {
                    id: webButton
                    visible: dialog.step === "web"
                    kind: "primary"
                    enabled: webAddress.text.trim().length > 8
                    text: qsTr("Subscribe")
                    onClicked: {
                        CalendarEngine.addWebCalendar(webName.text.trim(), webAddress.text.trim(), "");
                        dialog.step = "done";
                    }
                }
                UI.Button {
                    id: localButton
                    visible: dialog.step === "local"
                    kind: "primary"
                    enabled: localName.text.trim().length > 0
                    text: qsTr("Create")
                    onClicked: {
                        CalendarEngine.addLocalCalendar(localName.text.trim(), "");
                        dialog.close();
                    }
                }
                UI.Button {
                    visible: dialog.step === "done"
                    kind: "primary"
                    text: qsTr("Done")
                    onClicked: dialog.close()
                }
            }
        }
    }

    Keys.onEscapePressed: close()
}
