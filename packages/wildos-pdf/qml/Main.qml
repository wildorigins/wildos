import QtCore
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.PDF
import WildOS.UI as UI

// Wild OS PDF (AppPdf.dc): the title bar with the file's name, the toolbar
// (pages, zoom, page number; Select, Highlight, Draw, Text, Sign and Fill
// in; find, print, share and more), the page thumbnails, the pages, and the
// Sign pane while signing.
UI.AppWindow {
    id: window

    // From main.cpp: why the file it was started with didn't open.
    property string openError
    // From main.cpp: the tool to start with, "sign" for --sign.
    property string startTool
    property string tool: "select"
    property bool showThumbnails: true
    // Set while a save opens the saved file again, so the view stays put.
    property bool keepPlace: false

    property int selectionPage: -1
    property var selectionRects: []
    property string selectionText: ""

    property string findText: ""
    property bool finding: false
    property var hits: []
    property int hitIndex: -1

    // Signatures, dates and initials put on pages, waiting for Save a
    // signed copy: { uid, page, kind, x, y, width, height, signature, text,
    // pointSize }, x to height in fractions of the page.
    property var placements: []
    property string selectedPlacement: ""
    property string signatureId: ""
    property bool addDate: true
    property bool addInitials: false
    property int placementCount: 0
    readonly property bool hasSignaturePlaced: placements.some(p => p.kind === "signature")

    readonly property bool ready: PdfDocument.loaded
    readonly property var sizes: PdfDocument.pageSizes

    // Typing in a form field or on the page is put into the document
    // before anything reads or saves it.
    signal commitEdits()

    width: 1384
    height: 802
    minimumWidth: 1000
    minimumHeight: 600
    visible: true
    title: PdfDocument.fileName.length > 0 ? PdfDocument.fileName : qsTr("PDF")

    function focusPages() {
        pagesArea.forceActiveFocus();
    }
    function say(message, action) {
        toast.show(message, action);
    }
    function setTool(name) {
        commitEdits();
        clearSelection();
        selectedPlacement = "";
        tool = name;
        if (name === "fill" && ready && !PdfDocument.hasForm) {
            say(qsTr("This PDF has no form to fill in. Use Text to type on it instead."));
        }
        if (name === "sign" && signatureId.length === 0 && Signatures.items.length > 0) {
            signatureId = Signatures.items[0].id;
        }
        focusPages();
    }

    function clearSelection() {
        selectionPage = -1;
        selectionRects = [];
        selectionText = "";
    }
    function setSelection(page, result) {
        selectionPage = page;
        selectionRects = result.rects ?? [];
        selectionText = result.text ?? "";
    }
    function copySelection() {
        if (selectionText.length > 0) {
            PdfDocument.copyText(selectionText);
        }
    }
    function highlightSelection() {
        if (selectionPage >= 0 && selectionRects.length > 0) {
            PdfDocument.addHighlight(selectionPage, selectionRects);
            clearSelection();
        }
    }

    function find(text) {
        findText = text;
        hits = [];
        hitIndex = -1;
        finding = text.trim().length > 0;
        PdfDocument.find(text);
    }
    function clearFind() {
        findText = "";
        finding = false;
        hits = [];
        hitIndex = -1;
        PdfDocument.find("");
    }
    function stepHit(step) {
        if (hits.length === 0) {
            return;
        }
        hitIndex = (hitIndex + step + hits.length) % hits.length;
        const h = hits[hitIndex];
        pages.showArea(h.page, h);
    }
    function isCurrentHit(h) {
        const c = hits[hitIndex];
        return c !== undefined && c.page === h.page && c.x === h.x && c.y === h.y;
    }
    Connections {
        target: PdfDocument
        function onFound(text, found) {
            if (text !== window.findText) {
                return;
            }
            window.finding = false;
            window.hits = found;
            // The first match from the page in view onwards.
            let first = found.findIndex(h => h.page >= pages.currentPage);
            window.hitIndex = found.length === 0 ? -1 : first < 0 ? 0 : first;
            if (window.hitIndex >= 0) {
                pages.showArea(found[window.hitIndex].page, found[window.hitIndex]);
            }
        }
        function onLoadedChanged() {
            window.clearSelection();
            if (!window.keepPlace) {
                window.placements = [];
                window.selectedPlacement = "";
                if (window.addInitials) {
                    window.placeInitials();
                }
            }
            if (window.findText.length > 0) {
                window.find(window.findText);
            }
            if (PdfDocument.locked) {
                passwordDialog.ask();
            }
        }
    }

    // Placing: a signature goes where it was clicked, a third of the page
    // across, and the date (if asked for) beside it on the same line.
    function placeSignature(page, point) {
        if (signatureId.length === 0) {
            say(qsTr("Make a signature first: Draw, Type or Image."));
            return;
        }
        const size = sizes[page];
        const aspect = Signatures.aspect(signatureId);
        let w = 0.3;
        let h = w * size.width / (aspect * size.height);
        if (h > 0.1) {
            h = 0.1;
            w = h * aspect * size.height / size.width;
        }
        const x = Math.max(0, Math.min(1 - w, point.x - w / 2));
        const y = Math.max(0, Math.min(1 - h, point.y - h / 2));
        const uid = "p" + (++placementCount);
        const added = [{ uid: uid, page: page, kind: "signature", signature: signatureId, x: x, y: y, width: w, height: h }];
        if (addDate) {
            const text = new Date().toLocaleDateString(Qt.locale(), "d MMMM yyyy");
            dateMetrics.text = text;
            const pointSize = 10.5;
            const dw = (dateMetrics.advanceWidth / 100 * pointSize + 6) / size.width;
            const dh = pointSize * 1.6 / size.height;
            let dx = x + w + 24 / size.width;
            let dy = y + h - dh;
            if (dx + dw > 1) {
                dx = x;
                dy = Math.min(1 - dh, y + h + 6 / size.height);
            }
            added.push({ uid: "p" + (++placementCount), page: page, kind: "date", text: text, pointSize: pointSize, x: dx, y: dy, width: dw, height: dh });
        }
        placements = placements.concat(added);
        selectedPlacement = uid;
    }
    // Initials go at the foot of every page, at the right, small.
    function placeInitials() {
        const kept = placements.filter(p => p.kind !== "initials");
        if (!addInitials || Signatures.initials.length === 0) {
            placements = kept;
            return;
        }
        const aspect = Signatures.initialsAspect();
        const added = [];
        for (let i = 0; i < sizes.length; ++i) {
            const size = sizes[i];
            const h = 22 / size.height;
            const w = Math.min(0.2, h * aspect * size.height / size.width);
            added.push({ uid: "p" + (++placementCount), page: i, kind: "initials", x: 1 - w - 36 / size.width, y: 1 - h - 30 / size.height, width: w, height: h });
        }
        placements = kept.concat(added);
    }
    function setAddInitials(on) {
        addInitials = on;
        placeInitials();
    }
    function updatePlacement(uid, rect) {
        placements = placements.map(p => p.uid === uid ? Object.assign({}, p, rect) : p);
    }
    function removePlacement(uid) {
        placements = placements.filter(p => p.uid !== uid);
        if (selectedPlacement === uid) {
            selectedPlacement = "";
        }
    }
    function askRemoveSignature(id) {
        removeSignatureDialog.signature = id;
        removeSignatureDialog.open();
    }
    Connections {
        target: Signatures
        function onChanged() {
            const ids = Signatures.items.map(s => s.id);
            if (window.signatureId.length > 0 && ids.indexOf(window.signatureId) < 0) {
                window.signatureId = ids.length > 0 ? ids[0] : "";
            } else if (window.signatureId.length === 0 && ids.length > 0) {
                window.signatureId = ids[0];
            }
            if (window.addInitials) {
                window.placeInitials();
            }
        }
    }

    function save() {
        if (!ready) {
            return;
        }
        commitEdits();
        keepPlace = true;
        const ok = PdfDocument.save();
        keepPlace = false;
        if (ok) {
            say(qsTr("Saved"));
        } else {
            say(PdfDocument.lastError(), qsTr("Save as"));
        }
    }
    function saveAs() {
        if (!ready) {
            return;
        }
        commitEdits();
        saveAsDialog.currentFolder = PdfDocument.folderUrl();
        saveAsDialog.selectedFile = PdfDocument.fileUrl();
        saveAsDialog.open();
    }
    function saveSigned() {
        if (!hasSignaturePlaced) {
            say(qsTr("Click on a page to place your signature first."));
            return;
        }
        commitEdits();
        signedDialog.currentFolder = PdfDocument.folderUrl();
        signedDialog.selectedFile = PdfDocument.signedCopyUrl();
        signedDialog.open();
    }
    function openFile() {
        openDialog.currentFolder = ready ? PdfDocument.folderUrl() : StandardPaths.writableLocation(StandardPaths.DocumentsLocation);
        openDialog.open();
    }
    // One PDF to a window: another file opens in a window of its own.
    function openUrl(url) {
        if (ready || PdfDocument.locked) {
            PdfDocument.openInNewWindow(url);
            return;
        }
        openError = PdfDocument.open(url);
        if (openError === "locked") {
            openError = "";
        }
    }
    function print() {
        if (!ready) {
            return;
        }
        if (!PdfDocument.canPrint) {
            say(qsTr("Whoever made this PDF has set it so it can't be printed."));
            return;
        }
        commitEdits();
        PdfDocument.print();
    }

    property bool closingNow: false
    onClosing: close => {
        if (closingNow) {
            return;
        }
        commitEdits();
        if (PdfDocument.modified) {
            close.accepted = false;
            unsavedDialog.open();
        } else if (hasSignaturePlaced) {
            close.accepted = false;
            unsignedDialog.open();
        }
    }
    function closeNow() {
        closingNow = true;
        window.close();
    }

    TextMetrics {
        id: dateMetrics
        font.family: "Liberation Sans"
        font.pixelSize: 100
    }

    Shortcut { sequence: StandardKey.Open; onActivated: window.openFile() }
    Shortcut { sequence: StandardKey.Save; enabled: window.ready; onActivated: window.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: window.ready; onActivated: window.saveAs() }
    Shortcut { sequence: StandardKey.Print; enabled: window.ready; onActivated: window.print() }
    Shortcut { sequence: StandardKey.Find; enabled: window.ready; onActivated: findBar.start() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: PdfDocument.canUndo; onActivated: PdfDocument.undo() }
    Shortcut { sequence: StandardKey.Copy; enabled: window.selectionText.length > 0; onActivated: window.copySelection() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; enabled: window.ready; onActivated: pages.setZoom(pages.zoom * 1.25) }
    Shortcut { sequences: [StandardKey.ZoomOut]; enabled: window.ready; onActivated: pages.setZoom(pages.zoom / 1.25) }
    Shortcut { sequence: "Ctrl+0"; enabled: window.ready; onActivated: pages.setZoom(1) }
    Shortcut { sequence: StandardKey.Close; onActivated: window.close() }
    Shortcut {
        sequence: StandardKey.Delete
        enabled: window.selectedPlacement.length > 0
        onActivated: window.removePlacement(window.selectedPlacement)
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (window.selectedPlacement.length > 0) {
                window.selectedPlacement = "";
            } else if (window.selectionText.length > 0) {
                window.clearSelection();
            } else if (findBar.visible) {
                findBar.stop();
            } else if (window.tool !== "select") {
                window.setTool("select");
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: window.frame
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
                    source: "qrc:/qt/qml/WildOS/PDF/data/wildos-pdf.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: PdfDocument.fileName.length > 0
                    text: PdfDocument.fileName
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: PdfDocument.modified ? qsTr("Not saved") : qsTr("PDF")
                    color: UI.Style.inkSubtle
                }
            }
        }

        // The toolbar: 46px on a slightly deeper band, a hairline under it.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: UI.Style.shade(0.2, 0.02)

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: UI.Style.divider
            }

            Row {
                id: leftTools
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                ToolButton {
                    iconName: "pages"
                    iconSize: 17
                    tip: window.showThumbnails ? qsTr("Hide pages") : qsTr("Show pages")
                    onClicked: window.showThumbnails = !window.showThumbnails
                }
                Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: UI.Style.divider }
                ToolButton {
                    iconName: "zoom-out"
                    tip: qsTr("Zoom out")
                    enabled: window.ready && pages.zoom > 0.25
                    onClicked: pages.setZoom(pages.zoom / 1.25)
                }
                Rectangle {
                    id: zoomChip
                    anchors.verticalCenter: parent.verticalCenter
                    width: zoomLabel.implicitWidth + 20
                    height: 28
                    radius: UI.Style.radiusXs
                    color: zoomMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                    opacity: window.ready ? 1 : 0.45

                    UI.Label {
                        id: zoomLabel
                        anchors.centerIn: parent
                        text: Math.round(pages.zoom * 100) + "%"
                        size: 12.5
                    }
                    MouseArea {
                        id: zoomMouse
                        anchors.fill: parent
                        enabled: window.ready
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        Accessible.role: Accessible.ButtonMenu
                        Accessible.name: qsTr("Zoom, %1").arg(zoomLabel.text)
                        onClicked: zoomMenu.popup(zoomChip, 0, zoomChip.height + 4)
                    }
                    UI.Menu {
                        id: zoomMenu
                        entries: [
                            { text: qsTr("Fit width"), run: () => pages.fitWidth() },
                            { text: qsTr("Fit page"), run: () => pages.fitPage() },
                            { separator: true }
                        ].concat([50, 75, 100, 125, 150, 200, 300, 400].map(z => ({
                            text: z + "%",
                            radio: true,
                            checked: Math.round(pages.zoom * 100) === z,
                            run: () => pages.setZoom(z / 100)
                        })))
                    }
                }
                ToolButton {
                    iconName: "zoom-in"
                    tip: qsTr("Zoom in")
                    enabled: window.ready && pages.zoom < 4
                    onClicked: pages.setZoom(pages.zoom * 1.25)
                }
                Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: UI.Style.divider }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(28, pageInput.contentWidth + 20)
                    height: 28
                    radius: UI.Style.radiusXs
                    color: UI.Style.surfaceRaised
                    border.width: pageInput.activeFocus ? 1 : 0
                    border.color: UI.Style.accent
                    opacity: window.ready ? 1 : 0.45

                    TextInput {
                        id: pageInput
                        anchors.centerIn: parent
                        enabled: window.ready
                        text: window.ready ? String(pages.currentPage + 1) : "0"
                        font.family: UI.Style.fontFamily
                        font.pixelSize: UI.Style.px(12.5)
                        color: UI.Style.ink
                        selectionColor: UI.Style.textSelection
                        selectedTextColor: UI.Style.ink
                        selectByMouse: true
                        validator: IntValidator { bottom: 1; top: Math.max(1, PdfDocument.pageCount) }
                        Accessible.name: qsTr("Page number")
                        // Typing replaces the binding; it goes back to following
                        // the page in view once the number is used or left.
                        function follow() {
                            text = Qt.binding(() => window.ready ? String(pages.currentPage + 1) : "0");
                        }
                        onAccepted: {
                            pages.goToPage(parseInt(text) - 1);
                            follow();
                            window.focusPages();
                        }
                        // The text menu holds focus while it is open, and coming
                        // back from it keeps the selection it worked on.
                        property bool inMenu: false
                        onActiveFocusChanged: {
                            if (pageMenu.opened) {
                                inMenu = true;
                            } else if (activeFocus) {
                                if (!inMenu) {
                                    selectAll();
                                }
                                inMenu = false;
                            } else {
                                follow();
                            }
                        }
                        Keys.onPressed: event => {
                            if (pageMenu.isMenuKey(event)) {
                                pageMenu.openAtCaret();
                                event.accepted = true;
                            }
                        }

                        UI.TextMenu {
                            id: pageMenu
                            onFocusLeft: {
                                pageInput.inMenu = false;
                                pageInput.follow();
                            }
                        }
                    }
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 6
                    rightPadding: 6
                    text: qsTr("of %1").arg(PdfDocument.pageCount)
                    size: 12.5
                    color: UI.Style.inkMuted
                }
            }

            // The tools sit in the middle of the bar, moved aside only if
            // the window is too narrow to leave them there.
            Row {
                id: toolRow
                x: Math.max(leftTools.x + leftTools.width + 12, Math.min((parent.width - width) / 2, rightTools.x - 12 - width))
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Repeater {
                    model: [
                        { id: "select", icon: "select", name: qsTr("Select") },
                        { id: "highlight", icon: "highlight", name: qsTr("Highlight") },
                        { id: "draw", icon: "draw", name: qsTr("Draw") },
                        { id: "text", icon: "text", name: qsTr("Text") },
                        { id: "sign", icon: "sign", name: qsTr("Sign") },
                        { id: "fill", icon: "fill-in", name: qsTr("Fill in") }
                    ]
                    delegate: ToolButton {
                        required property var modelData
                        iconName: modelData.icon
                        text: modelData.name
                        active: window.tool === modelData.id
                        enabled: window.ready && (modelData.id === "select" || (modelData.id === "fill" ? PdfDocument.canFill : PdfDocument.canAnnotate))
                        onClicked: window.setTool(modelData.id)
                    }
                }
            }

            Row {
                id: rightTools
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                ToolButton {
                    iconName: "search"
                    tip: qsTr("Find")
                    enabled: window.ready
                    active: findBar.visible
                    onClicked: findBar.visible ? findBar.stop() : findBar.start()
                }
                ToolButton {
                    iconName: "print"
                    tip: qsTr("Print")
                    enabled: window.ready
                    onClicked: window.print()
                }
                ToolButton {
                    id: shareButton
                    iconName: "share"
                    tip: qsTr("Share")
                    enabled: window.ready
                    onClicked: shareMenu.popup(shareButton, 0, shareButton.height + 4)
                }
                ToolButton {
                    id: moreButton
                    iconName: "more"
                    iconSize: 17
                    tip: qsTr("More")
                    onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height + 4)
                }
            }

            UI.Menu {
                id: shareMenu
                entries: [
                    { text: qsTr("Show in Files"), icon: "folder", run: () => PdfDocument.showInFolder() },
                    { text: qsTr("Copy the file"), icon: "copy", run: () => {
                        PdfDocument.copyFile();
                        window.say(qsTr("Copied. Paste it into a folder to make a copy there."));
                    } }
                ]
            }

            UI.Menu {
                id: moreMenu
                entries: [
                    { text: qsTr("Open"), shortcut: "Ctrl+O", run: () => window.openFile() },
                    { text: qsTr("Save"), shortcut: "Ctrl+S", enabled: window.ready && PdfDocument.modified, run: () => window.save() },
                    { text: qsTr("Save as"), shortcut: "Ctrl+Shift+S", enabled: window.ready, run: () => window.saveAs() },
                    { text: qsTr("Save a signed copy"), enabled: window.hasSignaturePlaced, run: () => window.saveSigned() },
                    { separator: true },
                    { text: qsTr("Undo"), icon: "undo", shortcut: "Ctrl+Z", enabled: PdfDocument.canUndo, run: () => PdfDocument.undo() },
                    { text: qsTr("Copy"), icon: "copy", shortcut: "Ctrl+C", enabled: window.selectionText.length > 0, run: () => window.copySelection() },
                    { separator: true },
                    { text: qsTr("Print"), shortcut: "Ctrl+P", enabled: window.ready, run: () => window.print() },
                    { text: qsTr("Show in Files"), enabled: window.ready, run: () => PdfDocument.showInFolder() },
                    { separator: true },
                    { text: qsTr("Close"), shortcut: "Ctrl+W", run: () => window.close() },
                    { separator: true },
                    { text: qsTr("Report a problem"), run: () => Qt.openUrlExternally("wildos-report:pdf") }
                ]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Thumbnails {
                Layout.preferredWidth: 170
                Layout.fillHeight: true
                visible: window.showThumbnails && window.ready
                current: pages.currentPage
                onPageChosen: index => pages.goToPage(index)
            }

            // The pages, on the darker canvas.
            FocusScope {
                id: pagesArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                activeFocusOnTab: true

                Keys.onPressed: event => {
                    const step = event.key === Qt.Key_Down ? 60 : event.key === Qt.Key_Up ? -60
                        : event.key === Qt.Key_PageDown || event.key === Qt.Key_Space ? pages.height * 0.9
                        : event.key === Qt.Key_PageUp ? -pages.height * 0.9 : 0;
                    if (step !== 0) {
                        pages.contentY = Math.max(0, Math.min(pages.contentHeight - pages.height, pages.contentY + step));
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Home) {
                        pages.goToPage(0);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_End) {
                        pages.goToPage(PdfDocument.pageCount - 1);
                        event.accepted = true;
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: UI.Style.shade(0.55, 0.035)
                }

                PageView {
                    id: pages
                    anchors.fill: parent
                    app: window
                    visible: window.ready
                }

                FindBar {
                    id: findBar
                    app: window
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 12
                    z: 5
                }

                // Nothing open yet, or the file could not be read.
                Column {
                    anchors.centerIn: parent
                    width: Math.min(420, parent.width - 48)
                    spacing: 12
                    visible: !window.ready

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 72
                        height: 72
                        source: "qrc:/qt/qml/WildOS/PDF/data/wildos-pdf.svg"
                        sourceSize.width: 144
                        sourceSize.height: 144
                    }
                    UI.Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: PdfDocument.locked ? qsTr("This PDF has a password") : window.openError.length > 0 ? qsTr("That PDF couldn't be opened")
                            : qsTr("Open a PDF to read, fill in or sign it")
                        size: 18
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: PdfDocument.locked ? qsTr("Type it to see the pages.") : window.openError.length > 0 ? window.openError : qsTr("Or drop one here.")
                        color: UI.Style.inkMuted
                    }
                    UI.Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        kind: "primary"
                        text: PdfDocument.locked ? qsTr("Type the password") : qsTr("Open a PDF")
                        onClicked: PdfDocument.locked ? passwordDialog.ask() : window.openFile()
                    }
                }

                DropArea {
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: drop => {
                        const pdf = drop.urls.find(u => u.toString().toLowerCase().endsWith(".pdf"));
                        if (pdf) {
                            window.openUrl(pdf);
                            drop.accept();
                        }
                    }
                }

                UI.Toast {
                    id: toast
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 24
                    z: 10
                    onActionClicked: {
                        if (actionText === qsTr("Show in Files")) {
                            PdfDocument.showInFolder();
                        } else if (actionText === qsTr("Save as")) {
                            window.saveAs();
                        }
                    }
                }
            }

            SignPane {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                visible: window.tool === "sign" && window.ready
                app: window
                onCloseRequested: window.setTool("select")
                onDrawRequested: signatureDialog.start("draw")
                onTypeRequested: signatureDialog.start("type")
                onImageRequested: pictureDialog.open()
                onSaveRequested: window.saveSigned()
            }
        }
    }

    SignatureDialog {
        id: signatureDialog
        onAdded: id => window.signatureId = id
    }

    FileDialog {
        id: pictureDialog
        title: qsTr("Choose a picture of your signature")
        fileMode: FileDialog.OpenFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg *.webp *.bmp)")]
        onAccepted: {
            const id = Signatures.addImage(selectedFile);
            if (id.length > 0) {
                window.signatureId = id;
                window.say(qsTr("Signature added. The paper around it is made see-through."));
            } else {
                window.say(qsTr("That picture couldn't be used. Try a photo or scan of your signature on white paper."));
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open a PDF")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: window.openUrl(selectedFile)
    }

    FileDialog {
        id: saveAsDialog
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: {
            window.keepPlace = true;
            const ok = PdfDocument.saveAs(selectedFile);
            window.keepPlace = false;
            window.say(ok ? qsTr("Saved") : PdfDocument.lastError());
        }
    }

    FileDialog {
        id: signedDialog
        title: qsTr("Save a signed copy")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: {
            // Off before the signed copy opens, or its pages would be given
            // initials again; back on if the copy couldn't be written.
            const initials = window.addInitials;
            window.addInitials = false;
            window.keepPlace = true;
            const ok = PdfDocument.saveSignedCopy(selectedFile, window.placements);
            window.keepPlace = false;
            if (ok) {
                window.placements = [];
                window.selectedPlacement = "";
                window.setTool("select");
                window.say(qsTr("Signed copy saved. The original hasn't changed."), qsTr("Show in Files"));
            } else {
                window.addInitials = initials;
                window.say(PdfDocument.lastError());
            }
        }
    }

    UI.Dialog {
        id: passwordDialog
        function ask() {
            passwordField.text = "";
            passwordField.error = false;
            open();
            passwordField.forceActiveFocus();
        }
        title: qsTr("This PDF has a password")
        text: qsTr("Type the password to open %1.").arg(PdfDocument.fileName)
        UI.TextField {
            id: passwordField
            Layout.fillWidth: true
            echoMode: TextInput.Password
            revealable: true
            placeholderText: qsTr("Password")
            onAccepted: unlockButton.clicked()
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: passwordDialog.close()
            },
            UI.Button {
                id: unlockButton
                kind: "primary"
                text: qsTr("Open")
                onClicked: {
                    if (PdfDocument.unlock(passwordField.text)) {
                        passwordDialog.close();
                    } else {
                        passwordField.error = true;
                        passwordField.selectAll();
                    }
                }
            }
        ]
    }

    UI.Dialog {
        id: removeSignatureDialog
        property string signature
        title: qsTr("Remove this signature?")
        text: qsTr("It is taken out of your keyring. PDFs you've already signed keep it.")
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: removeSignatureDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Remove")
                onClicked: {
                    const id = removeSignatureDialog.signature;
                    window.placements = window.placements.filter(p => p.signature !== id);
                    Signatures.remove(id);
                    removeSignatureDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: unsavedDialog
        title: qsTr("Save changes to %1?").arg(PdfDocument.fileName)
        text: qsTr("Your notes and what you filled in are lost if you don't save them.")
        buttons: [
            UI.Button {
                text: qsTr("Don't save")
                onClicked: {
                    unsavedDialog.close();
                    window.closeNow();
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
                    window.commitEdits();
                    if (PdfDocument.save()) {
                        window.closeNow();
                    } else {
                        window.say(PdfDocument.lastError(), qsTr("Save as"));
                    }
                }
            }
        ]
    }

    UI.Dialog {
        id: unsignedDialog
        title: qsTr("Close without saving a signed copy?")
        text: qsTr("The signature you placed isn't saved anywhere yet.")
        buttons: [
            UI.Button {
                text: qsTr("Close anyway")
                onClicked: {
                    unsignedDialog.close();
                    window.closeNow();
                }
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Keep working")
                onClicked: unsignedDialog.close()
            }
        ]
    }

    Component.onCompleted: {
        if (PdfDocument.locked) {
            passwordDialog.ask();
        } else if (startTool.length > 0) {
            setTool(startTool);
        } else {
            focusPages();
        }
    }
}
