pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.private.clipboard as Klipper
import WildOS.UI as UI
import WildOS.Clipboard

// Clipboard history (ClipboardEmoji.dc, Win+V): text, pictures and file
// paths copied since the computer started, newest first, each with when and
// from which app, a pin and a menu. Click one to paste it. The history is
// Klipper's, the same model Plasma's clipboard widget uses; pins are kept
// by Pins so they outlast a restart.
Item {
    id: page

    required property string activeApp
    required property bool terminal

    // When each item arrived and what was in front then; Klipper keeps
    // neither for us.
    property var meta: ({})
    property var items: []
    property real now: Date.now()

    function focusList(): void {
        list.forceActiveFocus();
        list.currentIndex = -1;
    }

    // Klipper's roles (historymodel.h): Qt::UserRole + 1 is the item's id,
    // + 3 its type, + 4 a picture's file.
    readonly property int uuidRole: 257
    readonly property int typeRole: 259
    readonly property int imageRole: 260

    function rebuild(): void {
        const out = [];
        const keys = {};
        const meta = page.meta;
        for (let i = 0; i < history.count; i++) {
            const index = history.index(i, 0);
            const uuid = String(history.data(index, uuidRole));
            const type = history.data(index, typeRole);
            const kind = type === 4 ? "image" : type === 8 ? "url" : "text";
            const text = kind === "image" ? "" : String(history.data(index, 0) || "");
            const image = kind === "image" ? history.data(index, imageRole) : "";
            let m = meta[uuid];
            if (!m) {
                m = { time: Date.now(), source: page.activeApp, key: Pins.keyFor(kind, text, image) };
                meta[uuid] = m;
            }
            keys[m.key] = true;
            out.push({ uuid: uuid, kind: kind, text: text, image: String(image), time: m.time, source: m.source, key: m.key });
        }
        // Pins from before the last restart, which Klipper no longer has.
        const pins = Pins.items;
        for (let j = 0; j < pins.length; j++) {
            const p = pins[j];
            if (!keys[p.key]) {
                out.push({ uuid: "", kind: p.kind, text: p.text || "", image: p.image || "", time: p.time, source: p.source || "", key: p.key });
            }
        }
        page.meta = meta;
        page.items = out;
    }

    // "9 Nov" or "Nov 9" as the regional format in Settings orders them,
    // from its long date with the weekday and year taken out.
    function dayAndMonth(when: var): string {
        const format = Qt.locale().dateFormat(Locale.LongFormat)
            .replace(/d{4}[,.\s]*/, "").replace(/[,.\s]*y+[.年]?/, "").replace("MMMM", "MMM").trim();
        return Qt.locale("en_GB").toString(when, format);
    }

    function ago(time: real): string {
        const minutes = Math.floor((now - time) / 60000);
        if (minutes < 1) {
            return "Just now";
        }
        if (minutes < 60) {
            return minutes + " min ago";
        }
        const hours = Math.floor(minutes / 60);
        if (hours < 24) {
            return hours + " hr ago";
        }
        return hours < 48 ? "Yesterday" : dayAndMonth(new Date(time));
    }

    // A file path as people read it: ~ for home, and how many more.
    function pathText(text: string): string {
        const lines = text.split("\n").filter(l => l.trim().length > 0);
        if (lines.length === 0) {
            return text;
        }
        let first = decodeURIComponent(lines[0].replace(/^file:\/\//, ""));
        const home = PanelApp.home;
        if (home.length > 0 && first.startsWith(home + "/")) {
            first = "~" + first.substring(home.length);
        }
        return lines.length > 1 ? first + " and " + (lines.length - 1) + " more" : first;
    }

    function paste(item: var): void {
        if (item.uuid.length > 0) {
            history.moveToTop(item.uuid);
        } else {
            Pins.restore(item.key);
        }
        PanelApp.pasteIntoApp(page.terminal);
    }

    function copyOnly(item: var): void {
        if (item.uuid.length > 0) {
            history.moveToTop(item.uuid);
        } else {
            Pins.restore(item.key);
        }
        PanelApp.shown = false;
    }

    function remove(item: var): void {
        if (Pins.isPinned(item.key)) {
            Pins.unpin(item.key);
        }
        if (item.uuid.length > 0) {
            history.remove(item.uuid);
        }
        rebuild();
    }

    function togglePin(item: var): void {
        if (Pins.isPinned(item.key)) {
            Pins.unpin(item.key);
        } else {
            Pins.pin(item.kind, item.text, item.image, item.time, item.source);
        }
    }

    Klipper.HistoryModel {
        id: history
    }

    Connections {
        target: history
        function onRowsInserted() { page.rebuild(); }
        function onRowsRemoved() { page.rebuild(); }
        function onRowsMoved() { page.rebuild(); }
        function onModelReset() { page.rebuild(); }
        function onDataChanged() { page.rebuild(); }
    }

    // The history goes first: Klipper puts its newest item back on the
    // clipboard when the clipboard empties. Item by item, because Klipper's
    // own clearHistory() asks first in a dialog, which the lock screen
    // would hide until sign-in.
    Connections {
        target: PanelApp
        function onClearRequested() {
            for (let i = history.count - 1; i >= 0; i--) {
                history.remove(String(history.data(history.index(i, 0), page.uuidRole)));
            }
            Pins.clear();
            PanelApp.clearClipboard();
        }
    }

    Connections {
        target: Pins
        function onChanged() { page.rebuild(); }
    }

    Timer {
        interval: 30000
        repeat: true
        running: page.visible
        onTriggered: page.now = Date.now()
    }

    onVisibleChanged: {
        if (visible) {
            now = Date.now();
        }
    }

    Component.onCompleted: rebuild()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            Layout.topMargin: 2
            Layout.bottomMargin: 2

            UI.Label {
                Layout.fillWidth: true
                text: "Clipboard history"
                font.weight: UI.Style.weightSemiBold
            }

            UI.LinkButton {
                visible: history.count > 0
                text: "Clear all"
                size: 12
                onClicked: history.clearHistory()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.items.length > 0
            clip: true
            spacing: 10
            model: page.items
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: false
            ScrollBar.vertical: UI.ScrollBar {}

            // Enter straight after Win+V pastes the newest, as on Windows.
            Keys.onReturnPressed: page.paste(page.items[Math.max(0, currentIndex)])
            Keys.onEnterPressed: page.paste(page.items[Math.max(0, currentIndex)])
            Keys.onDeletePressed: if (currentIndex >= 0) page.remove(page.items[currentIndex])

            delegate: ClipboardItem {
                required property var modelData
                required property int index
                width: ListView.view.width
                item: modelData
                pinned: Pins.isPinned(modelData.key)
                meta: page.ago(modelData.time) + (modelData.source.length > 0 ? " · " + modelData.source : "")
                shownText: modelData.kind === "url" ? page.pathText(modelData.text) : modelData.text
                current: list.activeFocus && ListView.isCurrentItem
                onPasteRequested: page.paste(modelData)
                onPinToggled: page.togglePin(modelData)
                onCopyRequested: page.copyOnly(modelData)
                onRemoveRequested: page.remove(modelData)
            }
        }

        // Nothing copied yet: say why the list is empty and what fills it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.items.length === 0
            spacing: 6

            Item { Layout.fillHeight: true }

            UI.Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "paste"
                size: 32
                color: UI.Style.iconMuted
            }
            UI.Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                horizontalAlignment: Text.AlignHCenter
                text: "Nothing copied yet"
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                size: 12
                color: UI.Style.inkMuted
                text: "Text, pictures and file paths you copy will show up here."
            }

            Item { Layout.fillHeight: true }
        }

        UI.Label {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            wrapMode: Text.Wrap
            size: 11
            color: UI.Style.inkPlaceholder
            text: PanelApp.canPaste
                ? "History stays on this computer and is cleared when you restart, except pinned items."
                : "Click an item to copy it, then press Ctrl+V. History is cleared when you restart, except pinned items."
        }
    }
}
