import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Remote
import WildOS.UI as UI

// Remote's home window (RemoteHosts.dc, RemoteStart.dc): the title bar with
// the address box, the side column, and the hosts, keychain, snippets,
// port forwarding and known hosts pages. Connecting starts here: SSH hosts
// have their identity checked (RemoteHostChanged.dc) before a terminal tab
// opens; desktops ask about their certificate and sign-in here
// (RemoteCheck.dc, RemoteSignIn.dc) before their own window opens.
UI.AppWindow {
    id: window

    property string page: "hosts"

    width: 1384
    height: 802
    minimumWidth: 980
    minimumHeight: 620
    visible: !App.desktopOnly
    title: qsTr("Remote")

    function addressOf(h) {
        return h.user ? qsTr("%1 on %2").arg(h.user).arg(h.address) : h.address;
    }

    // What happens once an SSH host checks out: a terminal tab, unless
    // something else asked for the check (sending a key).
    property var afterCheck: null

    // An SSH host: is it the same server as last time?
    function connectHost(id, then) {
        const h = Hosts.host(id);
        if (!h.id) {
            return;
        }
        if (h.kind === "desktop") {
            desktops.start(h);
            return;
        }
        afterCheck = then || (() => App.openTerminal(id));
        toast.show(qsTr("Checking %1…").arg(h.name));
        HostKeys.check(Hosts.hostName(h), Hosts.port(h), result => {
            toast.hide();
            if (result.state === "known") {
                afterCheck();
            } else if (result.state === "new") {
                hostKeyDialog.ask(h, result);
            } else if (result.state === "changed") {
                hostChangedDialog.ask(h, result);
            } else if (result.state === "revoked") {
                problemDialog.show(qsTr("%1 can't be trusted").arg(h.name),
                    qsTr("The key %1 offers has been revoked on this PC, so Remote won't connect to it.").arg(h.name));
            } else {
                problemDialog.show(qsTr("Couldn't reach %1").arg(h.name), result.error);
            }
        });
    }

    // What was typed in the address box: a saved host with that address,
    // or a new one saved as typed.
    function quickConnect(address) {
        for (const h of Hosts.hosts) {
            if (h.kind === address.kind && Hosts.hostName(h) === Hosts.hostName(address) && Hosts.port(h) === Hosts.port(address) && (h.user === address.user || !address.user)) {
                connectHost(h.id);
                return;
            }
        }
        const id = Hosts.save({
            kind: address.kind,
            name: Hosts.hostName(address),
            address: Hosts.joinAddress(Hosts.hostName(address), Hosts.port(address), address.kind),
            user: address.user,
            group: ""
        });
        toast.show(qsTr("Saved to your hosts"));
        connectHost(id);
    }

    function openHostMenu(id, item, x, y) {
        const h = Hosts.host(id);
        const entries = [{ text: qsTr("Connect"), icon: h.kind === "desktop" ? "monitor" : "terminal", bold: true, run: () => connectHost(id) }];
        if (h.kind === "desktop") {
            entries.push({ text: h.desktop.display === "window" ? qsTr("Connect full screen") : qsTr("Connect in a window"), icon: "fullscreen",
                run: () => desktops.start(h, h.desktop.display === "window" ? "full" : "window") });
        }
        entries.push({ separator: true },
            { text: h.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites"), icon: "star", run: () => Hosts.setFavourite(id, !h.favourite) },
            { text: qsTr("Edit"), icon: "pencil", run: () => hostDialog.edit(id) },
            { text: qsTr("Copy address"), icon: "copy", run: () => App.copyText(h.address) },
            { separator: true },
            { text: qsTr("Delete"), icon: "delete", danger: true, run: () => deleteDialog.ask(id) });
        hostMenu.entries = entries;
        hostMenu.popup(item, x, y);
    }

    Connections {
        target: App
        function onConnectRequested(id) {
            window.connectHost(id);
        }
        function onEditRequested(id) {
            window.raise();
            window.requestActivate();
            hostDialog.edit(id);
        }
        function onQuickConnectRequested(address) {
            window.raise();
            window.quickConnect(address);
        }
        function onRaiseRequested() {
            window.show();
            window.raise();
            window.requestActivate();
        }
        function onSettingsRequested() {
            window.show();
            window.raise();
            window.requestActivate();
            window.page = "settings";
        }
    }
    Connections {
        target: AskPass
        function onPassphraseNeeded(id, title, prompt, kind, context) {
            // The terminal window asks its own.
            if (context === "terminal") {
                return;
            }
            window.raise();
            passphraseDialog.ask(id, title, prompt, kind);
        }
        function onQuestionClosed(id) {
            passphraseDialog.closeFor(id);
        }
    }

    Shortcut {
        sequence: "Ctrl+L"
        onActivated: search.forceActiveFocus()
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: window.page = "settings"
    }
    Shortcut {
        sequence: "Ctrl+N"
        onActivated: hostDialog.create(window.page === "keychain" ? "terminal" : "")
    }

    // No inset: the title bar, and the window buttons in it, reach the
    // window's top and right edges as in Files and Settings, so a pointer
    // flung into the corner of a maximised window lands on Close.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The title bar: 56px, as the boards draw it.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 56

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                property point origin
                onPressed: m => origin = Qt.point(m.x, m.y)
                onPositionChanged: m => {
                    if (pressed && Math.abs(m.x - origin.x) + Math.abs(m.y - origin.y) > 4) {
                        window.startSystemMove();
                    }
                }
                onDoubleClicked: window.frame.toggleMaximised()
            }

            Image {
                id: appIcon
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                source: "qrc:/qt/qml/WildOS/Remote/data/org.wildos.remote.svg"
                sourceSize: Qt.size(48, 48)
            }
            UI.Label {
                x: 54
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Remote")
                size: 15
                font.weight: UI.Style.weightSemiBold
            }

            UI.TextField {
                id: search
                x: 256
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(480, parent.width - 256 - 340)
                height: 34
                leftPadding: 36
                font.family: text.length > 0 ? UI.Style.fontFamily : "JetBrains Mono"
                placeholderText: qsTr("ssh user@host, or search your hosts")
                onTextChanged: if (window.page !== "hosts" && window.page !== "favourites" && !window.page.startsWith("group:")) window.page = "hosts"
                onAccepted: {
                    const address = Hosts.parseAddress(text);
                    if (address.kind) {
                        window.quickConnect(address);
                        text = "";
                    }
                }
                UI.Icon {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "terminal"
                    size: 15
                    color: UI.Style.iconMuted
                }
            }

            UI.Button {
                anchors.right: windowButtons.left
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                kind: "primary"
                size: "small"
                iconName: "plus"
                text: window.page === "keychain" ? qsTr("New key") : window.page === "snippets" ? qsTr("New snippet") : qsTr("New host")
                onClicked: {
                    if (window.page === "keychain") {
                        newKeyDialog.open();
                    } else if (window.page === "snippets") {
                        snippetDialog.edit({});
                    } else {
                        hostDialog.create("");
                    }
                }
            }

            UI.WindowButtons {
                id: windowButtons
                anchors.right: parent.right
                anchors.top: parent.top
                buttonHeight: parent.height
                frame: window.frame
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: UI.Style.divider
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                Layout.preferredWidth: 236
                Layout.fillHeight: true
                page: window.page
                onPageChosen: p => {
                    window.page = p;
                    if (p === "knownhosts") {
                        HostKeys.reload();
                    } else if (p === "keychain") {
                        Keychain.refresh();
                    }
                }
                onNewGroup: groupDialog.ask("")
                onGroupMenu: (name, item) => {
                    groupMenu.entries = [
                        { text: qsTr("Rename"), icon: "rename", run: () => groupDialog.ask(name) },
                        { text: qsTr("Delete group"), icon: "delete", danger: true, run: () => {
                            Hosts.removeGroup(name);
                            if (window.page === "group:" + name) window.page = "hosts";
                        } }
                    ];
                    groupMenu.popup(item, item.width - 20, item.height);
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.page === "keychain" ? 1 : window.page === "snippets" ? 2 : window.page === "forwarding" ? 3
                    : window.page === "knownhosts" ? 4 : window.page === "settings" ? 5 : 0

                HostsPage {
                    view: window.page
                    search: search.text
                    onConnectHost: id => window.connectHost(id)
                    onHostMenu: (id, item, x, y) => window.openHostMenu(id, item, x, y)
                    onNewHost: kind => hostDialog.create(kind)
                }
                KeychainPage {
                    onImportRequested: importDialog.open()
                    onNewKeyRequested: newKeyDialog.open()
                    onSendRequested: (path, item) => window.openSendMenu(path, item)
                    onExportRequested: path => {
                        exportDialog.keyPath = path;
                        exportDialog.open();
                    }
                    onDeleteRequested: path => deleteKeyDialog.ask(path)
                    onPassphraseRequested: (path, mode) => keyPassphraseDialog.ask(path, mode)
                    onNotice: text => toast.show(text)
                }
                SnippetsPage {
                    onEditRequested: s => snippetDialog.edit(s)
                }
                ForwardingPage {
                    onEditHost: id => hostDialog.edit(id)
                }
                KnownHostsPage {
                    onNotice: text => toast.show(text)
                }
                SettingsPage {}
            }
        }
    }

    // Sending a public key to a server: ssh-copy-id in a terminal tab, where
    // the server asks for its password as it would anywhere else.
    function openSendMenu(path, item) {
        const entries = [];
        for (const h of Hosts.hosts) {
            if (h.kind === "terminal") {
                entries.push({ text: h.name, icon: "terminal", run: () => window.connectHost(h.id, () => App.sendKey(path, h.id)) });
            }
        }
        if (entries.length === 0) {
            entries.push({ text: qsTr("No SSH hosts yet"), enabled: false });
        }
        sendMenu.entries = entries;
        sendMenu.popup(item, 0, item.height + 4);
    }

    UI.Menu {
        id: hostMenu
        minimumWidth: 230
    }
    UI.Menu {
        id: groupMenu
    }
    UI.Menu {
        id: sendMenu
        minimumWidth: 220
    }

    HostDialog {
        id: hostDialog
        onSaved: (id, connect) => {
            if (connect) {
                window.connectHost(id);
            }
        }
    }
    HostKeyDialog {
        id: hostKeyDialog
        onAccepted: (h, result) => {
            if (HostKeys.trust(Hosts.hostName(h), Hosts.port(h), result.keyLine)) {
                window.afterCheck();
            } else {
                toast.show(qsTr("Your known hosts file couldn't be written."));
            }
        }
    }
    HostChangedDialog {
        id: hostChangedDialog
        onRebuilt: (h, result) => {
            if (HostKeys.replace(Hosts.hostName(h), Hosts.port(h), result.keyLine)) {
                window.afterCheck();
            } else {
                toast.show(qsTr("Your known hosts file couldn't be written."));
            }
        }
    }
    DesktopConnector {
        id: desktops
        onNotice: text => toast.show(text)
    }
    PassphraseDialog {
        id: passphraseDialog
    }
    NewKeyDialog {
        id: newKeyDialog
        onNotice: text => toast.show(text)
    }
    KeyPassphraseDialog {
        id: keyPassphraseDialog
        onNotice: text => toast.show(text)
    }
    SnippetDialog {
        id: snippetDialog
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import a key")
        currentFolder: "file://" + App.homePath() + "/.ssh"
        onAccepted: Keychain.importKey(selectedFile, (ok, message, path) => toast.show(ok ? qsTr("Key imported into your .ssh folder") : message))
    }
    FolderDialog {
        id: exportDialog
        property string keyPath
        title: qsTr("Export the key to")
        onAccepted: Keychain.exportKey(keyPath, selectedFolder, (ok, message) => toast.show(ok ? qsTr("Exported to %1").arg(message) : message))
    }

    UI.Dialog {
        id: problemDialog
        function show(heading, body) {
            title = heading;
            text = body;
            open();
        }
        buttons: [
            UI.Button {
                kind: "primary"
                text: qsTr("OK")
                onClicked: problemDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: deleteDialog
        property string hostId
        function ask(id) {
            hostId = id;
            const h = Hosts.host(id);
            title = qsTr("Delete %1?").arg(h.name);
            text = h.kind === "desktop"
                ? qsTr("It goes from your hosts, with its saved password and its last screen. The PC itself isn't changed.")
                : qsTr("It goes from your hosts, with any password saved for it. Its entry in known hosts stays, so Remote still knows it if you add it again.");
            open();
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteDialog.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete")
                onClicked: {
                    Secrets.remove("desktop:" + deleteDialog.hostId);
                    Hosts.remove(deleteDialog.hostId);
                    deleteDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: deleteKeyDialog
        property string keyPath
        function ask(path) {
            keyPath = path;
            const k = Keychain.key(path);
            title = qsTr("Delete %1?").arg(k.label);
            text = k.usedBy.length > 0
                ? qsTr("%1 use it. The key and its public half go to the bin, so you can bring them back from there until it's emptied.").arg(k.usedBy.join(", "))
                : qsTr("The key and its public half go to the bin, so you can bring them back from there until it's emptied.");
            open();
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: deleteKeyDialog.close()
            },
            UI.Button {
                kind: "primary"
                danger: true
                text: qsTr("Delete")
                onClicked: {
                    if (!Keychain.remove(deleteKeyDialog.keyPath)) {
                        toast.show(qsTr("The key couldn't be moved to the bin."));
                    }
                    deleteKeyDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: groupDialog
        property string renaming
        function ask(name) {
            renaming = name;
            title = name.length > 0 ? qsTr("Rename %1").arg(name) : qsTr("New group");
            groupName.text = name;
            open();
            groupName.forceActiveFocus();
        }
        UI.TextField {
            id: groupName
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            onAccepted: groupSave.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: groupDialog.close()
            },
            UI.Button {
                id: groupSave
                kind: "primary"
                text: groupDialog.renaming.length > 0 ? qsTr("Rename") : qsTr("Add")
                enabled: groupName.text.trim().length > 0
                onClicked: {
                    if (groupDialog.renaming.length > 0) {
                        Hosts.renameGroup(groupDialog.renaming, groupName.text);
                        if (window.page === "group:" + groupDialog.renaming) window.page = "group:" + groupName.text.trim();
                    } else {
                        Hosts.addGroup(groupName.text);
                    }
                    groupDialog.close();
                }
            }
        ]
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }

    // The automated screen test (App.testScript), never in normal use.
    Loader {
        active: App.testScript.toString().length > 0
        source: App.testScript
    }
}
