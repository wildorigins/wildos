import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Remote
import WildOS.UI as UI

// A new host, or changes to one (RemoteNew.dc). Desktops: the computer,
// how to sign in, how to show it, and what it may reach of this PC.
// Terminals: the server, the account and key, ports to forward and the
// colour of its tab.
Panel {
    id: dialog

    property var host: ({})
    property string kind: "desktop"
    property bool editing: false
    property var forwards: []

    signal saved(string id, bool connect)

    width: Math.min(900, parent ? parent.width - 48 : 900)
    padding: 28
    topPadding: 26

    function create(k) {
        editing = false;
        host = {};
        kind = k || "desktop";
        load();
        open();
        address.forceActiveFocus();
    }
    function edit(id) {
        const h = Hosts.host(id);
        if (!h.id) {
            return;
        }
        editing = true;
        host = h;
        kind = h.kind;
        load();
        open();
    }
    function load() {
        const h = host;
        const d = h.desktop || {};
        const s = h.ssh || {};
        const where = Hosts.addressParts(h.address || "");
        address.text = where.host;
        port.text = where.port > 0 ? String(where.port) : "";
        name.text = h.name && h.name !== h.address && h.name !== where.host ? h.name : "";
        group.currentIndex = Math.max(0, groupModel.findIndex(o => o.value === (h.group || "")));
        user.text = h.user || "";
        note.text = h.note || "";
        passwordMode = d.password || "ask";
        // A new desktop starts with Remote's settings; a saved one has its own.
        display = d.display || RemoteSettings.desktopDisplay;
        resolution.currentIndex = Math.max(0, resolutionModel.findIndex(o => o.value === (d.resolution || "match")));
        scaling.currentIndex = Math.max(0, scalingModel.findIndex(o => o.value === (d.scaling || "auto")));
        clipboard.checked = d.clipboard === undefined ? RemoteSettings.desktopClipboard : d.clipboard !== false;
        sound.checked = d.sound !== false;
        microphone.checked = d.microphone === true;
        printers.checked = d.printers === true;
        folders.checked = d.folders === true;
        folderPath = d.folderPath || App.homePath() + "/Downloads";
        gateway.text = d.gateway || "";
        speed.currentIndex = Math.max(0, speedModel.findIndex(o => o.value === (d.speed || "auto")));
        advanced = (d.gateway || "").length > 0;
        forwards = (s.forwards || []).slice();
        colour = s.colour || Hosts.colours()[0];
        identity.currentIndex = Math.max(0, identityModel.findIndex(o => o.value === (s.identity || "")));
        passwordSaved = false;
        if (editing && h.kind === "terminal") {
            const key = Hosts.passwordKey(h);
            Secrets.exists(key, found => {
                if (dialog.host.id === h.id) {
                    dialog.passwordSaved = found;
                }
            });
        }
    }
    function collect() {
        const out = Object.assign({}, host);
        out.kind = kind;
        // A port typed into the address still counts; the Port field wins.
        const where = Hosts.addressParts(address.text.trim());
        out.address = Hosts.joinAddress(where.host, port.text.length > 0 ? parseInt(port.text) : where.port, kind);
        out.name = name.text.trim() || where.host;
        out.group = group.currentValue || "";
        out.user = user.text.trim();
        out.note = note.text.trim();
        if (kind === "desktop") {
            out.desktop = Object.assign({}, host.desktop || {}, {
                password: passwordMode,
                display: display,
                resolution: resolution.currentValue,
                scaling: scaling.currentValue,
                clipboard: clipboard.checked,
                sound: sound.checked,
                microphone: microphone.checked,
                printers: printers.checked,
                folders: folders.checked,
                folderPath: folderPath,
                gateway: gateway.text.trim(),
                speed: speed.currentValue
            });
        } else {
            out.ssh = Object.assign({}, host.ssh || {}, {
                identity: identity.currentValue || "",
                forwards: forwards,
                colour: colour
            });
        }
        return out;
    }
    function save(connect) {
        if (kind === "desktop" && passwordMode === "ask" && host.desktop && host.desktop.password === "keyring") {
            // Asking every time from now on: the saved password goes.
            Secrets.remove("desktop:" + host.id);
        }
        const id = Hosts.save(collect());
        close();
        saved(id, connect);
    }

    property string passwordMode: "ask"
    property string display: "full"
    property string folderPath
    property bool advanced: false
    property string colour: "#5fd39a"
    // An SSH password saved for this host's account, as it was opened.
    property bool passwordSaved: false

    readonly property var groupModel: [{ value: "", text: qsTr("No group") }].concat(Hosts.groups.map(g => ({ value: g, text: g })))
    readonly property var resolutionModel: [
        { value: "match", text: qsTr("Match my screen") },
        { value: "1920x1080", text: "1920 × 1080" },
        { value: "1600x900", text: "1600 × 900" },
        { value: "1366x768", text: "1366 × 768" },
        { value: "1280x720", text: "1280 × 720" }
    ]
    readonly property var scalingModel: [
        { value: "auto", text: qsTr("Automatic") },
        { value: "100", text: "100%" },
        { value: "125", text: "125%" },
        { value: "150", text: "150%" },
        { value: "200", text: "200%" }
    ]
    readonly property var speedModel: [
        { value: "auto", text: qsTr("Work it out") },
        { value: "lan", text: qsTr("Fast network") },
        { value: "broadband", text: qsTr("Home broadband") },
        { value: "modem", text: qsTr("Slow connection") }
    ]
    readonly property var identityModel: [{ value: "", text: qsTr("Any key in my keychain") }].concat(Keychain.keys.map(k => ({ value: k.path, text: k.label })))

    contentItem: ColumnLayout {
        spacing: 18

        // Heading
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Image {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                source: "qrc:/qt/qml/WildOS/Remote/data/org.wildos.remote.svg"
                sourceSize: Qt.size(80, 80)
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                UI.Label {
                    text: dialog.editing ? qsTr("Edit %1").arg(dialog.host.name) : dialog.kind === "desktop" ? qsTr("New connection") : qsTr("New host")
                    size: 22
                    font.weight: UI.Style.weightBold
                }
                UI.Label {
                    text: dialog.kind === "desktop" ? qsTr("Connect to a Windows PC or server with Remote Desktop switched on")
                        : qsTr("Connect to a server over SSH, in a terminal with its files beside it")
                    size: 14
                    color: UI.Style.inkMuted
                }
            }
            Segments {
                visible: !dialog.editing
                Layout.alignment: Qt.AlignTop
                options: [{ value: "desktop", text: qsTr("Desktop") }, { value: "terminal", text: qsTr("Terminal") }]
                value: dialog.kind
                onChosen: v => dialog.kind = v
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 32

            // Left: the computer and signing in.
            ColumnLayout {
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8

                Heading { text: dialog.kind === "desktop" ? qsTr("Computer") : qsTr("Server") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FieldLabel { text: qsTr("Address") }
                        UI.TextField {
                            id: address
                            Layout.fillWidth: true
                            size: "large"
                            leftPadding: 40
                            placeholderText: dialog.kind === "desktop" ? "studio-02.office.lan" : "web-01.hosting.lan"
                            // "host:2222" typed or pasted: the port goes to its own field.
                            onEditingFinished: {
                                const where = Hosts.addressParts(text.trim());
                                if (where.port > 0) {
                                    text = where.host;
                                    port.text = String(where.port);
                                }
                            }
                            UI.Icon {
                                x: 14
                                anchors.verticalCenter: parent.verticalCenter
                                name: dialog.kind === "desktop" ? "monitor" : "terminal"
                                size: 15
                                color: UI.Style.iconMuted
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 110
                        spacing: 8
                        FieldLabel { text: qsTr("Port") }
                        UI.TextField {
                            id: port
                            Layout.fillWidth: true
                            size: "large"
                            placeholderText: dialog.kind === "desktop" ? "3389" : "22"
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                    }
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: dialog.kind === "desktop" ? qsTr("A name or IP address. Leave the port empty for 3389, the usual one.")
                        : qsTr("A name or IP address. Leave the port empty for 22, the usual one.")
                    size: 12
                    color: UI.Style.inkSubtle
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FieldLabel { text: qsTr("Name") }
                        UI.TextField {
                            id: name
                            Layout.fillWidth: true
                            size: "large"
                            placeholderText: address.text.split(":")[0] || qsTr("Studio 2")
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 156
                        spacing: 8
                        FieldLabel { text: qsTr("Group") }
                        UI.ComboBox {
                            id: group
                            Layout.fillWidth: true
                            look: "field"
                            model: dialog.groupModel
                        }
                    }
                }

                Heading { text: qsTr("Signing in"); Layout.topMargin: 10 }
                FieldLabel { text: qsTr("Username") }
                UI.TextField {
                    id: user
                    Layout.fillWidth: true
                    size: "large"
                    leftPadding: 40
                    placeholderText: dialog.kind === "desktop" ? "administrator" : "alex"
                    UI.Icon {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        name: "account"
                        size: 15
                        color: UI.Style.iconMuted
                    }
                }
                Segments {
                    visible: dialog.kind === "desktop"
                    Layout.fillWidth: true
                    options: [{ value: "ask", text: qsTr("Ask every time") }, { value: "keyring", text: qsTr("Save in keyring") }]
                    value: dialog.passwordMode
                    onChosen: v => dialog.passwordMode = v
                }
                UI.Label {
                    visible: dialog.kind === "desktop" && dialog.passwordMode === "keyring"
                    Layout.fillWidth: true
                    text: qsTr("You type it once when you first connect; it's kept in your keyring, never in a file.")
                    size: 12
                    color: UI.Style.inkSubtle
                    wrapMode: Text.Wrap
                }
                ColumnLayout {
                    visible: dialog.kind === "terminal"
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel { text: qsTr("Key") }
                    UI.ComboBox {
                        id: identity
                        Layout.fillWidth: true
                        look: "field"
                        model: dialog.identityModel
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: identity.currentIndex === 0
                            ? qsTr("ssh tries your keys and the key agent, then asks for a password.")
                            : qsTr("Only this key is offered to the server.")
                        size: 12
                        color: UI.Style.inkSubtle
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        visible: dialog.passwordSaved
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 12
                        UI.Label {
                            Layout.fillWidth: true
                            text: qsTr("A password for %1 is saved in your keyring.").arg(dialog.host.user ? dialog.host.user + "@" + Hosts.hostName(dialog.host) : Hosts.hostName(dialog.host))
                            size: 12
                            color: UI.Style.inkSubtle
                            wrapMode: Text.Wrap
                        }
                        UI.Button {
                            size: "small"
                            text: qsTr("Forget saved password")
                            onClicked: {
                                Secrets.remove(Hosts.passwordKey(dialog.host));
                                dialog.passwordSaved = false;
                            }
                        }
                    }
                }

                Heading { text: dialog.kind === "desktop" ? qsTr("Display") : qsTr("Card"); Layout.topMargin: 10 }
                Segments {
                    visible: dialog.kind === "desktop"
                    Layout.fillWidth: true
                    options: [{ value: "window", text: qsTr("Window") }, { value: "full", text: qsTr("Full screen") }, { value: "all", text: qsTr("All monitors") }]
                    value: dialog.display
                    onChosen: v => dialog.display = v
                }
                RowLayout {
                    visible: dialog.kind === "desktop"
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FieldLabel { text: qsTr("Resolution") }
                        UI.ComboBox {
                            id: resolution
                            Layout.fillWidth: true
                            look: "field"
                            model: dialog.resolutionModel
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FieldLabel { text: qsTr("Scaling") }
                        UI.ComboBox {
                            id: scaling
                            Layout.fillWidth: true
                            look: "field"
                            model: dialog.scalingModel
                        }
                    }
                }
                ColumnLayout {
                    visible: dialog.kind === "terminal"
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel { text: qsTr("Note") }
                    UI.TextField {
                        id: note
                        Layout.fillWidth: true
                        size: "large"
                        placeholderText: qsTr("Shown on its card, like Ubuntu 24.04")
                    }
                    Row {
                        Layout.topMargin: 6
                        spacing: 8
                        Repeater {
                            model: Hosts.colours()
                            Rectangle {
                                required property string modelData
                                width: 26
                                height: 26
                                radius: 13
                                color: modelData
                                border.width: dialog.colour === modelData ? 3 : 0
                                border.color: UI.Style.ink
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: qsTr("Tab colour")
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: dialog.colour = parent.modelData
                                }
                            }
                        }
                    }
                }
            }

            // Right: what the remote PC may reach, or ports to forward.
            ColumnLayout {
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 0

                Heading { text: dialog.kind === "desktop" ? qsTr("Share with this PC") : qsTr("Port forwarding") }
                UI.Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    text: dialog.kind === "desktop"
                        ? qsTr("Anything left off, the remote PC can't reach. Changes made from the toolbar during a session take effect when it reconnects.")
                        : qsTr("Reach a port on the server, or a machine it can see, as if it were on this PC. Only this PC can use it, and only while the terminal is open.")
                    size: 13
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    lineHeight: 1.35
                }

                ColumnLayout {
                    visible: dialog.kind === "desktop"
                    Layout.fillWidth: true
                    spacing: 0
                    ShareRow { id: clipboard; title: qsTr("Clipboard"); detail: qsTr("Copy and paste both ways"); glyph: RemoteIcons.clipboard }
                    ShareRow { id: sound; title: qsTr("Sound plays here"); detail: qsTr("Through this PC's speakers"); iconName: "sound" }
                    ShareRow { id: microphone; title: qsTr("Microphone"); detail: qsTr("For calls on the remote PC"); iconName: "mic" }
                    ShareRow { id: printers; title: qsTr("Printers"); detail: qsTr("Print from the remote PC to yours"); iconName: "printer" }
                    ShareRow {
                        id: folders
                        title: qsTr("Folders")
                        detail: dialog.folderPath.replace(App.homePath(), "~").split("/").pop() + qsTr(", the remote PC can change what's in it")
                        glyph: RemoteIcons.folder
                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Choose")
                            onClicked: folderDialog.open()
                        }
                    }
                    ShareRow {
                        id: monitors
                        title: qsTr("Use all my monitors")
                        detail: qsTr("Only in full screen")
                        glyph: RemoteIcons.displays
                        checked: dialog.display === "all"
                        onToggled: dialog.display = checked ? "all" : "full"
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            UI.Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: dialog.advanced ? "chevron-down" : "chevron-right"
                                size: 13
                                color: UI.Style.iconMuted
                            }
                            UI.Label {
                                text: qsTr("Advanced: gateway, connection speed")
                                size: 14
                                color: UI.Style.inkMuted
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.advanced = !dialog.advanced
                        }
                    }
                    GridLayout {
                        visible: dialog.advanced
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        FieldLabel { text: qsTr("Gateway") }
                        FieldLabel { text: qsTr("Connection") }
                        UI.TextField {
                            id: gateway
                            Layout.fillWidth: true
                            placeholderText: qsTr("None")
                        }
                        UI.ComboBox {
                            id: speed
                            Layout.fillWidth: true
                            model: dialog.speedModel
                        }
                    }
                }

                ColumnLayout {
                    visible: dialog.kind === "terminal"
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: dialog.forwards
                        Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            radius: 10
                            color: UI.Style.surfaceRest
                            border.width: 1
                            border.color: UI.Style.borderSubtle
                            UI.Glyph {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                path: RemoteIcons.forward
                                color: UI.Style.iconInk
                            }
                            UI.Label {
                                x: 38
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("localhost:%1 to %2:%3").arg(modelData.localPort).arg(modelData.remoteHost || "localhost").arg(modelData.remotePort)
                                size: 13
                                font.family: "JetBrains Mono"
                            }
                            UI.IconButton {
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "close"
                                iconSize: 13
                                width: 30
                                height: 30
                                tip: qsTr("Remove")
                                onClicked: {
                                    const f = dialog.forwards.slice();
                                    f.splice(index, 1);
                                    dialog.forwards = f;
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        UI.TextField {
                            id: localPort
                            Layout.preferredWidth: 100
                            placeholderText: qsTr("This PC's port")
                            validator: IntValidator { bottom: 1024; top: 65535 }
                        }
                        UI.Label { text: qsTr("to"); size: 13; color: UI.Style.inkMuted }
                        UI.TextField {
                            id: remoteHost
                            Layout.fillWidth: true
                            placeholderText: "localhost"
                        }
                        UI.TextField {
                            id: remotePort
                            Layout.preferredWidth: 80
                            placeholderText: qsTr("Port")
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                        UI.Button {
                            text: qsTr("Add")
                            enabled: localPort.acceptableInput && remotePort.acceptableInput
                            onClicked: {
                                dialog.forwards = dialog.forwards.concat([{
                                    localPort: parseInt(localPort.text),
                                    remoteHost: remoteHost.text.trim() || "localhost",
                                    remotePort: parseInt(remotePort.text)
                                }]);
                                localPort.text = "";
                                remoteHost.text = "";
                                remotePort.text = "";
                            }
                        }
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Ports from 1024 up, so no administrator rights are needed.")
                        size: 12
                        color: UI.Style.inkSubtle
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 10
            Item { Layout.fillWidth: true }
            UI.Button {
                size: "large"
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            UI.Button {
                size: "large"
                text: qsTr("Save")
                enabled: address.text.trim().length > 0 && (port.text.length === 0 || port.acceptableInput)
                onClicked: dialog.save(false)
            }
            UI.Button {
                size: "large"
                kind: "primary"
                text: qsTr("Save and connect")
                enabled: address.text.trim().length > 0 && (port.text.length === 0 || port.acceptableInput)
                onClicked: dialog.save(true)
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose a folder to share")
        currentFolder: "file://" + dialog.folderPath
        onAccepted: dialog.folderPath = decodeURIComponent(selectedFolder.toString().replace("file://", ""))
    }

    component Heading: UI.Label {
        size: 12
        font.weight: UI.Style.weightBold
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
        color: UI.Style.inkMuted
    }
    component FieldLabel: UI.Label {
        size: 13
        font.weight: UI.Style.weightSemiBold
    }
    component ShareRow: Item {
        id: shareRow
        property string title
        property string detail
        property string iconName
        property string glyph
        property alias checked: toggle.checked
        default property alias extra: extraRow.data
        signal toggled()

        Layout.fillWidth: true
        Layout.preferredHeight: 52

        Rectangle {
            width: parent.width
            height: 1
            color: UI.Style.divider
        }
        UI.Icon {
            visible: shareRow.glyph.length === 0
            anchors.verticalCenter: parent.verticalCenter
            name: shareRow.iconName
            size: 16
            color: UI.Style.iconMuted
        }
        UI.Glyph {
            visible: shareRow.glyph.length > 0
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            path: shareRow.glyph
            color: UI.Style.iconMuted
        }
        Column {
            x: 29
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 29 - toggle.width - extraRow.width - 20
            spacing: 2
            UI.Label {
                width: parent.width
                text: shareRow.title
                size: 14
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                width: parent.width
                text: shareRow.detail
                size: 12
                color: UI.Style.inkMuted
            }
        }
        Row {
            id: extraRow
            anchors.right: toggle.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        }
        UI.Toggle {
            id: toggle
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Accessible.name: shareRow.title
            onToggled: shareRow.toggled()
        }
    }
}
