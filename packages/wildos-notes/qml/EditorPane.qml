import QtCore
import QtQuick
import QtQuick.Templates as T
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Notes
import WildOS.UI as UI

// The right-hand side (AppNotes.dc): the formatting bar with "Saved on this
// PC", pin, share and more at its right, then the note: chips for its
// notebook and tags, the title, when it was edited, and the text.
Item {
    id: pane

    required property NoteDocument doc

    signal showNotebook(string name)
    signal showTag(string tag)
    signal moveRequested(string path, string notebook)
    signal deleteRequested(string path)
    signal said(string message)

    property bool syncing: false
    // The line the cursor is on, for the lit formatting buttons.
    property string lineKind: ""

    function focusTitle() {
        titleEdit.forceActiveFocus();
    }
    function focusText() {
        body.focusEnd();
    }
    function syncTitle() {
        syncing = true;
        titleEdit.text = doc.title;
        syncing = false;
    }

    Connections {
        target: pane.doc
        function onBodyReplaced() {
            pane.syncTitle();
            body.load(pane.doc.body());
            scroller.contentY = 0;
        }
        function onTitleChanged() {
            if (!titleEdit.activeFocus) {
                pane.syncTitle();
            }
        }
        function onPathChanged() {
            if (!pane.doc.open) {
                pane.syncTitle();
                body.load("");
            }
        }
    }
    Connections {
        target: body.textEdit
        function onCursorPositionChanged() {
            pane.lineKind = body.current();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The formatting bar: 46px, 14 in, a hairline under it.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 46

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: UI.Style.borderSubtle
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                enabled: pane.doc.open && !pane.doc.readOnly

                ToolButton {
                    side: 30
                    iconName: "heading"
                    tip: qsTr("Heading")
                    active: pane.lineKind === "heading"
                    onClicked: body.heading()
                }
                ToolButton {
                    side: 30
                    iconName: "bold"
                    tip: qsTr("Bold (Ctrl+B)")
                    onClicked: body.wrapWith("**", "**")
                }
                ToolButton {
                    side: 30
                    iconName: "italic"
                    tip: qsTr("Italic (Ctrl+I)")
                    onClicked: body.wrapWith("*", "*")
                }
                ToolButton {
                    side: 30
                    iconName: "underline"
                    tip: qsTr("Underline (Ctrl+U)")
                    onClicked: body.wrapWith("<u>", "</u>")
                }
                ToolButton {
                    side: 30
                    iconName: "bullets"
                    tip: qsTr("Bulleted list")
                    active: pane.lineKind === "bullet"
                    onClicked: body.list("bullet")
                }
                ToolButton {
                    side: 30
                    iconName: "checklist"
                    tip: qsTr("Checklist (Ctrl+Shift+L)")
                    active: pane.lineKind === "check"
                    onClicked: body.list("check")
                }
                ToolButton {
                    side: 30
                    iconName: "link"
                    tip: qsTr("Link (Ctrl+K)")
                    onClicked: body.link()
                }
                ToolButton {
                    side: 30
                    iconName: "image"
                    tip: qsTr("Picture")
                    onClicked: pictureDialog.open()
                }
                ToolButton {
                    side: 30
                    iconName: "table"
                    tip: qsTr("Table")
                    onClicked: body.table()
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                visible: pane.doc.open

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: 8
                    text: pane.doc.readOnly ? qsTr("Read only") : pane.doc.saved ? qsTr("Saved on this PC") : qsTr("Saving")
                    size: 12
                    color: UI.Style.inkSubtle
                }
                ToolButton {
                    iconName: "pin"
                    lit: pane.doc.pinned
                    enabled: !pane.doc.readOnly
                    tip: pane.doc.pinned ? qsTr("Unpin") : qsTr("Pin")
                    onClicked: pane.doc.pinned = !pane.doc.pinned
                }
                ToolButton {
                    id: shareButton
                    iconName: "share"
                    tip: qsTr("Share")
                    onClicked: shareMenu.popup(shareButton, shareButton.width - shareMenu.width, shareButton.height + 4)

                    UI.Menu {
                        id: shareMenu
                        entries: [
                            { text: qsTr("Copy the text"), icon: "copy", run: () => {
                                NoteStore.copyText(pane.doc.markdown());
                                pane.said(qsTr("Copied"));
                            } },
                            { text: qsTr("Email"), icon: "mail", run: () => Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent(pane.doc.title) + "&body=" + encodeURIComponent(pane.doc.body())) },
                            { separator: true },
                            { text: qsTr("Show in Files"), icon: "folder", run: () => NoteStore.showInFiles(pane.doc.path) }
                        ]
                    }
                }
                ToolButton {
                    id: moreButton
                    iconName: "dots"
                    tip: qsTr("More")
                    onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height + 4)

                    UI.Menu {
                        id: moreMenu
                        entries: [
                            { text: qsTr("Colour"), icon: "palette", enabled: !pane.doc.readOnly, submenu: [
                                { text: qsTr("None"), radio: true, checked: !NoteColours.known(pane.doc.colour), run: () => pane.doc.colour = "" }
                            ].concat(NoteColours.all.map(c => ({ text: c.name, radio: true, checked: pane.doc.colour === c.id, run: () => pane.doc.colour = c.id }))) },
                            { text: qsTr("Move to"), icon: "folder", submenu: NoteStore.notebooks.map(n => ({
                                text: n.name, enabled: n.name !== pane.doc.notebook, run: () => pane.moveRequested(pane.doc.path, n.name)
                            })) },
                            { text: qsTr("Show in Files"), icon: "open-folder", run: () => NoteStore.showInFiles(pane.doc.path) },
                            { separator: true },
                            { text: qsTr("Delete"), icon: "delete", danger: true, run: () => pane.deleteRequested(pane.doc.path) },
                            { separator: true },
                            { text: qsTr("Report a problem"), icon: "warning", run: () => Qt.openUrlExternally("wildos-report:notes") }
                        ]
                    }
                }
            }
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: pane.doc.open
            clip: true
            contentWidth: width
            contentHeight: page.height + 56 + 120
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: UI.ScrollBar {}

            // Clicking below the text puts the cursor at its end.
            MouseArea {
                width: scroller.width
                height: Math.max(scroller.contentHeight, scroller.height)
                cursorShape: Qt.IBeamCursor
                enabled: !pane.doc.readOnly
                onClicked: body.focusEnd()
            }

            ColumnLayout {
                id: page
                x: 48
                y: 28
                width: Math.min(680, scroller.width - 96)
                spacing: 10

                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: [{ kind: "notebook", text: pane.doc.notebook }].concat(pane.doc.tags.map(t => ({ kind: "tag", text: "#" + t, tag: t })))

                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            width: chipLabel.implicitWidth + 18
                            height: 22
                            radius: 11
                            color: chipMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.layer(0.07, false, 0.049)
                            Accessible.role: Accessible.Button
                            Accessible.name: chipLabel.text

                            UI.Label {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: chip.modelData.text
                                size: 12
                                color: UI.Style.inkMuted
                            }
                            MouseArea {
                                id: chipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: chip.modelData.kind === "tag" ? pane.showTag(chip.modelData.tag) : pane.showNotebook(chip.modelData.text)
                            }
                        }
                    }
                }

                TextEdit {
                    id: titleEdit
                    Layout.fillWidth: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    readOnly: pane.doc.readOnly
                    selectByMouse: true
                    font.family: UI.Style.fontFamily
                    font.pixelSize: UI.Style.fontTitleXl
                    font.weight: UI.Style.weightBold
                    font.letterSpacing: UI.Style.tracking(UI.Style.fontTitleXl)
                    color: UI.Style.ink
                    selectionColor: UI.Style.textSelection
                    selectedTextColor: UI.Style.ink
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Title")

                    onTextChanged: {
                        if (!pane.syncing) {
                            pane.doc.title = text;
                        }
                    }
                    // Resetting the text would drop the selection the open text
                    // menu is about to copy; the menu holds focus while it is up.
                    onActiveFocusChanged: if (!activeFocus && !titleMenu.opened) pane.syncTitle()
                    Keys.onPressed: event => {
                        if (titleMenu.isMenuKey(event)) {
                            titleMenu.openAtCaret();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            body.focusStart();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Down && cursorRectangle.y + cursorRectangle.height >= contentHeight - 2) {
                            body.focusStart();
                            event.accepted = true;
                        }
                    }

                    UI.TextMenu {
                        id: titleMenu
                        onFocusLeft: pane.syncTitle()
                    }

                    UI.Label {
                        visible: titleEdit.length === 0
                        text: qsTr("Title")
                        size: 28
                        font.weight: UI.Style.weightBold
                        color: UI.Style.inkPlaceholder
                    }
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: pane.doc.editedText
                    size: 12.5
                    color: UI.Style.inkSubtle
                }

                UI.Label {
                    visible: pane.doc.readOnly
                    Layout.fillWidth: true
                    text: qsTr("This note isn't UTF-8 text, so Notes shows it but leaves it as it is.")
                    size: 12.5
                    color: UI.Style.warning
                    wrapMode: Text.Wrap
                }

                NoteEditor {
                    id: body
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    flickable: scroller
                    baseDir: pane.doc.noteDir
                    readOnly: pane.doc.readOnly
                    placeholderText: qsTr("Start writing. Notes saves as you type.")
                    onEdited: text => pane.doc.setBody(text)
                }
            }
        }

        UI.Label {
            visible: !pane.doc.open
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Pick a note, or make a new one.")
            size: 13
            color: UI.Style.inkSubtle
        }
    }

    FileDialog {
        id: pictureDialog
        title: qsTr("Add a picture")
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg)")]
        onAccepted: {
            if (!body.picture(selectedFile)) {
                pane.said(qsTr("Notes couldn't add that picture."));
            }
        }
    }
}
