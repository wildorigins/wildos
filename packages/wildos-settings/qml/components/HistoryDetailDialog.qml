import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// One event of System > History (HistoryDetail.dc): what happened, what
// Wild OS did and what you can do, in plain words, then the lines it was
// made from, which Copy puts on the clipboard. Report a problem opens
// wildos-report with the event attached.
RecoveryDialog {
    id: dialog

    property var event: null
    readonly property var words: event ? HistoryWords.describe(event, History.events) : ({})
    property bool copied: false

    signal failed(string message)

    function openFor(e) {
        event = e;
        copied = false;
        open();
    }

    title: words.title || ""
    iconName: words.icon || "info"
    dialogWidth: 600

    Repeater {
        model: [
            { heading: "What happened", text: dialog.words.happened || "" },
            { heading: "What Wild OS did", text: dialog.words.did || "" },
            { heading: "What you can do", text: dialog.words.todo || "" }
        ]

        ColumnLayout {
            id: part
            required property var modelData
            Layout.fillWidth: true
            spacing: 4

            UI.Label {
                Layout.fillWidth: true
                text: part.modelData.heading.toUpperCase()
                size: 12.5
                font.weight: UI.Style.weightBold
                font.letterSpacing: 0.25
                color: UI.Style.inkSubtle
            }

            UI.Label {
                Layout.fillWidth: true
                text: part.modelData.text
                size: 13.5
                lineHeightMode: Text.ProportionalHeight
                lineHeight: 1.5
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        UI.Label {
            Layout.fillWidth: true
            text: "TECHNICAL DETAIL"
            size: 12.5
            font.weight: UI.Style.weightBold
            font.letterSpacing: 0.25
            color: UI.Style.inkSubtle
        }

        UI.LinkButton {
            text: dialog.copied ? "Copied" : "Copy"
            Accessible.name: "Copy the technical detail"
            onClicked: {
                clipboard.text = dialog.event ? HistoryWords.fullText(dialog.event, History.events) : "";
                clipboard.selectAll();
                clipboard.copy();
                dialog.copied = true;
                copiedTimer.restart();
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: techText.implicitHeight + 24
        radius: 8
        color: UI.Style.dark ? Qt.rgba(0, 0, 0, 0.28) : Qt.rgba(0, 0, 0, 0.04)
        border.width: 1
        border.color: UI.Style.tone(0.07)
        clip: true

        // Selectable, so part of it can be copied as well as all of it.
        // TextEdit has no line height of its own; the board's 1.45 comes
        // from the rich text instead.
        TextEdit {
            id: techText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.topMargin: 12
            readonly property string plain: dialog.event ? HistoryWords.techText(dialog.event) : ""
            text: "<div style=\"line-height: 145%; white-space: pre-wrap\">"
                + plain.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") + "</div>"
            textFormat: TextEdit.RichText
            readOnly: true
            selectByMouse: true
            font.family: "monospace"
            font.pixelSize: UI.Style.px(11.5)
            color: UI.Style.inkMuted
            selectionColor: UI.Style.textSelection
            selectedTextColor: UI.Style.ink
            wrapMode: TextEdit.WrapAnywhere
            Keys.onPressed: event => {
                if (techMenu.isMenuKey(event)) {
                    techMenu.openAtCaret();
                    event.accepted = true;
                }
            }

            UI.TextMenu {
                id: techMenu
            }
        }
    }

    TextEdit {
        id: clipboard
        visible: false
    }

    Timer {
        id: copiedTimer
        interval: 2000
        onTriggered: dialog.copied = false
    }

    buttons: [
        UI.Button {
            implicitHeight: 32
            text: "Report a problem"
            onClicked: {
                const e = dialog.event;
                const sent = History.report({ id: e.id, kind: e.kind, when: e.when, data: e.data, title: dialog.words.title },
                                            HistoryWords.fullText(e, History.events));
                dialog.close();
                if (!sent) {
                    dialog.failed("Report a problem isn't installed, or this event couldn't be handed to it.");
                }
            }
        },
        UI.Button {
            implicitHeight: 32
            kind: "primary"
            text: "Close"
            onClicked: dialog.close()
        }
    ]
}
