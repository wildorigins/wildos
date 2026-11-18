import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import WildOS.Notes
import WildOS.UI as UI

// The quick note Super+N opens anywhere: a small window that becomes a
// note in the default notebook with the first word typed, and saves as you
// type. The first line is its title.
UI.AppWindow {
    id: window

    stateKey: "quicknote"
    width: 400
    height: 320
    minimumWidth: 300
    minimumHeight: 220
    title: qsTr("Quick note")

    NoteDocument {
        id: doc
    }

    function update(text) {
        if (!doc.open) {
            if (text.trim().length === 0 || !doc.create(NoteStore.defaultNotebook)) {
                return;
            }
        }
        const lineEnd = text.indexOf("\n");
        const first = (lineEnd < 0 ? text : text.slice(0, lineEnd)).replace(/^#+\s+/, "");
        doc.title = first;
        doc.setBody(lineEnd < 0 ? "" : text.slice(lineEnd + 1).replace(/^\n+/, ""));
    }

    onClosing: doc.close()

    Shortcut {
        sequence: "Escape"
        onActivated: window.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: window.frame
            showTitle: false
            buttons.showMaximise: false

            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/WildOS/Notes/data/wildos-notes.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Quick note")
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }

            ToolButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                side: 30
                iconName: "open"
                iconSize: 15
                enabled: doc.open
                tip: qsTr("Open in Notes")
                onClicked: {
                    const path = doc.path;
                    doc.save();
                    NotesApp.openNote(path);
                    window.close();
                }
            }
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: UI.ScrollBar {}

            MouseArea {
                width: scroller.width
                height: Math.max(scroller.contentHeight, scroller.height)
                cursorShape: Qt.IBeamCursor
                onClicked: body.focusEnd()
            }

            NoteEditor {
                id: body
                x: 20
                y: 12
                width: scroller.width - 40
                flickable: scroller
                baseDir: doc.noteDir
                placeholderText: qsTr("Type a note. The first line is its title.")
                onEdited: text => window.update(text)
                Component.onCompleted: body.textEdit.forceActiveFocus()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: UI.Style.borderSubtle
        }

        UI.Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            leftPadding: 20
            rightPadding: 20
            text: !doc.open ? qsTr("Saved to %1 as you type").arg(NoteStore.defaultNotebook)
                : doc.saved ? qsTr("Saved to %1 on this PC").arg(doc.notebook)
                : qsTr("Saving")
            size: 12
            color: UI.Style.inkSubtle
        }
    }
}
