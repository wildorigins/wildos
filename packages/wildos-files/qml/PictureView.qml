import QtQuick
import QtQuick.Window
import WildOS.UI as UI

// The picture in Quick Look. It stays in place from one picture to the
// next: the pictures either side of the one on screen are decoded ahead of
// time, so an arrow key swaps to one that is already there, and a picture
// that is not ready yet leaves the last one up until it is. Three pictures
// at most are held, each no larger than the screen.
Item {
    id: stage

    // The picture to show, and the ones next to it in the folder with the
    // likelier next step first. Empty when Quick Look shows something else,
    // which lets every picture go.
    property url current
    property var ahead: []

    // True when the picture could not be read, so Quick Look can show the
    // file's icon and details in its place.
    property bool failed: false
    property bool busy: false
    property bool slow: false
    property int shown: -1

    // Pictures are shown fitted to the window, which is never larger than
    // the screen, so nothing is gained by decoding beyond it.
    readonly property size bounds: Qt.size(
        Math.max(1, Math.min(3840, Math.ceil(Screen.width * Screen.devicePixelRatio))),
        Math.max(1, Math.min(3840, Math.ceil(Screen.height * Screen.devicePixelRatio))))

    function holder(item) {
        for (let i = 0; i < slots.count; ++i) {
            const slot = slots.itemAt(i);
            if (slot && slot.item === item) {
                return i;
            }
        }
        return -1;
    }

    // A slot nothing needs: an empty one, else one still decoding (taking
    // it stops that decode), else any. Never the one on screen.
    function spare(wanted) {
        let best = -1;
        let bestScore = 0;
        for (let i = 0; i < slots.count; ++i) {
            const slot = slots.itemAt(i);
            if (!slot || i === shown || wanted.indexOf(slot.item) >= 0) {
                continue;
            }
            const score = slot.item.length === 0 ? 3 : slot.status === Image.Loading ? 2 : 1;
            if (score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    function load(at, item, now) {
        const slot = slots.itemAt(at);
        if (!slot) {
            return;
        }
        slot.item = item;
        slot.source = "image://picture/" + (now ? "now" : "next") + "/" + encodeURIComponent(item);
    }

    function release(at) {
        const slot = slots.itemAt(at);
        if (!slot) {
            return;
        }
        slot.item = "";
        slot.source = "";
    }

    function arrange() {
        const item = current.toString();
        if (item.length === 0) {
            shown = -1;
            for (let i = 0; i < slots.count; ++i) {
                release(i);
            }
            settle();
            return;
        }
        if (holder(item) < 0) {
            const at = spare([item]);
            if (at >= 0) {
                load(at, item, true);
            }
        }
        settle();
        // Holding an arrow key passes pictures faster than they decode; the
        // neighbours are only fetched once it rests on one.
        aheadTimer.restart();
    }

    function settle() {
        const item = current.toString();
        const at = item.length > 0 ? holder(item) : -1;
        const slot = at >= 0 ? slots.itemAt(at) : null;
        const done = slot !== null && (slot.status === Image.Ready || slot.status === Image.Error);
        failed = slot !== null && slot.status === Image.Error;
        busy = item.length > 0 && !done;
        if (done && shown !== at) {
            shown = at;
            if (!aheadTimer.running) {
                fillAhead();
            }
        }
    }

    function fillAhead() {
        const item = current.toString();
        if (item.length === 0) {
            return;
        }
        const wanted = [item].concat(ahead.filter(a => a.length > 0 && a !== item));
        for (let k = 1; k < wanted.length; ++k) {
            if (holder(wanted[k]) >= 0) {
                continue;
            }
            const at = spare(wanted);
            if (at < 0) {
                break;
            }
            load(at, wanted[k], false);
        }
        for (let i = 0; i < slots.count; ++i) {
            const slot = slots.itemAt(i);
            if (slot && i !== shown && slot.item.length > 0 && wanted.indexOf(slot.item) < 0) {
                release(i);
            }
        }
    }

    onCurrentChanged: arrange()
    onAheadChanged: aheadTimer.restart()
    onBusyChanged: if (!busy) slow = false

    Timer {
        id: aheadTimer
        interval: 120
        onTriggered: stage.fillAhead()
    }

    // The spinner only shows for a picture that takes a while; most arrive
    // before anyone would see it.
    Timer {
        interval: 400
        running: stage.busy
        onTriggered: stage.slow = true
    }

    Repeater {
        id: slots
        model: 3

        delegate: Image {
            required property int index
            // The file this slot holds, as text; empty when it is free.
            property string item

            anchors.centerIn: parent
            // Pictures at their own size, or shrunk to fit the window;
            // never stretched past it, so an icon-sized PNG stays crisp.
            width: Math.min(stage.width - 32, implicitWidth)
            height: Math.min(stage.height - 32, implicitHeight)
            visible: index === stage.shown && status === Image.Ready
            opacity: stage.slow ? 0.45 : 1
            sourceSize: stage.bounds
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
            onStatusChanged: stage.settle()

            Behavior on opacity {
                NumberAnimation { duration: UI.Style.motionFast }
            }
        }
    }

    UI.Spinner {
        anchors.centerIn: parent
        visible: stage.slow
        size: 24
    }
}
