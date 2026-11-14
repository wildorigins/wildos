pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.ksvg as KSvg
import WildOS.UI as UI

// The quick settings flyout. Plasma's theme draws the glass, so this item
// stays transparent and only paints what sits on it. Sizes come from the
// design's outer edge, with the theme's frame margins taken off.
FocusScope {
    id: flyout

    required property var quick

    // "" is the tiles and sliders; "wifi", "bluetooth" and "vpn" are the
    // detail pages a tile's chevron opens in place.
    property string page: ""
    property bool editing: false

    readonly property var network: quick.network
    readonly property var audio: quick.audio
    readonly property var bluetooth: quick.bluetooth
    readonly property var power: quick.power
    readonly property var dnd: quick.dnd

    KSvg.FrameSvgItem {
        id: frame
        imagePath: "dialogs/background"
        visible: false
    }

    readonly property real edgeLeft: frame.margins.left
    readonly property real edgeRight: frame.margins.right
    readonly property real edgeTop: frame.margins.top
    readonly property real edgeBottom: frame.margins.bottom

    readonly property real innerWidth: Metrics.flyoutWidth - 2 * (Metrics.glassBorder + Metrics.pad)
    readonly property real pageHeight: 344
    readonly property bool hasVolume: audio !== null && audio.available
    readonly property bool hasBrightness: power !== null && power.brightnessAvailable
    readonly property real bodyHeight: page === "" ? mainBody.implicitHeight : pageHeight
    readonly property real glassHeight: 2 * Metrics.glassBorder + Metrics.pad + bodyHeight + Metrics.sectionGap + Metrics.footerHeight

    readonly property real contentWidth: Metrics.flyoutWidth - edgeLeft - edgeRight
    readonly property real contentHeight: glassHeight - edgeTop - edgeBottom

    Layout.minimumWidth: contentWidth
    Layout.preferredWidth: contentWidth
    Layout.maximumWidth: contentWidth
    Layout.minimumHeight: contentHeight
    Layout.preferredHeight: contentHeight
    Layout.maximumHeight: contentHeight

    focus: true

    // Tiles for hardware the computer does not have are left out, as Windows
    // does, rather than shown greyed out (EmptyStates.dc): on a desktop PC
    // Ethernet moves up to the first slot. Cast and Accessibility fill the
    // free slots of a six-tile grid, or stay once someone adds them.
    readonly property var allTiles: ["wifi", "ethernet", "bluetooth", "flight", "dnd", "nightlight", "vpn", "cast", "accessibility", "awake"]
    readonly property var fillers: ["cast", "accessibility", "awake"]
    readonly property var presentTiles: allTiles.filter(id => tileApplies(id))
    readonly property var visibleTiles: {
        const core = presentTiles.filter(id => fillers.indexOf(id) < 0 && quick.hiddenTiles.indexOf(id) < 0);
        const extra = presentTiles.filter(id => fillers.indexOf(id) >= 0 && quick.hiddenTiles.indexOf(id) < 0);
        const chosen = extra.filter(id => quick.addedTiles.indexOf(id) >= 0);
        const free = extra.filter(id => chosen.indexOf(id) < 0).slice(0, Math.max(0, 6 - core.length - chosen.length));
        return presentTiles.filter(id => core.indexOf(id) >= 0 || chosen.indexOf(id) >= 0 || free.indexOf(id) >= 0);
    }
    readonly property var shownTiles: editing ? presentTiles : visibleTiles

    function tileApplies(id: string): bool {
        switch (id) {
        case "wifi":
            return network !== null && network.wifiAvailable;
        case "ethernet":
            return network !== null && (network.wiredAvailable || network.wiredConnected);
        case "bluetooth":
            return bluetooth !== null && bluetooth.available;
        case "flight":
            return network !== null && network.flightAvailable;
        case "dnd":
            return dnd !== null;
        case "nightlight":
            return power !== null && power.nightLightAvailable;
        case "vpn":
            return network !== null;
        case "cast":
            return true;
        case "accessibility":
            return quick.access !== null;
        case "awake":
            return power !== null;
        }
        return false;
    }

    function tileInfo(id: string): var {
        const net = network;
        switch (id) {
        case "wifi": {
            const status = !net.wifiEnabled ? "Off"
                : net.wifiConnected ? quick.connectionWords : "Not connected";
            return {
                label: net.wifiConnected ? net.activeWifiName : "Wi-Fi",
                status: status,
                icon: Icons.wifi,
                fill: Icons.wifiDot,
                checked: net.wifiEnabled,
                hasPage: true,
                pageName: "Show Wi-Fi networks",
                description: (net.wifiConnected ? "Wi-Fi, " + net.activeWifiName + ", " : "Wi-Fi, ") + status
            };
        }
        case "ethernet": {
            const status = net.wiredConnected ? quick.connectionWords : "Not connected";
            return {
                label: "Ethernet",
                status: status,
                icon: Icons.ethernetPort,
                fill: "",
                checked: net.wiredConnected,
                switchable: false,
                hasPage: false,
                pageName: "",
                description: "Ethernet, " + status + ". Opens network settings"
            };
        }
        case "bluetooth": {
            const device = bluetooth.connectedDevices.length > 0 ? bluetooth.connectedDevices[0].name : "";
            return {
                label: "Bluetooth",
                status: !bluetooth.enabled ? "Off" : device.length > 0 ? device : "Not connected",
                icon: Icons.bluetooth,
                fill: "",
                checked: bluetooth.enabled,
                hasPage: true,
                pageName: "Show Bluetooth devices",
                description: "Bluetooth, " + (!bluetooth.enabled ? "off" : device.length > 0 ? "connected to " + device : "on")
            };
        }
        case "flight":
            return {
                label: "Flight mode",
                status: "",
                icon: Icons.flight,
                fill: "",
                checked: net.flightMode,
                hasPage: false,
                pageName: "",
                description: net.flightMode ? "Wireless connections are off" : "Off"
            };
        case "dnd":
            return {
                label: "Do not disturb",
                status: "",
                icon: Icons.moon,
                fill: "",
                checked: dnd.active,
                hasPage: false,
                pageName: "",
                description: dnd.active ? "Notifications are kept quiet" : "Off"
            };
        case "nightlight":
            return {
                label: "Night light",
                status: "",
                icon: Icons.sun,
                fill: "",
                checked: quick.nightLightOn,
                hasPage: false,
                pageName: "",
                description: quick.nightLightOn ? "Warmer colours on" : "Off"
            };
        case "vpn": {
            const status = net.vpnActive ? "Connected" : net.vpnConfigured ? "Not connected" : "Not set up";
            return {
                label: "VPN",
                status: status,
                icon: Icons.vpn,
                fill: "",
                checked: net.vpnActive,
                hasPage: true,
                pageName: "Show VPN connections",
                description: net.vpnActive ? "VPN, connected to " + net.activeVpnName : "VPN, " + status.toLowerCase()
            };
        }
        // Wild OS has no wireless display (Miracast) support yet, so Cast
        // opens the display settings, where a second screen plugged in by
        // cable is set up, rather than pretending to search.
        case "cast":
            return {
                label: "Cast",
                status: "",
                icon: Icons.cast,
                fill: "",
                checked: false,
                switchable: false,
                hasPage: false,
                pageName: "",
                description: "Opens display settings to set up another screen"
            };
        case "accessibility":
            return {
                label: "Accessibility",
                status: "",
                icon: Icons.accessibility,
                fill: "",
                checked: quick.access.anyOn,
                switchable: false,
                hasPage: false,
                pageName: "",
                description: quick.access.anyOn ? "Some accessibility features are on. Opens the list" : "Opens magnifier and colour settings"
            };
        case "awake": {
            const until = quick.keepAwakeUntil > 0
                ? "Until " + Qt.locale().toString(new Date(quick.keepAwakeUntil), UI.TimeFormat.shortFormat) : "";
            return {
                label: "Keep awake",
                status: quick.keepingAwake ? until : "",
                icon: Icons.coffee,
                fill: "",
                checked: quick.keepingAwake,
                hasPage: false,
                pageName: "",
                description: !quick.keepingAwake ? "Off"
                    : until.length > 0 ? "The screen stays on and the computer won't sleep " + until.toLowerCase()
                    : "The screen stays on and the computer won't sleep until you turn this off"
            };
        }
        }
        return {};
    }

    function toggle(id: string): void {
        const net = network;
        switch (id) {
        case "wifi":
            net.setWifiEnabled(!net.wifiEnabled);
            break;
        case "ethernet":
            quick.openSettings("network");
            break;
        case "bluetooth":
            bluetooth.setEnabled(!bluetooth.enabled);
            break;
        case "flight":
            net.setFlightMode(!net.flightMode);
            break;
        case "dnd":
            dnd.setActive(!dnd.active);
            break;
        case "nightlight":
            quick.setNightLight(!quick.nightLightOn);
            break;
        case "vpn":
            if (!net.toggleVpn()) {
                showPage("vpn");
            }
            break;
        case "cast":
            quick.openSettings("display");
            break;
        case "accessibility":
            showPage("accessibility");
            break;
        case "awake":
            quick.keepAwake(quick.keepingAwake ? -1 : 0);
            break;
        }
    }

    // The settings page each tile's menu offers; a tile with no page of its
    // own in Settings has no settings row.
    readonly property var tileSettings: ({
        "wifi": ["Wi-Fi settings", "network"],
        "ethernet": ["Network settings", "network"],
        "flight": ["Network settings", "network"],
        "vpn": ["Network settings", "network"],
        "bluetooth": ["Bluetooth settings", "bluetooth"],
        "dnd": ["Notification settings", "notifications"],
        "nightlight": ["Night light settings", "nightlight"],
        "cast": ["Display settings", "display"]
    })

    function openTileMenu(id: string, item: Item, x: real, y: real): void {
        const info = tileInfo(id);
        const entries = [];
        if (info.switchable !== false) {
            entries.push({ text: info.checked ? "Turn off" : "Turn on", icon: "menu-power", run: () => flyout.toggle(id) });
        }
        if (id === "awake") {
            entries.push({ text: "For 1 hour", icon: "clock", run: () => flyout.quick.keepAwake(60) });
            entries.push({ text: "For 4 hours", icon: "clock", run: () => flyout.quick.keepAwake(240) });
            entries.push({ text: "Until I turn it off", icon: "clock", run: () => flyout.quick.keepAwake(0) });
            if (info.checked) {
                entries.push({ text: "Turn off", icon: "menu-power", run: () => flyout.quick.keepAwake(-1) });
            }
            entries.push({ separator: true });
            entries.push({ text: "Power settings", icon: "menu-settings", run: () => flyout.quick.openSettings("power") });
            entries.push({ separator: true });
            entries.push({ text: "Edit quick settings", icon: "menu-apps", run: () => flyout.editing = true });
            tileMenu.entries = entries;
            tileMenu.popup(item, x, y);
            return;
        }
        const settings = tileSettings[id];
        if (settings) {
            entries.push({ text: settings[0], icon: "menu-settings", run: () => flyout.quick.openSettings(settings[1]) });
        }
        if (entries.length > 0) {
            entries.push({ separator: true });
        }
        entries.push({ text: "Edit quick settings", icon: "menu-apps", run: () => flyout.editing = true });
        tileMenu.entries = entries;
        tileMenu.popup(item, x, y);
    }

    UI.Menu {
        id: tileMenu
        minimumWidth: 280
    }

    // Windows' names for the three power-profiles-daemon modes.
    function modeLabel(mode: string): string {
        return mode === "power-saver" ? "Best power efficiency"
            : mode === "performance" ? "Best performance"
            : mode === "balanced" ? "Balanced" : mode;
    }

    function showPage(name: string): void {
        page = name;
        editing = false;
        if (Nav.keyboard) {
            Qt.callLater(() => pageLoader.item && pageLoader.item.focusFirst());
        }
    }

    function showMain(): void {
        const from = page;
        page = "";
        if (Nav.keyboard) {
            const index = shownTiles.indexOf(from);
            const tile = index >= 0 ? tiles.itemAt(index) : null;
            if (tile) {
                (tile.hasPage ? tile.pageButton : tile.toggleButton).forceActiveFocus(Qt.BacktabFocusReason);
            }
        }
    }

    function reset(): void {
        page = "";
        editing = false;
        Nav.keyboard = false;
        flyout.forceActiveFocus();
    }

    Connections {
        target: flyout.quick
        function onExpandedChanged() {
            if (flyout.quick.expanded) {
                flyout.reset();
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            if (page !== "") {
                showMain();
            } else if (editing) {
                editing = false;
            } else {
                quick.expanded = false;
            }
            event.accepted = true;
            return;
        }
        if ([Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
            Nav.keyboard = true;
        }
        event.accepted = false;
    }

    HoverHandler {
        onPointChanged: Nav.keyboard = false
    }

    Item {
        id: body
        x: Metrics.glassBorder + Metrics.pad - flyout.edgeLeft
        y: Metrics.glassBorder + Metrics.pad - flyout.edgeTop
        width: flyout.innerWidth
        height: flyout.bodyHeight

        // Laid out by hand: the sliders are 44 px tall for touch, but the design
        // spaces them by their 20 px visible height, so each row sits 12 px
        // above and below where the design draws it.
        Item {
            id: mainBody
            visible: flyout.page === ""
            width: parent.width
            // Back from a detail page, the tiles slide in from the left, as
            // the page slid in from the right.
            transform: UI.Reveal {
                item: mainBody
                dx: -UI.Style.travelMedium
            }

            // EmptyStates.dc: the 18 px icon rows sit 14 px apart, 18 px
            // under the tiles. A row for hardware that is not there (no
            // sound device, no built-in screen) is left out entirely.
            readonly property real gridHeight: grid.visible ? grid.implicitHeight : 0
            readonly property real slidersTop: (grid.visible ? gridHeight + Metrics.sectionGap : 0) - 13
            implicitHeight: volumeRow.visible ? volumeRow.y + 44 - 13
                : brightnessRow.visible ? brightnessRow.y + 44 - 13
                : gridHeight

            GridLayout {
                id: grid
                width: parent.width
                visible: tiles.count > 0
                columns: 3
                columnSpacing: Metrics.tileGap
                rowSpacing: Metrics.tileGap

                Repeater {
                    id: tiles
                    model: flyout.shownTiles

                    Tile {
                        id: tile
                        required property string modelData
                        readonly property var info: flyout.tileInfo(modelData)

                        // Equal thirds whatever the label lengths.
                        Layout.preferredWidth: (grid.width - 2 * Metrics.tileGap) / 3
                        Layout.maximumWidth: Layout.preferredWidth
                        // A row is as tall as its tallest tile, so a wrapped name lines up.
                        Layout.fillHeight: true
                        label: info.label
                        status: info.status
                        iconPath: info.icon
                        iconFill: info.fill
                        checked: info.checked
                        switchable: info.switchable !== false
                        hasPage: info.hasPage
                        pageName: info.pageName
                        description: info.description
                        editing: flyout.editing
                        shown: flyout.visibleTiles.indexOf(modelData) >= 0
                        onToggled: flyout.toggle(modelData)
                        onPageRequested: flyout.showPage(modelData)
                        onEditToggled: flyout.quick.setTileShown(modelData, !shown)
                        onMenuRequested: (x, y) => flyout.openTileMenu(modelData, tile, x, y)
                    }
                }
            }

            Item {
                id: brightnessRow
                visible: flyout.hasBrightness
                y: mainBody.slidersTop
                width: parent.width
                height: 44

                UI.Glyph {
                    x: 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    path: Icons.sun
                    strokeWidth: 2
                }

                UI.Slider {
                    id: brightnessSlider
                    anchors.left: parent.left
                    anchors.leftMargin: 32
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 44
                    flat: true
                    accessibleName: "Brightness"
                    from: 1
                    value: flyout.power ? flyout.power.brightnessPercent : 0
                    onMoved: flyout.power.setBrightnessPercent(value)
                }
            }

            Item {
                id: volumeRow
                visible: flyout.hasVolume
                y: brightnessRow.visible ? brightnessRow.y + 18 + 14 : mainBody.slidersTop
                width: parent.width
                height: 44

                UI.IconButton {
                    // Centred on the design's 18 px icon at the left edge.
                    x: 9 - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    tip: flyout.hasVolume && flyout.audio.muted ? "Unmute" : "Mute"

                    VolumeGlyph {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        oneWave: true
                        strokeWidth: 2
                        percent: flyout.audio ? flyout.audio.percent : 0
                        muted: flyout.hasVolume && flyout.audio.muted
                    }

                    onClicked: flyout.audio.toggleMute()
                }

                UI.Slider {
                    anchors.left: parent.left
                    anchors.leftMargin: 32
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 44
                    flat: true
                    accessibleName: "Volume"
                    dimmed: flyout.hasVolume && flyout.audio.muted
                    value: flyout.hasVolume ? flyout.audio.percent : 0
                    onMoved: flyout.audio.setPercent(value)
                }
            }
        }

        Loader {
            id: pageLoader
            anchors.fill: parent
            active: flyout.page !== ""
            transform: UI.Reveal {
                item: pageLoader
                key: flyout.page
                dx: UI.Style.travelMedium
            }
            sourceComponent: flyout.page === "wifi" ? wifiPage
                : flyout.page === "bluetooth" ? bluetoothPage
                : flyout.page === "vpn" ? vpnPage
                : flyout.page === "accessibility" ? accessibilityPage : null
        }
    }

    Component {
        id: wifiPage
        WifiPage {
            quick: flyout.quick
            onBack: flyout.showMain()
        }
    }

    Component {
        id: bluetoothPage
        BluetoothPage {
            quick: flyout.quick
            onBack: flyout.showMain()
        }
    }

    Component {
        id: accessibilityPage
        AccessibilityPage {
            quick: flyout.quick
            onBack: flyout.showMain()
        }
    }

    Component {
        id: vpnPage
        VpnPage {
            quick: flyout.quick
            onBack: flyout.showMain()
        }
    }

    // The darker battery band along the foot, edge to edge inside the glass
    // border, with its lower corners following the frame's radius.
    Rectangle {
        id: band
        anchors.left: parent.left
        anchors.leftMargin: Metrics.glassBorder - flyout.edgeLeft
        anchors.right: parent.right
        anchors.rightMargin: Metrics.glassBorder - flyout.edgeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.glassBorder - flyout.edgeBottom
        height: Metrics.footerHeight
        color: UI.Style.surfaceBand
        bottomLeftRadius: Metrics.frameRadius - Metrics.glassBorder
        bottomRightRadius: Metrics.frameRadius - Metrics.glassBorder

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: UI.Style.tone(0.08)
        }

        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.right: buttons.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            visible: !flyout.editing && flyout.power !== null && flyout.power.hasBattery

            BatteryGlyph {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 18
                percent: flyout.power ? flyout.power.percent : 0
                charging: flyout.power ? flyout.power.pluggedIn : false
                low: flyout.quick.batteryLow
            }

            UI.Label {
                text: (flyout.power ? flyout.power.percent : 0) + "%"
                font.weight: UI.Style.weightMedium
                Accessible.name: "Battery " + text
            }

            UI.Label {
                Layout.fillWidth: true
                text: flyout.quick.batteryDetail
                size: 12
                color: UI.Style.inkMuted
            }
        }

        // No battery: the row becomes Power mode (EmptyStates.dc), the
        // power-profiles-daemon setting Windows calls the same. Choosing it
        // lists the modes this computer offers.
        T.AbstractButton {
            id: powerModeButton
            visible: !flyout.editing && flyout.power !== null && !flyout.power.hasBattery && flyout.power.powerModesAvailable
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(modeRow.implicitWidth + 16, buttons.x - 12 - x)
            height: 40
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: "Power mode, " + modeRow.modeName
            onClicked: powerMenu.popup(powerModeButton, 0, -powerMenu.height - 6)

            background: Rectangle {
                radius: UI.Style.radiusSm
                color: powerModeButton.down ? UI.Style.surfacePressed : powerModeButton.hovered ? UI.Style.surfaceHover : "transparent"
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                UI.FocusRing {
                    visible: powerModeButton.visualFocus || (powerModeButton.activeFocus && Nav.keyboard)
                    targetRadius: UI.Style.radiusSm
                }
            }

            contentItem: Item {
                RowLayout {
                    id: modeRow
                    readonly property string modeName: flyout.modeLabel(flyout.power ? flyout.power.powerMode : "")
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    UI.Glyph {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        path: Icons.bolt
                        strokeWidth: 1.9
                    }

                    UI.Label {
                        text: "Power mode"
                        font.weight: UI.Style.weightMedium
                    }

                    UI.Label {
                        text: modeRow.modeName
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }
            }

            UI.Menu {
                id: powerMenu
                entries: (flyout.power ? flyout.power.powerModes : []).map(mode => ({
                    text: flyout.modeLabel(mode),
                    checked: flyout.power.powerMode === mode,
                    run: () => flyout.power.setPowerMode(mode)
                }))
            }
        }

        UI.Label {
            visible: flyout.editing
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.right: buttons.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Choose the tiles to show"
            size: 12
            color: UI.Style.inkMuted
        }

        Row {
            id: buttons
            anchors.right: parent.right
            // The design's 40 px buttons, 12 px from the edge and 10 px apart.
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            UI.IconButton {
                id: editButton
                visible: flyout.page === ""
                width: 40
                height: 40
                radius: 10
                iconName: flyout.editing ? "check" : "pencil"
                iconSize: 17
                stroke: flyout.editing ? 2 : 1.8
                checked: flyout.editing
                tip: flyout.editing ? "Done editing quick settings" : "Edit quick settings"
                onClicked: flyout.editing = !flyout.editing
            }

            UI.IconButton {
                width: 40
                height: 40
                radius: 10
                iconName: "cog"
                iconSize: 17
                stroke: 1.6
                tip: "Open Settings"
                onClicked: flyout.quick.openSettings("")
            }
        }
    }
}
