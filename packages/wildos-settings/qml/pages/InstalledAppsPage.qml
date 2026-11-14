import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Installed apps: everything that shows in Start, with Uninstall for the
// apps that can safely go. Store apps are removed with flatpak, others with
// PackageKit, which asks for the administrator password. Apps that installed
// themselves into the home folder (their own install script, an AppImage)
// belong to no package: their files go to the Trash, listed first. Windows
// mode's apps are uninstalled inside Windows: a Store app at once, any
// other program by its own uninstaller, which opens in a window.
PageScroll {
    id: page

    property string message
    property bool messageIsError: false

    // Start's Uninstall for an app it can't remove itself (Wild OS's own,
    // one in the home folder or in Windows mode) opens this page with
    // Navigator.request "uninstall:<launcher id>".
    function takeRequest() {
        if (!Navigator.request.startsWith("uninstall:")) {
            return;
        }
        const id = Navigator.request.substring(10).replace(/.desktop$/, "");
        Navigator.request = "";
        const name = apps.nameOf(id);
        if (name.length === 0) {
            return;
        }
        if (apps.canUninstall(id)) {
            page.ask(id, name);
        } else {
            page.messageIsError = true;
            page.message = name + " is part of Wild OS, so it can't be uninstalled.";
        }
    }

    function ask(id, name) {
        confirm.appId = id;
        confirm.appName = name;
        confirm.files = apps.homeFiles(id);
        confirm.data = apps.homeData(id);
        confirm.windows = apps.isWindowsApp(id);
        confirm.withData = false;
        confirm.open();
    }

    Component.onCompleted: takeRequest()

    Connections {
        target: Navigator
        function onRequestChanged() {
            page.takeRequest();
        }
    }

    AppsModel {
        id: apps
        onUninstalled: name => {
            page.messageIsError = false;
            page.message = name + " was uninstalled.";
        }
        onUninstallOpened: name => {
            page.messageIsError = false;
            page.message = name + "'s uninstaller is open in Windows. Finish there, and " + name + " leaves this list once it's done.";
        }
        onUninstallFailed: (name, reason) => {
            page.messageIsError = true;
            page.message = name + " was not uninstalled. " + (reason.length > 0 ? reason : "Try again from the Store.");
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 16

        UI.SearchField {
            Layout.preferredWidth: 320
            placeholderText: "Search apps"
            onTextChanged: apps.filter = text
        }

        UI.Label {
            Layout.fillWidth: true
            text: Format.plural(apps.count, "app", "apps")
            color: UI.Style.inkMuted
            horizontalAlignment: Text.AlignRight
        }
    }

    StatusCard {
        visible: page.message.length > 0
        iconName: page.messageIsError ? "warning" : "check"
        warning: page.messageIsError
        title: page.messageIsError ? "Not uninstalled" : "Uninstalled"
        description: page.message

        UI.Button {
            text: "Dismiss"
            onClicked: page.message = ""
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            Repeater {
                model: apps

                UI.SettingRow {
                    id: app
                    required property string name
                    required property string appIcon
                    required property string appId
                    required property string source
                    required property string packageName
                    required property bool canUninstall
                    themeIcon: appIcon.length > 0 ? appIcon : "application-x-executable"
                    title: name
                    description: source === "store" ? "From the Store" : source === "home" ? "Installed in your home folder" : source === "windows" ? "Runs in Windows mode" : canUninstall ? "Installed app" : "Part of Wild OS"

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: app.canUninstall
                        text: apps.busyId === app.appId ? "Uninstalling" : "Uninstall"
                        enabled: apps.busyId.length === 0
                        onClicked: page.ask(app.appId, app.name)
                    }
                }
            }
        }
    }

    UI.Dialog {
        id: confirm

        property string appId
        property string appName
        property var files: []
        property var data: []
        property bool withData: false
        property bool windows: false
        readonly property bool home: files.length > 0

        title: "Uninstall " + appName + "?"
        text: windows
              ? "Windows uninstalls " + appName + ". If it has an uninstaller of its own, that opens in a window to finish. If it came as part of a bigger program, like Word in Microsoft 365, the whole program goes. Files you saved in your folders stay."
              : home
              ? appName + " installed itself in your home folder. These go to the Recycle Bin, so you can put them back:"
              : appName + " will be removed from this computer. Your own files are not touched."

        UI.Label {
            Layout.fillWidth: true
            visible: confirm.home
            text: confirm.files.concat(confirm.withData ? confirm.data : []).join("\n")
            color: UI.Style.inkMuted
            size: 12
            wrapMode: Text.WrapAnywhere
        }

        UI.CheckBox {
            visible: confirm.home && confirm.data.length > 0
            text: "Also remove its settings and data"
            checked: confirm.withData
            onToggled: confirm.withData = checked
        }
        buttons: [
            UI.Button {
                text: "Uninstall"
                kind: "primary"
                onClicked: {
                    confirm.close();
                    page.message = "";
                    apps.uninstall(confirm.appId, confirm.withData);
                }
            },
            UI.Button {
                text: "Keep it"
                onClicked: confirm.close()
            }
        ]
    }
}
