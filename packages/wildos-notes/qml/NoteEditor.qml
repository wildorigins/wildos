import QtQuick
import WildOS.Notes
import WildOS.UI as UI

// The text of a note, edited as Markdown with the marks styled as you type
// (MarkdownEditor): what is saved is exactly what was typed. Checklist
// boxes, bullets and pictures are drawn over the marks they stand for.
Item {
    id: root

    property string baseDir
    property string placeholderText
    property bool readOnly: false
    // The view this scrolls in, kept on the cursor while typing.
    property Flickable flickable
    property bool loading: false
    property int layoutTick: 0
    property int lastCursor: 0
    readonly property alias textEdit: edit
    readonly property alias markdown: editor

    signal edited(string text)

    implicitHeight: Math.max(edit.contentHeight, metrics.height * 1.5)

    function load(text) {
        loading = true;
        editor.setText(text);
        edit.cursorPosition = 0;
        lastCursor = 0;
        loading = false;
        layoutTick++;
    }
    function text() {
        return editor.text();
    }
    function focusStart() {
        edit.forceActiveFocus();
        edit.cursorPosition = editor.clampCursor(0, 0);
    }
    function focusEnd() {
        edit.forceActiveFocus();
        edit.cursorPosition = edit.length;
    }
    function select(range) {
        if (range && range.start !== undefined) {
            edit.select(range.start, range.end);
        }
        edit.forceActiveFocus();
    }
    function wrapWith(open, close) {
        select(editor.wrap(edit.selectionStart, edit.selectionEnd, open, close));
    }
    function heading() {
        edit.cursorPosition = editor.toggleHeading(edit.cursorPosition);
        edit.forceActiveFocus();
    }
    function list(kind) {
        const had = edit.selectionStart !== edit.selectionEnd;
        const range = editor.toggleList(edit.selectionStart, edit.selectionEnd, kind);
        if (had) {
            select(range);
        } else {
            edit.cursorPosition = editor.clampCursor(range.end, range.end);
            edit.forceActiveFocus();
        }
    }
    function link() {
        select(editor.insertLink(edit.selectionStart, edit.selectionEnd));
    }
    function table() {
        edit.cursorPosition = editor.insertTable(edit.cursorPosition);
        edit.forceActiveFocus();
    }
    function picture(url) {
        const to = editor.insertImage(edit.cursorPosition, url);
        if (to >= 0) {
            edit.cursorPosition = to;
        }
        edit.forceActiveFocus();
        return to >= 0;
    }
    function current() {
        return editor.lineKind(edit.cursorPosition);
    }

    FontMetrics {
        id: metrics
        font: edit.font
    }

    MarkdownEditor {
        id: editor
        document: edit.textDocument
        cursorPosition: edit.cursorPosition
        baseDir: root.baseDir
        ink: UI.Style.ink
        inkMuted: UI.Style.inkMuted
        inkSubtle: UI.Style.inkSubtle
        link: UI.Style.accentText
        codeFill: UI.Style.surfaceControl
        fontFamily: UI.Style.fontFamily
        scale: UI.Style.textScale
        maxImageWidth: Math.max(120, Math.min(480, Math.round(edit.width)))
        onMarkersChanged: root.layoutTick++
    }

    TextEdit {
        id: edit
        width: parent.width
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        readOnly: root.readOnly
        selectByMouse: true
        persistentSelection: true
        font.family: UI.Style.fontFamily
        font.pixelSize: UI.Style.px(14.5)
        color: UI.Style.ink
        selectionColor: UI.Style.textSelection
        selectedTextColor: UI.Style.ink
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Note")
        Accessible.multiLine: true

        onTextChanged: {
            if (!root.loading) {
                root.edited(editor.text());
            }
        }
        onContentHeightChanged: root.layoutTick++
        onWidthChanged: root.layoutTick++
        onCursorPositionChanged: {
            if (selectionStart === selectionEnd && !root.loading) {
                const to = editor.clampCursor(cursorPosition, root.lastCursor);
                if (to !== cursorPosition) {
                    cursorPosition = to;
                    return;
                }
            }
            root.lastCursor = cursorPosition;
        }
        onCursorRectangleChanged: {
            const view = root.flickable;
            if (!view || !activeFocus) {
                return;
            }
            const at = edit.mapToItem(view.contentItem, cursorRectangle.x, cursorRectangle.y);
            if (at.y < view.contentY + 8) {
                view.contentY = Math.max(0, at.y - 8);
            } else if (at.y + cursorRectangle.height > view.contentY + view.height - 24) {
                view.contentY = Math.min(Math.max(0, view.contentHeight - view.height), at.y + cursorRectangle.height - view.height + 24);
            }
        }

        Keys.onPressed: event => {
            const selected = selectionStart !== selectionEnd;
            const control = (event.modifiers & Qt.ControlModifier) !== 0;
            const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
            if (textMenu.isMenuKey(event)) {
                textMenu.openAtCaret();
                event.accepted = true;
                return;
            }
            if (readOnly) {
                return;
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (control) {
                    return;
                }
                if (selected) {
                    remove(selectionStart, selectionEnd);
                }
                const to = editor.handleReturn(cursorPosition, shift);
                if (to >= 0) {
                    cursorPosition = to;
                    event.accepted = true;
                } else if (shift) {
                    // Markdown has no line break of its own; Shift+Enter
                    // writes an ordinary one rather than Qt's separator.
                    insert(cursorPosition, "\n");
                    event.accepted = true;
                }
            } else if (event.key === Qt.Key_Backspace && !selected && !control) {
                const to = editor.handleBackspace(cursorPosition);
                if (to >= 0) {
                    cursorPosition = to;
                    event.accepted = true;
                }
            } else if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) && !control) {
                const to = editor.handleTab(cursorPosition, event.key === Qt.Key_Backtab || shift);
                if (to >= 0) {
                    cursorPosition = to;
                    event.accepted = true;
                }
            } else if (control && event.key === Qt.Key_Z) {
                shift ? editor.redo() : editor.undo();
                event.accepted = true;
            } else if (control && !shift && event.key === Qt.Key_Y) {
                editor.redo();
                event.accepted = true;
            } else if (control && !shift && event.key === Qt.Key_B) {
                root.wrapWith("**", "**");
                event.accepted = true;
            } else if (control && !shift && event.key === Qt.Key_I) {
                root.wrapWith("*", "*");
                event.accepted = true;
            } else if (control && !shift && event.key === Qt.Key_U) {
                root.wrapWith("<u>", "</u>");
                event.accepted = true;
            } else if (control && !shift && event.key === Qt.Key_K) {
                root.link();
                event.accepted = true;
            } else if (control && shift && event.key === Qt.Key_L) {
                root.list("check");
                event.accepted = true;
            }
        }

        // Undo goes through the editor, as Ctrl+Z does, so stepping back
        // keeps each edit's line spacing with it.
        UI.TextMenu {
            id: textMenu
            undoAction: () => editor.undo()
        }

        // Ctrl+click follows a link, as in other editors; a plain click
        // puts the cursor in it to change it.
        TapHandler {
            acceptedModifiers: Qt.ControlModifier
            onTapped: point => {
                const url = editor.linkAt(edit.positionAt(point.position.x, point.position.y));
                if (url.length > 0) {
                    Qt.openUrlExternally(url);
                }
            }
        }
    }

    UI.Label {
        visible: edit.length === 0 && !root.readOnly
        width: parent.width
        text: root.placeholderText
        size: 14.5
        color: UI.Style.inkPlaceholder
        wrapMode: Text.Wrap
    }

    Repeater {
        model: editor.markers

        delegate: Item {
            id: marker

            required property var modelData
            readonly property rect at: {
                root.layoutTick;
                // Marks from the text before a new note was loaded may point
                // past its end for a moment.
                return modelData.pos <= edit.length ? edit.positionToRectangle(modelData.pos) : Qt.rect(0, 0, 0, 0);
            }
            // The text line the mark sits on, not the extra line spacing.
            readonly property real lineMiddle: at.y + Math.min(at.height, metrics.height) / 2

            Rectangle {
                id: box
                visible: marker.modelData.kind === "check"
                x: marker.at.x
                y: Math.round(marker.lineMiddle - height / 2)
                width: 18
                height: 18
                radius: 5
                color: marker.modelData.checked ? (boxMouse.containsMouse ? UI.Style.accentHover : UI.Style.accent) : boxMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                border.width: marker.modelData.checked ? 0 : 1.5
                border.color: UI.Style.toggleOffBorder
                Accessible.role: Accessible.CheckBox
                Accessible.checkable: true
                Accessible.checked: marker.modelData.checked === true

                UI.Icon {
                    visible: marker.modelData.checked === true
                    anchors.centerIn: parent
                    name: "checkmark"
                    size: 12
                    strokeWidth: 3
                    color: UI.Style.textOnAccent
                }
                MouseArea {
                    id: boxMouse
                    anchors.fill: parent
                    anchors.margins: -3
                    enabled: !root.readOnly
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: editor.toggleCheck(marker.modelData.pos)
                }
            }

            Rectangle {
                visible: marker.modelData.kind === "bullet"
                x: marker.at.x + 7
                y: Math.round(marker.lineMiddle - height / 2)
                width: 5
                height: 5
                radius: 2.5
                color: UI.Style.ink
            }

            Image {
                visible: marker.modelData.kind === "image" && !(edit.activeFocus && editor.cursorBlock === marker.modelData.block)
                x: marker.at.x
                y: marker.at.y + 4
                width: marker.modelData.width ?? 0
                height: marker.modelData.height ?? 0
                source: marker.modelData.kind === "image" ? marker.modelData.source : ""
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                mipmap: true
            }
        }
    }

    DropArea {
        anchors.fill: parent
        enabled: !root.readOnly && root.baseDir.length > 0
        onDropped: drop => {
            if (!drop.hasUrls) {
                return;
            }
            let at = edit.positionAt(drop.x, drop.y);
            for (const url of drop.urls) {
                if (/\.(png|jpe?g|gif|webp|bmp|svg)$/i.test(url.toString())) {
                    const to = editor.insertImage(at, url);
                    if (to >= 0) {
                        at = to;
                        edit.cursorPosition = to;
                    }
                }
            }
            drop.accept(Qt.CopyAction);
        }
    }
}
