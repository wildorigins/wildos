import QtQuick
import QtQuick.Layouts
import WildOS.Files
import WildOS.UI as UI

// Questions from a copy, move or delete in progress: name clashes, files
// that cannot be read, and confirmations before anything is lost.
UI.Dialog {
    id: prompt

    required property bool mine
    // Keeps the question it opened with while the dialog fades out. Answering
    // clears FileOps.prompt at once, and a live binding emptied the title and
    // text and turned Cancel into Stop during the fade (#23).
    property var p: ({})
    readonly property string kind: p.kind ?? ""
    property bool all: false
    property bool answered: false

    title: p.title ?? ""
    text: p.text ?? ""

    // Reads FileOps.prompt itself: a binding on it may not have caught up
    // when this handler runs, which once left every question unasked and the
    // queue behind it stuck (Empty Recycle Bin did nothing).
    Connections {
        target: FileOps
        function onPromptChanged() {
            const next = FileOps.prompt;
            prompt.all = false;
            prompt.answered = false;
            if ((next.kind ?? "").length > 0) {
                prompt.p = next;
                if (prompt.mine) {
                    prompt.open();
                }
            } else {
                prompt.close();
            }
        }
    }
    onMineChanged: if (mine && (FileOps.prompt.kind ?? "").length > 0) {
        p = FileOps.prompt;
        open();
    }

    // A question where going ahead could do harm (running a program nothing
    // has checked) puts the keyboard on Cancel, so Enter does nothing risky.
    readonly property bool cancelDefault: p.cancelDefault === true
    onOpened: if (cancelDefault) cancelButton.forceActiveFocus(Qt.TabFocusReason)

    // Escape, or the job ending under it, counts as Cancel.
    onAboutToHide: if (!answered && (FileOps.prompt.kind ?? "").length > 0) {
        answered = true;
        FileOps.answer("cancel", false);
    }

    function reply(choice) {
        // The buttons stay clickable while the dialog fades out, and a click
        // then would answer the next question in the queue.
        if (answered || (FileOps.prompt.kind ?? "").length === 0) {
            return;
        }
        answered = true;
        close();
        FileOps.answer(choice, all);
    }

    GridLayout {
        visible: (prompt.p.incoming ?? "").length > 0
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 16
        rowSpacing: 4
        UI.Label { text: qsTr("Copying"); color: UI.Style.inkMuted; font.pixelSize: UI.Style.px(12) }
        UI.Label { text: prompt.p.incoming ?? ""; font.pixelSize: UI.Style.px(12) }
        UI.Label { text: qsTr("Already there"); color: UI.Style.inkMuted; font.pixelSize: UI.Style.px(12) }
        UI.Label { text: prompt.p.existing ?? ""; font.pixelSize: UI.Style.px(12) }
    }

    UI.CheckBox {
        visible: prompt.p.multiple === true
        text: qsTr("Do this for every clash")
        checked: prompt.all
        onToggled: prompt.all = checked
    }

    // The main choice sits at the right, as in DialogCopy.dc.html.
    buttons: [
        UI.Button {
            id: cancelButton
            visible: prompt.p.informational !== true
            text: prompt.kind === "confirm" ? "Cancel" : "Stop"
            onClicked: prompt.reply("cancel")
        },
        UI.Button {
            visible: (prompt.kind === "conflict" && prompt.p.canSkip === true) || prompt.kind === "skip"
            text: qsTr("Skip")
            onClicked: prompt.reply("skip")
        },
        UI.Button {
            visible: prompt.kind === "skip"
            text: qsTr("Try again")
            onClicked: prompt.reply("retry")
        },
        UI.Button {
            visible: prompt.kind === "conflict" && prompt.p.canKeepBoth === true
            text: qsTr("Keep both")
            onClicked: prompt.reply("both")
        },
        UI.Button {
            visible: prompt.kind === "confirm" && (prompt.p.secondary ?? "").length > 0
            text: prompt.p.secondary ?? ""
            onClicked: prompt.reply("secondary")
        },
        UI.Button {
            visible: prompt.kind === "conflict" && prompt.p.canReplace === true
            kind: "primary"
            text: prompt.p.replace ?? qsTr("Replace")
            onClicked: prompt.reply("replace")
        },
        UI.Button {
            visible: prompt.kind === "confirm"
            kind: prompt.cancelDefault ? "secondary" : "primary"
            danger: prompt.cancelDefault
            text: prompt.p.primary ?? qsTr("Continue")
            onClicked: prompt.reply("primary")
        }
    ]
}
