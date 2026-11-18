import QtQuick
import QtQuick.Shapes

// The wait and the finish of the start-up animation (BootPace.dc.html, with
// BootPaceTarget.dc.html's keyframes; AfterUnlockD.dc.html's place for the W
// after an encrypted disk was unlocked). Plymouth plays the wait until it
// quits; after a moment of black while KWin takes the display over, this
// carries the same wait on, one lap of light every 1.2 s, with the W drawn
// from Plymouth's own images at the same scale and in the same real pixels
// (plymouth/wildos/wildos.script, files/libexec/boot-handoff).
//
// The finish starts the moment the screen beneath says it is ready, never on
// a timer: the sign-in screen is ready at once, the session splash when the
// desktop has drawn itself. In 0.9 s the W lights up, the spark flashes and
// the screen flies through the W into the wallpaper, or the real desktop.
// The screen underneath is usable from the first frame: typing or clicking
// skips straight to the end.
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
    // The screen's size in real pixels, written by boot-handoff as an empty
    // SVG of that size.
    property url panelMarker: "file:///run/wildos/boot-handoff-panel.svg"
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
    // Set by the session splash once Plasma has drawn the desktop: the W then
    // opens onto the real desktop, taskbar and windows included, instead of
    // the photo, so nothing is left to fade in when the flight ends.
    property bool live: false
    // When the finish may start. The sign-in screen is ready as soon as it
    // shows; the session splash waits for the desktop.
    property bool ready: true
    // Reduce motion: the logo holds still and fades out when ready, with no
    // laps, spark or flight.
    property bool reducedMotion: false
    // Ends as the flight lands instead of after the sign-in screen's clock
    // fades in: the desktop has no clock to wait for.
    property bool endOnLanding: false
    // The photo is only needed once the finish starts, which on the session
    // splash is never at once.
    property bool photoLater: false

    readonly property bool active: markerImage.status === Image.Ready && !done
    readonly property bool afterUnlock: unlockedImage.status === Image.Ready

    // The finish's keyframes, in seconds from when it starts (BootPaceTarget
    // puts the finish at 53.23% of its 6.2 s loop, 3.3 s in).
    readonly property var k: ({
        lap: 1.2, lapFrames: 60, background: "#000000",
        fillUp: [0, 0.17], fillHold: 0.3, fillOff: [0.3, 0.4],
        baseOff: [0, 0.15], lightOff: [0, 0.12], statusOff: [0, 0.15],
        wordOff: [0.1, 0.28],
        flash: [0, 0.12, 0.45],
        pulse: [0, 0.1, 0.2], sparkOff: [0.2, 0.32],
        cover: 0.3, zoom: [0.3, 0.8], photo: [0.3, 0.9],
        clock: [0.62, 0.92]
    })

    // For the screen beneath: the clock appears as the flight ends, the
    // sign-in controls with it.
    readonly property real arrived: active ? Math.max(skip, f < 0 ? 0 : between(f, k.clock)) : 1
    readonly property real stageOpacity: arrived
    readonly property real clockShift: active ? Math.round(16 * (1 - arrived)) : 0
    readonly property real uiOpacity: arrived

    signal finished()

    function finish() {
        if (!active || skipping.running)
            return;
        skipping.start();
    }

    // Seconds since the first frame (the wait's clock, which Plymouth left at
    // the start of a lap) and since the finish started, or -1 before it.
    property real w: waitStart
    // The wait as handed over: two whole laps in on the board's clock, so
    // the light comes back at the start of a lap.
    readonly property real waitStart: 2.4
    readonly property real fillWait: 0.14
    readonly property real baseWait: 1
    // The light fades back in after the black and laps on.
    readonly property real lightWait: segment(w, waitStart, waitStart + 0.2)
    readonly property real statusWait: 1
    property real f: -1
    property real skip: 0
    property bool done: false
    // Decode the photo at the size it is shown, grown 1.18 times at most,
    // rather than at its full 4K: decoding it twice at full size held up the
    // first frame. Wild OS photos are 16:9.
    readonly property int photoDecodeWidth: Math.ceil(1.18 * Math.max(screenW, screenH * 16 / 9))
    readonly property string assets: "file:///usr/share/plymouth/themes/wildos/"

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
    // 0 before the finish, then from 0 to 1 over the span.
    function fin(span) {
        return f < 0 ? 0 : between(f, span);
    }

    // Where the mark's units land, in artboard pixels from the screen centre
    // (render-boot-assets.py: POSE_I, POSE_U), and the canvases drawn there.
    readonly property var pose: afterUnlock ? { s: 1.1287, x: 615.22 - 720, y: 367.04 - 450 }
                                            : { s: 1.6, x: -120 - 18 * 1.6, y: -220 + 10.5 * 1.6 }
    readonly property string g: afterUnlock ? "u" : "i"
    readonly property var glass: afterUnlock ? { x: -100, y: -85 } : { x: -135, y: -200 }

    // Plymouth's scale choice and rounding, in device pixels.
    // Real pixels per scene unit. Qt's Screen.devicePixelRatio is the
    // desktop's scaling rounded up to a whole number (3 at 225%), which drew
    // the W at three quarters of Plymouth's size; the screen's real width
    // over this item's gives the exact factor.
    readonly property real dpr: panelImage.status === Image.Ready && panelImage.implicitWidth > 0 && width > 0
        ? panelImage.implicitWidth / width
        : (Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1)
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
        id: panelImage
        visible: false
        asynchronous: false
        cache: false
        source: handoff.panelMarker
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
        visible: !(handoff.live && handoff.f >= handoff.k.cover)
    }

    // The wallpaper, there from the moment the cover goes up, settling from
    // 1.18 to its place as the screen flies through the W. When the desktop
    // is ready it never shows: the W opens onto the desktop itself.
    Item {
        id: photoLayer
        anchors.fill: parent
        visible: handoff.f >= handoff.k.cover && opacity > 0
        opacity: handoff.live ? 0 : 1
        Behavior on opacity {
            NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
        }

        Image {
            id: photo

            readonly property real coverScale: implicitWidth > 0
                ? Math.max(photoLayer.width / implicitWidth, photoLayer.height / implicitHeight) : 1
            readonly property real grow: 1.18 - 0.18 * handoff.bezier(
                (handoff.f - handoff.k.photo[0]) / (handoff.k.photo[1] - handoff.k.photo[0]), 0.3, 0, 0.2, 1)

            // Framed as the sign-in screen frames the wallpaper, so the last
            // frame here is the first frame there.
            width: implicitWidth * coverScale
            height: implicitHeight * coverScale
            x: (photoLayer.width - width) / 2
            y: (photoLayer.height - height) * handoff.photoFocus
            source: handoff.active ? handoff.photoSource : ""
            sourceSize.width: handoff.photoDecodeWidth
            // The sign-in screen loads it before its first frame, as it
            // finishes at once; until then KWin still shows the handover
            // picture. The session splash loads it while the wait runs, so
            // its first frame comes sooner. Cached, so the screen around this
            // shares the one decode.
            asynchronous: handoff.photoLater
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
        visible: handoff.f >= handoff.k.cover && zoomFactor < 120
        preferredRendererType: Shape.CurveRenderer

        readonly property real zoomFactor: 1 + 139 * handoff.bezier(
            (handoff.f - handoff.k.zoom[0]) / (handoff.k.zoom[1] - handoff.k.zoom[0]), 0.55, 0, 0.75, 0.2)

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

    // Glass during the wait, lit up to full colour as the finish starts,
    // then gone as the cover takes over.
    Placed {
        // The intro's fill has its own canvas; after unlocking it shares the
        // glass canvas.
        source: handoff.active ? handoff.dir + (handoff.afterUnlock ? "fill-u.png" : "fill.png") : ""
        ax: handoff.afterUnlock ? handoff.glass.x : -115
        ay: handoff.afterUnlock ? handoff.glass.y : -180
        opacity: handoff.f < handoff.k.fillHold
            ? handoff.fillWait + (1 - handoff.fillWait) * handoff.fin(handoff.k.fillUp)
            : 1 - handoff.fin(handoff.k.fillOff)
    }

    Placed {
        source: handoff.active ? handoff.dir + "base-" + handoff.g + ".png" : ""
        ax: handoff.glass.x
        ay: handoff.glass.y
        opacity: handoff.baseWait * (1 - handoff.fin(handoff.k.baseOff))
    }

    // The light lapping the outline, from the start of a lap where Plymouth
    // left it, still running as it fades.
    Placed {
        readonly property int frame: Math.trunc((handoff.w % handoff.k.lap) / handoff.k.lap * handoff.k.lapFrames) % handoff.k.lapFrames
        source: handoff.active && opacity > 0
            ? handoff.dir + "light-" + handoff.g + "-" + String(frame).padStart(2, "0") + ".png" : ""
        ax: handoff.glass.x
        ay: handoff.glass.y
        opacity: handoff.reducedMotion ? 0 : handoff.lightWait * (1 - handoff.fin(handoff.k.lightOff))
    }

    // "Wild OS" stays through the wait; after unlocking there is no name.
    Placed {
        source: handoff.active && !handoff.afterUnlock ? handoff.dir + "word-40.png" : ""
        ax: -150
        ay: 45
        opacity: 1 - handoff.fin(handoff.k.wordOff)
    }

    Placed {
        source: handoff.active ? handoff.dir + "status-" + handoff.g + ".png" : ""
        ax: -100
        ay: handoff.afterUnlock ? 95 : 100
        opacity: 0.75 * handoff.statusWait * (1 - handoff.fin(handoff.k.statusOff))
    }

    // The flash behind the spark as the W lights up.
    Image {
        readonly property real t0: handoff.k.flash[0]
        readonly property real peak: handoff.k.flash[1]
        readonly property real t1: handoff.k.flash[2]
        readonly property real grow: handoff.f < peak ? 0.3 + 0.7 * handoff.segment(handoff.f, t0, peak)
                                                       : 1 + 0.9 * handoff.segment(handoff.f, peak, t1)
        // flash.png is 210 artboard pixels: the glow at 1.9 times its size
        // in the 240px box.
        readonly property real size: 210 * handoff.s / 1.9 * handoff.pose.s / 1.6 * grow / handoff.dpr
        source: handoff.active ? handoff.dir + "flash.png" : ""
        asynchronous: false
        smooth: true
        visible: handoff.f >= 0
        width: size
        height: size
        x: handoff.markX(148.5) - size / 2
        y: handoff.markY(40) - size / 2
        opacity: handoff.f < t0 ? 0 : handoff.f < peak ? 0.95 * handoff.segment(handoff.f, t0, peak)
                                                       : 0.95 * (1 - handoff.segment(handoff.f, peak, t1))
    }

    // The spark: a pulse, then away up and to the right.
    Placed {
        id: spark
        readonly property var p: handoff.k.pulse
        readonly property real away: handoff.fin(handoff.k.sparkOff)
        readonly property real grow: handoff.f < 0 ? 1
                                     : handoff.f < p[1] ? 1 + 0.3 * handoff.segment(handoff.f, p[0], p[1])
                                     : handoff.f < p[2] ? 1.3 - 0.3 * handoff.segment(handoff.f, p[1], p[2])
                                     : 1 - 0.4 * away
        source: handoff.active ? handoff.dir + (handoff.afterUnlock ? "spark-u.png" : "spark-13.png") : ""
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
    property real finishAt: -1

    function tick() {
        const now = Date.now();
        // The light laps from the board's time 0, so its phase carries on.
        w = waitStart + (now - startedAt) / 1000;
        if (finishAt < 0 && ready) {
            // With Reduce motion the logo simply fades out.
            if (reducedMotion) {
                finish();
                return;
            }
            finishAt = now;
        }
        if (finishAt < 0)
            return;
        const end = endOnLanding ? k.zoom[1] : k.clock[1] + 0.1;
        f = Math.min(end, (now - finishAt) / 1000);
        if (f >= end) {
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
        duration: handoff.reducedMotion ? 400 : 250
        easing.type: handoff.reducedMotion ? Easing.InOutQuad : Easing.OutCubic
        onFinished: {
            handoff.done = true;
            handoff.finished();
        }
    }

    // The clock starts with the first frame on screen, not when the scene is
    // built.
    Connections {
        target: handoff.Window.window
        enabled: handoff.active && handoff.startedAt < 0
        function onFrameSwapped() {
            handoff.startedAt = Date.now();
        }
    }
}
