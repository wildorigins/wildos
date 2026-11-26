pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WildOS.UI as UI
import WildOS.Clipboard

// The emoji and symbols tabs (ClipboardEmoji.dc, Win+full stop): a search
// box, then the characters in groups under 12px headings, eight to a row in
// 38px cells. Clicking one, or Enter on the one the arrow keys reached,
// types it into the app you were using. Emoji also have recently used, a
// skin tone choice and a row of group buttons to jump with.
Item {
    id: page

    required property bool emoji
    required property bool terminal

    readonly property int columns: 8
    // Recently used as it was when the panel opened. Updated live, a pick
    // could add a row to it and push the whole grid down while the panel's
    // picture stood in, which read as a flicker; Windows also only shows
    // new picks the next time its panel opens.
    property var recentAtOpen: []
    Component.onCompleted: recentAtOpen = PanelApp.recentEmoji.slice()
    Connections {
        target: PanelApp
        function onShownChanged() {
            if (PanelApp.shown) {
                page.recentAtOpen = PanelApp.recentEmoji.slice();
            }
        }
    }
    property int cursor: -1
    readonly property string query: search.text.trim()

    readonly property var tones: ["", "\u{1F3FB}", "\u{1F3FC}", "\u{1F3FD}", "\u{1F3FE}", "\u{1F3FF}"]
    readonly property var toneNames: [qsTr("Default"), qsTr("Light"), qsTr("Medium light"), qsTr("Medium"), qsTr("Medium dark"), qsTr("Dark")]

    readonly property var symbolGroups: [
        { name: qsTr("Punctuation"), emoji: chars("‘ ’ “ ” ‚ „ « » ‹ › … • · ¡ ¿ § ¶ † ‡ ′ ″ ‰ ‽ ⁂ ※ © ® ™ ℗ № ° ℃ ℉ ℓ ℮") },
        { name: qsTr("Currency"), emoji: chars("£ € $ ¢ ¥ ₹ ₽ ₩ ₪ ₺ ₫ ₴ ₦ ₱ ₿ ฿ ₣ ₡ ₲ ₵ ₸ ₼ ₾ ¤") },
        { name: qsTr("Maths"), emoji: chars("± × ÷ = ≠ ≈ ≡ < > ≤ ≥ ∞ √ ∛ ∑ ∏ ∫ ∂ ∆ ∇ ∈ ∉ ∩ ∪ ⊂ ⊃ ⊆ ⊇ ∧ ∨ ¬ ∀ ∃ ∅ ∝ ∠ ⊥ ∥ ‱ ½ ⅓ ⅔ ¼ ¾ ⅕ ⅛ ⅜ ⅝ ⅞ ⁰ ¹ ² ³ ⁴ ⁵ ⁶ ⁷ ⁸ ⁹ ⁿ ₀ ₁ ₂ ₃ ₄ ₅ ₆ ₇ ₈ ₉") },
        { name: qsTr("Arrows"), emoji: chars("← ↑ → ↓ ↔ ↕ ↖ ↗ ↘ ↙ ⇐ ⇑ ⇒ ⇓ ⇔ ⇕ ↩ ↪ ↺ ↻ ⤴ ⤵ ➜ ➔ ▸ ◂ ▴ ▾") },
        { name: qsTr("Shapes and signs"), emoji: chars("■ □ ▪ ▫ ▲ △ ▼ ▽ ◆ ◇ ○ ● ◎ ◉ ★ ☆ ♠ ♣ ♥ ♦ ♤ ♧ ♡ ♢ ✓ ✔ ✗ ✘ ☐ ☑ ☒ ♪ ♫ ☀ ☁ ☂ ☎ ✉ ✂ ✎ ⌘ ⌥ ⇧ ⌫ ⏎ ⌂ ⚠ ♻ ⚑") },
        { name: qsTr("Latin letters"), emoji: chars("à á â ä ã å ā æ ç ć č ď è é ê ë ē ė ę ě ğ ì í î ï ī ı ł ľ ñ ń ň ò ó ô ö õ ø ō œ ŕ ř ß ś š ş ť ù ú û ü ū ů ý ÿ ž ź ż ð þ À Á Â Ä Ã Å Ā Æ Ç Ć Č Ď È É Ê Ë Ē Ė Ę Ě Ğ Ì Í Î Ï Ī İ Ł Ľ Ñ Ń Ň Ò Ó Ô Ö Õ Ø Ō Œ Ŕ Ř Ś Š Ş Ť Ù Ú Û Ü Ū Ů Ý Ÿ Ž Ź Ż Ð Þ") },
        { name: qsTr("Greek"), emoji: chars("α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ ς τ υ φ χ ψ ω Α Β Γ Δ Ε Ζ Η Θ Ι Κ Λ Μ Ν Ξ Ο Π Ρ Σ Τ Υ Φ Χ Ψ Ω") }
    ]
    // The emoji group names tools/make-emoji.py writes into emoji.json,
    // listed so they reach the translations; they are shown with qsTr().
    readonly property var groupNames: [QT_TR_NOOP("Smileys and people"), QT_TR_NOOP("Animals and nature"), QT_TR_NOOP("Food and drink"),
        QT_TR_NOOP("Travel and places"), QT_TR_NOOP("Activities"), QT_TR_NOOP("Objects"), QT_TR_NOOP("Symbols"), QT_TR_NOOP("Flags")]
    // One emoji standing for each group, for the jump buttons.
    readonly property var groupFaces: ["\u{1F600}", "\u{1F43B}", "\u{1F354}", "\u{2708}\u{FE0F}", "\u{26BD}", "\u{1F4A1}", "\u{2764}\u{FE0F}", "\u{1F3F3}\u{FE0F}"]

    function chars(list: string): var {
        return list.split(" ").filter(c => c.length > 0).map(c => [c, c, ""]);
    }

    function focusSearch(): void {
        search.forceActiveFocus();
        cursor = -1;
    }

    function shown(entry: var): string {
        if (!emoji || PanelApp.skinTone === 0 || entry.length < 4 || !entry[3]) {
            return entry[0];
        }
        return entry[3][PanelApp.skinTone - 1] || entry[0];
    }

    function pick(entry: var): void {
        if (emoji) {
            PanelApp.addRecentEmoji(entry[0]);
        }
        PanelApp.typeText(shown(entry), page.terminal);
    }

    // Headings and rows of eight, flattened so the list only makes what is
    // on screen; `cells` are the characters in order, for the arrow keys.
    readonly property var layout: {
        const rows = [];
        const cells = [];
        const add = (title, entries) => {
            if (entries.length === 0) {
                return;
            }
            rows.push({ heading: title, group: title });
            for (let i = 0; i < entries.length; i += columns) {
                const slice = entries.slice(i, i + columns);
                rows.push({ heading: "", first: cells.length, cells: slice });
                for (let j = 0; j < slice.length; j++) {
                    cells.push(slice[j]);
                }
            }
        };
        if (query.length > 0) {
            if (emoji) {
                add(qsTr("Results"), EmojiData.search(query));
            } else {
                const q = query.toLowerCase();
                const hits = [];
                for (const g of symbolGroups) {
                    for (const e of g.emoji) {
                        if (e[0].toLowerCase() === q || g.name.toLowerCase().indexOf(q) >= 0) {
                            hits.push(e);
                        }
                    }
                }
                add(qsTr("Results"), hits);
            }
        } else if (emoji) {
            add(qsTr("Recently used"), EmojiData.find(page.recentAtOpen));
            const groups = EmojiData.groups;
            for (const g of groups) {
                add(qsTr(g.name), g.emoji);
            }
        } else {
            for (const g of symbolGroups) {
                add(g.name, g.emoji);
            }
        }
        return { rows: rows, cells: cells };
    }

    onQueryChanged: {
        cursor = -1;
        grid.positionViewAtBeginning();
    }

    function move(delta: int): void {
        const count = layout.cells.length;
        if (count === 0) {
            return;
        }
        if (cursor < 0) {
            cursor = 0;
        } else {
            cursor = Math.max(0, Math.min(count - 1, cursor + delta));
        }
        for (let r = 0; r < layout.rows.length; r++) {
            const row = layout.rows[r];
            if (row.cells && cursor >= row.first && cursor < row.first + row.cells.length) {
                grid.positionViewAtIndex(r, ListView.Contain);
                break;
            }
        }
    }

    function jumpTo(name: string): void {
        search.clear();
        for (let r = 0; r < layout.rows.length; r++) {
            if (layout.rows[r].group === name) {
                grid.positionViewAtIndex(r, ListView.Beginning);
                return;
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Right) {
            move(1);
        } else if (event.key === Qt.Key_Left) {
            move(-1);
        } else if (event.key === Qt.Key_Down) {
            move(cursor < 0 ? 0 : columns);
        } else if (event.key === Qt.Key_Up) {
            if (cursor >= 0 && cursor < columns) {
                cursor = -1;
                search.forceActiveFocus();
            } else {
                move(-columns);
            }
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && cursor >= 0) {
            pick(layout.cells[cursor]);
        } else {
            return;
        }
        event.accepted = true;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            UI.SearchField {
                id: search
                Layout.fillWidth: true
                implicitHeight: 34
                iconLeading: true
                placeholderText: page.emoji ? qsTr("Search emoji") : qsTr("Search symbols")
                onDownPressed: {
                    page.forceActiveFocus();
                    page.move(0);
                }
                onEscapePressed: PanelApp.shown = false
                Keys.onReturnPressed: {
                    if (page.layout.cells.length > 0) {
                        page.pick(page.layout.cells[Math.max(0, page.cursor)]);
                    }
                }
            }

            // Skin tone for the people and hands that come in tones.
            UI.IconButton {
                id: toneButton
                visible: page.emoji
                width: 34
                height: 34
                tip: qsTr("Skin tone: %1").arg(page.toneNames[PanelApp.skinTone])
                onClicked: toneMenu.popup(toneButton, toneButton.width - toneMenu.width, toneButton.height + 4)

                Text {
                    anchors.centerIn: parent
                    text: "\u{270B}" + page.tones[PanelApp.skinTone]
                    font.family: "Noto Color Emoji"
                    font.pixelSize: 18
                }

                UI.Menu {
                    id: toneMenu
                    minimumWidth: 200

                    Repeater {
                        model: 6
                        delegate: UI.MenuItem {
                            required property int index
                            text: "\u{270B}" + page.tones[index] + "  " + page.toneNames[index]
                            checked: PanelApp.skinTone === index
                            radio: true
                            onTriggered: PanelApp.skinTone = index
                        }
                    }
                }
            }
        }

        ListView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.layout.rows
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 400
            ScrollBar.vertical: UI.ScrollBar {}
            visible: page.layout.cells.length > 0

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: modelData.heading.length > 0 ? heading.implicitHeight + (index > 0 ? 18 : 8) : 40

                UI.Label {
                    id: heading
                    visible: row.modelData.heading.length > 0
                    x: 4
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    text: row.modelData.heading
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkAlpha(0.75, 0.70)
                }

                Row {
                    visible: !!row.modelData.cells
                    spacing: 2

                    Repeater {
                        model: row.modelData.cells || []

                        delegate: UI.IconButton {
                            id: cell
                            required property var modelData
                            required property int index
                            readonly property int cellIndex: row.modelData.first + index
                            width: (grid.width - 2 * (page.columns - 1)) / page.columns
                            height: 38
                            radius: UI.Style.radiusSm
                            tip: page.emoji ? modelData[1] : ""
                            checked: page.cursor === cellIndex
                            focusPolicy: Qt.NoFocus
                            Accessible.name: page.emoji ? modelData[1] : modelData[0]
                            onClicked: page.pick(modelData)

                            Text {
                                anchors.centerIn: parent
                                text: page.shown(cell.modelData)
                                font.family: page.emoji ? "Noto Color Emoji" : UI.Style.fontFamily
                                font.pixelSize: page.emoji ? 22 : UI.Style.px(20)
                                color: UI.Style.ink
                            }
                        }
                    }
                }
            }
        }

        // A search that found nothing says so and offers the way back.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.layout.cells.length === 0
            spacing: 6

            Item { Layout.fillHeight: true }
            UI.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: page.emoji ? qsTr("No emoji found") : qsTr("No symbols found")
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                size: 12
                color: UI.Style.inkMuted
                text: page.emoji ? qsTr("Try another word, like smile, heart or cat.") : qsTr("Type the symbol itself, or a group such as maths or currency.")
            }
            UI.Button {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                size: "small"
                text: qsTr("Clear search")
                onClicked: {
                    search.clear();
                    page.focusSearch();
                }
            }
            Item { Layout.fillHeight: true }
        }

        // Jump to a group.
        Row {
            Layout.fillWidth: true
            visible: page.emoji
            spacing: (width - 9 * 34) / 8

            UI.IconButton {
                width: 34
                height: 32
                tip: qsTr("Recently used")
                iconName: "clock"
                iconSize: 16
                enabled: page.recentAtOpen.length > 0
                onClicked: page.jumpTo(qsTr("Recently used"))
            }

            Repeater {
                model: EmojiData.groups

                delegate: UI.IconButton {
                    id: jump
                    required property var modelData
                    required property int index
                    width: 34
                    height: 32
                    tip: qsTr(modelData.name)
                    onClicked: page.jumpTo(qsTr(modelData.name))

                    Text {
                        anchors.centerIn: parent
                        text: page.groupFaces[jump.index] || ""
                        font.family: "Noto Color Emoji"
                        font.pixelSize: 16
                    }
                }
            }
        }
    }
}
