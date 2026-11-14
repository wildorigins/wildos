pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI

// The Capture editor (AppCaptureEdit.dc), opened after every screenshot:
// the file's name in the title bar; select, pen, highlighter, arrow, box,
// text, blur and crop, five colours, undo and redo, then Copy, Share and
// Save; the shot centred on a darker ground; its size and what just
// happened along the foot.
//
// Marks are kept as a list and drawn over the picture at its own pixel
// size, so Copy and Save render every pixel of it, whatever the zoom.
UI.AppWindow {
    id: win

    required property url source
    // The frozen image's key in the app, released when the window closes;
    // empty for a picture opened from a file.
    required property string key
    // Where Save writes, and whether the picture is there already.
    required property string path
    property bool saved: false

    // A scanned page from the Scan tab: Save puts it back in the scan.
    property bool scanPage: false

    // Closing for good, after any question about saving.
    signal done()
    // Save wrote the picture to `path`.
    signal wrote(string path)

    readonly property string fileName: path.substring(path.lastIndexOf("/") + 1)
    // A scanned page's file carries its page's number after a #, for the
    // Scan tab; the title leaves it out.
    readonly property string shownName: scanPage ? fileName.replace(/ #\d+\.png$/, "") : fileName
    readonly property string placeName: path.indexOf("/Pictures/Screenshots/") >= 0 ? qsTr("Pictures › Screenshots")
        : path.substring(0, path.lastIndexOf("/")).replace(/^.*\//, "")
    // Line widths and text are drawn at the screen's scale, so a mark looks
    // the same size on the picture as it did on screen.
    // A scanned page is far bigger than the screen, so there the unit is
    // what one screen pixel is on the page, shown whole.
    readonly property real unit: scanPage ? Math.max(1, base.implicitHeight / Math.max(200, Screen.desktopAvailableHeight - 362))
        : Math.max(1, Screen.devicePixelRatio)
    readonly property int imageWidth: base.implicitWidth
    readonly property int imageHeight: base.implicitHeight

    property string tool: "arrow"
    property int inkIndex: 0
    readonly property color ink: inks[inkIndex].colour
    // The marks: { kind, colour, width, points } for the pen and
    // highlighter, { from, to } for an arrow, { x, y, w, h } for a box, a
    // blur and a piece of text (with its `text`), all in picture pixels.
    property var items: []
    // The part of the picture kept, or null for all of it.
    property var cropRect: null
    property var past: []
    property var future: []
    property var drawing: null
    property var cropDraft: null
    property int selectedIndex: -1
    property bool dirty: false
    property bool typing: false
    property point textAt: Qt.point(0, 0)
    property bool closingNow: false
    property string status: key.length > 0 ? qsTr("Copied to the clipboard") : ""

    // The colours marks are drawn in become part of the picture, so they
    // are the board's own and not the scheme's, apart from the blue, which
    // the board takes from the accent in each scheme.
    readonly property var inks: [
        { name: qsTr("Red"), colour: "#ff5468" },
        { name: qsTr("Amber"), colour: "#ffc46a" },
        { name: qsTr("Green"), colour: "#5fd38d" },
        { name: qsTr("Blue"), colour: UI.Style.dark ? "#8fb4ff" : "#3563d8" },
        { name: qsTr("White"), colour: "#ffffff" }
    ]
    readonly property var tools: [
        { id: "select", name: qsTr("Select") },
        { id: "pen", name: qsTr("Pen") },
        { id: "highlighter", name: qsTr("Highlighter") },
        { id: "arrow", name: qsTr("Arrow") },
        { id: "box", name: qsTr("Box") },
        { id: "text", name: qsTr("Text") },
        { id: "blur", name: qsTr("Blur") },
        { id: "crop", name: qsTr("Crop") }
    ]

    readonly property var region: tool === "crop" || !cropRect ? { x: 0, y: 0, width: Math.max(1, imageWidth), height: Math.max(1, imageHeight) } : cropRect

    // The bands above and below the picture: deeper than the window in
    // dark mode, a touch greyer in light (#15171d and #0d0f15 on #181a20;
    // #f2f2f2 and #eeeeee on #f4f4f4).
    function band(darkAlpha: real, lightAlpha: real): color {
        return UI.Style.dark ? UI.Style.shade(darkAlpha, 0) : UI.Style.layer(0, false, lightAlpha);
    }

    deep: true
    title: shownName
    width: Math.round(Math.min(Math.max(900, imageWidth / unit + 200), Screen.desktopAvailableWidth - 80))
    height: Math.round(Math.min(Math.max(560, imageHeight / unit + 266), Screen.desktopAvailableHeight - 80))
    minimumWidth: 880
    minimumHeight: 480

    function commit(newItems: var, newCrop: var): void {
        past = past.concat([{ items: items, crop: cropRect }]);
        future = [];
        items = newItems;
        cropRect = newCrop;
        dirty = true;
    }
    function undo(): void {
        finishText();
        if (past.length === 0) {
            return;
        }
        const back = past.slice();
        const state = back.pop();
        future = future.concat([{ items: items, crop: cropRect }]);
        past = back;
        items = state.items;
        cropRect = state.crop;
        selectedIndex = -1;
        dirty = true;
    }
    function redo(): void {
        finishText();
        if (future.length === 0) {
            return;
        }
        const ahead = future.slice();
        const state = ahead.pop();
        past = past.concat([{ items: items, crop: cropRect }]);
        future = ahead;
        items = state.items;
        cropRect = state.crop;
        selectedIndex = -1;
        dirty = true;
    }

    function setTool(name: string): void {
        finishText();
        if (name !== "select") {
            selectedIndex = -1;
        }
        tool = name;
        stage.forceActiveFocus();
    }

    function normalised(x1: real, y1: real, x2: real, y2: real): var {
        return { x: Math.min(x1, x2), y: Math.min(y1, y2), w: Math.abs(x2 - x1), h: Math.abs(y2 - y1) };
    }

    function bounds(item: var): var {
        if (item.points) {
            let l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
            for (const p of item.points) {
                l = Math.min(l, p.x);
                t = Math.min(t, p.y);
                r = Math.max(r, p.x);
                b = Math.max(b, p.y);
            }
            const half = item.width / 2;
            return { x: l - half, y: t - half, w: r - l + item.width, h: b - t + item.width };
        }
        if (item.kind === "arrow") {
            const box = normalised(item.from.x, item.from.y, item.to.x, item.to.y);
            const pad = arrowHead(item) / 2;
            return { x: box.x - pad, y: box.y - pad, w: box.w + pad * 2, h: box.h + pad * 2 };
        }
        return { x: item.x, y: item.y, w: item.w, h: item.h };
    }

    function arrowHead(item: var): real {
        return Math.max(14 * unit, item.width * 4.5);
    }

    function distanceToSegment(p: point, a: point, b: point): real {
        const dx = b.x - a.x;
        const dy = b.y - a.y;
        const length = dx * dx + dy * dy;
        const t = length === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length));
        return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
    }

    function hit(item: var, p: point): bool {
        const reach = 8 / Math.max(0.01, view.fit);
        if (item.points) {
            for (let i = 0; i < item.points.length; i++) {
                const a = item.points[i];
                const b = item.points[Math.min(i + 1, item.points.length - 1)];
                if (distanceToSegment(p, a, b) <= item.width / 2 + reach) {
                    return true;
                }
            }
            return false;
        }
        if (item.kind === "arrow") {
            return distanceToSegment(p, item.from, item.to) <= item.width / 2 + reach;
        }
        return p.x >= item.x - reach && p.x <= item.x + item.w + reach && p.y >= item.y - reach && p.y <= item.y + item.h + reach;
    }

    function moved(item: var, dx: real, dy: real): var {
        const copy = Object.assign({}, item);
        if (item.points) {
            copy.points = item.points.map(p => Qt.point(p.x + dx, p.y + dy));
        } else if (item.kind === "arrow") {
            copy.from = Qt.point(item.from.x + dx, item.from.y + dy);
            copy.to = Qt.point(item.to.x + dx, item.to.y + dy);
        } else {
            copy.x = item.x + dx;
            copy.y = item.y + dy;
        }
        return copy;
    }

    function removeSelected(): void {
        if (selectedIndex < 0 || selectedIndex >= items.length) {
            return;
        }
        const list = items.slice();
        list.splice(selectedIndex, 1);
        selectedIndex = -1;
        commit(list, cropRect);
    }

    // Light inks take dark text and dark ones white, so a label reads.
    function textColour(c: color): color {
        return UI.Style.luminance(Qt.lighter(c, 1)) > 0.45 ? Qt.rgba(0, 0, 0, 0.88) : "#ffffff";
    }

    function startText(at: point): void {
        textAt = at;
        textInput.text = "";
        typing = true;
        textInput.forceActiveFocus();
    }
    function finishText(): void {
        if (!typing) {
            return;
        }
        typing = false;
        const words = textInput.text.trim();
        textInput.text = "";
        stage.forceActiveFocus();
        if (words.length === 0) {
            return;
        }
        textMetrics.text = words;
        commit(items.concat([{
            kind: "text",
            colour: ink,
            text: words,
            size: 15 * unit,
            x: textAt.x,
            y: textAt.y,
            w: Math.ceil(textMetrics.advanceWidth + 24 * unit),
            h: Math.ceil(textMetrics.height + 12 * unit)
        }]), cropRect);
    }

    function cropArgument(): rect {
        return cropRect ? Qt.rect(cropRect.x, cropRect.y, cropRect.width, cropRect.height) : Qt.rect(0, 0, 0, 0);
    }
    function render(then: var): void {
        finishText();
        paper.grabToImage(result => then(result.image), Qt.size(paper.width, paper.height));
    }
    function copy(): void {
        render(image => {
            status = CaptureApp.copyImage(image, cropArgument()) ? qsTr("Copied to the clipboard") : qsTr("Couldn't copy it. Try again.");
        });
    }
    function save(then: var): void {
        render(image => {
            if (CaptureApp.saveImage(image, cropArgument(), path)) {
                saved = true;
                dirty = false;
                status = scanPage ? qsTr("Put back in the scan") : qsTr("Saved to %1").arg(placeName);
                win.wrote(path);
                if (then) {
                    then();
                }
            } else {
                status = scanPage ? qsTr("Couldn't put it back in the scan") : qsTr("Couldn't save to %1").arg(placeName);
            }
        });
    }
    function share(): void {
        const file = "file://" + path;
        if (saved && !dirty) {
            CaptureApp.openShare(file);
        } else {
            save(() => CaptureApp.openShare(file));
        }
    }
    function closeNow(): void {
        closingNow = true;
        close();
    }

    onClosing: close => {
        if (closingNow) {
            done();
            return;
        }
        finishText();
        if (dirty || !saved) {
            close.accepted = false;
            unsavedDialog.open();
            return;
        }
        done();
    }

    Shortcut { sequences: [StandardKey.Undo]; enabled: !win.typing; onActivated: win.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: !win.typing; onActivated: win.redo() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: win.save(null) }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !win.typing; onActivated: win.copy() }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: !win.typing && win.selectedIndex >= 0; onActivated: win.removeSelected() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: win.close() }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (win.typing) {
                textInput.text = "";
                win.finishText();
            } else if (win.selectedIndex >= 0) {
                win.selectedIndex = -1;
            } else {
                win.close();
            }
        }
    }

    TextMetrics {
        id: textMetrics
        font.family: UI.Style.fontFamily
        font.pixelSize: 15 * win.unit
        font.weight: UI.Style.weightBold
    }

    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 22
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        color: UI.Style.divider
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: win.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: win.frame
            showTitle: false

            // The title bar puts its content 16px in; the board's icon is 14.
            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/WildOS/Capture/data/wildos-capture.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.shownName
                    font.weight: UI.Style.weightMedium
                    color: win.active ? UI.Style.ink : UI.Style.inkMuted
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Capture")
                    color: UI.Style.inkSubtle
                }
            }
        }

        // The toolbar: 50px on a slightly deeper band, a hairline under it.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: win.band(0.2, 0.01)

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: UI.Style.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 4

                Repeater {
                    model: win.tools
                    delegate: ToolButton {
                        required property var modelData
                        tool: true
                        iconName: modelData.id
                        tip: modelData.name
                        active: win.tool === modelData.id
                        onClicked: win.setTool(modelData.id)
                    }
                }

                Divider {}

                Row {
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: 10

                    Repeater {
                        model: win.inks
                        delegate: T.Button {
                            id: swatch
                            required property var modelData
                            required property int index
                            readonly property bool current: win.inkIndex === index
                            width: 18
                            height: 18
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: modelData.name
                            Accessible.role: Accessible.RadioButton
                            onClicked: {
                                win.inkIndex = index;
                                if (win.tool === "select" && win.selectedIndex >= 0) {
                                    const list = win.items.slice();
                                    const changed = Object.assign({}, list[win.selectedIndex]);
                                    changed.colour = modelData.colour;
                                    list[win.selectedIndex] = changed;
                                    win.commit(list, win.cropRect);
                                }
                            }

                            background: Item {
                                // The ring round the colour in use, 2px off it.
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 26
                                    height: 26
                                    radius: 13
                                    visible: swatch.current || swatch.visualFocus
                                    color: "transparent"
                                    border.width: 2
                                    border.color: swatch.visualFocus && !swatch.current ? UI.Style.accent : swatch.modelData.colour
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: swatch.modelData.colour
                                    border.width: swatch.index === 4 || swatch.hovered ? 1 : 0
                                    border.color: UI.Style.borderControl
                                }
                            }
                            contentItem: Item {}

                            UI.Tooltip {
                                text: swatch.modelData.name
                                visible: swatch.hovered
                            }
                        }
                    }
                }

                Divider {}

                ToolButton {
                    iconName: "undo"
                    tip: qsTr("Undo (Ctrl+Z)")
                    enabled: win.past.length > 0 || win.typing
                    onClicked: win.undo()
                }
                ToolButton {
                    iconName: "redo"
                    tip: qsTr("Redo (Ctrl+Y)")
                    enabled: win.future.length > 0
                    onClicked: win.redo()
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    iconName: "copy"
                    text: qsTr("Copy")
                    onClicked: win.copy()
                }
                ToolButton {
                    iconName: "share"
                    text: qsTr("Share")
                    onClicked: win.share()
                }
                ToolButton {
                    Layout.leftMargin: 6
                    primary: true
                    iconName: "save"
                    text: qsTr("Save")
                    onClicked: win.save(null)
                }
            }
        }

        // The shot, centred on the deeper ground.
        Rectangle {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: win.band(0.5, 0.035)
            clip: true
            focus: true

            Rectangle {
                id: card
                x: view.x
                y: view.y
                width: view.width
                height: view.height
                radius: UI.Style.radiusXs
                color: UI.Style.windowOpaque
                visible: false
            }

            MultiEffect {
                anchors.fill: card
                source: card
                shadowEnabled: true
                shadowColor: UI.Style.shadowWindow
                shadowBlur: 0.8
                shadowVerticalOffset: 8
                autoPaddingEnabled: true
            }

            Item {
                id: view
                readonly property real fit: Math.max(0.01, Math.min(1 / win.unit, (stage.width - 96) / win.region.width, (stage.height - 96) / win.region.height))
                x: Math.round((stage.width - width) / 2)
                y: Math.round((stage.height - height) / 2)
                width: Math.round(win.region.width * fit)
                height: Math.round(win.region.height * fit)
                clip: win.tool !== "crop"

                Item {
                    id: canvas
                    x: -win.region.x * view.fit
                    y: -win.region.y * view.fit
                    width: win.imageWidth
                    height: win.imageHeight
                    transform: Scale { xScale: view.fit; yScale: view.fit }

                    // The picture and its marks, at the picture's own size:
                    // what Copy and Save render.
                    Item {
                        id: paper
                        anchors.fill: parent

                        Image {
                            id: base
                            source: win.source
                            cache: false
                            smooth: true
                        }

                        Repeater {
                            model: win.drawing ? win.items.concat([win.drawing]) : win.items

                            delegate: Item {
                                id: mark
                                required property var modelData
                                width: paper.width
                                height: paper.height

                                Loader {
                                    anchors.fill: parent
                                    sourceComponent: mark.modelData.points ? strokeShape
                                        : mark.modelData.kind === "arrow" ? arrowShape
                                        : mark.modelData.kind === "box" ? boxShape
                                        : mark.modelData.kind === "text" ? textShape
                                        : mark.modelData.kind === "blur" ? blurShape : null
                                }

                                Component {
                                    id: strokeShape
                                    Shape {
                                        opacity: mark.modelData.kind === "highlighter" ? 0.4 : 1
                                        preferredRendererType: Shape.CurveRenderer
                                        ShapePath {
                                            strokeColor: mark.modelData.colour
                                            strokeWidth: mark.modelData.width
                                            fillColor: "transparent"
                                            capStyle: mark.modelData.kind === "highlighter" ? ShapePath.SquareCap : ShapePath.RoundCap
                                            joinStyle: ShapePath.RoundJoin
                                            PathPolyline { path: mark.modelData.points }
                                        }
                                    }
                                }

                                Component {
                                    id: arrowShape
                                    Shape {
                                        id: arrow
                                        readonly property var d: mark.modelData
                                        readonly property real angle: Math.atan2(d.to.y - d.from.y, d.to.x - d.from.x)
                                        readonly property real head: win.arrowHead(d)
                                        preferredRendererType: Shape.CurveRenderer
                                        ShapePath {
                                            strokeColor: arrow.d.colour
                                            strokeWidth: arrow.d.width
                                            fillColor: "transparent"
                                            capStyle: ShapePath.RoundCap
                                            joinStyle: ShapePath.RoundJoin
                                            PathSvg {
                                                path: {
                                                    const s = arrow;
                                                    const d = s.d;
                                                    const a1 = s.angle + Math.PI - 0.5;
                                                    const a2 = s.angle + Math.PI + 0.5;
                                                    return "M" + d.from.x + " " + d.from.y + "L" + d.to.x + " " + d.to.y
                                                        + "M" + (d.to.x + Math.cos(a1) * s.head) + " " + (d.to.y + Math.sin(a1) * s.head)
                                                        + "L" + d.to.x + " " + d.to.y
                                                        + "L" + (d.to.x + Math.cos(a2) * s.head) + " " + (d.to.y + Math.sin(a2) * s.head);
                                                }
                                            }
                                        }
                                    }
                                }

                                Component {
                                    id: boxShape
                                    Item {
                                        Rectangle {
                                            x: mark.modelData.x
                                            y: mark.modelData.y
                                            width: mark.modelData.w
                                            height: mark.modelData.h
                                            radius: 10 * win.unit
                                            color: "transparent"
                                            border.width: mark.modelData.width
                                            border.color: mark.modelData.colour
                                        }
                                    }
                                }

                                Component {
                                    id: textShape
                                    Item {
                                        Rectangle {
                                            x: mark.modelData.x
                                            y: mark.modelData.y
                                            width: mark.modelData.w
                                            height: mark.modelData.h
                                            radius: 6 * win.unit
                                            color: mark.modelData.colour

                                            Text {
                                                anchors.centerIn: parent
                                                text: mark.modelData.text
                                                color: win.textColour(mark.modelData.colour)
                                                font.family: UI.Style.fontFamily
                                                font.pixelSize: mark.modelData.size
                                                font.weight: UI.Style.weightBold
                                            }
                                        }
                                    }
                                }

                                // Blur: the picture under the box drawn from a
                                // texture a twelfth of its size, without
                                // smoothing, so it comes out in coarse blocks.
                                Component {
                                    id: blurShape
                                    Item {
                                        ShaderEffectSource {
                                            readonly property real block: 12 * win.unit
                                            x: mark.modelData.x
                                            y: mark.modelData.y
                                            width: mark.modelData.w
                                            height: mark.modelData.h
                                            sourceItem: base
                                            sourceRect: Qt.rect(mark.modelData.x, mark.modelData.y, mark.modelData.w, mark.modelData.h)
                                            textureSize: Qt.size(Math.max(1, Math.ceil(mark.modelData.w / block)), Math.max(1, Math.ceil(mark.modelData.h / block)))
                                            smooth: false
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // On top, and left out of Copy and Save: the crop being
                    // set, the selected mark's outline, text being typed and
                    // the pointer.
                    Item {
                        id: marks
                        anchors.fill: parent
                        readonly property real hairline: 1.5 / view.fit
                        readonly property var cropShown: win.cropDraft ?? win.cropRect

                        Item {
                            id: cropShade
                            anchors.fill: parent
                            visible: win.tool === "crop" && marks.cropShown !== null

                            readonly property var c: marks.cropShown ?? { x: 0, y: 0, width: 0, height: 0 }
                            readonly property color dim: Qt.rgba(0, 0, 0, 0.5)

                            Rectangle { width: cropShade.width; height: cropShade.c.y; color: cropShade.dim }
                            Rectangle { y: cropShade.c.y; width: cropShade.c.x; height: cropShade.c.height; color: cropShade.dim }
                            Rectangle { x: cropShade.c.x + cropShade.c.width; y: cropShade.c.y; width: cropShade.width - x; height: cropShade.c.height; color: cropShade.dim }
                            Rectangle { y: cropShade.c.y + cropShade.c.height; width: cropShade.width; height: cropShade.height - y; color: cropShade.dim }
                            Rectangle {
                                x: cropShade.c.x
                                y: cropShade.c.y
                                width: cropShade.c.width
                                height: cropShade.c.height
                                color: "transparent"
                                border.width: marks.hairline
                                border.color: UI.Style.inkOnWallpaper
                            }
                        }

                        Rectangle {
                            readonly property var b: win.tool === "select" && win.selectedIndex >= 0 && win.selectedIndex < win.items.length
                                ? win.bounds(win.items[win.selectedIndex]) : null
                            visible: b !== null
                            x: b ? b.x - 4 * marks.hairline : 0
                            y: b ? b.y - 4 * marks.hairline : 0
                            width: b ? b.w + 8 * marks.hairline : 0
                            height: b ? b.h + 8 * marks.hairline : 0
                            radius: 4 * marks.hairline
                            color: "transparent"
                            border.width: marks.hairline
                            border.color: UI.Style.accent
                        }

                        MouseArea {
                            id: pointer
                            anchors.fill: parent
                            property point start
                            property var before: null
                            cursorShape: win.tool === "select" ? Qt.ArrowCursor : win.tool === "text" ? Qt.IBeamCursor : Qt.CrossCursor

                            onPressed: mouse => {
                                const p = Qt.point(mouse.x, mouse.y);
                                start = p;
                                if (win.typing) {
                                    win.finishText();
                                    if (win.tool === "text") {
                                        return;
                                    }
                                }
                                stage.forceActiveFocus();
                                switch (win.tool) {
                                case "select": {
                                    let found = -1;
                                    for (let i = win.items.length - 1; i >= 0; i--) {
                                        if (win.hit(win.items[i], p)) {
                                            found = i;
                                            break;
                                        }
                                    }
                                    win.selectedIndex = found;
                                    before = found >= 0 ? win.items : null;
                                    break;
                                }
                                case "pen":
                                case "highlighter":
                                    win.drawing = { kind: win.tool, colour: win.ink, width: (win.tool === "highlighter" ? 16 : 4) * win.unit, points: [p] };
                                    break;
                                case "arrow":
                                    win.drawing = { kind: "arrow", colour: win.ink, width: 5 * win.unit, from: p, to: p };
                                    break;
                                case "box":
                                    win.drawing = { kind: "box", colour: win.ink, width: 4 * win.unit, x: p.x, y: p.y, w: 0, h: 0 };
                                    break;
                                case "blur":
                                    win.drawing = { kind: "blur", x: p.x, y: p.y, w: 0, h: 0 };
                                    break;
                                case "text":
                                    win.startText(p);
                                    break;
                                case "crop":
                                    win.cropDraft = { x: p.x, y: p.y, width: 0, height: 0 };
                                    break;
                                }
                            }
                            onPositionChanged: mouse => {
                                const x = Math.max(0, Math.min(win.imageWidth, mouse.x));
                                const y = Math.max(0, Math.min(win.imageHeight, mouse.y));
                                if (win.tool === "select") {
                                    if (before) {
                                        const list = before.slice();
                                        list[win.selectedIndex] = win.moved(before[win.selectedIndex], mouse.x - start.x, mouse.y - start.y);
                                        win.items = list;
                                    }
                                    return;
                                }
                                if (win.tool === "crop" && win.cropDraft) {
                                    const r = win.normalised(start.x, start.y, x, y);
                                    win.cropDraft = { x: r.x, y: r.y, width: r.w, height: r.h };
                                    return;
                                }
                                const d = win.drawing;
                                if (!d) {
                                    return;
                                }
                                if (d.points) {
                                    const end = d.points[d.points.length - 1];
                                    if (Math.abs(end.x - mouse.x) + Math.abs(end.y - mouse.y) >= win.unit) {
                                        win.drawing = Object.assign({}, d, { points: d.points.concat([Qt.point(mouse.x, mouse.y)]) });
                                    }
                                } else if (d.kind === "arrow") {
                                    win.drawing = Object.assign({}, d, { to: Qt.point(mouse.x, mouse.y) });
                                } else {
                                    const r = win.normalised(start.x, start.y, x, y);
                                    win.drawing = Object.assign({}, d, r);
                                }
                            }
                            onReleased: {
                                if (win.tool === "select") {
                                    if (before && win.items !== before) {
                                        const now = win.items;
                                        win.items = before;
                                        win.commit(now, win.cropRect);
                                    }
                                    before = null;
                                    return;
                                }
                                if (win.tool === "crop") {
                                    const c = win.cropDraft;
                                    win.cropDraft = null;
                                    if (c && c.width >= 8 && c.height >= 8) {
                                        win.commit(win.items, c);
                                    }
                                    return;
                                }
                                const d = win.drawing;
                                win.drawing = null;
                                if (!d) {
                                    return;
                                }
                                let keep = true;
                                if (d.points) {
                                    if (d.points.length === 1) {
                                        // A click leaves a dot.
                                        d.points = [d.points[0], Qt.point(d.points[0].x + 0.5, d.points[0].y)];
                                    }
                                } else if (d.kind === "arrow") {
                                    keep = Math.hypot(d.to.x - d.from.x, d.to.y - d.from.y) > 4 * win.unit;
                                } else {
                                    keep = d.w > 3 * win.unit && d.h > 3 * win.unit;
                                }
                                if (keep) {
                                    win.commit(win.items.concat([d]), win.cropRect);
                                }
                            }
                        }

                        // Text being typed, on its colour, where it was
                        // clicked; Enter or a click elsewhere places it.
                        Rectangle {
                            visible: win.typing
                            x: win.textAt.x
                            y: win.textAt.y
                            width: Math.max(40 * win.unit, textInput.contentWidth + 24 * win.unit)
                            height: textInput.contentHeight + 12 * win.unit
                            radius: 6 * win.unit
                            color: win.ink

                            TextInput {
                                id: textInput
                                anchors.centerIn: parent
                                color: win.textColour(win.ink)
                                font.family: UI.Style.fontFamily
                                font.pixelSize: 15 * win.unit
                                font.weight: UI.Style.weightBold
                                selectionColor: UI.Style.textSelection
                                cursorVisible: win.typing
                                Accessible.name: qsTr("Text to add")
                                onAccepted: win.finishText()
                                // The text menu holds focus while it is open; the text is
                                // still being typed.
                                onActiveFocusChanged: if (!activeFocus && win.typing && !textMenu.opened) win.finishText()
                                Keys.onPressed: event => {
                                    if (textMenu.isMenuKey(event)) {
                                        textMenu.openAtCaret();
                                        event.accepted = true;
                                    }
                                }

                                UI.TextMenu {
                                    id: textMenu
                                    onFocusLeft: win.finishText()
                                }
                            }
                        }
                    }
                }
            }
        }

        // The foot: the picture's size and what just happened on the left,
        // where Save writes on the right.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: win.band(0.3, 0.02)
            bottomLeftRadius: win.maximised ? 0 : UI.Style.windowRadius - 1
            bottomRightRadius: bottomLeftRadius

            Rectangle {
                width: parent.width
                height: 1
                color: UI.Style.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 18

                UI.Label {
                    size: 12
                    color: UI.Style.inkMuted
                    font.features: { "tnum": 1 }
                    text: Math.round(win.cropRect ? win.cropRect.width : win.imageWidth) + " × "
                        + Math.round(win.cropRect ? win.cropRect.height : win.imageHeight)
                }
                UI.Label {
                    size: 12
                    color: UI.Style.inkMuted
                    text: win.status
                }
                Item { Layout.fillWidth: true }
                UI.Label {
                    size: 12
                    color: UI.Style.inkMuted
                    text: win.scanPage ? qsTr("Save puts it back in the scan") : qsTr("Saves to %1").arg(win.placeName)
                }
            }
        }
    }

    UI.Dialog {
        id: unsavedDialog
        title: win.saved ? qsTr("Save your changes?") : qsTr("Save this screenshot?")
        text: win.saved ? qsTr("Your marks are lost if you don't save them.") : qsTr("It's still on the clipboard if you don't.")
        buttons: [
            UI.Button {
                text: qsTr("Don't save")
                onClicked: {
                    unsavedDialog.close();
                    win.closeNow();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: unsavedDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Save")
                onClicked: {
                    unsavedDialog.close();
                    win.save(() => win.closeNow());
                }
            }
        ]
    }

    Component.onCompleted: stage.forceActiveFocus()
}
