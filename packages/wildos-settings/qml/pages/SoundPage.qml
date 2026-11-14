import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.bluezqt as BluezQt
import org.kde.plasma.private.volume
import WildOS.Settings
import WildOS.UI as UI

// Sound, after Windows 11 (SettingsSound.dc): where sound plays and how
// loud, the microphone with a live level, then every app playing sound with
// its own volume and output. Everything goes through plasma-pa, the same
// service the taskbar's volume uses, so both always agree.
//
// The page is built again on every visit and mixer rows come and go with
// the apps, so all of it is bindings to plasma-pa's objects: nothing is set
// up once in a handler and then lost when an item is recreated.
PageScroll {
    id: page

    function percent(volume) {
        return Math.round(volume / PulseAudio.NormalVolume * 100);
    }

    function toVolume(value) {
        return Math.round(value / 100 * PulseAudio.NormalVolume);
    }

    function props(object) {
        return object && object.properties ? object.properties : {};
    }

    function activePort(device) {
        if (!device || !device.ports) {
            return null;
        }
        const i = device.activePortIndex;
        return i >= 0 && i < device.ports.length ? device.ports[i] : null;
    }

    function isBluetooth(device) {
        const p = props(device);
        return p["device.bus"] === "bluetooth" || p["device.api"] === "bluez5";
    }

    // The battery of a Bluetooth output, from BlueZ's Battery1 through
    // BluezQt; -1 when the device does not report one. Reading the device
    // list and its battery here keeps the card's text bound to both.
    function batteryOf(device) {
        const p = props(device);
        const address = String(p["api.bluez5.address"] || p["device.string"] || "").toUpperCase();
        if (address.length === 0) {
            return -1;
        }
        const devices = BluezQt.Manager.devices;
        for (let i = 0; i < devices.length; ++i) {
            const d = devices[i];
            if (d && d.address.toUpperCase() === address) {
                return d.battery ? d.battery.percentage : -1;
            }
        }
        return -1;
    }

    // What a Bluetooth or USB device is, from the form factor it reports.
    function kindOf(device) {
        switch (device.formFactor) {
        case "headphone":
            return "Headphones";
        case "headset":
        case "hands-free":
            return "Headset";
        case "speaker":
        case "portable":
            return "Speaker";
        case "car":
            return "Car";
        default:
            return "";
        }
    }

    // A card's words as the design has them: what it is on top ("Speakers",
    // "Headphones", "External display"), how it is connected underneath.
    function outputInfo(sink) {
        const p = props(sink);
        const port = activePort(sink);
        const portName = port ? port.name : "";
        const description = sink.description || sink.name || "Unknown device";
        if (isBluetooth(sink)) {
            const battery = batteryOf(sink);
            const kind = kindOf(sink);
            return {
                title: kind || "Headphones",
                caption: battery >= 0 ? "Bluetooth, " + battery + "% battery" : "Bluetooth",
                icon: kind === "Speaker" || kind === "Car" ? "sound" : "headphones"
            };
        }
        if (/hdmi|displayport/i.test(portName) || (port && port.type === Port.HDMI) || /hdmi|displayport/i.test(description)) {
            return {
                title: "External display",
                caption: /displayport/i.test(portName + " " + description) && !/hdmi/i.test(portName) ? "DisplayPort" : "HDMI",
                icon: "display"
            };
        }
        if (p["device.bus"] === "usb") {
            const kind = kindOf(sink);
            return {
                title: kind || description,
                caption: "USB",
                icon: kind === "Headphones" || kind === "Headset" ? "headphones" : "sound"
            };
        }
        if (/headphone/i.test(portName) || (port && port.type === Port.Headphones)) {
            return { title: "Headphones", caption: "Headphone jack", icon: "headphones" };
        }
        if (/lineout/i.test(portName) || (port && port.type === Port.Line)) {
            return { title: "Line out", caption: "Built-in", icon: "sound" };
        }
        if (/speaker/i.test(portName) || sink.formFactor === "internal" || (port && port.type === Port.Speaker)) {
            return { title: "Speakers", caption: "Built-in", icon: "sound" };
        }
        return { title: description, caption: "Sound output", icon: "sound" };
    }

    // The microphone's name for the drop-down ("Headphones") and for the
    // sentence above the level ("Headphones microphone").
    function inputInfo(source) {
        const p = props(source);
        const port = activePort(source);
        const portName = port ? port.name : "";
        const description = source.description || source.name || "Unknown device";
        if (isBluetooth(source)) {
            const kind = kindOf(source);
            const title = kind === "Speaker" || kind === "Car" || kind === "" ? "Headphones" : kind;
            return { title: title, phrase: title + " microphone" };
        }
        if (p["device.bus"] === "usb") {
            return { title: description, phrase: description };
        }
        if (/headset/i.test(portName)) {
            return { title: "Headset", phrase: "Headset microphone" };
        }
        if (/internal|builtin/i.test(portName) || source.formFactor === "internal") {
            return { title: "Built-in", phrase: "Built-in microphone" };
        }
        if (/mic/i.test(portName)) {
            return { title: "Microphone jack", phrase: "Plugged-in microphone" };
        }
        return { title: description, phrase: description };
    }

    // Short names, with a device's full description where two would read
    // the same (two pairs of Bluetooth headphones).
    function labels(list, info) {
        const titles = list.map(d => info(d).title);
        return list.map((d, i) => titles.indexOf(titles[i]) !== titles.lastIndexOf(titles[i]) ? (d.description || titles[i]) : titles[i]);
    }

    function options(list, info) {
        const names = labels(list, info);
        return list.map((d, i) => ({ value: d.index, text: names[i] }));
    }

    function byIndex(list, index) {
        return list.find(d => d.index === index) || null;
    }

    function byName(list, name) {
        return list.find(d => d.name === name) || null;
    }

    // Physical devices only, as the volume flyout lists them.
    PulseObjectFilterModel {
        id: sinks
        filterOutInactiveDevices: true
        filterVirtualDevices: true
        sourceModel: SinkModel {}
    }

    PulseObjectFilterModel {
        id: sources
        filterOutInactiveDevices: true
        filterVirtualDevices: true
        sourceModel: SourceModel {}
    }

    // Apps' playback streams, without the ones PipeWire makes for itself,
    // as the volume flyout lists them.
    PulseObjectFilterModel {
        id: streams
        filters: [ { role: "VirtualStream", value: false } ]
        sourceModel: SinkInputModel {}
    }

    // The saved volume and output for notification and alert sounds, which
    // the KDE sound module shows as Notification Sounds. plasma-pa makes this
    // entry itself when the sound server has none yet.
    PulseObjectFilterModel {
        id: restores
        filters: [ { role: "Name", value: "sink-input-by-media-role:event" } ]
        sourceModel: StreamRestoreModel {}
    }

    property var outputs: []
    property var inputs: []
    property var playing: []
    property var eventSounds: null

    readonly property var outputOptions: options(outputs, outputInfo)

    component Collector: Instantiator {
        delegate: QtObject {
            required property var model
            readonly property var object: model.PulseObject
        }
        function collect() {
            const list = [];
            for (let i = 0; i < count; ++i) {
                const o = objectAt(i);
                if (o && o.object) {
                    list.push(o.object);
                }
            }
            return list;
        }
    }

    Collector {
        model: sinks
        onObjectAdded: page.outputs = collect()
        onObjectRemoved: page.outputs = collect()
    }

    Collector {
        model: sources
        onObjectAdded: page.inputs = collect()
        onObjectRemoved: page.inputs = collect()
    }

    Collector {
        model: streams
        onObjectAdded: page.playing = collect()
        onObjectRemoved: page.playing = collect()
    }

    Collector {
        model: restores
        onObjectAdded: page.eventSounds = collect()[0] || null
        onObjectRemoved: page.eventSounds = collect()[0] || null
    }

    readonly property var output: PreferredDevice.sink && PreferredDevice.sink.name !== "auto_null" ? PreferredDevice.sink : null
    readonly property var input: PreferredDevice.source
    // The microphone is only opened while someone tests it, never just
    // because the page is open.
    property bool testingMic: false

    // Windows mode's apps all play through its one FreeRDP connection,
    // whose PulseAudio backend names itself "freerdp" (rdpsnd_pulse.c) and
    // runs as wildos-rdp (wildos-winmode rdp.py), so they are one stream and
    // one row.
    function isWindows(stream) {
        const p = props(stream);
        const binary = String(p["application.process.binary"] || "");
        return p["application.name"] === "freerdp" || /freerdp/i.test(binary)
            || (stream.client !== null && stream.client !== undefined && stream.client.name === "freerdp");
    }

    function appName(stream) {
        const p = props(stream);
        return String(p["application.name"] || (stream.client ? stream.client.name : "") || stream.name || "App");
    }

    // One row per app, as Windows has it: a browser with three tabs playing
    // is one Brave, and all of Windows mode is one Windows apps.
    readonly property var groups: {
        const list = [];
        const found = {};
        for (const s of playing) {
            if (!s) {
                continue;
            }
            const windows = isWindows(s);
            const p = props(s);
            const key = windows ? "windows" : "app:" + String(p["application.process.binary"] || appName(s));
            if (found[key] === undefined) {
                found[key] = list.length;
                list.push({ key: key, windows: windows, name: windows ? "Windows apps" : appName(s), streams: [] });
            }
            list[found[key]].streams.push(s);
        }
        return list;
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Output"
        }

        GridLayout {
            Layout.fillWidth: true
            visible: page.outputs.length > 0
            columns: 3
            columnSpacing: 10
            rowSpacing: 10

            Repeater {
                model: page.outputs

                OutputCard {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 100
                    Layout.minimumWidth: 0
                    device: modelData
                }
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    visible: page.output === null
                    iconName: "sound"
                    title: "No speakers or headphones found"
                    description: "Plug some in or connect them over Bluetooth"
                }

                UI.SettingRow {
                    id: volumeRow
                    visible: page.output !== null
                    title: "Volume"
                    minHeight: 50

                    readonly property int level: page.output ? Math.min(100, page.percent(page.output.volume)) : 0

                    UI.Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 260
                        flat: true
                        accessibleName: "Output volume"
                        dimmed: page.output !== null && page.output.muted
                        value: volumeRow.level
                        onMoved: {
                            const device = page.output;
                            if (!device) {
                                return;
                            }
                            device.volume = page.toVolume(value);
                            if (device.muted && value > 0) {
                                device.muted = false;
                            }
                        }
                    }

                    Percent {
                        anchors.verticalCenter: parent.verticalCenter
                        text: volumeRow.level + "%"
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Input"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: "Microphone"
                    description: !page.input ? "No microphone found"
                        : page.testingMic ? page.inputInfo(page.input).phrase + ". Listening, speak to test it."
                        : page.inputInfo(page.input).phrase
                    minHeight: 54

                    // The level of whatever the microphone hears, only while
                    // a test runs: with no target the monitor opens no
                    // stream. Leaving the page ends it too.
                    VolumeMonitor {
                        id: micLevel
                        target: page.testingMic ? page.input : null
                    }

                    Timer {
                        running: page.testingMic
                        interval: 15000
                        onTriggered: page.testingMic = false
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.input !== null && page.testingMic
                        spacing: 3
                        Accessible.role: Accessible.Indicator
                        Accessible.name: "Microphone level"

                        // Peaks are linear, so ordinary speech would barely
                        // light the first bars; the square root spreads it
                        // across the meter as Windows' does.
                        readonly property int lit: page.input && !page.input.muted
                            ? Math.round(Math.sqrt(Math.max(0, Math.min(1, micLevel.volume))) * 14) : 0

                        Repeater {
                            model: 14

                            Rectangle {
                                required property int index
                                width: 5
                                height: 14
                                radius: 2
                                color: index < parent.lit ? UI.Style.accent : UI.Style.tone(0.14)
                            }
                        }
                    }

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.input !== null
                        text: page.testingMic ? "Stop" : "Test"
                        Accessible.name: page.testingMic ? "Stop testing the microphone" : "Test the microphone"
                        onClicked: page.testingMic = !page.testingMic
                    }

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.inputs.length > 0
                        Accessible.name: "Microphone"
                        model: page.options(page.inputs, page.inputInfo)
                        readonly property var choice: page.input ? page.input.index : -1
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => {
                            const device = page.byIndex(page.inputs, model[index].value);
                            if (device) {
                                device.default = true;
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Volume mixer"
        }

        UI.Label {
            Layout.fillWidth: true
            text: "Set the volume and output for each app on its own. Apps appear here while they're playing sound."
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Card {
            Layout.fillWidth: true
            visible: page.groups.length > 0 || page.eventSounds !== null

            Column {
                width: parent.width

                Repeater {
                    model: page.groups

                    AppRow {
                        required property var modelData
                        group: modelData
                    }
                }

                SystemSoundsRow {
                    visible: page.eventSounds !== null
                    entry: page.eventSounds
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Sounds"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "sound"
                    title: "Sound scheme"
                    description: "The sounds for notifications, warnings, devices and more"

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: "Sound scheme"
                        model: [
                            { value: "wildos", text: "Wild OS" },
                            { value: "ocean", text: "Ocean (KDE)" },
                            { value: "none", text: "No sounds" }
                        ]
                        currentIndex: model.findIndex(o => o.value === SoundTheme.scheme)
                        onActivated: index => SoundTheme.setScheme(model[index].value)
                    }
                }

                UI.SettingRow {
                    iconName: "power"
                    title: "Play a sound when Wild OS starts"
                    description: "When the desktop appears after you sign in"
                    status: SoundTheme.startupSound ? "On" : "Off"
                    enabled: SoundTheme.scheme !== "none"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: SoundTheme.startupSound
                        Accessible.name: "Play a sound when Wild OS starts"
                        onToggled: {
                            SoundTheme.setStartupSound(checked);
                            if (checked) {
                                SoundTheme.preview("desktop-login");
                            }
                        }
                    }
                }
            }
        }
    }

    // The percentage beside a slider keeps its place as the digits change.
    component Percent: UI.Label {
        width: 34
        Layout.preferredWidth: 34
        size: 12
        color: UI.Style.inkMuted
        font.features: ({ "tnum": 1 })
    }

    // One output device as a card (SettingsSound.dc): the chosen one has
    // the accent edge. Choosing one makes it the default, so apps that have
    // no output of their own move to it, as in the volume flyout.
    component OutputCard: T.AbstractButton {
        id: card

        property var device: null
        readonly property var info: device ? page.outputInfo(device) : ({ title: "", caption: "", icon: "sound" })
        readonly property bool chosen: device !== null && page.output !== null && page.output.index === device.index

        implicitHeight: 62
        padding: 0
        leftPadding: 14
        rightPadding: 14
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.RadioButton
        Accessible.name: info.title
        Accessible.description: info.caption
        Accessible.checked: chosen

        onClicked: if (device && !chosen) device.default = true

        background: Rectangle {
            radius: UI.Style.radiusMd
            color: card.chosen ? UI.Style.accentSoft : card.down ? UI.Style.surfacePressed : card.hovered ? UI.Style.surfaceHover : UI.Style.surfaceRest
            border.width: card.chosen ? 2 : 1
            border.color: card.chosen ? UI.Style.accent : UI.Style.borderField
            Behavior on color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }

            UI.FocusRing {
                visible: card.visualFocus
                targetRadius: UI.Style.radiusMd
            }
        }

        contentItem: RowLayout {
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 9
                color: UI.Style.surfaceTile

                UI.Icon {
                    anchors.centerIn: parent
                    name: card.info.icon
                    size: 18
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: card.info.title
                    size: 13
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: card.info.caption
                    size: 11
                    color: UI.Style.inkMuted
                }
            }
        }
    }

    // The parts every mixer row shares: a 30px tile, the name over a
    // second line in a 200px column, mute, a 200px slider, the percentage
    // and the output drop-down on the right (SettingsSound.dc).
    component MixerRow: Item {
        id: row

        property string title
        property string subtitle
        property bool windowsMode: false
        property bool muted: false
        property int level: 0
        property int deviceIndex: -1
        default property alias tile: tileBox.data

        signal volumeMoved(int value)
        signal muteClicked()
        signal deviceChosen(int index)

        width: parent ? parent.width : 0
        implicitHeight: Math.max(56, layout.implicitHeight + 16)

        Rectangle {
            visible: row.y > 0
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: UI.Style.divider
        }

        RowLayout {
            id: layout
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 14

            Item {
                id: tileBox
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
            }

            ColumnLayout {
                Layout.preferredWidth: 200
                Layout.maximumWidth: 200
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    UI.Label {
                        Layout.fillWidth: !row.windowsMode
                        Layout.maximumWidth: 200
                        text: row.title
                        size: 13
                    }

                    UI.Chip {
                        visible: row.windowsMode
                        text: "Windows mode"
                    }

                    Item {
                        visible: row.windowsMode
                        Layout.fillWidth: true
                    }
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: row.subtitle
                    size: 11
                    color: UI.Style.inkMuted
                }
            }

            UI.IconButton {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 7
                iconName: row.muted ? "sound-off" : "sound"
                iconSize: 16
                tip: (row.muted ? "Unmute " : "Mute ") + row.title
                onClicked: row.muteClicked()
            }

            UI.Slider {
                Layout.preferredWidth: 200
                flat: true
                accessibleName: row.title + " volume"
                dimmed: row.muted
                value: row.level
                onMoved: row.volumeMoved(Math.round(value))
            }

            Percent {
                text: row.level + "%"
            }

            Item {
                Layout.fillWidth: true
            }

            UI.ComboBox {
                visible: page.outputOptions.length > 0
                Accessible.name: row.title + " output device"
                model: page.outputOptions
                currentIndex: model ? model.findIndex(o => o.value === row.deviceIndex) : -1
                onActivated: index => row.deviceChosen(model[index].value)
            }
        }
    }

    // An app, or all of Windows mode, with every stream it has open. The
    // first stream speaks for the row; a change goes to all of them, so a
    // second tab never plays louder than the slider says.
    component AppRow: MixerRow {
        id: app

        property var group: ({ streams: [], windows: false, name: "" })
        readonly property var lead: group.streams.length > 0 ? group.streams[0] : null
        readonly property bool paused: group.streams.length > 0 && group.streams.every(s => s && s.corked)

        // What the app is playing, unless it only names itself.
        readonly property string mediaName: {
            const shown = group.streams.find(s => s && !s.corked) || lead;
            if (!shown) {
                return "";
            }
            const text = String(page.props(shown)["media.name"] || shown.name || "");
            return /^(playback|audio ?stream|output|sound|audio|simple ?dsp)$/i.test(text.trim()) || text === group.name ? "" : text;
        }

        // The Windows apps that are open now, from Windows mode's service.
        readonly property string openWindowsApps: {
            if (!group.windows) {
                return "";
            }
            const apps = WindowsMode.status.apps || [];
            return apps.filter(a => a.open).map(a => a.name).join(", ");
        }

        title: group.name
        windowsMode: group.windows
        subtitle: group.windows
            ? (openWindowsApps.length > 0 ? openWindowsApps + ". Share one volume" : "All Windows apps share one volume")
            : paused ? "Paused" : mediaName
        muted: lead !== null && lead.muted
        level: lead ? Math.min(100, page.percent(lead.volume)) : 0
        deviceIndex: lead ? lead.deviceIndex : -1

        onVolumeMoved: value => {
            for (const s of group.streams) {
                if (!s) {
                    continue;
                }
                s.volume = page.toVolume(value);
                if (s.muted && value > 0) {
                    s.muted = false;
                }
            }
        }
        onMuteClicked: {
            const mute = !muted;
            for (const s of group.streams) {
                if (s) {
                    s.muted = mute;
                }
            }
        }
        // Moving the streams is PipeWire's own per-stream routing; it
        // remembers the choice for the app next time.
        onDeviceChosen: index => {
            for (const s of group.streams) {
                if (s && s.deviceIndex !== index) {
                    s.deviceIndex = index;
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: app.group.windows
            radius: 8
            color: "#3b3d41"

            Grid {
                anchors.centerIn: parent
                columns: 2
                spacing: 2

                Repeater {
                    model: 4

                    Rectangle {
                        width: 6
                        height: 6
                        color: "#8fb4ff"
                    }
                }
            }
        }

        UI.ThemeIcon {
            id: appIcon
            anchors.fill: parent
            visible: !app.group.windows && valid
            name: app.lead ? app.lead.iconName : ""
            fallback: app.lead ? String(page.props(app.lead)["application.icon_name"] || page.props(app.lead)["application.process.binary"] || "") : ""
        }

        Rectangle {
            anchors.fill: parent
            visible: !app.group.windows && !appIcon.valid
            radius: 8
            color: UI.Style.tone(0.10)

            UI.Icon {
                anchors.centerIn: parent
                name: "music"
                size: 15
            }
        }
    }

    // Notifications and alerts: plasma-pa's saved entry for the "event"
    // role, which every notification sound plays under. Its output is a
    // device name; none means the default output.
    component SystemSoundsRow: MixerRow {
        id: sounds

        property var entry: null
        readonly property var target: entry && entry.device ? page.byName(page.outputs, entry.device) : null

        title: "System sounds"
        subtitle: "Notifications and alerts"
        muted: entry !== null && entry.muted
        level: entry ? Math.min(100, page.percent(entry.volume)) : 0
        deviceIndex: target ? target.index : page.output ? page.output.index : -1

        onVolumeMoved: value => {
            if (!entry) {
                return;
            }
            entry.volume = page.toVolume(value);
            if (entry.muted && value > 0) {
                entry.muted = false;
            }
        }
        onMuteClicked: if (entry) entry.muted = !entry.muted
        onDeviceChosen: index => {
            const sink = page.byIndex(page.outputs, index);
            if (entry && sink) {
                entry.device = sink.name;
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: UI.Style.tone(0.10)

            UI.Icon {
                anchors.centerIn: parent
                name: "bell"
                size: 15
            }
        }
    }
}
