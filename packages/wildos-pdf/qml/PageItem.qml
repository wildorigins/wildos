import QtQuick
import QtQuick.Shapes
import QtQuick.Window
import WildOS.PDF
import WildOS.UI as UI

// One page: drawn by Poppler, with what the tools put over it. Everything
// on it is placed in fractions of the page (0 to 1 across and down), as
// PdfDocument hands them over.
Item {
    id: page

    property int index: 0
    property var app
    // Screen pixels per PDF point at the current zoom.
    property real pointScale: 1
    readonly property int revision: PdfDocument.revisions[index] ?? 0
    readonly property string tool: app.tool
    // The sharp image is asked for again only once zooming has paused.
    property int sharpWidth: 0
    property var fields: []
    property var inkPoints: []

    // Paper colours: these sit on the page itself, which is white in either
    // scheme, so they are the document's colours rather than the window's.
    readonly property color paperFieldFill: "#eef3ff"
    readonly property color paperFieldEdge: "#7f9ad8"
    readonly property color paperInk: "#1f2633"
    readonly property color penInk: "#1f2a6b"

    function norm(x, y) {
        return Qt.point(Math.max(0, Math.min(1, x / width)), Math.max(0, Math.min(1, y / height)));
    }
    function refreshFields() {
        fields = tool === "fill" ? PdfDocument.formFields(index) : [];
    }
    function pageUrl(pixels) {
        return "image://page/" + PdfDocument.generation + "/" + index + "/" + pixels + "/" + revision;
    }

    onToolChanged: refreshFields()
    Component.onCompleted: {
        sharpWidth = Math.round(width * Screen.devicePixelRatio);
        refreshFields();
    }
    onWidthChanged: {
        if (sharpWidth === 0) {
            sharpWidth = Math.round(width * Screen.devicePixelRatio);
        } else {
            sharpen.restart();
        }
    }

    Connections {
        target: PdfDocument
        function onLoadedChanged() {
            page.refreshFields();
        }
    }

    Timer {
        id: sharpen
        interval: 160
        onTriggered: page.sharpWidth = Math.round(page.width * Screen.devicePixelRatio)
    }

    // A soft edge under the sheet (the board's 0 4px 20px shadow, in short).
    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 0
        anchors.bottomMargin: -3
        radius: 2
        color: UI.Style.dark ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(0, 0, 0, 0.10)
    }

    Rectangle {
        anchors.fill: parent
        color: "#ffffff"
    }

    // The thumbnail's picture, already drawn and cached, fills the page at
    // once and is covered by the sharp one when it arrives.
    Image {
        anchors.fill: parent
        source: page.pageUrl(Math.round(104 * Screen.devicePixelRatio))
        asynchronous: true
        smooth: true
        visible: sharp.status !== Image.Ready
    }

    Image {
        id: sharp
        anchors.fill: parent
        source: page.sharpWidth > 0 ? page.pageUrl(page.sharpWidth) : ""
        asynchronous: true
        smooth: true
        retainWhileLoading: true
    }

    // Search matches; the current one is stronger.
    Repeater {
        model: page.app.hits.filter(h => h.page === page.index)
        delegate: Rectangle {
            required property var modelData
            x: modelData.x * page.width - 1
            y: modelData.y * page.height - 1
            width: modelData.width * page.width + 2
            height: modelData.height * page.height + 2
            radius: 2
            color: page.app.isCurrentHit(modelData) ? Qt.rgba(1, 0.6, 0, 0.45) : Qt.rgba(1, 0.85, 0, 0.35)
        }
    }

    // Selected text, in the accent's selection colour.
    Repeater {
        model: page.app.selectionPage === page.index ? page.app.selectionRects : []
        delegate: Rectangle {
            required property var modelData
            x: modelData.x * page.width
            y: modelData.y * page.height
            width: modelData.width * page.width
            height: modelData.height * page.height
            color: page.tool === "highlight" ? Qt.rgba(1, 0.85, 0.2, 0.45) : UI.Style.textSelection
        }
    }

    // The line being drawn with Draw, before it becomes an ink note.
    Shape {
        anchors.fill: parent
        visible: page.inkPoints.length > 1
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: page.penInk
            strokeWidth: 2 * page.pointScale
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline {
                path: page.inkPoints.map(p => Qt.point(p.x * page.width, p.y * page.height))
            }
        }
    }

    MouseArea {
        id: tools
        anchors.fill: parent
        enabled: page.tool !== "fill"
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        // A drag here selects, draws or places; the wheel still scrolls.
        preventStealing: true
        cursorShape: page.tool === "draw" || page.tool === "sign" ? Qt.CrossCursor : Qt.IBeamCursor
        property point start

        onPressed: mouse => {
            page.app.focusPages();
            if (mouse.button === Qt.RightButton) {
                return;
            }
            start = page.norm(mouse.x, mouse.y);
            if (page.tool === "draw") {
                page.inkPoints = [start];
            } else if (page.tool === "select" || page.tool === "highlight") {
                page.app.clearSelection();
            } else if (page.tool === "sign") {
                page.app.selectedPlacement = "";
            }
        }
        onPositionChanged: mouse => {
            if (!pressed || pressedButtons !== Qt.LeftButton) {
                return;
            }
            const here = page.norm(mouse.x, mouse.y);
            if (page.tool === "draw") {
                const last = page.inkPoints[page.inkPoints.length - 1];
                // Every pixel or so is plenty; it keeps the note small.
                if (Math.abs(here.x - last.x) * page.width + Math.abs(here.y - last.y) * page.height >= 1.5) {
                    page.inkPoints = page.inkPoints.concat([here]);
                }
            } else if (page.tool === "select" || page.tool === "highlight") {
                page.app.setSelection(page.index, PdfDocument.selectText(page.index, start, here));
            }
        }
        onReleased: mouse => {
            if (mouse.button === Qt.RightButton) {
                return;
            }
            const here = page.norm(mouse.x, mouse.y);
            if (page.tool === "draw") {
                if (page.inkPoints.length > 1) {
                    PdfDocument.addInk(page.index, page.inkPoints);
                }
                page.inkPoints = [];
            } else if (page.tool === "highlight") {
                if (page.app.selectionPage === page.index && page.app.selectionRects.length > 0) {
                    PdfDocument.addHighlight(page.index, page.app.selectionRects);
                }
                page.app.clearSelection();
            } else if (page.tool === "text") {
                editor.begin(here);
            } else if (page.tool === "sign" && Math.abs(here.x - start.x) * page.width + Math.abs(here.y - start.y) * page.height < 4) {
                page.app.placeSignature(page.index, here);
            }
        }
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton && page.tool === "select" && page.app.selectionText.length > 0) {
                selectionMenu.popup();
            }
        }
    }

    UI.Menu {
        id: selectionMenu
        entries: [
            { text: qsTr("Copy"), icon: "copy", shortcut: "Ctrl+C", run: () => page.app.copySelection() },
            { text: qsTr("Highlight"), run: () => page.app.highlightSelection(), enabled: PdfDocument.canAnnotate }
        ]
    }

    // Text typed onto the page with the Text tool. Enter puts it on the
    // page, Shift+Enter starts a new line, Escape drops it.
    Rectangle {
        id: editor
        property point at
        property bool open: false
        readonly property real fontPoints: 11

        function begin(point) {
            if (open) {
                commit();
            }
            at = point;
            input.text = "";
            open = true;
            input.forceActiveFocus();
        }
        function commit() {
            if (!open) {
                return;
            }
            open = false;
            const text = input.text;
            if (text.trim().length > 0) {
                // A little more room than the words need, so Poppler never
                // wraps them onto a line the box has no space for.
                const w = (input.contentWidth + 8 * page.pointScale) / page.width;
                const h = (input.contentHeight + 4 * page.pointScale) / page.height;
                PdfDocument.addText(page.index, Qt.rect(at.x, at.y, Math.min(w, 1 - at.x), Math.min(h, 1 - at.y)), text, fontPoints);
            }
            input.text = "";
        }

        visible: open
        x: at.x * page.width - 3
        y: at.y * page.height - 3
        width: Math.max(80, input.contentWidth + 12)
        height: input.contentHeight + 6
        color: "transparent"
        border.width: 1
        border.color: UI.Style.accent
        radius: 3

        TextEdit {
            id: input
            x: 3
            y: 3
            width: Math.max(60, contentWidth + 2)
            font.family: "Liberation Sans"
            font.pixelSize: editor.fontPoints * page.pointScale
            color: "#000000"
            selectionColor: UI.Style.textSelection
            selectedTextColor: "#000000"
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
            // Escape belongs to the text being typed, not the window's
            // shortcut that leaves the tool.
            Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Escape
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                    editor.commit();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    editor.open = false;
                    input.text = "";
                    event.accepted = true;
                } else if (inputMenu.isMenuKey(event)) {
                    inputMenu.openAtCaret();
                    event.accepted = true;
                }
            }
            // The text menu holds focus while it is open; the text is still
            // being typed.
            onActiveFocusChanged: if (!activeFocus && !inputMenu.opened) editor.commit()

            UI.TextMenu {
                id: inputMenu
                onFocusLeft: editor.commit()
            }
        }
    }

    Connections {
        target: page.app
        function onCommitEdits() {
            editor.commit();
        }
    }

    // Fill in: the form's own fields, each ready to type in or click.
    Repeater {
        model: page.fields
        delegate: Item {
            id: field
            required property var modelData
            readonly property string kind: modelData.kind
            x: modelData.x * page.width
            y: modelData.y * page.height
            width: modelData.width * page.width
            height: modelData.height * page.height

            Rectangle {
                anchors.fill: parent
                visible: field.kind === "text"
                color: page.paperFieldFill
                border.width: textBox.activeFocus ? 1.5 : 0
                border.color: UI.Style.accent

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: page.paperFieldEdge
                }

                TextEdit {
                    id: textBox
                    anchors.fill: parent
                    anchors.leftMargin: 3
                    anchors.rightMargin: 3
                    enabled: field.kind === "text"
                    text: field.modelData.value ?? ""
                    font.family: "Liberation Sans"
                    font.pixelSize: Math.max(6, (field.modelData.fontSize > 0 ? field.modelData.fontSize : Math.min(12, field.modelData.height * page.height / page.pointScale * 0.62)) * page.pointScale)
                    color: page.paperInk
                    selectionColor: UI.Style.textSelection
                    selectedTextColor: page.paperInk
                    textFormat: TextEdit.PlainText
                    wrapMode: field.modelData.multiline ? TextEdit.Wrap : TextEdit.NoWrap
                    verticalAlignment: field.modelData.multiline ? TextEdit.AlignTop : TextEdit.AlignVCenter
                    clip: true
                    activeFocusOnTab: true
                    property bool dirty: false
                    onTextChanged: {
                        const most = field.modelData.maxLength ?? 0;
                        if (most > 0 && text.length > most) {
                            remove(most, text.length);
                        }
                        // A paste from the text menu lands while the menu holds focus.
                        dirty = activeFocus || textBoxMenu.opened;
                    }
                    function save() {
                        if (dirty) {
                            dirty = false;
                            PdfDocument.setFieldText(page.index, field.modelData.id, text);
                        }
                    }
                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !field.modelData.multiline) {
                            save();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Tab) {
                            save();
                            nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                        } else if (textBoxMenu.isMenuKey(event)) {
                            textBoxMenu.openAtCaret();
                            event.accepted = true;
                        }
                    }
                    onActiveFocusChanged: if (!activeFocus && !textBoxMenu.opened) save()
                    UI.TextMenu {
                        id: textBoxMenu
                        onFocusLeft: textBox.save()
                    }
                    Connections {
                        target: page.app
                        function onCommitEdits() {
                            textBox.save();
                        }
                    }
                    Accessible.name: field.modelData.name
                }
            }

            // Ticks, round choices and lists: the page shows their state, so
            // here they are only a place to click, outlined under the pointer.
            Rectangle {
                anchors.fill: parent
                visible: field.kind !== "text"
                color: picker.containsMouse ? Qt.rgba(0.21, 0.39, 0.85, 0.12) : Qt.rgba(0.21, 0.39, 0.85, 0.06)
                border.width: picker.containsMouse ? 1.5 : 1
                border.color: picker.containsMouse ? UI.Style.accent : Qt.rgba(0.5, 0.6, 0.85, 0.6)
                radius: field.kind === "radio" ? width / 2 : 2

                MouseArea {
                    id: picker
                    anchors.fill: parent
                    enabled: field.kind !== "text"
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.name: field.modelData.name
                    Accessible.role: field.kind === "choice" ? Accessible.ComboBox : field.kind === "radio" ? Accessible.RadioButton : Accessible.CheckBox
                    onClicked: {
                        // Keeps what was just typed: the list below is read
                        // again from the file, and focus stays in the page.
                        page.app.commitEdits();
                        page.app.focusPages();
                        if (field.kind === "check") {
                            PdfDocument.setFieldChecked(page.index, field.modelData.id, !field.modelData.checked);
                            page.refreshFields();
                        } else if (field.kind === "radio") {
                            PdfDocument.setFieldChecked(page.index, field.modelData.id, true);
                            page.refreshFields();
                        } else if (field.kind === "choice") {
                            choices.popup(field, 0, field.height + 2);
                        }
                    }
                }
            }

            UI.Menu {
                id: choices
                entries: (field.modelData.choices ?? []).map((c, i) => ({
                    text: c,
                    radio: true,
                    checked: i === field.modelData.current,
                    run: () => {
                        PdfDocument.setFieldChoice(page.index, field.modelData.id, i);
                        page.refreshFields();
                    }
                }))
            }
        }
    }

    // Signatures, dates and initials waiting for Save a signed copy.
    Repeater {
        model: page.app.placements.filter(p => p.page === page.index)
        delegate: Placement {
            required property var modelData
            placement: modelData
            app: page.app
            pageItem: page
        }
    }
}
