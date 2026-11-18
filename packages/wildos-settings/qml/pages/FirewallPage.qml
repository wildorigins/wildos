import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Privacy and security > Firewall (SettingsFirewall.dc): the firewall
// itself, how much the network in use is trusted, and one card of what
// other computers can reach, with what the firewall turned away today at
// its foot. Everything here goes through Firewall and its one root helper,
// settings-firewall. A public network gets none of the rows, Maximum
// protection keeps discovery off, and Windows mode is always let through,
// so the switches say so rather than claim otherwise.
PageScroll {
    id: page

    readonly property var net: NetworkSettings.current
    readonly property bool connected: (net.uuid || "").length > 0
    readonly property bool isPublic: connected && Firewall.publicNetworks[net.uuid] === true
    readonly property bool isHome: connected && Firewall.homeNetworks[net.uuid] === true
    // Joined but never called Home or Public: discovery works as it always
    // did, and nothing else is let in until Home is chosen.
    readonly property bool undecided: connected && !isPublic && !isHome
    readonly property bool maximum: System.protectionLevel === "maximum"
    readonly property bool on: Firewall.enabled
    // The rows can let anything in: the firewall is on and this network is
    // trusted.
    readonly property bool live: on && !isPublic && !undecided
    readonly property string publicReason: qsTr("Off while you're on a public network")
    readonly property string lockReason: undecided ? qsTr("Off until you choose Home for this network") : publicReason

    // "Today" moves on at midnight without anything new coming in.
    property date now: new Date()
    readonly property var today: Firewall.blocked.filter(b => b.when.toDateString() === page.now.toDateString())

    function whoPhrase(kind) {
        return kind === "router" ? qsTr("your router")
            : kind === "local" ? qsTr("another PC on this network")
            : kind === "internet" ? qsTr("the internet")
            : qsTr("another local network");
    }

    function blockedLine() {
        const n = page.today.length;
        if (n === 0) {
            return qsTr("Nothing has been turned away today.");
        }
        const latest = page.today[0];
        return n === 1 ? qsTr("Turned away today: 1 connection. It was at %1, from %2.").arg(Format.time(latest.when)).arg(page.whoPhrase(latest.kind))
            : qsTr("Turned away today: %1 connections. The latest was at %2, from %3.").arg(n).arg(Format.time(latest.when)).arg(page.whoPhrase(latest.kind));
    }

    readonly property var serviceNames: ({ files: qsTr("file sharing"), remote: qsTr("Remote Desktop"), ssh: qsTr("the SSH server") })

    function serviceText(name, normal, whenOn) {
        if (Firewall.busyWhat === name) {
            const s = Firewall.services[name] || {};
            // The step is the helper's English word: matched here and
            // shown in the display language.
            const what = page.serviceNames[name];
            if (Firewall.step === "Downloading") {
                return Firewall.progress >= 0 ? qsTr("Downloading %1, %2%").arg(what).arg(Firewall.progress) : qsTr("Downloading %1").arg(what);
            }
            if (Firewall.step === "Installing") {
                return Firewall.progress >= 0 ? qsTr("Installing %1, %2%").arg(what).arg(Firewall.progress) : qsTr("Installing %1").arg(what);
            }
            if (Firewall.step.length > 0) {
                return Firewall.step;
            }
            return s.on ? qsTr("Turning it off…") : qsTr("Turning it on…");
        }
        const s = Firewall.services[name] || {};
        if (page.live && name === "remote" && s.on && s.running === false) {
            return qsTr("Nobody can sign in yet. Add an account under Who can sign in.");
        }
        return page.live && s.on && whenOn ? whenOn : normal;
    }

    function scopeText(rule) {
        if (rule.scope === "anyone") {
            return qsTr("Anyone, including the internet");
        }
        if (rule.scope === "device") {
            return qsTr("%1 only").arg(rule.address);
        }
        return qsTr("Devices on this network only");
    }

    function ruleText(rule) {
        const port = rule.proto === "udp" ? qsTr("UDP port %1").arg(rule.port) : qsTr("Port %1").arg(rule.port);
        const when = rule.session ? qsTr("Until you restart")
            : Format.isValid(rule.until) ? qsTr("Until %1").arg(Format.time(rule.until))
            : qsTr("Added by you");
        return port + " · " + page.scopeText(rule) + " · " + when;
    }

    // Coming back from Who can sign in may have started Remote Desktop.
    Component.onCompleted: Firewall.refresh()

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: Firewall
        function onFailed(message) {
            notice.show(qsTr("Nothing was changed"), message, true);
        }
        function onDone(what) {
            notice.hide();
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 16

        // The firewall itself.
        Rectangle {
            id: hero
            readonly property bool good: Firewall.enabled && Firewall.loaded
            Layout.fillWidth: true
            implicitHeight: heroRow.implicitHeight + 32
            radius: 12
            color: UI.Style.tone(0.05)
            border.width: 1
            border.color: UI.Style.tone(0.07)

            RowLayout {
                id: heroRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18

                Rectangle {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    radius: 26
                    color: hero.good ? Qt.rgba(95 / 255, 211 / 255, 141 / 255, 0.16) : UI.Style.warningSoft

                    PathIcon {
                        anchors.centerIn: parent
                        name: hero.good ? "shield-round-check" : "shield-round-alert"
                        size: 26
                        strokeWidth: 2.2
                        color: hero.good ? UI.Style.success : UI.Style.warning
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: !Firewall.enabled ? qsTr("The firewall is off")
                            : !Firewall.loaded ? qsTr("The firewall didn't load")
                            : qsTr("The firewall is on")
                        size: 17
                        font.weight: UI.Style.weightBold
                        wrapMode: Text.Wrap
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: !Firewall.enabled ? qsTr("Other computers can reach any app on this PC that waits for connections. Windows mode is still kept to itself.")
                            : !Firewall.loaded ? qsTr("Other computers can reach any app on this PC that waits for connections. Turn the firewall off and on again to load it.")
                            : qsTr("Nothing can connect to this PC unless you allow it below. Your own apps go online as normal.")
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                UI.Toggle {
                    checked: Firewall.enabled
                    enabled: Firewall.available && !Firewall.busy
                    Accessible.name: qsTr("Firewall")
                    onToggled: Firewall.setEnabled(checked)
                }
            }
        }

        // The network in use, and how far it is trusted.
        UI.Card {
            Layout.fillWidth: true

            UI.SettingRow {
                width: parent.width
                minHeight: 60
                enabled: page.on
                leading: PathIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: page.net.wifi === false ? "ethernet" : "wifi-round"
                    size: 20
                    strokeWidth: 1.8
                    color: UI.Style.ink
                }
                title: page.connected ? page.net.name : qsTr("Not connected")
                description: !page.connected ? qsTr("Choose Home or Public for each network when you join it.")
                    : page.isPublic ? qsTr("A public network. Everything below is switched off until you leave.")
                    : page.undecided ? qsTr("Is this a network you trust? Choose Home to let in what you switch on below, or Public to keep it all out.")
                    : qsTr("A network you trust. On a public network, everything below is switched off until you leave.")

                Segmented {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.connected
                    enabled: page.on && !Firewall.busy
                    options: [{ value: "home", text: qsTr("Home") }, { value: "public", text: qsTr("Public") }]
                    value: page.isPublic ? "public" : page.isHome ? "home" : ""
                    onPicked: v => Firewall.setPublic(page.net.uuid, v === "public")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: -4
            spacing: 12

            SectionTitle {
                Layout.fillWidth: true
                text: qsTr("What other computers can reach")
            }

            UI.Button {
                implicitHeight: 32
                iconName: "plus"
                text: qsTr("Allow an app or port")
                enabled: page.on && Firewall.available && !Firewall.busy
                onClicked: allowDialog.openFor(null)
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    minHeight: 60
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "printer-round"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    enabled: page.on
                    title: qsTr("Network discovery")
                    description: page.maximum ? qsTr("Off at Maximum protection, which keeps this PC hidden on the network.")
                        : qsTr("Find printers, screens and speakers nearby, and let them find this PC.")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.on && !page.isPublic && Firewall.discovery && !page.maximum
                        enabled: page.on && !Firewall.busy
                        locked: page.on && (page.isPublic || page.maximum)
                        lockedReason: page.maximum ? qsTr("Maximum protection keeps this off") : page.publicReason
                        Accessible.name: qsTr("Network discovery")
                        onToggled: Firewall.setDiscovery(checked)
                    }
                }

                Repeater {
                    model: [
                        { key: "files", icon: "folder-round", title: qsTr("File sharing"), text: qsTr("Share folders with other PCs on this network."),
                          on: qsTr("Share a folder from its Properties in Files, under Sharing.") },
                        { key: "remote", icon: "monitor-round", title: qsTr("Remote Desktop"), text: qsTr("Let another PC see and control this one."),
                          on: qsTr("Let another PC see and control this one, signing in with an account you add under Who can sign in.") },
                        { key: "ssh", icon: "terminal-round", title: "SSH", text: qsTr("Terminal access from another computer."),
                          on: qsTr("Terminal access from another computer, signing in with an account on this PC.") }
                    ]

                    UI.SettingRow {
                        id: serviceRow
                        required property var modelData
                        readonly property var svc: Firewall.services[modelData.key] || ({})
                        width: parent.width
                        minHeight: 60
                        leading: PathIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: serviceRow.modelData.icon
                            size: 20
                            strokeWidth: 1.8
                            color: UI.Style.ink
                        }
                        enabled: page.on
                        title: modelData.title
                        description: page.serviceText(modelData.key, modelData.text, modelData.on)

                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: serviceRow.modelData.key === "remote" && serviceRow.svc.on === true && Firewall.busyWhat !== "remote"
                            text: qsTr("Who can sign in")
                            enabled: page.on
                            onClicked: Navigator.open("remotedesktop")
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: page.live && serviceRow.svc.on === true
                            enabled: page.on && !Firewall.busy
                            locked: page.on && (page.isPublic || page.undecided)
                            lockedReason: page.lockReason
                            Accessible.name: serviceRow.modelData.title
                            onToggled: Firewall.setService(serviceRow.modelData.key, checked)
                        }
                    }
                }

                Repeater {
                    model: Firewall.rules

                    UI.SettingRow {
                        id: ruleRow
                        required property var modelData
                        width: parent.width
                        minHeight: 60
                        leading: PathIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "server"
                            size: 20
                            strokeWidth: 1.8
                            color: UI.Style.ink
                        }
                        enabled: page.on
                        title: modelData.name
                        description: page.ruleText(modelData)

                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Remove")
                            enabled: page.on && !Firewall.busy
                            Accessible.name: qsTr("Remove %1").arg(ruleRow.modelData.name)
                            onClicked: Firewall.removeRule(ruleRow.modelData.id)
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: page.live && ruleRow.modelData.on
                            enabled: page.on && !Firewall.busy
                            locked: page.on && (page.isPublic || page.undecided)
                            lockedReason: page.lockReason
                            Accessible.name: ruleRow.modelData.name
                            onToggled: Firewall.setRuleOn(ruleRow.modelData.id, checked)
                        }
                    }
                }

                // Windows mode's virtual PC reaches this one through its own
                // bridge, which its own firewall (wildos_win) limits.
                UI.SettingRow {
                    visible: WindowsMode.installed
                    minHeight: 60
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "lock-round"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    title: qsTr("Windows mode")
                    description: qsTr("Talks to this PC only and never to your network. Managed by Wild OS.")

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Always on")
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                }

                Item {
                    width: parent.width
                    height: 44

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: UI.Style.borderSubtle
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 18

                        UI.Label {
                            Layout.fillWidth: true
                            text: page.blockedLine()
                            size: 12.5
                            color: UI.Style.inkMuted
                            elide: Text.ElideRight
                        }

                        UI.LinkButton {
                            visible: Firewall.blocked.length > 0
                            text: qsTr("See what was blocked")
                            onClicked: activityDialog.open()
                        }
                    }
                }
            }
        }
    }

    Timer {
        interval: 60000
        running: page.visible
        repeat: true
        onTriggered: page.now = new Date()
    }

    FirewallAllowDialog {
        id: allowDialog
        networkName: page.connected ? page.net.name : ""
    }

    FirewallActivityDialog {
        id: activityDialog
        onAllowRequested: entry => allowDialog.openFor(entry)
    }
}
