pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import QtQuick.Window
import WildOS.UI as UI
import org.wildos.overlays

// One screen frozen under a dimmed layer (AppCapture.dc). Area: drag one
// out, then move it or pull its handles, and Enter or Capture takes it;
// window: the window under the pointer lights up, click it; full screen:
// click anywhere; freeform: draw round what you want. What is picked shows
// at full brightness inside a white edge, with its size in pixels under
// its bottom left corner. Escape or the cross leaves without a capture.
//
// The frozen screen and the dim behind it read as a photograph in either
// scheme, so the edge, handles and labels on it keep the wallpaper tokens.
Window {
    id: overlay

    required property var screenInfo
    readonly property string screenName: screenInfo ? screenInfo.name : ""
    readonly property bool primary: screenName === CaptureApp.primaryScreen
    readonly property real pixelRatio: screenInfo ? screenInfo.devicePixelRatio : 1
    readonly property string mode: CaptureApp.mode
    // The board's dim: plain black, so the frozen screen keeps its colours.
    readonly property color dim: Qt.rgba(0, 0, 0, 0.5)

    property var hoveredWindow: null
    // While a drag is under way: "new", "move" or "resize".
    property string dragKind: ""
    property rect selection: Qt.rect(0, 0, 0, 0)
    property var freehandPoints: []
    property point pointer: Qt.point(-1, -1)
    property bool spaceHeld: false

    // The area picked on this screen: the one being dragged, or the one the
    // app holds when it is on this screen.
    readonly property rect area: dragKind.length > 0 ? selection
        : CaptureApp.pickedScreen === screenName ? CaptureApp.pickedArea : Qt.rect(0, 0, 0, 0)

    // The screen, and its position: on first show Qt moves a window to the
    // screen that holds its position, so at 0,0 every screen's overlay went
    // to the first screen. Given once the window exists rather than bound,
    // as Qt can reset a bound screen while the window is being made.
    Component.onCompleted: screen = screenInfo
    x: screenInfo ? screenInfo.virtualX : 0
    y: screenInfo ? screenInfo.virtualY : 0
    width: screenInfo ? screenInfo.width : 0
    height: screenInfo ? screenInfo.height : 0
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: UI.Style.windowOpaque
    visible: false
    title: qsTr("Capture")

    OverlayPlacement {
        id: placement
        window: overlay
        edges: OverlayPlacement.Top | OverlayPlacement.Bottom | OverlayPlacement.Left | OverlayPlacement.Right
        layer: OverlayPlacement.Overlay
        keyboard: overlay.primary ? OverlayPlacement.Exclusive : OverlayPlacement.OnDemand
        scope: "wildos-capture"
    }

    Connections {
        target: CaptureApp
        function onSelectingChanged() {
            if (CaptureApp.selecting) {
                overlay.reset();
                // Under X11 only full screen gets past the taskbar's reserved
                // strip; on Wayland the layer surface covers the output.
                if (placement.wayland) {
                    overlay.visible = true;
                } else {
                    overlay.showFullScreen();
                }
                if (overlay.primary) {
                    overlay.requestActivate();
                    surface.forceActiveFocus();
                }
            } else {
                overlay.visible = false;
            }
        }
    }

    function reset(): void {
        selection = Qt.rect(0, 0, 0, 0);
        dragKind = "";
        freehandPoints = [];
        hoveredWindow = null;
        pointer = Qt.point(-1, -1);
        spaceHeld = false;
    }

    function windowAt(x: real, y: real): var {
        const list = CaptureApp.windows;
        const gx = x + screenInfo.virtualX;
        const gy = y + screenInfo.virtualY;
        for (let i = list.length - 1; i >= 0; i--) {
            const w = list[i];
            if (gx >= w.x && gx < w.x + w.width && gy >= w.y && gy < w.y + w.height) {
                return w;
            }
        }
        return null;
    }

    // Which handle of the picked area is at a point: "l", "tr" and so on,
    // "inside", or "".
    function zoneAt(x: real, y: real): string {
        const a = area;
        if (a.width <= 0 || a.height <= 0) {
            return "";
        }
        const reach = 8;
        const nearL = Math.abs(x - a.x) <= reach;
        const nearR = Math.abs(x - (a.x + a.width)) <= reach;
        const nearT = Math.abs(y - a.y) <= reach;
        const nearB = Math.abs(y - (a.y + a.height)) <= reach;
        const midX = Math.abs(x - (a.x + a.width / 2)) <= reach;
        const midY = Math.abs(y - (a.y + a.height / 2)) <= reach;
        if (nearT && nearL) return "tl";
        if (nearT && nearR) return "tr";
        if (nearB && nearL) return "bl";
        if (nearB && nearR) return "br";
        if (nearT && midX) return "t";
        if (nearB && midX) return "b";
        if (nearL && midY) return "l";
        if (nearR && midY) return "r";
        if (x > a.x && x < a.x + a.width && y > a.y && y < a.y + a.height) return "inside";
        return "";
    }

    function cursorFor(zone: string): int {
        switch (zone) {
        case "tl": case "br": return Qt.SizeFDiagCursor;
        case "tr": case "bl": return Qt.SizeBDiagCursor;
        case "t": case "b": return Qt.SizeVerCursor;
        case "l": case "r": return Qt.SizeHorCursor;
        case "inside": return Qt.SizeAllCursor;
        }
        return Qt.CrossCursor;
    }

    // What is lit: the area, the hovered window or the whole screen.
    readonly property rect lit: {
        if (mode === "screen") {
            return pointer.x >= 0 ? Qt.rect(0, 0, width, height) : Qt.rect(0, 0, 0, 0);
        }
        if (mode === "window" && hoveredWindow && dragKind.length === 0) {
            const w = hoveredWindow;
            const x = Math.max(0, w.x - screenInfo.virtualX);
            const y = Math.max(0, w.y - screenInfo.virtualY);
            const r = Math.min(width, w.x - screenInfo.virtualX + w.width);
            const b = Math.min(height, w.y - screenInfo.virtualY + w.height);
            return Qt.rect(x, y, Math.max(0, r - x), Math.max(0, b - y));
        }
        if (mode === "rectangle" || mode === "window") {
            return area;
        }
        return Qt.rect(0, 0, 0, 0);
    }
    readonly property bool hasLit: lit.width > 0 && lit.height > 0
    readonly property bool showHandles: mode === "rectangle" && hasLit

    Item {
        id: surface
        anchors.fill: parent
        focus: true

        Keys.onEscapePressed: CaptureApp.cancel()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                CaptureApp.capture(overlay.screenName);
                event.accepted = true;
            } else if (event.key === Qt.Key_Space) {
                overlay.spaceHeld = true;
                event.accepted = true;
            }
        }
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                overlay.spaceHeld = false;
                event.accepted = true;
            }
        }

        Image {
            id: frozen
            anchors.fill: parent
            source: overlay.visible ? "image://frozen/" + overlay.screenName + "/" + CaptureApp.serial : ""
            cache: false
            smooth: true
            fillMode: Image.Stretch
        }

        // Dim everything but what is lit, in four bands round it, so the
        // picked part shows the frozen screen itself.
        Item {
            anchors.fill: parent
            visible: overlay.mode !== "freehand"

            Rectangle {
                width: parent.width
                height: overlay.hasLit ? overlay.lit.y : parent.height
                color: overlay.dim
            }
            Rectangle {
                visible: overlay.hasLit
                y: overlay.lit.y
                width: overlay.lit.x
                height: overlay.lit.height
                color: overlay.dim
            }
            Rectangle {
                visible: overlay.hasLit
                x: overlay.lit.x + overlay.lit.width
                y: overlay.lit.y
                width: parent.width - x
                height: overlay.lit.height
                color: overlay.dim
            }
            Rectangle {
                visible: overlay.hasLit
                y: overlay.lit.y + overlay.lit.height
                width: parent.width
                height: parent.height - y
                color: overlay.dim
            }
        }

        // The edge: 1.5px white, with a faint dark hairline outside it so it
        // reads on white windows too.
        Rectangle {
            visible: overlay.hasLit && overlay.mode !== "screen"
            x: overlay.lit.x - 2.5
            y: overlay.lit.y - 2.5
            width: overlay.lit.width + 5
            height: overlay.lit.height + 5
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.3)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: "transparent"
                border.width: 1.5
                border.color: UI.Style.inkOnWallpaper
            }
        }

        Repeater {
            model: overlay.showHandles ? [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]] : []

            delegate: Rectangle {
                required property var modelData
                x: overlay.lit.x + overlay.lit.width * modelData[0] - 6
                y: overlay.lit.y + overlay.lit.height * modelData[1] - 6
                width: 12
                height: 12
                radius: 6
                color: Qt.rgba(0, 0, 0, 0.35)

                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    radius: 5
                    color: UI.Style.inkOnWallpaper
                }
            }
        }

        // Freeform: the drawn shape lit through a mask, with a white line.
        Item {
            anchors.fill: parent
            visible: overlay.mode === "freehand"

            Rectangle {
                anchors.fill: parent
                color: overlay.dim
            }

            Item {
                anchors.fill: parent
                visible: overlay.freehandPoints.length > 2

                Image {
                    id: litCopy
                    anchors.fill: parent
                    source: frozen.source
                    cache: false
                    visible: false
                }

                Shape {
                    id: freehandMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    ShapePath {
                        strokeColor: "transparent"
                        fillColor: UI.Style.inkOnWallpaper
                        PathPolyline { path: overlay.freehandPoints }
                    }
                }

                MultiEffect {
                    anchors.fill: parent
                    source: litCopy
                    maskEnabled: true
                    maskSource: freehandMask
                }
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: UI.Style.inkOnWallpaper
                    strokeWidth: 1.5
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    PathPolyline { path: overlay.freehandPoints }
                }
            }
        }

        // The size of what you get, in pixels, under the bottom left corner.
        Rectangle {
            visible: overlay.hasLit && overlay.mode !== "screen"
            x: Math.max(0, Math.min(overlay.lit.x, overlay.width - width - 8))
            y: overlay.lit.y + overlay.lit.height + 10 + height < overlay.height
                ? overlay.lit.y + overlay.lit.height + 10 : overlay.lit.y + overlay.lit.height - height - 10
            width: sizeLabel.implicitWidth + 16
            height: sizeLabel.implicitHeight + 8
            radius: UI.Style.radiusXs
            color: UI.Style.glassOnWallpaper

            UI.Label {
                id: sizeLabel
                anchors.centerIn: parent
                size: 12
                color: UI.Style.inkOnWallpaper
                font.features: { "tnum": 1 }
                text: Math.round(overlay.lit.width * overlay.pixelRatio) + " × " + Math.round(overlay.lit.height * overlay.pixelRatio)
            }
        }

        MouseArea {
            id: picker
            anchors.fill: parent
            hoverEnabled: true
            property point origin
            property point last
            property rect start
            property string zone
            property string hoverZone

            cursorShape: overlay.mode === "window" || overlay.mode === "screen" ? Qt.PointingHandCursor
                : overlay.mode === "rectangle" ? overlay.cursorFor(pressed ? zone : hoverZone) : Qt.CrossCursor

            function clamp(r: rect): rect {
                const x = Math.max(0, Math.min(r.x, overlay.width - r.width));
                const y = Math.max(0, Math.min(r.y, overlay.height - r.height));
                return Qt.rect(x, y, r.width, r.height);
            }

            onPositionChanged: mouse => {
                overlay.pointer = Qt.point(mouse.x, mouse.y);
                if (!pressed) {
                    hoverZone = overlay.mode === "rectangle" ? overlay.zoneAt(mouse.x, mouse.y) : "";
                    if (overlay.mode === "window") {
                        const w = overlay.windowAt(mouse.x, mouse.y);
                        if (w !== overlay.hoveredWindow) {
                            overlay.hoveredWindow = w;
                            if (w) {
                                CaptureApp.pickWindow(w);
                            }
                        }
                    }
                    return;
                }
                if (overlay.mode === "freehand") {
                    const points = overlay.freehandPoints;
                    const end = points[points.length - 1];
                    if (!end || Math.abs(end.x - mouse.x) + Math.abs(end.y - mouse.y) >= 2) {
                        overlay.freehandPoints = points.concat([Qt.point(mouse.x, mouse.y)]);
                    }
                    return;
                }
                const dx = mouse.x - last.x;
                const dy = mouse.y - last.y;
                last = Qt.point(mouse.x, mouse.y);
                if (overlay.mode === "window" && overlay.dragKind.length === 0) {
                    // A drag in window mode, as in Snipping Tool, picks an
                    // area instead.
                    if (Math.abs(mouse.x - origin.x) + Math.abs(mouse.y - origin.y) < 6) {
                        return;
                    }
                    overlay.dragKind = "new";
                    overlay.hoveredWindow = null;
                }
                if (overlay.dragKind === "new") {
                    // Space held moves the area being drawn instead of
                    // growing it.
                    if (overlay.spaceHeld) {
                        origin = Qt.point(origin.x + dx, origin.y + dy);
                    }
                    overlay.selection = Qt.rect(Math.min(origin.x, mouse.x), Math.min(origin.y, mouse.y),
                                                Math.abs(mouse.x - origin.x), Math.abs(mouse.y - origin.y));
                } else if (overlay.dragKind === "move") {
                    overlay.selection = clamp(Qt.rect(start.x + mouse.x - origin.x, start.y + mouse.y - origin.y, start.width, start.height));
                } else if (overlay.dragKind === "resize") {
                    let l = start.x, t = start.y, r = start.x + start.width, b = start.y + start.height;
                    const mx = Math.max(0, Math.min(overlay.width, mouse.x));
                    const my = Math.max(0, Math.min(overlay.height, mouse.y));
                    if (zone.indexOf("l") >= 0) l = mx;
                    if (zone.indexOf("r") >= 0) r = mx;
                    if (zone.indexOf("t") >= 0) t = my;
                    if (zone.indexOf("b") >= 0) b = my;
                    overlay.selection = Qt.rect(Math.min(l, r), Math.min(t, b), Math.abs(r - l), Math.abs(b - t));
                }
            }
            onExited: {
                overlay.pointer = Qt.point(-1, -1);
                hoverZone = "";
            }
            onPressed: mouse => {
                origin = Qt.point(mouse.x, mouse.y);
                last = origin;
                overlay.dragKind = "";
                if (overlay.mode === "freehand") {
                    overlay.freehandPoints = [Qt.point(mouse.x, mouse.y)];
                    return;
                }
                if (overlay.mode !== "rectangle") {
                    return;
                }
                zone = overlay.zoneAt(mouse.x, mouse.y);
                start = overlay.area;
                if (zone === "inside") {
                    overlay.selection = start;
                    overlay.dragKind = "move";
                } else if (zone.length > 0) {
                    overlay.selection = start;
                    overlay.dragKind = "resize";
                } else {
                    overlay.selection = Qt.rect(mouse.x, mouse.y, 0, 0);
                    overlay.dragKind = "new";
                }
            }
            onReleased: mouse => {
                if (overlay.mode === "freehand") {
                    if (overlay.freehandPoints.length > 2) {
                        CaptureApp.takeFreehand(overlay.screenName, overlay.freehandPoints);
                    }
                    return;
                }
                if (overlay.mode === "screen") {
                    CaptureApp.takeScreen(overlay.screenName);
                    return;
                }
                const kind = overlay.dragKind;
                const r = overlay.selection;
                overlay.dragKind = "";
                if (overlay.mode === "window" && kind.length === 0) {
                    const w = overlay.windowAt(mouse.x, mouse.y);
                    if (w) {
                        CaptureApp.takeWindow(w.id, Qt.rect(w.x, w.y, w.width, w.height));
                    }
                    return;
                }
                if (r.width >= 4 && r.height >= 4) {
                    // A drag in window mode takes its area straight away;
                    // an area waits for Enter or Capture.
                    if (overlay.mode === "window") {
                        CaptureApp.takeArea(overlay.screenName, r);
                    } else {
                        CaptureApp.pickArea(overlay.screenName, r);
                    }
                } else if (kind === "new") {
                    CaptureApp.pickArea("", Qt.rect(0, 0, 0, 0));
                }
                hoverZone = overlay.zoneAt(mouse.x, mouse.y);
            }
            onDoubleClicked: mouse => {
                if (overlay.mode === "rectangle" && overlay.zoneAt(mouse.x, mouse.y) === "inside") {
                    CaptureApp.capture(overlay.screenName);
                }
            }
        }

        // Window mode has nothing to light when KWin could not list the
        // windows; say so rather than leave clicks doing nothing.
        readonly property string hint: {
            if (overlay.mode === "window" && CaptureApp.windows.length === 0) {
                return qsTr("No windows to pick here. Drag over an area instead.");
            }
            const verb = CaptureApp.recordMode ? qsTr("record") : qsTr("capture");
            switch (overlay.mode) {
            case "window":
                return CaptureApp.recordMode ? qsTr("Click a window to record it · Esc to cancel") : qsTr("Click a window to capture it · Esc to cancel");
            case "screen":
                return CaptureApp.recordMode ? qsTr("Click or press Enter to record the screen · Esc to cancel")
                    : qsTr("Click or press Enter to capture the screen · Esc to cancel");
            case "freehand":
                return qsTr("Draw round what you want · Esc to cancel");
            }
            return qsTr("Drag to pick an area · Space to move it · Enter to %1 · Esc to cancel").arg(verb);
        }

        Toolbar {
            id: toolbar
            visible: overlay.primary
            keyTarget: surface
            anchors.horizontalCenter: parent.horizontalCenter
            y: 24
        }

        Rectangle {
            visible: overlay.primary && overlay.dragKind.length === 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: toolbar.y + toolbar.height + 6
            width: hintLabel.implicitWidth + 24
            height: hintLabel.implicitHeight + 12
            radius: UI.Style.radiusSm
            color: UI.Style.glassOnWallpaper

            UI.Label {
                id: hintLabel
                anchors.centerIn: parent
                size: 12
                color: UI.Style.inkOnWallpaperMuted
                text: surface.hint
            }
        }
    }
}
