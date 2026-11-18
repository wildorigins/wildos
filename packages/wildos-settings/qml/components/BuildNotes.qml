import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// What one or more Wild OS builds changed, under New, Improved and Fixed,
// one short line each. A computer several builds behind gets every build's
// lines together. Past `limit` lines the rest wait behind "Show all".
ColumnLayout {
    id: notes

    // Builds as Updates.notes gives them: {build, date, items}.
    property var builds: []
    property int limit: 6
    property bool expanded: false

    readonly property var sections: [
        { category: "new", title: qsTr("New") },
        { category: "improved", title: qsTr("Improved") },
        { category: "fixed", title: qsTr("Fixed") }
    ]

    // Every line once, in section order.
    readonly property var lines: {
        const seen = {};
        const all = [];
        for (const s of sections) {
            for (const b of builds) {
                for (const item of b.items) {
                    if (item.category === s.category && !seen[item.text]) {
                        seen[item.text] = true;
                        all.push(item);
                    }
                }
            }
        }
        return all;
    }
    // Collapsed, each section takes a line in turn, so a long list of new
    // things never hides that there are fixes too.
    readonly property var shown: {
        if (expanded || lines.length <= limit) {
            return lines;
        }
        const queues = sections.map(s => lines.filter(i => i.category === s.category));
        const picked = new Set();
        for (let round = 0; picked.size < limit; ++round) {
            for (const q of queues) {
                if (round < q.length && picked.size < limit) {
                    picked.add(q[round]);
                }
            }
        }
        return lines.filter(i => picked.has(i));
    }

    spacing: 12

    Repeater {
        model: notes.sections

        ColumnLayout {
            id: section
            required property var modelData
            readonly property var items: notes.shown.filter(i => i.category === modelData.category)

            Layout.fillWidth: true
            visible: items.length > 0
            spacing: 4

            UI.Label {
                text: section.modelData.title
                size: 13
                font.weight: UI.Style.weightSemiBold
                Accessible.role: Accessible.Heading
            }

            Repeater {
                model: section.items

                RowLayout {
                    id: line
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8

                    UI.Label {
                        Layout.alignment: Qt.AlignTop
                        text: "•"
                        size: 13
                        color: UI.Style.inkMuted
                        lineHeight: 1.35
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: line.modelData.text
                        size: 13
                        color: UI.Style.inkStatus
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                }
            }
        }
    }

    UI.LinkButton {
        Layout.leftMargin: -4
        visible: notes.lines.length > notes.limit
        text: notes.expanded ? qsTr("Show fewer") : qsTr("Show all %1 changes").arg(notes.lines.length)
        onClicked: notes.expanded = !notes.expanded
    }
}
