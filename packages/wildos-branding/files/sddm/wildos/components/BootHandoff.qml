import QtQuick
import QtQuick.Shapes

// The end of the start-up animation (BootIntro.dc.html steps 5 and 6, or
// AfterUnlockD.dc.html after an encrypted disk was unlocked). Plymouth quits
// with --retain-splash and leaves a fixed frame of its loading loop on screen:
// the W as glass with the light at the start of its outline, the spark at
// full strength and "Starting securely". This opens on that exact frame,
// drawn from Plymouth's own images at the same scale and positions (see
// plymouth/wildos/wildos.script), then plays the rest: one last full trace,
// the W fills back to colour and the spark flashes; the wallpaper shows
// through the W, the screen flies through it and the clock fades in. The
// screen underneath is usable from the first frame: typing or clicking skips
// straight to the end.
//
// It only plays straight after boot. The boot-handoff helper that quits
// Plymouth leaves a marker in /run for a minute, one for the sign-in screen
// and one for the session splash when the system signs in automatically, and
// a second one when a disk was unlocked, which moves the W to where the
// unlock screen's glide left it.
Item {
    id: handoff

    property url marker: "file:///run/wildos/boot-handoff-greeter.png"
    property url unlockedMarker: "file:///run/wildos/boot-handoff-unlocked.png"
    property url photoSource
    // Kept for the screens that pass them; the photo here is always its own.
    property Item photoItem: null
    // Vertical focus of the cover-cropped photo: 0.45 on the sign-in screen,
    // 0.5 where the desktop wallpaper takes over.
    property real photoFocus: 0.45
    // Fades with the sign-in controls, so the photo ends as the lock screen.
    property bool scrim: true
    // Typing into these skips to the end.
    property Item input
    property Item secondInput
    // Plays the boards' timeline this much faster.
    property real speed: 1.15
    // Set by the session splash once Plasma has drawn the desktop: the W then
    // opens onto the real desktop, taskbar and windows included, instead of
    // the photo, so nothing is left to fade in when the flight ends.
    property bool live: false
    // Ends as the flight lands instead of after the sign-in screen's clock
    // fades in: the desktop has no clock to wait for.
    property bool endOnLanding: false

    readonly property bool active: markerImage.status === Image.Ready && !done
    readonly property bool afterUnlock: unlockedImage.status === Image.Ready
    readonly property var k: afterUnlock ? unlockTimes : introTimes

    // For the screen beneath: the clock appears as the flight ends, the
    // sign-in controls once it has settled.
    readonly property real stageOpacity: active ? Math.max(skip, segment(d, k.clock, k.clockEnd)) : 1
    readonly property real clockShift: active ? Math.round(16 * (1 - Math.max(skip, segment(d, k.clock, k.clockEnd)))) : 0
    readonly property real uiOpacity: active ? Math.max(skip, segment(d, k.clockEnd - 0.3, k.clockEnd + 0.3)) : 1

    signal finished()

    function finish() {
        if (!active || skipping.running)
            return;
        skipping.start();
    }

    // Each artboard's keyframes, in seconds of its own loop (BootIntro's
    // 14 s, AfterUnlockD's 10 s), from the moment the final trace starts.
    readonly property var introTimes: ({
        start: 8.33, lap: 1.75, lapFrames: 88, background: "#000000",
        lightOff: [8.33, 8.61], trace: [8.33, 9.24], traceOff: [9.24, 9.8],
        baseOff: [8.54, 9.1], fillUp: [8.47, 9.24], fillOff: [9.8, 10.3],
        statusOff: [8.4, 8.96], pulse: [8.68, 9.31, 9.66], sparkOff: [9.66, 10.22],
        flash: [9.1, 9.38, 10.22], cover: 9.8, zoom: [10.3, 11.76], photo: [9.8, 12.04],
        clock: 11.76, clockEnd: 12.6
    })
    readonly property var unlockTimes: ({
        start: 5.1, lap: 1.6, lapFrames: 80, background: "#000000",
        lightOff: [5.1, 5.4], trace: [5.1, 5.9], traceOff: [5.9, 6.4],
        baseOff: [5.4, 6.0], fillUp: [5.4, 6.1], fillOff: [6.4, 6.9],
        statusOff: [5.2, 5.7], pulse: [5.6, 5.9, 6.2], sparkOff: [6.2, 6.7],
        flash: [5.7, 6.0, 6.8], cover: 6.2, zoom: [6.8, 8.2], photo: [6.2, 8.4],
        clock: 8.2, clockEnd: 8.9
    })

    // Where the mark's units land, in artboard pixels from the screen centre
    // (render-boot-assets.py: POSE_I, POSE_U), and the canvases drawn there.
    readonly property var pose: afterUnlock ? { s: 1.1287, x: 615.22 - 720, y: 367.04 - 450 }
                                            : { s: 1.6, x: -120 - 18 * 1.6, y: -220 + 10.5 * 1.6 }
    readonly property string g: afterUnlock ? "u" : "i"
    readonly property var glass: afterUnlock ? { x: -100, y: -85 } : { x: -135, y: -200 }

    property real d: 0
    property real skip: 0
    property bool done: false
    // Decode the photo at the size it is shown, grown 1.18 times at most,
    // rather than at its full 4K: decoding it twice at full size held up the
    // first frame. Wild OS photos are 16:9.
    readonly property int photoDecodeWidth: Math.ceil(1.18 * Math.max(screenW, screenH * 16 / 9))
    readonly property string assets: "file:///usr/share/plymouth/themes/wildos/"
    readonly property string handoffAssets: "file:///usr/share/wildos/boot-handoff/"

    // CSS cubic-bezier, solved as wildos.script does; (.4,0,.2,1) is the
    // design's timing for every keyframe without its own.
    function bezier(u, x1, y1, x2, y2) {
        if (u <= 0)
            return 0;
        if (u >= 1)
            return 1;
        let lo = 0, hi = 1;
        for (let n = 0; n < 24; n++) {
            const m = (lo + hi) / 2;
            const x = 3 * (1 - m) * (1 - m) * m * x1 + 3 * (1 - m) * m * m * x2 + m * m * m;
            if (x < u)
                lo = m;
            else
                hi = m;
        }
        const m = (lo + hi) / 2;
        return 3 * (1 - m) * (1 - m) * m * y1 + 3 * (1 - m) * m * m * y2 + m * m * m;
    }
    function ease(u) {
        return bezier(u, 0.4, 0, 0.2, 1);
    }
    function segment(t, t0, t1) {
        return ease((t - t0) / (t1 - t0));
    }
    function between(t, span) {
        return segment(t, span[0], span[1]);
    }

    // Plymouth's scale choice and rounding, in device pixels.
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property int screenW: Math.round(width * dpr)
    readonly property int screenH: Math.round(height * dpr)
    readonly property var scales: [0.8, 1.0, 1.2, 1.6, 2.0, 2.4]
    readonly property real s: {
        let want = screenH / 900;
        if (screenW < 1200 * want)
            want = screenW / 1200;
        let pick = 0;
        for (let i = 1; i < scales.length; i++) {
            if (Math.abs(scales[i] - want) < Math.abs(scales[pick] - want))
                pick = i;
        }
        return scales[pick];
    }
    readonly property string folder: "s" + String(Math.round(s * 100)).padStart(3, "0") + "/"
    readonly property string dir: assets + folder
    readonly property int cx: Math.trunc(screenW / 2)
    readonly property int cy: Math.trunc(screenH / 2)
    function px(v) {
        return v < 0 ? -Math.trunc(-v * s + 0.5) : Math.trunc(v * s + 0.5);
    }
    // A point in the mark's units, in scene units.
    function markX(x) {
        return (cx + (pose.x + pose.s * x) * s) / dpr;
    }
    function markY(y) {
        return (cy + (pose.y + pose.s * y) * s) / dpr;
    }

    visible: active || skipping.running
    opacity: 1 - skip

    Image {
        id: markerImage
        visible: false
        asynchronous: false
        cache: false
        source: handoff.marker
    }

    Image {
        id: unlockedImage
        visible: false
        asynchronous: false
        cache: false
        source: handoff.unlockedMarker
    }

    Rectangle {
        anchors.fill: parent
        color: handoff.k.background
        visible: !(handoff.live && handoff.d >= handoff.k.cover)
    }

    // The wallpaper, there from the moment the cover goes up, settling from
    // 1.18 to its place as the screen flies through the W. When the desktop
    // is ready before the cover goes up it never shows; when it is ready
    // later, it gives way to the desktop part way through.
    Item {
        id: photoLayer
        anchors.fill: parent
        visible: handoff.d >= handoff.k.cover && opacity > 0
        opacity: handoff.live ? 0 : 1
        Behavior on opacity {
            NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
        }

        Image {
            id: photo

            readonly property real coverScale: implicitWidth > 0
                ? Math.max(photoLayer.width / implicitWidth, photoLayer.height / implicitHeight) : 1
            readonly property real grow: 1.18 - 0.18 * handoff.bezier(
                (handoff.d - handoff.k.photo[0]) / (handoff.k.photo[1] - handoff.k.photo[0]), 0.3, 0, 0.2, 1)

            // Framed as the sign-in screen frames the wallpaper, so the last
            // frame here is the first frame there.
            width: implicitWidth * coverScale
            height: implicitHeight * coverScale
            x: (photoLayer.width - width) / 2
            y: (photoLayer.height - height) * handoff.photoFocus
            source: handoff.active ? handoff.photoSource : ""
            sourceSize.width: handoff.photoDecodeWidth
            // Loaded before the first frame; until then the screen still
            // shows Plymouth's identical last frame. Cached, so the screen
            // around this shares the one decode.
            asynchronous: false
            cache: true
            smooth: true
            mipmap: true
            transform: Scale {
                origin.x: photoLayer.width / 2 - photo.x
                origin.y: photoLayer.height * (handoff.afterUnlock ? 440 / 900 : 340 / 900) - photo.y
                xScale: photo.grow
                yScale: photo.grow
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: handoff.scrim
            color: Qt.rgba(15 / 255, 17 / 255, 21 / 255, 0.22 * handoff.uiOpacity)
        }
    }

    // The cover with a W-shaped hole: the wallpaper shows through the W, then
    // the hole grows past the edges of the screen.
    Shape {
        id: cover
        anchors.fill: parent
        visible: handoff.d >= handoff.k.cover && zoomFactor < 120
        preferredRendererType: Shape.CurveRenderer

        readonly property real zoomFactor: 1 + 139 * handoff.bezier(
            (handoff.d - handoff.k.zoom[0]) / (handoff.k.zoom[1] - handoff.k.zoom[0]), 0.55, 0, 0.75, 0.2)

        transform: [
            Scale { xScale: handoff.pose.s * handoff.s / handoff.dpr; yScale: xScale },
            Translate { x: handoff.markX(0); y: handoff.markY(0) },
            Scale {
                origin.x: handoff.markX(92.5)
                origin.y: handoff.markY(42)
                xScale: cover.zoomFactor
                yScale: cover.zoomFactor
            }
        ]

        ShapePath {
            fillColor: handoff.k.background
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            PathSvg {
                path: "M-3000 -3000 H4000 V4000 H-3000Z M37 17 L50.34 40 L64.75 64.84 L79.16 40 L92.5 17 L105.84 40 L134.26 89 L120.76 112.28 L92.5 63.55 L64.91 111.12 L23.66 40Z"
            }
        }
    }

    // Everything below is placed in device pixels exactly as Plymouth placed
    // it, then divided back to scene units.
    component Placed: Image {
        property real ax
        property real ay
        asynchronous: false
        smooth: false
        x: (handoff.cx + handoff.px(ax)) / handoff.dpr
        y: (handoff.cy + handoff.px(ay)) / handoff.dpr
        width: implicitWidth / handoff.dpr
        height: implicitHeight / handoff.dpr
    }

    Placed {
        // The intro's fill has its own canvas; after unlocking it shares the
        // glass canvas.
        source: handoff.active ? handoff.dir + (handoff.afterUnlock ? "fill-u.png" : "fill.png") : ""
        ax: handoff.afterUnlock ? handoff.glass.x : -115
        ay: handoff.afterUnlock ? handoff.glass.y : -180
        opacity: handoff.d < handoff.k.fillOff[0]
            ? 0.1 + 0.9 * handoff.between(handoff.d, handoff.k.fillUp)
            : 1 - handoff.between(handoff.d, handoff.k.fillOff)
    }

    Placed {
        source: handoff.active ? handoff.dir + "base-" + handoff.g + ".png" : ""
        ax: handoff.glass.x
        ay: handoff.glass.y
        opacity: 1 - handoff.between(handoff.d, handoff.k.baseOff)
    }

    // The running light carries on from where Plymouth left it as it fades.
    Placed {
        readonly property int frame: Math.max(0, Math.min(handoff.k.lapFrames - 1,
            Math.trunc((handoff.d - handoff.k.start) / handoff.k.lap * handoff.k.lapFrames)))
        source: handoff.active && opacity > 0
            ? handoff.dir + "light-" + handoff.g + "-" + String(frame).padStart(2, "0") + ".png" : ""
        ax: handoff.glass.x
        ay: handoff.glass.y
        opacity: 1 - handoff.between(handoff.d, handoff.k.lightOff)
    }

    // One last full trace of the outline.
    Placed {
        readonly property real drawn: handoff.bezier(
            (handoff.d - handoff.k.trace[0]) / (handoff.k.trace[1] - handoff.k.trace[0]), 0.45, 0, 0.2, 1)
        source: handoff.active && opacity > 0
            ? handoff.handoffAssets + handoff.folder + "complete-" + handoff.g + "-"
              + String(Math.round(drawn * 45)).padStart(2, "0") + ".png" : ""
        ax: handoff.glass.x
        ay: handoff.glass.y
        opacity: 1 - handoff.bezier(
            (handoff.d - handoff.k.traceOff[0]) / (handoff.k.traceOff[1] - handoff.k.traceOff[0]), 0.45, 0, 0.2, 1)
    }

    Placed {
        source: handoff.active ? handoff.dir + "status-" + handoff.g + ".png" : ""
        ax: -100
        ay: handoff.afterUnlock ? 95 : 50
        opacity: 0.75 * (1 - handoff.between(handoff.d, handoff.k.statusOff))
    }

    // The flash behind the spark as it pulses.
    Image {
        readonly property real t0: handoff.k.flash[0]
        readonly property real peak: handoff.k.flash[1]
        readonly property real t1: handoff.k.flash[2]
        readonly property real grow: handoff.d < peak ? 0.3 + 0.7 * handoff.segment(handoff.d, t0, peak)
                                                       : 1 + 0.9 * handoff.segment(handoff.d, peak, t1)
        // flash.png is the glow at 1.8 times its size in the 240px box.
        readonly property real size: 200 * handoff.s / 1.8 * handoff.pose.s / 1.6 * grow / handoff.dpr
        source: handoff.active ? handoff.dir + "flash.png" : ""
        asynchronous: false
        smooth: true
        width: size
        height: size
        x: handoff.markX(148.5) - size / 2
        y: handoff.markY(40) - size / 2
        opacity: handoff.d < t0 ? 0 : handoff.d < peak ? 0.95 * handoff.segment(handoff.d, t0, peak)
                                                       : 0.95 * (1 - handoff.segment(handoff.d, peak, t1))
    }

    // The spark: a pulse, then away up and to the right.
    Placed {
        id: spark
        readonly property var p: handoff.k.pulse
        readonly property real away: handoff.between(handoff.d, handoff.k.sparkOff)
        readonly property real grow: handoff.d < p[1] ? 1 + 0.35 * handoff.segment(handoff.d, p[0], p[1])
                                     : handoff.d < p[2] ? 1.35 - 0.35 * handoff.segment(handoff.d, p[1], p[2])
                                     : 1 - 0.4 * away
        source: handoff.active ? handoff.dir + (handoff.afterUnlock ? "spark-u.png" : "spark-25.png") : ""
        ax: handoff.afterUnlock ? 40 : 35
        ay: handoff.afterUnlock ? -75 : -195
        opacity: 1 - away
        transform: [
            Scale {
                origin.x: handoff.markX(148.5) - spark.x
                origin.y: handoff.markY(40) - spark.y
                xScale: spark.grow
                yScale: spark.grow
            },
            Translate {
                // 14 of the mark's units up and to the right.
                x: 14 * handoff.pose.s * handoff.s / handoff.dpr * spark.away
                y: -14 * handoff.pose.s * handoff.s / handoff.dpr * spark.away
            }
        ]
    }

    MouseArea {
        anchors.fill: parent
        enabled: handoff.active
        onPressed: mouse => {
            handoff.finish();
            mouse.accepted = false;
        }
    }

    Connections {
        target: handoff.input
        ignoreUnknownSignals: true
        function onTextChanged() { handoff.finish(); }
        function onSubmitted() { handoff.finish(); }
    }
    Connections {
        target: handoff.secondInput
        ignoreUnknownSignals: true
        function onTextChanged() { handoff.finish(); }
        function onSubmitted() { handoff.finish(); }
    }

    Connections {
        target: photo
        function onStatusChanged() {
            if (photo.status === Image.Error)
                handoff.finish();
        }
    }

    // Driven by the wall clock rather than the animation timer, which steps
    // once per frame and runs slow when software rendering drops frames.
    property real startedAt: -1

    Component.onCompleted: d = k.start

    function tick() {
        const end = endOnLanding ? k.zoom[1] : k.clockEnd + 0.4;
        d = Math.min(end, k.start + speed * (Date.now() - startedAt) / 1000);
        if (d >= end) {
            done = true;
            finished();
        }
    }

    FrameAnimation {
        running: handoff.active && handoff.startedAt >= 0 && !skipping.running
        onTriggered: handoff.tick()
    }

    NumberAnimation {
        id: skipping
        target: handoff
        property: "skip"
        to: 1
        duration: 250
        easing.type: Easing.OutCubic
        onFinished: {
            handoff.done = true;
            handoff.finished();
        }
    }

    // The clock starts with the first frame on screen, not when the scene is
    // built: loading the sign-in screen's wallpaper can take longer than the
    // first part of the animation.
    Connections {
        target: handoff.Window.window
        enabled: handoff.active && handoff.startedAt < 0
        function onFrameSwapped() {
            handoff.startedAt = Date.now();
        }
    }
}
