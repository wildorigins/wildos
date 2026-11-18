import QtQuick
import QtQuick.Layouts
import org.kde.notificationmanager as NotificationManager
import WildOS.Settings
import WildOS.UI as UI

// Privacy and security > Advanced protection (AdvancedProtection.dc,
// AdvancedProtection2.dc): extra protection, none of it needed every day.
// Every choice is kept by the root helper settings-advanced in one store
// (Advanced), and the part of Wild OS that carries it out reads it or runs
// a hook when it changes. A row whose part isn't installed is left out, so
// no switch here does nothing.
//
// Three rows are the same switch as one elsewhere and show that switch's
// own state: USB is Block new USB devices (USBGuard), the VPN row is the
// kill switch in Advanced network settings, and presenting mode is the
// Notifications page's do not disturb while sharing the screen. Strict and
// Maximum protection switch some of these on (settings-advanced level),
// never one the person set here.
PageScroll {
    id: page

    readonly property var values: Advanced.values
    readonly property var installed: Advanced.installed
    readonly property bool ready: Advanced.available && !Advanced.busy

    readonly property bool usbShown: Protection.status.devices !== undefined
    readonly property bool usbOn: Protection.status.devices === "on"
    readonly property bool hasVpn: NetworkSettings.vpns.length > 0
    readonly property bool presentingOn: notifications.inhibitNotificationsWhenScreenSharing && notifications.inhibitNotificationsWhenScreensMirrored

    // Each row shown, and whether it is on; the lights are always on.
    readonly property var rows: [
        { shown: page.usbShown, on: page.usbOn },
        { shown: page.installed.quiet_lock === true, on: page.values.quiet_lock === "on" },
        { shown: true, on: page.values.fingerprint_hours > 0 },
        { shown: true, on: page.values.restart_locked_hours > 0 },
        { shown: true, on: page.values.radios_idle_off === "on" },
        { shown: true, on: page.values.dns !== "off" },
        { shown: true, on: NetworkSettings.killSwitch },
        { shown: true, on: true },
        { shown: true, on: page.values.clipboard_forget === "on" },
        { shown: true, on: page.presentingOn },
        { shown: page.installed.scrub_files === true, on: page.values.scrub_files === "on" },
        { shown: true, on: page.values.usb_encrypt === "on" },
        { shown: page.installed.guest === true, on: page.values.guest === "on" },
        { shown: page.installed.safebox === true, on: page.values.safebox === "on" },
        { shown: page.installed.panic_key === true, on: page.values.panic_key === "on" }
    ]
    readonly property int shownCount: rows.filter(r => r.shown).length
    readonly property int onCount: rows.filter(r => r.shown && r.on).length

    NotificationManager.Settings {
        id: notifications
        live: true
    }

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: Advanced
        function onFailed(key, message) {
            notice.show(qsTr("Nothing was changed"), message, true);
        }
        function onDone(key) {
            notice.hide();
        }
    }

    // A row with a switch, and a drop-down before it when it has one.
    component OptionRow: UI.SettingRow {
        id: optionRow

        property string glyph
        property bool on
        property bool switchEnabled: page.ready
        property bool locked: false
        property string lockedReason
        // What goes before the switch: a drop-down, or the panic key.
        property alias extra: extraRow.data
        signal switched(bool on)

        width: parent ? parent.width : implicitWidth
        minHeight: 54
        leading: PathIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: optionRow.glyph
            size: 19
            strokeWidth: 1.8
            color: UI.Style.ink
        }

        Row {
            id: extraRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            visible: children.length > 0
        }

        UI.Toggle {
            id: toggle
            anchors.verticalCenter: parent.verticalCenter
            checked: optionRow.on
            enabled: optionRow.switchEnabled
            locked: optionRow.locked
            lockedReason: optionRow.lockedReason
            Accessible.name: optionRow.title
            onToggled: optionRow.switched(checked)

            // A cancelled password leaves the choice as it was, so the
            // switch goes back to it.
            Connections {
                target: Advanced
                function onChanged() {
                    toggle.checked = Qt.binding(() => optionRow.on);
                }
            }
            Connections {
                target: NetworkSettings
                function onKillSwitchChanged() {
                    toggle.checked = Qt.binding(() => optionRow.on);
                }
            }
        }
    }

    // A note in place of a switch ("Always on").
    component NoteRow: UI.SettingRow {
        id: noteRow

        property string glyph
        property string note

        width: parent ? parent.width : implicitWidth
        minHeight: 54
        leading: PathIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: noteRow.glyph
            size: 19
            strokeWidth: 1.8
            color: UI.Style.ink
        }

        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            text: noteRow.note
            size: 12.5
            color: UI.Style.inkSubtle
        }
    }

    component Heading: RowLayout {
        property alias text: title.text
        property alias trailing: trailingItems.data

        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.bottomMargin: -4
        spacing: 12

        SectionTitle {
            id: title
            Layout.fillWidth: true
        }

        Row {
            id: trailingItems
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 16

        // How much is switched on.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: heroRow.implicitHeight + 28
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
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    radius: 23
                    color: Qt.rgba(95 / 255, 211 / 255, 141 / 255, 0.16)

                    PathIcon {
                        anchors.centerIn: parent
                        name: "shield-round-check"
                        size: 24
                        strokeWidth: 2.1
                        color: UI.Style.success
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("%1 of %2 switched on").arg(page.onCount).arg(page.shownCount)
                        size: 16.5
                        font.weight: UI.Style.weightSemiBold
                        wrapMode: Text.Wrap
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Extra protection for people who want it. None of it is needed for everyday use.")
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                UI.Button {
                    implicitHeight: 32
                    text: qsTr("Reset to defaults")
                    enabled: page.ready
                    busy: Advanced.busyKey === "reset"
                    onClicked: resetDialog.open()
                }
            }
        }

        Heading {
            text: qsTr("When this PC is locked")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                // The same switch as Block new USB devices: USBGuard, plus
                // holding back every new device while locked
                // (wildos-usb-lock).
                OptionRow {
                    visible: page.usbShown
                    glyph: "usb-round"
                    title: qsTr("Ignore new USB devices while locked")
                    description: qsTr("A stick or cable plugged into a locked PC does nothing until you sign in.")
                    on: page.usbOn
                    onSwitched: on => Advanced.set("usb_locked", on)
                }

                OptionRow {
                    visible: page.installed.quiet_lock === true
                    glyph: "eye-off-round"
                    title: qsTr("Quiet lock screen")
                    description: qsTr("Notifications show that something arrived, never what it says.")
                    on: page.values.quiet_lock === "on"
                    onSwitched: on => Advanced.set("quiet_lock", on)
                }

                // fingerprint-turn and pam_wildos_fingerprint read it.
                OptionRow {
                    id: fingerprintRow
                    glyph: "fingerprint"
                    title: qsTr("Fingerprint needs your password sometimes")
                    description: qsTr("Always after a restart, and again after this long.")
                    on: page.values.fingerprint_hours > 0
                    onSwitched: on => Advanced.set("fingerprint_hours", on ? fingerprintBox.currentValue : 0)

                    extra: UI.ComboBox {
                        id: fingerprintBox
                        width: 120
                        enabled: page.ready
                        Accessible.name: qsTr("How long the fingerprint works before the password is needed")
                        model: [
                            { value: 4, text: qsTr("4 hours") },
                            { value: 8, text: qsTr("8 hours") },
                            { value: 12, text: qsTr("12 hours") },
                            { value: 24, text: qsTr("24 hours") },
                            { value: 48, text: qsTr("48 hours") },
                            { value: 72, text: qsTr("72 hours") }
                        ]
                        readonly property int stored: page.values.fingerprint_hours > 0 ? page.values.fingerprint_hours : 48
                        currentIndex: Math.max(0, model.findIndex(o => o.value === stored))
                        onActivated: Advanced.set("fingerprint_hours", currentValue)
                    }
                }

                // wildos-advanced-session, in the person's session.
                OptionRow {
                    glyph: "power-round"
                    title: qsTr("Restart when left locked")
                    description: qsTr("Puts the disk back to fully encrypted.")
                    on: page.values.restart_locked_hours > 0
                    onSwitched: on => Advanced.set("restart_locked_hours", on ? restartBox.currentValue : 0)

                    extra: [
                        UI.ComboBox {
                            id: restartBox
                            width: 150
                            enabled: page.ready
                            Accessible.name: qsTr("Restart after")
                            model: [
                                { value: 4, text: qsTr("After 4 hours") },
                                { value: 8, text: qsTr("After 8 hours") },
                                { value: 12, text: qsTr("After 12 hours") },
                                { value: 18, text: qsTr("After 18 hours") },
                                { value: 24, text: qsTr("After 24 hours") },
                                { value: 48, text: qsTr("After 48 hours") }
                            ]
                            readonly property int stored: page.values.restart_locked_hours > 0 ? page.values.restart_locked_hours : 18
                            currentIndex: Math.max(0, model.findIndex(o => o.value === stored))
                            onActivated: Advanced.set("restart_locked_hours", currentValue)
                        },
                        // What happens when an app is still open with work
                        // in it: the list says it in full, the box in short.
                        UI.ComboBox {
                            width: 160
                            visible: page.values.restart_locked_hours > 0
                            enabled: page.ready
                            Accessible.name: qsTr("When apps are still open")
                            model: [
                                { value: "off", text: qsTr("Wait for open apps to close"), short: qsTr("Wait for apps") },
                                { value: "on", text: qsTr("Close open apps anyway (unsaved work may be lost)"), short: qsTr("Close apps anyway") }
                            ]
                            currentIndex: page.values.restart_locked_force === "on" ? 1 : 0
                            displayText: model[currentIndex].short
                            onActivated: Advanced.set("restart_locked_force", currentValue)
                        }
                    ]
                }
            }
        }

        Heading {
            text: qsTr("Network")
            trailing: UI.LinkButton {
                text: qsTr("Firewall")
                visible: Firewall.available
                onClicked: Navigator.open("firewall")
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                // wildos-radios-idle, which the hook starts and stops.
                OptionRow {
                    glyph: "wifi-off-round"
                    title: qsTr("Wi-Fi and Bluetooth switch themselves off")
                    description: qsTr("When nothing has been connected for 30 minutes.")
                    on: page.values.radios_idle_off === "on"
                    onSwitched: on => Advanced.set("radios_idle_off", on)
                }

                // wildos-network. On is encrypted wherever the network
                // allows it, and on every network or not at all under
                // Maximum protection (dns_strict); off leaves each network
                // on its own servers, unencrypted.
                OptionRow {
                    glyph: "globe-lock"
                    title: qsTr("Encrypted DNS")
                    description: qsTr("Your internet provider cannot see which sites you look up.")
                    on: page.values.dns !== "off"
                    onSwitched: on => Advanced.set("dns", on)

                    extra: UI.ComboBox {
                        width: 120
                        enabled: page.ready
                        Accessible.name: qsTr("Encrypted DNS provider")
                        model: [
                            { value: "quad9", text: "Quad9" },
                            { value: "mullvad", text: "Mullvad" },
                            { value: "cloudflare", text: "Cloudflare" }
                        ]
                        currentIndex: Math.max(0, model.findIndex(o => o.value === page.values.dns_provider))
                        onActivated: Advanced.set("dns_provider", currentValue)
                    }
                }

                // The kill switch of Advanced network settings itself.
                OptionRow {
                    glyph: "shield-off"
                    title: qsTr("Block everything unless the VPN is on")
                    description: NetworkSettings.killSwitchState === "blocking" ? qsTr("The VPN dropped, so the internet is blocked until it connects again or you turn this off.")
                        : NetworkSettings.killSwitchState === "mullvad" ? qsTr("The Mullvad app's own kill switch is guarding its connection.")
                        : qsTr("Nothing leaks if the VPN drops. Needs a VPN set up first.")
                    descriptionColor: NetworkSettings.killSwitchState === "blocking" ? UI.Style.warning : UI.Style.inkMuted
                    on: NetworkSettings.killSwitch
                    switchEnabled: NetworkSettings.busy.length === 0 && (page.hasVpn || NetworkSettings.killSwitch)
                    locked: !page.hasVpn && !NetworkSettings.killSwitch
                    lockedReason: qsTr("Add a VPN in Advanced network settings first")
                    onSwitched: on => NetworkSettings.setKillSwitch(on)
                }
            }
        }

        Heading {
            text: qsTr("Your data")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                NoteRow {
                    glyph: "video-round"
                    title: qsTr("Camera and microphone lights")
                    description: qsTr("An icon in the taskbar whenever either is in use, and which app is using it.")
                    note: qsTr("Always on")
                }

                // The clipboard panel (wildos-clipboard) wipes it.
                OptionRow {
                    glyph: "clipboard-x"
                    title: qsTr("Clipboard forgets passwords")
                    description: qsTr("Anything copied from a password box is wiped after 60 seconds.")
                    on: page.values.clipboard_forget === "on"
                    onSwitched: on => Advanced.set("clipboard_forget", on)
                }

                // The Notifications page's switch, which is the person's
                // own: set here at once, and through the store for the
                // protection levels and every other session
                // (wildos-advanced-session).
                OptionRow {
                    glyph: "presentation"
                    title: qsTr("Presenting mode")
                    description: qsTr("Holds notifications back while you share or record the screen.")
                    on: page.presentingOn
                    onSwitched: on => {
                        notifications.inhibitNotificationsWhenScreenSharing = on;
                        notifications.inhibitNotificationsWhenScreensMirrored = on;
                        notifications.save();
                        Advanced.set("presenting", on);
                    }
                }

                OptionRow {
                    visible: page.installed.scrub_files === true
                    glyph: "image-off"
                    title: qsTr("Remove personal details from files")
                    description: qsTr("Location, device and author names. Right-click a file in Files to use it.")
                    on: page.values.scrub_files === "on"
                    onSwitched: on => Advanced.set("scrub_files", on)

                    // Files keeps which details go (wildos-files).
                    extra: UI.LinkButton {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Choose what is removed")
                        enabled: page.values.scrub_files === "on"
                        onClicked: UI.System.run("wildos-files", ["--remove-details-choices"])
                    }
                }

                // Files' format window offers encryption only while it is on.
                OptionRow {
                    glyph: "lock-round"
                    title: qsTr("Password-locked USB sticks")
                    description: qsTr("Tick \"Encrypt this drive\" when you format a drive.")
                    on: page.values.usb_encrypt === "on"
                    onSwitched: on => Advanced.set("usb_encrypt", on)
                }
            }
        }

        Heading {
            visible: lendingCard.visible
            text: qsTr("Lending and testing")
        }

        UI.Card {
            id: lendingCard
            Layout.fillWidth: true
            visible: page.installed.guest === true || page.installed.safebox === true

            Column {
                width: parent.width

                OptionRow {
                    visible: page.installed.guest === true
                    glyph: "user-round"
                    title: qsTr("Guest session")
                    description: qsTr("Lend this PC. A clean desktop, wiped when they sign out.")
                    on: page.values.guest === "on"
                    onSwitched: on => Advanced.set("guest", on)
                }

                OptionRow {
                    visible: page.installed.safebox === true
                    glyph: "box"
                    title: qsTr("Safe box")
                    description: qsTr("Open an unknown program or attachment sealed off from your files and the network.")
                    on: page.values.safebox === "on"
                    onSwitched: on => Advanced.set("safebox", on)
                }
            }
        }

        // The duress password (DuressSetup.dc) comes later; its row waits
        // for it rather than offer a Set up that does nothing.
        Heading {
            visible: emergencyCard.visible
            text: qsTr("Emergencies")
        }

        UI.Card {
            id: emergencyCard
            Layout.fillWidth: true
            visible: page.installed.panic_key === true

            Column {
                width: parent.width

                OptionRow {
                    glyph: "siren"
                    title: qsTr("Panic key")
                    description: qsTr("Locks the screen, clears the clipboard and cuts the network in one press.")
                    on: page.values.panic_key === "on"
                    onSwitched: on => Advanced.set("panic_key", on)

                    extra: Rectangle {
                        width: 150
                        height: 32
                        radius: 6
                        color: UI.Style.tone(0.05)
                        border.width: 1
                        border.color: UI.Style.tone(0.10)

                        UI.Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Ctrl + Alt + End")
                            size: 13
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            PathIcon {
                name: "info-round"
                size: 14
                strokeWidth: 2
                color: UI.Style.inkSubtle
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Strict and Maximum protection switch some of these on for you. Anything you change here stays as you set it.")
                size: 12.5
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
        }
    }

    UI.Dialog {
        id: resetDialog
        title: qsTr("Reset advanced protection?")
        text: qsTr("Everything on this page goes back to how Wild OS comes, and what you changed here is forgotten. Strict and Maximum protection switch their options on again.")
        buttons: [
            UI.Button {
                text: qsTr("Reset")
                kind: "primary"
                onClicked: {
                    resetDialog.close();
                    notice.hide();
                    Advanced.reset();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: resetDialog.close()
            }
        ]
    }
}
