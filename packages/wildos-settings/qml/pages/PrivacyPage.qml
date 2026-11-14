import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Privacy and security, laid out as Settings.dc.html: the Security card
// (security-status, one row per protection), the protection level, what
// apps can reach, searching files and activity history, USB protection,
// then Steam and 32-bit apps.
PageScroll {
    id: page

    readonly property var perms: AppAccess.summary
    property string usbError
    property string levelError

    function appCount(n, what) {
        if (n === 0) {
            return "No apps can use your " + what;
        }
        return Format.plural(n, "app", "apps") + " can use your " + what;
    }

    Connections {
        target: Advanced
        function onFailed(key, message) {
            if (key === "usb_locked") {
                page.usbError = "USB protection could not be changed.";
            }
        }
        // A cancelled password leaves it as it was.
        function onChanged() {
            usbToggle.checked = Qt.binding(() => Protection.status.devices === "on");
        }
    }

    Connections {
        target: System
        function onUsbProtectionFailed(reason) {
            page.usbError = reason;
            usbToggle.checked = Qt.binding(() => Protection.status.devices === "on");
        }
        function onSecurityChanged() {
            page.usbError = "";
        }
        function onProtectionLevelFailed(reason) {
            page.levelError = reason;
        }
    }

    SecurityCard {
        compact: false
        Layout.fillHeight: false
    }

    // Standard is what Wild OS ships, firewall included. Strict also holds
    // back unknown USB devices found at start-up; Maximum also blocks network
    // discovery, Bluetooth and the camera (settings-protection). Both
    // shorten the screen lock.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Protection level"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            UI.ChoiceCard {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                showIndicator: true
                checkable: false
                title: "Standard"
                description: "Strong protection that stays out of your way. Recommended for most people."
                checked: System.protectionLevel === "standard"
                enabled: !System.protectionBusy
                onClicked: {
                    page.levelError = "";
                    System.setProtectionLevel("standard");
                }
            }

            UI.ChoiceCard {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                showIndicator: true
                checkable: false
                title: "Strict"
                description: "Tighter defaults. Some apps and devices will ask for permission first."
                checked: System.protectionLevel === "strict"
                enabled: !System.protectionBusy
                onClicked: {
                    page.levelError = "";
                    System.setProtectionLevel("strict");
                }
            }

            UI.ChoiceCard {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                showIndicator: true
                checkable: false
                title: "Maximum"
                description: "For high-risk situations. Expect some things to stop working."
                checked: System.protectionLevel === "maximum"
                enabled: !System.protectionBusy
                onClicked: {
                    page.levelError = "";
                    System.setProtectionLevel("maximum");
                }
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: page.levelError.length > 0
            text: page.levelError
            size: 12
            color: UI.Style.danger
        }

        UI.Label {
            Layout.fillWidth: true
            visible: Compat32.enabled && !Compat32.locked
            text: "Strict and Maximum turn Steam and 32-bit apps off again, from the next restart."
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }

        UI.Card {
            Layout.fillWidth: true
            visible: Advanced.available

            UI.SettingRow {
                width: parent.width
                iconName: "shield"
                title: "Advanced protection"
                description: "Extra protection for people who want it"
                chevron: true
                onClicked: Navigator.open("advancedprotection")
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "App permissions"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "camera"
                    title: "Camera"
                    description: page.appCount(page.perms.camera || 0, "camera")
                    chevron: true
                    onClicked: Navigator.open("camera")
                }

                UI.SettingRow {
                    iconName: "mic"
                    title: "Microphone"
                    description: page.appCount(page.perms.microphone || 0, "microphone")
                    chevron: true
                    onClicked: Navigator.open("microphone")
                }

                UI.SettingRow {
                    iconName: "location"
                    title: "Location"
                    description: !System.locationServices ? "Off for all apps"
                        : (page.perms.location || 0) > 0 ? Format.plural(page.perms.location, "app", "apps") + " can use your location"
                        : "Apps ask before using your location"
                    chevron: true
                    onClicked: Navigator.open("location")
                }

                UI.SettingRow {
                    iconName: "folder"
                    title: "Files and folders"
                    description: (page.perms.files || 0) === 0
                        ? "Apps ask before opening anything outside their own folder"
                        : Format.plural(page.perms.files, "app", "apps") + " can open your files without asking"
                    chevron: true
                    onClicked: Navigator.open("appfiles")
                }

                UI.SettingRow {
                    iconName: "sandbox"
                    title: "All app permissions"
                    description: "Internet and everything else apps from the Store can reach"
                    chevron: true
                    onClicked: Navigator.open("apppermissions")
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Search and history"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "search"
                    title: "Searching files"
                    description: !FileSearch.available ? "File indexing isn't installed"
                        : FileSearch.enabled ? "Your files are indexed so search finds them at once" : "Indexing is off"
                    chevron: true
                    onClicked: Navigator.open("searching")
                }

                UI.SettingRow {
                    iconName: "clock"
                    title: "Activity history"
                    description: ActivityHistory.remember ? "Recent files are remembered on this computer" : "Recent files are not remembered"
                    chevron: true
                    onClicked: Navigator.open("activityhistory")
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                visible: Protection.status.devices !== undefined
                minHeight: 64
                title: "Block new USB devices"
                description: page.usbError.length > 0 ? page.usbError
                    : "Memory sticks, drives, a keyboard, a mouse, webcams and audio devices work straight away. Anything else waits until you allow it from the shield on the taskbar."
                status: (Protection.status.devices === "on") ? "On" : "Off"

                UI.Toggle {
                    id: usbToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Protection.status.devices === "on"
                    enabled: !(System.securityChecking)
                    Accessible.name: "Block new USB devices"
                    // One switch with Advanced protection's "Ignore new USB
                    // devices while locked", kept in its store.
                    onToggled: {
                        page.usbError = "";
                        if (Advanced.available) {
                            Advanced.set("usb_locked", checked);
                        } else {
                            System.setUsbProtection(checked);
                        }
                    }
                }
            }

            // Kicksecure's full set of processor flaw protections, forced on
            // whatever the processor, with every core's second thread off.
            // Off by default for speed; Strict and Maximum keep it on
            // (settings-cpu-threads protect, read by 40_wildos.cfg).
            UI.SettingRow {
                id: cpuRow
                readonly property bool pending: Processor.extraProtectionPending
                minHeight: 64
                title: "Extra processor protection"
                description: Processor.extraProtectionLocked
                    ? "On at Strict and Maximum protection."
                    : cpuRow.pending
                    ? "Restart to finish turning it " + (Processor.extraProtection ? "on." : "off.")
                    : Processor.extraProtection
                    ? "On. Every processor flaw protection is forced on and each core runs one thread, so heavy apps and Windows mode are slower."
                    : "Off for speed: Linux picks protections to suit your processor. On forces every one, even for flaws your processor doesn't have, and turns off the second thread of each core. Needs a restart."
                status: Processor.extraProtection ? "On" : "Off"

                UI.Toggle {
                    id: cpuToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Processor.extraProtection
                    enabled: !Processor.busy && !Processor.extraProtectionLocked
                    locked: Processor.extraProtectionLocked
                    lockedReason: "Strict and Maximum protection keep this on"
                    Accessible.name: "Extra processor protection"
                    onToggled: {
                        cpuError.text = "";
                        Processor.setExtraProtection(checked);
                    }
                }
            }

            UI.SettingRow {
                visible: cpuRow.pending
                title: "Restart to apply"
                description: "Open apps are asked to save first."

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Restart now"
                    onClicked: Updates.restart()
                }
            }
        }

        Connections {
            target: Processor
            // A cancelled password leaves the choice as it was, so the switch goes back to it.
            function onChanged() { cpuToggle.checked = Processor.extraProtection }
            function onFailed(message) { cpuError.text = message }
        }
    }

    UI.Label {
        id: cpuError
        visible: text.length > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        size: 13
        color: UI.Style.warning
    }

    // The kernel's 32-bit layer, which Steam and older games need. A boot
    // option (settings-32bit), so a change offers a restart; Strict and
    // Maximum keep it off.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        visible: Compat32.available

        RowLayout {
            Layout.fillWidth: true

            SectionTitle {
                Layout.fillWidth: true
                text: "Compatibility"
            }

            UI.LinkButton {
                text: "Learn more"
                Accessible.name: "Learn more about Steam and 32-bit apps"
                onClicked: Qt.openUrlExternally("https://help.wildos.io/apps/steam-and-32-bit-apps/")
            }
        }

        UI.Card {
            id: compatCard
            Layout.fillWidth: true
            readonly property bool pending: Compat32.enabled !== Compat32.active

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "games"
                    minHeight: 64
                    title: "Steam and 32-bit apps"
                    description: Compat32.locked
                        ? "Off at Strict and Maximum protection, which keep the 32-bit part of the system out."
                        : compatCard.pending
                        ? "Restart to finish turning it " + (Compat32.enabled ? "on." : "off.")
                        : Compat32.enabled
                        ? "On. Steam, older games and game controllers work, and your protection is a little lower while it is."
                        : "Off for security. Steam and many older games are 32-bit and need this on, and it also turns on Steam's game controller support. Turning it on lowers your protection a little and needs a restart."
                    status: Compat32.enabled && !Compat32.locked ? "On" : "Off"

                    UI.Toggle {
                        id: compatToggle
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Compat32.enabled && !Compat32.locked
                        enabled: !Compat32.busy && !Compat32.locked
                        locked: Compat32.locked
                        lockedReason: "Strict and Maximum protection keep this off"
                        Accessible.name: "Steam and 32-bit apps"
                        onToggled: {
                            compatError.text = "";
                            Compat32.setEnabled(checked);
                        }
                    }
                }

                // MangoHud for Flathub Steam (settings-game-overlay), only
                // while 32-bit apps are on.
                UI.SettingRow {
                    visible: Compat32.enabled && !Compat32.locked
                    title: "Show a performance overlay in games"
                    description: Compat32.overlayBusy
                        ? (Compat32.overlay ? "Turning it off…" : "Downloading the overlay…")
                        : !Compat32.steamInstalled && !Compat32.overlay
                        ? "Frame rate, frame time, and processor and graphics load and temperature. Install Steam from the Store first."
                        : "Frame rate, frame time, and processor and graphics load and temperature, in Vulkan and Proton games. Left Shift+F12 hides it."
                    status: overlayToggle.checked ? "On" : "Off"

                    UI.Toggle {
                        id: overlayToggle
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Compat32.overlay
                        enabled: !Compat32.overlayBusy && (Compat32.steamInstalled || Compat32.overlay)
                        Accessible.name: "Show a performance overlay in games"
                        onToggled: {
                            compatError.text = "";
                            Compat32.setOverlay(checked);
                        }
                    }
                }

                UI.SettingRow {
                    visible: compatCard.pending && !Compat32.locked
                    title: "Restart to apply"
                    description: "Open apps are asked to save first."

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Restart now"
                        onClicked: Updates.restart()
                    }
                }
            }

            Connections {
                target: Compat32
                // A cancelled password leaves the choice as it was, so the switch goes back to it.
                function onChanged() {
                    compatToggle.checked = Compat32.enabled && !Compat32.locked;
                    // Stays where it was flipped while the layer downloads.
                    if (!Compat32.overlayBusy) {
                        overlayToggle.checked = Compat32.overlay;
                    }
                }
                function onFailed(message) { compatError.text = message }
            }
        }

        UI.Label {
            id: compatError
            visible: text.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            size: 13
            color: UI.Style.warning
        }
    }
}
