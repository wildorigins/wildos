import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.plasma.private.brightnesscontrolplugin
import WildOS.Settings
import WildOS.UI as UI
import org.wildos.overlays

// Display (SettingsDisplay.dc): the screens as they sit on the desk, then
// everything about the screen picked there, with brightness and night light
// below. Screens go through Displays (libkscreen, as KDE's display module);
// brightness through powerdevil like the quick settings slider.
//
// Every value here is a binding to Displays, which outlives the page, so the
// page shows the same thing however often Settings rebuilds it; only a drag
// in progress lives in the page.
PageScroll {
    id: page

    // The screen the rows change. Until one is picked, and if the picked one
    // goes away, it is the main display.
    property int chosenId: -1

    // A mouse drag on a screen tile moves the screen, not the page.
    interactive: !canvas.holding && canvas.moving === null

    readonly property var outputs: Displays.outputs
    readonly property var enabledOutputs: outputs.filter(o => o.enabled)
    readonly property bool several: outputs.length > 1
    readonly property var blank: ({ id: -1, number: 0, label: "", enabled: false, primary: false, resolutions: [], resolution: "",
        rates: [], rate: "", scale: 1, recommendedScale: 1, rotation: 1, vrrCapable: false, vrr: false, hdrCapable: false,
        hdr: false, sdrBrightness: 200, sdrMin: 50, sdrMax: 500 })
    readonly property var selected: outputs.find(o => o.id === chosenId) || outputs.find(o => o.primary) || outputs[0] || blank

    readonly property var rotations: [
        { value: 1, text: qsTr("Landscape") },
        { value: 2, text: qsTr("Portrait") },
        { value: 4, text: qsTr("Landscape (flipped)") },
        { value: 8, text: qsTr("Portrait (flipped)") }
    ]

    function scaleOptions(current, recommended) {
        const list = [1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3];
        for (const extra of [recommended, current]) {
            if (!list.some(s => Math.abs(s - extra) < 0.001)) {
                list.push(extra);
            }
        }
        list.sort((a, b) => a - b);
        return list.map(s => ({
            value: s,
            text: Math.abs(s - recommended) < 0.001 ? qsTr("%1% (recommended)").arg(Math.round(s * 100)) : qsTr("%1%").arg(Math.round(s * 100))
        }));
    }

    function arrangementOptions(screen) {
        const list = [
            { value: "extend", text: qsTr("Extend") },
            { value: "duplicate", text: qsTr("Duplicate") },
            { value: "only", text: qsTr("Only this screen") }
        ];
        if (!screen.enabled || enabledOutputs.length > 1) {
            list.push({ value: "off", text: qsTr("Off") });
        }
        return list;
    }

    function arrangementOf(screen) {
        if (!screen.enabled) {
            return "off";
        }
        return Displays.arrangement;
    }

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: Displays
        function onFailed(reason) {
            notice.show(qsTr("The screens were not changed"), reason, true);
        }
        function onConfirmChanged() {
            if (Displays.confirming && !keepDialog.visible) {
                keepDialog.open();
            } else if (!Displays.confirming && keepDialog.visible) {
                keepDialog.close();
            }
        }
    }

    // A change still counting down when the page is opened again asks again.
    Component.onCompleted: {
        if (Displays.confirming) {
            keepDialog.open();
        }
    }

    Rectangle {
        id: canvas

        // The part of the desk drawing the screens fill, above the hint and
        // buttons along the foot (SettingsDisplay.dc: 26px down, 141px tall).
        readonly property real areaTop: 26
        readonly property real areaHeight: height - areaTop - 39
        readonly property real areaWidth: width - 48

        readonly property bool mirrored: Displays.arrangement === "duplicate"
        readonly property var bounds: {
            const list = page.enabledOutputs;
            if (list.length === 0) {
                return null;
            }
            let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity;
            for (const o of list) {
                left = Math.min(left, o.x);
                top = Math.min(top, o.y);
                right = Math.max(right, o.x + o.width);
                bottom = Math.max(bottom, o.y + o.height);
            }
            return right > left && bottom > top ? { x: left, y: top, w: right - left, h: bottom - top } : null;
        }
        readonly property real factor: bounds ? Math.min(areaWidth / bounds.w, areaHeight / bounds.h) : 0
        readonly property real originX: bounds ? (width - bounds.w * factor) / 2 - bounds.x * factor : 0
        readonly property real originY: bounds ? areaTop + (areaHeight - bounds.h * factor) / 2 - bounds.y * factor : 0

        // Duplicated screens show the same picture, so they are one tile,
        // numbered "1 | 2" as Windows does.
        readonly property var tiles: {
            if (!bounds) {
                return [];
            }
            if (mirrored) {
                return [{ id: -1, members: page.enabledOutputs.map(o => o.id), x: bounds.x, y: bounds.y, width: bounds.w, height: bounds.h,
                          title: page.enabledOutputs.map(o => o.number).join(" | "), caption: "" }];
            }
            return page.enabledOutputs.map(o => ({ id: o.id, members: [o.id], x: o.x, y: o.y, width: o.width, height: o.height,
                                                   title: String(o.number), caption: o.label }));
        }

        // { id, x, y } of a screen being dragged, where it would land.
        property var moving: null
        property bool holding: false

        // The 2px accent lines where the picked (or dragged) screen meets
        // another.
        readonly property var edges: {
            if (mirrored || factor <= 0 || page.enabledOutputs.length < 2) {
                return [];
            }
            const focus = page.enabledOutputs.find(o => o.id === (moving ? moving.id : page.selected.id));
            if (!focus) {
                return [];
            }
            const fx = moving ? moving.x : focus.x;
            const fy = moving ? moving.y : focus.y;
            const list = [];
            for (const o of page.enabledOutputs) {
                if (o.id === focus.id) {
                    continue;
                }
                if (fx + focus.width === o.x || o.x + o.width === fx) {
                    const from = Math.max(fy, o.y);
                    const to = Math.min(fy + focus.height, o.y + o.height);
                    if (to > from) {
                        list.push({ x: fx + focus.width === o.x ? o.x : fx, y: from, length: to - from, upright: true });
                    }
                }
                if (fy + focus.height === o.y || o.y + o.height === fy) {
                    const from = Math.max(fx, o.x);
                    const to = Math.min(fx + focus.width, o.x + o.width);
                    if (to > from) {
                        list.push({ x: from, y: fy + focus.height === o.y ? o.y : fy, length: to - from, upright: false });
                    }
                }
            }
            return list;
        }

        Layout.fillWidth: true
        Layout.preferredHeight: 206
        visible: Displays.available
        radius: UI.Style.radiusMd
        color: UI.Style.shade(0.32, 0.05)
        border.width: 1
        border.color: UI.Style.borderSubtle
        clip: true

        // The dotted desk: a 1px dot every 16px.
        Canvas {
            id: dots

            property color dot: UI.Style.tone(0.08)

            anchors.fill: parent
            anchors.margins: 1
            onDotChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.fillStyle = dot;
                for (let y = 7; y < height; y += 16) {
                    for (let x = 7; x < width; x += 16) {
                        ctx.beginPath();
                        ctx.arc(x, y, 1, 0, 2 * Math.PI);
                        ctx.fill();
                    }
                }
            }
        }

        Repeater {
            model: canvas.edges

            Rectangle {
                required property var modelData
                x: canvas.originX + modelData.x * canvas.factor + (modelData.upright ? 0 : 3)
                y: canvas.originY + modelData.y * canvas.factor + (modelData.upright ? 3 : 0)
                width: modelData.upright ? 2 : modelData.length * canvas.factor - 6
                height: modelData.upright ? modelData.length * canvas.factor - 6 : 2
                color: UI.Style.accent
                opacity: 0.8
                z: 3
            }
        }

        Repeater {
            model: canvas.tiles

            T.AbstractButton {
                id: tile

                required property var modelData
                readonly property bool current: modelData.members.indexOf(page.selected.id) >= 0
                readonly property bool movable: !canvas.mirrored && page.enabledOutputs.length > 1
                // Where the screen would land if let go now, in desktop
                // pixels; snapping is Displays', so the drop lands exactly
                // where the drag showed. 12 canvas pixels pull an edge in.
                readonly property point landing: drag.active
                    ? Displays.snap(modelData.id,
                                    Math.round(modelData.x + drag.activeTranslation.x / canvas.factor),
                                    Math.round(modelData.y + drag.activeTranslation.y / canvas.factor),
                                    Math.round(12 / canvas.factor))
                    : Qt.point(modelData.x, modelData.y)

                x: canvas.originX + landing.x * canvas.factor + 3
                y: canvas.originY + landing.y * canvas.factor + 3
                width: Math.max(0, modelData.width * canvas.factor - 6)
                height: Math.max(0, modelData.height * canvas.factor - 6)
                z: drag.active ? 2 : current ? 1 : 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.caption.length > 0 ? qsTr("Display %1, %2").arg(modelData.title).arg(modelData.caption) : qsTr("Display %1").arg(modelData.title)
                Accessible.checked: current

                onClicked: page.chosenId = modelData.members[0]
                onPressedChanged: canvas.holding = pressed
                onLandingChanged: {
                    if (drag.active) {
                        canvas.moving = { id: modelData.id, x: landing.x, y: landing.y };
                    }
                }

                DragHandler {
                    id: drag
                    target: null
                    enabled: tile.movable
                    cursorShape: Qt.ClosedHandCursor
                    // The page scrolls by mouse drag too; once a screen is
                    // moving, the page may not take the drag over.
                    grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                    onActiveChanged: {
                        if (active) {
                            page.chosenId = tile.modelData.id;
                        } else if (canvas.moving) {
                            const to = canvas.moving;
                            canvas.moving = null;
                            Displays.move(to.id, to.x, to.y);
                        }
                    }
                }

                HoverHandler {
                    enabled: tile.movable
                    cursorShape: Qt.OpenHandCursor
                }

                background: Rectangle {
                    radius: UI.Style.radiusSm
                    color: tile.current ? UI.Style.fillSelected(0.16, 0.12)
                         : tile.hovered ? UI.Style.tone(0.10) : UI.Style.tone(0.07)
                    border.width: tile.current ? 2 : 1
                    border.color: tile.current ? UI.Style.accent : UI.Style.tone(0.25)
                    Behavior on color {
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }

                    UI.FocusRing {
                        visible: tile.visualFocus
                        targetRadius: UI.Style.radiusSm
                    }
                }

                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 8
                        spacing: 4

                        UI.Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: tile.modelData.title
                            size: 30
                            font.weight: UI.Style.weightLight
                        }

                        UI.Label {
                            width: parent.width
                            visible: text.length > 0 && tile.height > 60
                            horizontalAlignment: Text.AlignHCenter
                            text: tile.modelData.caption
                            size: 11
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }
        }

        UI.Label {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: canvasButtons.left
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            text: qsTr("Drag the screens to match how they sit on your desk. Edges snap together.")
            size: 12
            color: UI.Style.inkMuted
        }

        Row {
            id: canvasButtons
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 8

            UI.Button {
                text: qsTr("Identify")
                implicitHeight: 30
                onClicked: {
                    identify.active = true;
                    identifyTimer.restart();
                }
            }

            UI.Button {
                text: qsTr("Detect")
                implicitHeight: 30
                onClicked: Displays.reload()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: Displays.available && page.several
        spacing: 12

        Rectangle {
            implicitWidth: choices.implicitWidth + 6
            implicitHeight: choices.implicitHeight + 6
            radius: 10
            color: UI.Style.tone(0.06)
            border.width: 1
            border.color: UI.Style.tone(0.08)
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Display to change")

            Row {
                id: choices
                x: 3
                y: 3
                spacing: 2

                Repeater {
                    model: page.outputs

                    ScreenChoice {
                        required property var modelData
                        text: modelData.number + "  " + modelData.label
                        current: modelData.id === page.selected.id
                        onClicked: page.chosenId = modelData.id
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Row {
            visible: page.selected.enabled && page.enabledOutputs.length > 1
            spacing: 10

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Main display")
                size: 13
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: page.selected.primary ? qsTr("On") : qsTr("Off")
                size: 12
                color: UI.Style.inkMuted
            }

            UI.Toggle {
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: qsTr("Make this my main display")
                checked: page.selected.primary
                // There is always a main display; another screen takes the
                // role over rather than this one giving it up.
                checkable: !page.selected.primary
                onToggled: Displays.setPrimary(page.selected.id)
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        visible: Displays.available
        columns: width >= 640 ? 2 : 1
        columnSpacing: 14
        rowSpacing: 14

        UI.Card {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Resolution")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Resolution")
                        model: page.selected.resolutions
                        readonly property string choice: page.selected.resolution
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => Displays.setResolution(page.selected.id, model[index].value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Scale")
                    description: qsTr("Size of text and apps")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Scale")
                        model: page.scaleOptions(page.selected.scale, page.selected.recommendedScale)
                        readonly property real choice: page.selected.scale
                        currentIndex: model ? model.findIndex(o => Math.abs(o.value - choice) < 0.001) : -1
                        onActivated: index => Displays.setScale(page.selected.id, model[index].value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Orientation")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Orientation")
                        model: page.rotations
                        readonly property int choice: page.selected.rotation
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => Displays.setRotation(page.selected.id, model[index].value)
                    }
                }

                UI.SettingRow {
                    visible: page.several
                    title: qsTr("With more than one display")
                    minHeight: 52

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Multiple displays")
                        model: page.arrangementOptions(page.selected)
                        readonly property string choice: page.arrangementOf(page.selected)
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => {
                            if (model[index].value !== choice) {
                                Displays.setArrangement(page.selected.id, model[index].value);
                            }
                        }
                    }
                }
            }
        }

        UI.Card {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            visible: page.selected.rates.length > 0 || page.selected.vrrCapable || page.selected.hdrCapable

            Column {
                width: parent.width

                UI.SettingRow {
                    visible: page.selected.rates.length > 0
                    title: qsTr("Refresh rate")
                    description: qsTr("Smoother motion, more power")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Refresh rate")
                        model: page.selected.rates
                        readonly property string choice: page.selected.rate
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => Displays.setRefreshRate(page.selected.id, model[index].value)
                    }
                }

                UI.SettingRow {
                    visible: page.selected.vrrCapable
                    title: qsTr("Variable refresh rate")
                    description: qsTr("Stops tearing in games and video")
                    status: page.selected.vrr ? qsTr("On") : qsTr("Off")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Variable refresh rate")
                        checked: page.selected.vrr
                        onToggled: Displays.setVrr(page.selected.id, checked)
                    }
                }

                UI.SettingRow {
                    visible: page.selected.hdrCapable
                    title: qsTr("HDR")
                    description: qsTr("Brighter, richer video and games")
                    status: page.selected.hdr ? qsTr("On") : qsTr("Off")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("HDR")
                        checked: page.selected.hdr
                        onToggled: Displays.setHdr(page.selected.id, checked)
                    }
                }

                UI.SettingRow {
                    visible: page.selected.hdrCapable && page.selected.hdr
                    title: qsTr("Brightness of everyday content")
                    description: qsTr("With HDR on")
                    minHeight: 52
                    enabled: page.selected.enabled

                    UI.Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 120
                        flat: true
                        accessibleName: qsTr("SDR content brightness")
                        from: page.selected.sdrMin
                        to: page.selected.sdrMax
                        stepSize: 10
                        value: page.selected.sdrBrightness
                        onMoved: Displays.setSdrBrightness(page.selected.id, Math.round(value))
                    }
                }
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: Displays.available
        text: qsTr("Changes to resolution, refresh rate and HDR show a preview and switch back on their own after 15 seconds unless you choose Keep.")
        size: 12
        color: UI.Style.inkMuted
        lineHeight: 1.5
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true
        visible: Displays.ready && !Displays.available

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "display"
                title: qsTr("Screen settings are not available")
                description: qsTr("Wild OS could not read the screens from the display service.")
            }
        }
    }

    ScreenBrightnessControl {
        id: brightness
        isSilent: true
    }

    NightLightControl {
        id: nightLight
    }

    property var dimmable: []

    Instantiator {
        id: dimmableDisplays
        model: brightness.displays
        delegate: QtObject {
            required property string displayName
            required property string label
            required property int brightness
            required property int maxBrightness
        }
        onObjectAdded: page.dimmable = page.collect()
        onObjectRemoved: page.dimmable = page.collect()
    }

    function collect() {
        const list = [];
        for (let i = 0; i < dimmableDisplays.count; ++i) {
            list.push(dimmableDisplays.objectAt(i));
        }
        return list;
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.dimmable.length > 0 || Pages.exists("nightlight")
        spacing: 10

        SectionTitle {
            text: qsTr("Brightness and colour")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.dimmable

                    UI.SettingRow {
                        id: brightnessRow
                        required property var modelData
                        readonly property int percent: modelData.maxBrightness > 0 ? Math.round(modelData.brightness / modelData.maxBrightness * 100) : 0
                        iconName: "sun"
                        title: page.dimmable.length > 1 ? qsTr("Brightness of %1").arg(modelData.label) : qsTr("Brightness")
                        description: page.dimmable.length > 1 ? qsTr("Makes this screen brighter or dimmer") : qsTr("Makes the screen brighter or dimmer")
                        status: qsTr("%1%").arg(percent)

                        UI.Slider {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 240
                            accessibleName: brightnessRow.title
                            from: 1
                            value: brightnessRow.percent
                            onMoved: brightness.setBrightness(brightnessRow.modelData.displayName,
                                Math.max(1, Math.round(value / 100 * brightnessRow.modelData.maxBrightness)))
                        }
                    }
                }

                UI.SettingRow {
                    visible: Pages.exists("nightlight")
                    iconName: "moon"
                    title: qsTr("Night light")
                    description: qsTr("Warmer colours to help you sleep")
                    status: nightLight.enabled ? (nightLight.inhibited ? qsTr("Paused") : qsTr("On")) : qsTr("Off")
                    chevron: true
                    onClicked: Navigator.open("nightlight")
                }
            }
        }

        // Only PCs with an NVIDIA card have a choice of driver.
        UI.Card {
            Layout.fillWidth: true
            visible: Graphics.hasNvidia

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "chip"
                    title: qsTr("Graphics driver")
                    description: Graphics.nvidiaName
                    status: Graphics.restartNeeded ? qsTr("Restart needed") : Graphics.driverInUse === "nvidia" ? "NVIDIA" : qsTr("Open")
                    chevron: true
                    onClicked: Navigator.open("graphicsdriver")
                }
            }
        }
    }

    // Identify: each screen's number, large in its bottom left corner, for
    // three seconds. Each is a layer surface on its own screen: KWin puts
    // an ordinary full-screen window on whichever screen is active, which
    // put every number on the same one. It is see-through and lets every
    // click pass, so nothing under it is blocked.
    Instantiator {
        id: identify
        active: false
        model: page.enabledOutputs

        delegate: Window {
            id: marker

            required property var modelData

            transientParent: null
            flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
            color: "transparent"
            title: qsTr("Display %1").arg(modelData.number)
            width: screen ? screen.width : 0
            height: screen ? screen.height : 0
            // The screen is given once the window exists, then it is shown,
            // so the layer surface is made on that screen. As a binding, Qt
            // put a window made inside the Settings window back on its screen.
            // Qt also moves a window, when first shown, to the screen that
            // holds its position, so the position is that screen's too.
            Component.onCompleted: {
                const screens = Qt.application.screens;
                for (let i = 0; i < screens.length; ++i) {
                    if (screens[i].name === modelData.name) {
                        screen = screens[i];
                        x = screens[i].virtualX;
                        y = screens[i].virtualY;
                    }
                }
                visible = true;
            }

            OverlayPlacement {
                window: marker
                edges: OverlayPlacement.Top | OverlayPlacement.Bottom | OverlayPlacement.Left | OverlayPlacement.Right
                layer: OverlayPlacement.Overlay
                keyboard: OverlayPlacement.NoKeyboard
                fromScreenEdge: true
                scope: "wildos-identify"
            }

            UI.Glass {
                level: "menu"
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 48
                width: 168
                height: 168

                UI.Label {
                    anchors.centerIn: parent
                    text: marker.modelData.number
                    size: 110
                    font.weight: UI.Style.weightLight
                }
            }
        }
    }

    Timer {
        id: identifyTimer
        interval: 3000
        onTriggered: identify.active = false
    }

    UI.Dialog {
        id: keepDialog

        title: qsTr("Keep these display settings?")
        text: Displays.secondsLeft === 1 ? qsTr("Switching back to the previous settings in 1 second.") : qsTr("Switching back to the previous settings in %1 seconds.").arg(Displays.secondsLeft)
        // Only Keep or the countdown ends it; a stray Escape must not leave
        // an unreadable screen in place.
        closePolicy: T.Popup.NoAutoClose

        buttons: [
            UI.Button {
                text: qsTr("Keep")
                kind: "primary"
                onClicked: Displays.keep()
            },
            UI.Button {
                text: qsTr("Switch back")
                onClicked: Displays.revert()
            }
        ]
    }

    component ScreenChoice: T.AbstractButton {
        id: choice

        property bool current: false

        implicitWidth: label.implicitWidth + 28
        implicitHeight: 30
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.RadioButton
        Accessible.name: text
        Accessible.checked: current

        background: Rectangle {
            radius: 7
            // Fades in on hover and clears at once, so moving along a list
            // never lights two items. It comes before the colour so it sees
            // the change in hover first.
            Behavior on color {
                enabled: choice.hovered
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
            color: choice.current ? UI.Style.tone(0.14) : choice.down ? UI.Style.surfacePressed : choice.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

            UI.FocusRing {
                visible: choice.visualFocus
                targetRadius: 7
            }
        }

        contentItem: UI.Label {
            id: label
            text: choice.text
            size: 13
            horizontalAlignment: Text.AlignHCenter
            font.weight: choice.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
            color: choice.current ? UI.Style.ink : UI.Style.inkAlpha(0.85, 0.80)
        }
    }
}
