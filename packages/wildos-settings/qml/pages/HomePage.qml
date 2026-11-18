import QtQuick
import QtQuick.Layouts
import org.kde.plasma.private.battery
import WildOS.Settings
import WildOS.UI as UI

// Home, after Windows 11's: the PC and its name at the top with network and
// update status beside it, then six cards in two equal columns and three
// rows (SettingsHome.dc). The grid fills the height that is left, so Home
// fits in one view at the window's size; the first row is a little taller.
// Every card in a row has the same height. In a window too short for what
// the cards need, the page scrolls as a whole and the cards keep that
// height rather than shrinking or scrolling inside.
PageScroll {
    id: page

    // Network and Bluetooth come from their own desktop modules; if either is
    // missing the card says so instead of the whole page failing to load.
    // They are made once the first frame is up: loading NetworkManager's
    // and BlueZ's modules first held Settings' window back by about 100 ms.
    property QtObject network: null
    property QtObject bluetooth: null
    property bool summariesReady: false

    Connections {
        target: page.Window.window
        enabled: !page.summariesReady
        function onFrameSwapped() {
            Qt.callLater(page.makeSummaries);
        }
    }

    function makeSummaries() {
        if (summariesReady) {
            return;
        }
        network = create("NetworkSummary");
        bluetooth = create("BluetoothSummary");
        summariesReady = true;
    }
    readonly property var volumes: System.volumes()
    readonly property var disk: volumes[0]
    readonly property var backgrounds: System.wallpapers().slice(0, 4)
    property date now: new Date()

    readonly property bool hasBattery: battery.hasInternalBatteries && battery.hasCumulative
    readonly property var winStatus: WindowsMode.status
    readonly property string vm: winStatus.vm || ""
    property string windowsMessage

    readonly property var profileNames: ({
        "power-saver": qsTr("Best battery life"),
        "balanced": qsTr("Balanced"),
        "performance": qsTr("Best performance")
    })

    // The room under the PC strip, less the column's 4px start and the
    // design's 28px foot.
    readonly property real gridRoom: page.height - 4 - hero.implicitHeight - Metrics.sectionGap - 28
    readonly property real rowUnit: {
        const need = Math.max(securityCard.minimumHeight / 1.25, personaliseCard.minimumHeight / 1.25,
                              storageCard.minimumHeight, bluetoothCard.minimumHeight,
                              windowsCard.minimumHeight, powerCard.minimumHeight);
        return Math.floor(Math.max(need, (page.gridRoom - 2 * grid.rowSpacing) / 3.25));
    }

    function create(name) {
        const component = Qt.createComponent("WildOS.Settings", name);
        return component.status === Component.Ready ? component.createObject(page) : null;
    }

    // The smoothed figure needs a few readings first; until then the raw one.
    readonly property real remaining: battery.smoothedRemainingMsec > 0 ? battery.smoothedRemainingMsec : (battery.remainingMsec || 0)

    function duration(msec) {
        const minutes = Math.round(msec / 60000);
        const h = Math.floor(minutes / 60);
        if (h > 0) {
            return h === 1 ? qsTr("about 1 hour") : qsTr("about %1 hours").arg(h);
        }
        return minutes <= 1 ? qsTr("about 1 minute") : qsTr("about %1 minutes").arg(minutes);
    }

    function sizeText(bytes) {
        return System.formatBytes(bytes);
    }

    Component.onCompleted: System.refreshPower()

    BatteryControlModel {
        id: battery
    }

    Timer {
        interval: 30000
        running: true
        repeat: true
        onTriggered: page.now = new Date()
    }

    Connections {
        target: WindowsMode
        function onPowerDone(result) {
            page.windowsMessage = result;
        }
    }

    RowLayout {
        id: hero
        Layout.fillWidth: true
        spacing: 18

        Rectangle {
            Layout.preferredWidth: 120
            Layout.preferredHeight: 72
            radius: UI.Style.radiusSm
            color: UI.Style.surfaceSunken
            border.width: 1
            border.color: UI.Style.borderGlass
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: System.thumbnail(System.wallpaper)
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 240
                asynchronous: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            // The computer name is never cut short; the links beside it give
            // way instead.
            Layout.minimumWidth: hostLabel.implicitWidth
            spacing: 3

            UI.Label {
                id: hostLabel
                Layout.fillWidth: true
                text: System.hostName
                size: 20
                font.weight: UI.Style.weightSemiBold
            }

            UI.Label {
                Layout.fillWidth: true
                text: {
                    const os = System.osName + (System.osVersion.length > 0 ? " " + System.osVersion : "");
                    const memory = System.memory.replace(" usable", "");
                    return memory.length > 0 ? qsTr("%1 · %2 memory").arg(os).arg(memory) : os;
                }
                color: UI.Style.inkMuted
            }

            UI.LinkButton {
                Layout.leftMargin: -4
                Layout.topMargin: -3
                Layout.bottomMargin: -3
                text: qsTr("Rename")
                onClicked: Navigator.open("about")
            }
        }

        HomeLink {
            Layout.fillWidth: true
            Layout.maximumWidth: 210
            Layout.preferredWidth: 210
            iconName: page.network ? page.network.iconName : "globe"
            title: page.network ? page.network.title : qsTr("Network and internet")
            caption: page.network ? page.network.caption : page.summariesReady ? qsTr("Not available") : ""
            onClicked: Navigator.open("network")
        }

        HomeLink {
            Layout.fillWidth: true
            Layout.maximumWidth: 210
            Layout.preferredWidth: 210
            iconName: "update"
            iconColor: Updates.updates.length > 0 ? UI.Style.accent : UI.Style.iconInk
            title: Updates.updates.length > 0
                ? (Updates.updates.length === 1 ? qsTr("1 update ready") : qsTr("%1 updates ready").arg(Updates.updates.length))
                : qsTr("Up to date")
            caption: {
                page.now;
                const when = Format.ago(Updates.lastChecked);
                return when.length > 0 ? qsTr("Last checked %1").arg(when) : qsTr("Not checked yet");
            }
            onClicked: Navigator.open("update")
        }
    }

    GridLayout {
        id: grid
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 12

        HomeCard {
            id: personaliseCard
            Layout.fillHeight: false
            Layout.preferredHeight: Math.round(page.rowUnit * 1.25)
            iconName: "palette"
            title: qsTr("Personalise")
            caption: qsTr("Background and accent colour")
            linkText: qsTr("More backgrounds")
            onLinkClicked: Navigator.open("background")

            footer: ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 16
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: page.backgrounds

                        WallpaperThumb {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            source: modelData.thumbnail
                            selected: modelData.path === System.wallpaper
                            showTick: false
                            Accessible.name: qsTr("Use %1 as background").arg(modelData.name)
                            onClicked: System.setWallpaper(modelData.path)
                        }
                    }
                }

                AccentPicker {
                    Layout.leftMargin: 4
                    size: 24
                }
            }
        }

        SecurityCard {
            id: securityCard
            Layout.fillHeight: false
            Layout.preferredHeight: Math.round(page.rowUnit * 1.25)
        }

        HomeCard {
            id: storageCard
            readonly property var others: page.volumes.slice(1)
            Layout.fillHeight: false
            Layout.preferredHeight: page.rowUnit
            iconName: "drive"
            title: qsTr("Storage")
            caption: {
                if (!page.disk) {
                    return "";
                }
                const free = page.volumes.reduce((sum, v) => sum + v.free, 0);
                return page.volumes.length === 1
                    ? qsTr("%1 free of %2").arg(page.sizeText(page.disk.free)).arg(page.sizeText(page.disk.total))
                    : qsTr("%1 drives · %2 free in all").arg(page.volumes.length).arg(page.sizeText(free));
            }
            linkText: qsTr("Manage")
            onLinkClicked: Navigator.open("storage")

            ColumnLayout {
                visible: page.disk !== undefined
                Layout.fillWidth: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UI.Style.divider
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.topMargin: 9
                    spacing: 12

                    UI.Label {
                        text: qsTr("System disk")
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: -8
                        text: page.disk ? "· " + page.sizeText(page.disk.total) : ""
                        color: UI.Style.inkSubtle
                    }

                    UI.Label {
                        text: page.disk ? qsTr("%1 free").arg(page.sizeText(page.disk.free)) : ""
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                }

                UsageBar {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.topMargin: 6
                    Layout.bottomMargin: 9
                    value: page.disk ? 1 - page.disk.free / page.disk.total : 0
                    fill: value > 0.9 ? UI.Style.warning : UI.Style.accent
                }
            }

            footer: CardRow {
                visible: storageCard.others.length > 0
                link: true
                title: storageCard.others.length === 0 ? ""
                    : storageCard.others.length === 1 ? storageCard.others[0].name
                    : qsTr("%1 and %2 more").arg(storageCard.others[0].name).arg(storageCard.others.length - 1)
                onClicked: Navigator.open("storage")
            }
        }

        HomeCard {
            id: bluetoothCard
            readonly property var bt: page.bluetooth
            readonly property bool ready: bt !== null && bt.present && bt.on
            readonly property var devices: ready ? bt.devices : []
            Layout.fillHeight: false
            Layout.preferredHeight: page.rowUnit
            iconName: "bluetooth"
            title: "Bluetooth"
            caption: bt ? bt.caption : page.summariesReady ? qsTr("Bluetooth is not available") : ""

            trailing: UI.Toggle {
                visible: bluetoothCard.bt !== null && bluetoothCard.bt.present
                checked: bluetoothCard.bt !== null && bluetoothCard.bt.on
                Accessible.name: "Bluetooth"
                onToggled: bluetoothCard.bt.setOn(checked)
            }

            Repeater {
                model: bluetoothCard.devices.slice(0, 2)

                CardRow {
                    required property var modelData
                    iconName: bluetoothCard.bt.iconFor(modelData.type)
                    iconColor: UI.Style.iconInk
                    title: modelData.name
                    value: bluetoothCard.bt.stateOf(modelData)
                    onClicked: Navigator.open("bluetooth")
                }
            }

            // Nothing to list: one line that says so and what to do.
            CardRow {
                visible: page.summariesReady && (bluetoothCard.bt === null || !bluetoothCard.ready || bluetoothCard.devices.length === 0)
                chevron: false
                enabled: bluetoothCard.ready
                title: bluetoothCard.bt === null || !bluetoothCard.bt.present ? qsTr("This computer has no Bluetooth")
                    : !bluetoothCard.bt.on ? qsTr("Bluetooth is off. Turn it on to see your devices.")
                    : qsTr("No devices yet.")
                titleColor: UI.Style.inkMuted
                value: bluetoothCard.ready ? qsTr("Add a device") : ""
                valueColor: UI.Style.accentText
                onClicked: bluetoothCard.bt.addDevice()
            }

            footer: CardRow {
                visible: bluetoothCard.devices.length > 2
                link: true
                title: bluetoothCard.devices.length - 2 === 1 ? qsTr("1 more device") : qsTr("%1 more devices").arg(bluetoothCard.devices.length - 2)
                onClicked: Navigator.open("bluetooth")
            }
        }

        HomeCard {
            id: windowsCard
            readonly property bool ready: WindowsMode.state === "ready"
            readonly property var lastApp: page.winStatus.lastApp || null
            readonly property bool live: page.vm === "running" || page.vm === "paused"
            Layout.fillHeight: false
            Layout.preferredHeight: page.rowUnit
            iconName: "monitor"
            title: qsTr("Windows mode")
            caption: {
                if (!WindowsMode.installed) {
                    return qsTr("Not installed");
                }
                if (!ready) {
                    return WindowsMode.state === "setup" ? qsTr("Setting up, %1% done").arg(page.winStatus.percent || 0)
                        : WindowsMode.state === "failed" ? qsTr("Couldn't finish setting up") : qsTr("Not set up");
                }
                if (page.windowsMessage.length > 0) {
                    return page.windowsMessage;
                }
                switch (WindowsMode.powerAction) {
                case "save": return qsTr("Saving to disk");
                case "shutdown": return qsTr("Shutting down");
                }
                switch (page.vm) {
                case "running": {
                    const open = (page.winStatus.apps || []).filter(a => a.open).length;
                    return open === 1 ? qsTr("Running · 1 app open") : open > 0 ? qsTr("Running · %1 apps open").arg(open) : qsTr("Running");
                }
                case "paused": return qsTr("Paused · opens in about a second");
                case "saved": return qsTr("Saved to disk · uses no memory");
                default: return qsTr("Off · uses no memory");
                }
            }
            linkText: qsTr("Settings")
            onLinkClicked: Navigator.open("windowsmode")

            CardRow {
                visible: windowsCard.ready
                iconName: "app-window"
                iconColor: UI.Style.iconInk
                title: qsTr("Last used")
                value: {
                    const app = windowsCard.lastApp;
                    if (!app) {
                        return qsTr("Nothing yet");
                    }
                    const launch = page.winStatus.lastLaunch;
                    if (launch && launch.app === app.id && launch.at) {
                        const at = new Date(launch.at);
                        return qsTr("%1, %2 %3").arg(app.name).arg(Format.dayText(at)).arg(Format.time(at));
                    }
                    return app.name;
                }
                onClicked: Navigator.open("windowsapps")
            }

            // Not set up or not installed: one line saying so and what to do.
            CardRow {
                visible: !windowsCard.ready
                chevron: false
                title: WindowsMode.installed ? qsTr("Run Windows apps in their own windows.") : qsTr("Get Windows mode from the Store.")
                titleColor: UI.Style.inkMuted
                value: !WindowsMode.installed ? (System.storeApp.length > 0 ? qsTr("Open the Store") : "")
                    : WindowsMode.state === "setup" ? qsTr("Show progress")
                    : WindowsMode.state === "failed" ? qsTr("Try again") : qsTr("Set up")
                valueColor: UI.Style.accentText
                onClicked: WindowsMode.installed ? WindowsMode.openSetup() : Qt.openUrlExternally("appstream://org.wildos.winmode")
            }

            footer: RowLayout {
                visible: windowsCard.ready
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 16
                spacing: 8

                UI.Button {
                    implicitHeight: 32
                    text: qsTr("Save to disk")
                    enabled: windowsCard.live && WindowsMode.powerAction.length === 0
                    busy: WindowsMode.powerAction === "save"
                    onClicked: {
                        page.windowsMessage = "";
                        WindowsMode.power("save");
                    }
                }

                UI.Button {
                    implicitHeight: 32
                    text: qsTr("Shut down")
                    enabled: (windowsCard.live || page.vm === "saved") && WindowsMode.powerAction.length === 0
                    busy: WindowsMode.powerAction === "shutdown"
                    onClicked: {
                        page.windowsMessage = "";
                        WindowsMode.power("shutdown");
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        // Battery on a laptop; Power on a desktop, with the same power mode.
        HomeCard {
            id: powerCard
            readonly property int lowLevel: System.config("powerdevilrc", "BatteryManagement", "BatteryLowLevel", 10)
            // powerdevil's low battery profile switches to power-saver only
            // when set to; its own default leaves the power mode alone.
            readonly property bool saverOn: System.config("powerdevilrc", "LowBattery/Performance", "PowerProfile", "") === "power-saver"
            Layout.fillHeight: false
            Layout.preferredHeight: page.rowUnit
            iconName: page.hasBattery ? (battery.pluggedIn ? "plug" : "battery") : "power"
            title: page.hasBattery ? qsTr("Battery") : qsTr("Power")
            caption: {
                if (!page.hasBattery) {
                    return qsTr("Plugged in");
                }
                const pct = battery.percent + "%";
                if (battery.state === BatteryControlModel.FullyCharged && battery.pluggedIn) {
                    return qsTr("%1 · fully charged").arg(pct);
                }
                if (battery.state === BatteryControlModel.Charging) {
                    return page.remaining > 0 ? qsTr("%1 · %2 until full").arg(pct).arg(page.duration(page.remaining)) : qsTr("%1 · charging").arg(pct);
                }
                if (battery.pluggedIn) {
                    return qsTr("%1 · plugged in").arg(pct);
                }
                return page.remaining > 0 ? qsTr("%1 · %2 left").arg(pct).arg(page.duration(page.remaining)) : pct;
            }
            linkText: qsTr("Power options")
            onLinkClicked: Navigator.open("power")

            UsageBar {
                visible: page.hasBattery
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 10
                value: battery.percent / 100
                fill: !battery.pluggedIn && battery.percent <= 10 ? UI.Style.warning : UI.Style.success
            }

            CardRow {
                visible: page.hasBattery
                iconName: "leaf"
                iconColor: UI.Style.iconInk
                title: qsTr("Battery saver")
                value: powerCard.saverOn ? qsTr("Turns on at %1%").arg(powerCard.lowLevel) : qsTr("Off")
                onClicked: Navigator.open("power")
            }

            CardRow {
                visible: !page.hasBattery
                iconName: "moon"
                iconColor: UI.Style.iconInk
                title: qsTr("Sleep")
                value: {
                    if (System.config("powerdevilrc", "AC/SuspendAndShutdown", "AutoSuspendAction", 1) === 0) {
                        return qsTr("Never");
                    }
                    const minutes = Math.round(System.config("powerdevilrc", "AC/SuspendAndShutdown", "AutoSuspendIdleTimeoutSec", 900) / 60);
                    if (minutes >= 60 && minutes % 60 === 0) {
                        return minutes === 60 ? qsTr("After 1 hour") : qsTr("After %1 hours").arg(minutes / 60);
                    }
                    return minutes === 1 ? qsTr("After 1 minute") : qsTr("After %1 minutes").arg(minutes);
                }
                onClicked: Navigator.open("power")
            }

            footer: RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 14
                spacing: 8

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Power mode")
                }

                UI.ComboBox {
                    visible: System.powerProfiles.length > 0
                    Accessible.name: qsTr("Power mode")
                    model: System.powerProfiles.map(p => ({ value: p, text: page.profileNames[p] || p }))
                    readonly property var choice: System.powerProfile
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => System.setPowerProfile(model[index].value)
                }

                UI.Label {
                    visible: System.powerProfiles.length === 0
                    text: qsTr("Not available here")
                    size: 12.5
                    color: UI.Style.inkMuted
                }
            }
        }
    }
}
