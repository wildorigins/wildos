import QtQuick
import QtQuick.Layouts
import WildOS.Backup
import WildOS.UI as UI

// Wild OS Backup (AppBackup.dc): the title bar, the side column with the
// five pages, and the page it picks.
UI.AppWindow {
    id: window

    property string page: "overview"

    width: 1384
    height: 802
    minimumWidth: 980
    minimumHeight: 600
    title: qsTr("Backup")

    function navigate(name) {
        page = name;
    }
    function say(message) {
        if (message.length > 0) {
            toast.show(message);
        }
    }

    Shortcut {
        sequences: [StandardKey.Refresh]
        onActivated: {
            Drives.refresh();
            Backup.refreshWindows();
        }
    }

    Connections {
        target: Backup
        function onRunEnded(entry) {
            if (!window.active) {
                return;
            }
            if (entry.result === "done") {
                window.say(qsTr("Backed up"));
            } else if (entry.result === "stopped") {
                window.say(qsTr("Backup stopped"));
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            tall: true
            frame: window.frame
            showTitle: false

            // The title bar puts its content 16px in; the board's icon is 14.
            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: BackupStyle.icon
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Backup")
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                Layout.preferredWidth: 210
                Layout.fillHeight: true
                page: window.page
                onNavigate: name => window.navigate(name)
            }

            Loader {
                id: pageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: true
                sourceComponent: {
                    switch (window.page) {
                    case "restore": return restorePage;
                    case "schedule": return schedulePage;
                    case "drives": return drivesPage;
                    case "settings": return settingsPage;
                    default: return overviewPage;
                    }
                }
            }
        }
    }

    Component {
        id: overviewPage
        OverviewPage {
            app: window
        }
    }
    Component {
        id: restorePage
        RestorePage {
            app: window
        }
    }
    Component {
        id: schedulePage
        SchedulePage {
            app: window
        }
    }
    Component {
        id: drivesPage
        DrivesPage {
            app: window
        }
    }
    Component {
        id: settingsPage
        SettingsPage {
            app: window
        }
    }

    // Setting a drive up can end in any page's hands: the new password to
    // write down, or the password of backups already on the drive.
    PasswordDialog {
        id: passwordDialog
        mode: "new"
        onDone: Setup.dismiss()
    }
    AskPasswordDialog {
        id: askDialog
        driveName: Setup.driveName
        error: Setup.error
        onGiven: text => Setup.givePassword(text)
        onCancelled: Setup.dismiss()
    }
    Connections {
        target: Setup
        function onChanged() {
            if (Setup.state === "created" && !passwordDialog.opened) {
                passwordDialog.open();
            } else if (Setup.state === "needPassword") {
                if (!askDialog.opened) {
                    askDialog.ask();
                }
            } else if (askDialog.opened) {
                askDialog.close();
            }
            if (Setup.state === "added") {
                window.say(qsTr("%1 is ready for backups").arg(Setup.driveName));
                Setup.dismiss();
            } else if (Setup.state === "failed") {
                errorDialog.show(qsTr("The drive couldn't be set up"), Setup.error, Setup.errorDetail);
                Setup.dismiss();
            }
        }
    }

    UI.Dialog {
        id: errorDialog
        // The backup tool's own words, added under the sentence on Details.
        property string detail
        function show(heading, message, more) {
            title = heading;
            text = message;
            detail = more ?? "";
            open();
        }
        buttons: [
            UI.Button {
                visible: errorDialog.detail.length > 0
                text: qsTr("Details")
                onClicked: {
                    errorDialog.text = errorDialog.text + "\n\n" + errorDialog.detail;
                    errorDialog.detail = "";
                }
            },
            UI.Button {
                kind: "primary"
                text: qsTr("OK")
                onClicked: errorDialog.close()
            }
        ]
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }
}
