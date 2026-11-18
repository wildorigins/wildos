import QtQuick
import QtQuick.Templates as T
import WildOS.PDF
import WildOS.UI as UI

// The pages, one under another on the dark canvas (AppPdf.dc: 22px above
// the first page, centred). Every page's place is worked out from its size,
// so going to a page, a search match or a zoom lands exactly; only the pages
// near the screen are drawn.
Flickable {
    id: view

    required property var app
    // 1 is the page at its real size on a 96 dpi screen.
    property real zoom: 1
    readonly property real pointScale: zoom * 96 / 72
    readonly property int gap: 16
    readonly property int topMargin: 22
    readonly property int sideMargin: 24
    // Read once: each read of the C++ list makes a new copy of it.
    readonly property var sizes: PdfDocument.pageSizes
    property var tops: []
    property real widest: 0
    property bool fitted: false
    // Quarter turns clockwise, from Rotate in the More menu or the
    // touchpad's two-finger rotate. Only the view turns, and the turn is
    // remembered with the file's place; the file is saved and printed as
    // it was.
    property int turns: 0
    // The rotation under the fingers, in degrees, before it settles on a
    // quarter turn.
    property real liveTurn: 0
    readonly property bool sideways: turns % 2 !== 0
    readonly property int currentPage: pageAt(contentY + height / 3)

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.AutoFlickIfNeeded
    contentWidth: Math.max(width, widest + sideMargin * 2)
    contentHeight: tops.length > 0 ? tops[tops.length - 1] + pageHeight(tops.length - 1) + topMargin : 0

    // A page as the file has it, before any turn.
    function baseWidth(i) {
        const s = sizes[i];
        return s ? s.width * pointScale : 0;
    }
    function baseHeight(i) {
        const s = sizes[i];
        return s ? s.height * pointScale : 0;
    }
    // A page as it sits in the view.
    function pageWidth(i) {
        return sideways ? baseHeight(i) : baseWidth(i);
    }
    function pageHeight(i) {
        return sideways ? baseWidth(i) : baseHeight(i);
    }
    function turnedWidth(s) {
        return sideways ? s.height : s.width;
    }
    function turnedHeight(s) {
        return sideways ? s.width : s.height;
    }
    // Turns by quarter turns, keeping the same page in view.
    function turn(quarters) {
        const page = currentPage;
        turns = ((turns + quarters) % 4 + 4) % 4;
        relayout();
        goToPage(page);
    }
    function relayout() {
        const out = [];
        let y = topMargin;
        let w = 0;
        for (let i = 0; i < PdfDocument.pageCount; ++i) {
            out.push(y);
            y += pageHeight(i) + gap;
            w = Math.max(w, pageWidth(i));
        }
        widest = w;
        tops = out;
    }
    function pageAt(y) {
        let low = 0;
        let high = tops.length - 1;
        if (high < 0) {
            return 0;
        }
        while (low < high) {
            const mid = Math.ceil((low + high) / 2);
            if (tops[mid] <= y) {
                low = mid;
            } else {
                high = mid - 1;
            }
        }
        return low;
    }
    function goToPage(i) {
        if (i < 0 || i >= tops.length) {
            return;
        }
        contentY = Math.max(0, Math.min(tops[i] - topMargin / 2, contentHeight - height));
    }
    // Where a link or the contents points: that far down a page, at the
    // top of the view. A turned page is shown from its top.
    function showSpot(page, top) {
        if (page < 0 || page >= tops.length) {
            return;
        }
        const along = turns === 0 ? top * pageHeight(page) : 0;
        contentY = Math.max(0, Math.min(tops[page] + along - topMargin / 2, contentHeight - height));
    }
    // A match or a signature on a page, brought into the middle of the view.
    function showArea(page, found) {
        if (page < 0 || page >= tops.length) {
            return;
        }
        // The area is in fractions of the page as the file has it.
        let rect = found;
        if (turns === 1) {
            rect = Qt.rect(1 - found.y - found.height, found.x, found.height, found.width);
        } else if (turns === 2) {
            rect = Qt.rect(1 - found.x - found.width, 1 - found.y - found.height, found.width, found.height);
        } else if (turns === 3) {
            rect = Qt.rect(found.y, 1 - found.x - found.width, found.height, found.width);
        }
        const top = tops[page] + rect.y * pageHeight(page);
        const bottom = top + rect.height * pageHeight(page);
        if (top < contentY || bottom > contentY + height) {
            contentY = Math.max(0, Math.min(top - height / 3, contentHeight - height));
        }
        const left = (contentWidth - pageWidth(page)) / 2 + rect.x * pageWidth(page);
        if (left < contentX || left > contentX + width - 40) {
            contentX = Math.max(0, Math.min(left - width / 3, contentWidth - width));
        }
    }
    // Zooms about the middle of the view, so what is there stays there.
    function setZoom(z) {
        const next = Math.max(0.25, Math.min(4, z));
        if (Math.abs(next - zoom) < 0.0001 || tops.length === 0) {
            return;
        }
        const page = pageAt(contentY + height / 2);
        const h = pageHeight(page);
        const along = h > 0 ? (contentY + height / 2 - tops[page]) / h : 0;
        const across = contentWidth > 0 ? (contentX + width / 2) / contentWidth : 0.5;
        zoom = next;
        app.keepZoom(next);
        relayout();
        contentY = Math.max(0, Math.min(tops[page] + along * pageHeight(page) - height / 2, contentHeight - height));
        contentX = Math.max(0, Math.min(across * contentWidth - width / 2, contentWidth - width));
    }
    function fitWidth() {
        let w = 0;
        for (let i = 0; i < sizes.length; ++i) {
            w = Math.max(w, turnedWidth(sizes[i]));
        }
        if (w > 0) {
            setZoom((width - sideMargin * 2) / (w * 96 / 72));
        }
    }
    function fitPage() {
        const s = sizes[currentPage];
        if (s && s.width > 0 && s.height > 0) {
            setZoom(Math.min((width - sideMargin * 2) / (turnedWidth(s) * 96 / 72), (height - topMargin * 2) / (turnedHeight(s) * 96 / 72)));
            goToPage(currentPage);
        }
    }
    // A document seen before opens where it was left: its page, zoom and
    // which way up. A new one opens at the zoom last chosen, or, if none
    // ever was, at its real size or smaller if that would not fit across
    // the window.
    function openingZoom() {
        let w = 0;
        for (let i = 0; i < sizes.length; ++i) {
            w = Math.max(w, sizes[i].width);
        }
        fitted = width > 0 && height > 0;
        const place = History.place(PdfDocument.path);
        const kept = place.zoom > 0 ? place.zoom : app.keptZoom;
        turns = place.turns > 0 ? place.turns % 4 : 0;
        zoom = kept > 0 ? Math.max(0.25, Math.min(4, kept))
            : w > 0 && width > 0 ? Math.max(0.25, Math.min(1, (width - sideMargin * 2) / (w * 96 / 72))) : 1;
        relayout();
        contentX = 0;
        contentY = 0;
        if (place.page >= 0 && place.page < tops.length) {
            contentY = Math.max(0, Math.min(tops[place.page] + place.offset * pageHeight(place.page), contentHeight - height));
        }
    }
    function rememberPlace() {
        if (!fitted || !PdfDocument.loaded || tops.length === 0) {
            return;
        }
        const page = pageAt(contentY + topMargin);
        const h = pageHeight(page);
        History.setPlace(PdfDocument.path, page, h > 0 ? (contentY - tops[page]) / h : 0, zoom, turns);
    }

    // A document saved and opened again keeps its zoom and place.
    onSizesChanged: {
        if (app.keepPlace) {
            const y = contentY;
            relayout();
            contentY = Math.max(0, Math.min(y, contentHeight - height));
        } else {
            openingZoom();
        }
    }
    Component.onCompleted: openingZoom()
    onWidthChanged: if (!fitted && width > 0) openingZoom()
    onHeightChanged: if (!fitted && height > 0) openingZoom()

    // Written a moment after the view comes to rest, not on every step of
    // a scroll.
    onContentYChanged: placeTimer.restart()
    onZoomChanged: placeTimer.restart()
    onTurnsChanged: placeTimer.restart()
    Timer {
        id: placeTimer
        interval: 800
        onTriggered: view.rememberPlace()
    }

    // Two fingers on a touchpad (or a touchscreen) pinch to zoom and turn
    // to rotate. The page follows the fingers and settles on the nearest
    // quarter turn when they lift.
    PinchHandler {
        id: pinch
        target: null
        property real startZoom: 1

        onActiveChanged: {
            if (active) {
                startZoom = view.zoom;
                return;
            }
            const quarters = Math.round(view.liveTurn / 90);
            view.liveTurn = 0;
            if (quarters !== 0) {
                view.turn(quarters);
            }
        }
        onScaleChanged: delta => {
            if (active) {
                view.setZoom(startZoom * activeScale);
            }
        }
        // A pinch always turns the fingers a little; small turns are left
        // out so zooming does not wobble the page.
        onRotationChanged: delta => {
            if (active) {
                view.liveTurn = Math.abs(activeRotation) > 15 ? activeRotation : 0;
            }
        }
    }

    // Ctrl and the wheel zooms, as in a browser; the wheel alone scrolls.
    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => {
            view.setZoom(view.zoom * (event.angleDelta.y > 0 ? 1.1 : 1 / 1.1));
        }
    }

    Repeater {
        model: PdfDocument.loaded ? PdfDocument.pageCount : 0

        delegate: Item {
            id: slot
            required property int index
            readonly property real pageTop: view.tops[index] ?? 0
            readonly property bool near: pageTop < view.contentY + view.height * 2 && pageTop + height > view.contentY - view.height

            x: (view.contentWidth - width) / 2
            y: pageTop
            width: view.pageWidth(index)
            height: view.pageHeight(index)

            // The page at its own shape, turned to fit the slot. Clicks,
            // selection and signing go through the turn, so the page keeps
            // working in its own fractions.
            Item {
                anchors.centerIn: parent
                width: view.baseWidth(slot.index)
                height: view.baseHeight(slot.index)
                rotation: view.turns * 90 + view.liveTurn

                Loader {
                    anchors.fill: parent
                    active: slot.near
                    sourceComponent: PageItem {
                        index: slot.index
                        app: view.app
                        pointScale: view.pointScale
                    }
                }
            }
        }
    }

    T.ScrollBar.vertical: UI.ScrollBar {}
    T.ScrollBar.horizontal: UI.ScrollBar {}
}
