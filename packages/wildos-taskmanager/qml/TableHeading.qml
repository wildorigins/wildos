import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// Column names over the Startup apps, Windows apps and Services tables, in
// the Processes table's 12px muted style, on a 40px band with the same
// 0.08 hairline under it. A column of width 0 takes the room left over.
//
// A column with an `id` sorts the table when its name is clicked, and
// again to turn the order round, with the Processes table's small chevron
// on the sorted one. Rows are compared by the field named by the id, or by
// the column's `value(row)` when it has one, then by name.
Item {
    id: heading

    property var columns: []
    property string sortColumn
    property bool sortDescending: false

    signal sortRequested(string column)

    implicitHeight: 40

    function sorted(rows) {
        const column = columns.find(c => c.id !== undefined && c.id === sortColumn);
        if (!column) {
            return rows;
        }
        const value = row => column.value ? column.value(row) : row[column.id];
        const direction = sortDescending ? -1 : 1;
        return rows.slice().sort((a, b) => {
            const x = value(a);
            const y = value(b);
            const order = typeof x === "string" ? x.localeCompare(y) : x < y ? -1 : x > y ? 1 : 0;
            return order !== 0 ? direction * order : a.name.localeCompare(b.name);
        });
    }

    Row {
        anchors.fill: parent

        Repeater {
            model: heading.columns

            T.AbstractButton {
                id: cell

                required property var modelData
                required property int index
                readonly property bool sortable: modelData.id !== undefined
                readonly property bool current: sortable && heading.sortColumn === modelData.id

                width: modelData.width > 0 ? modelData.width : heading.width - heading.fixedWidth()
                height: heading.height
                padding: 0
                enabled: sortable
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: modelData.title
                Accessible.ignored: modelData.title.length === 0
                onClicked: heading.sortRequested(modelData.id)

                background: Rectangle {
                    color: cell.down ? UI.Style.surfacePressed : cell.hovered ? UI.Style.surfaceRaised : "transparent"
                    Behavior on color {
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }

                    UI.FocusRing {
                        visible: cell.visualFocus
                        anchors.margins: 2
                        targetRadius: 4
                    }
                }

                contentItem: Item {
                    UI.Icon {
                        visible: cell.current
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 3
                        name: heading.sortDescending ? "chevron-down" : "chevron-up"
                        size: 10
                        strokeWidth: 2
                        color: UI.Style.iconMuted
                    }

                    UI.Label {
                        x: cell.index === 0 ? 16 : 12
                        width: parent.width - x - 12
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: cell.modelData.right ? Text.AlignRight : Text.AlignLeft
                        text: cell.modelData.title
                        size: 12
                        color: UI.Style.inkAlpha(0.7, 0.64)
                    }
                }
            }
        }
    }

    function fixedWidth() {
        let w = 0;
        for (const c of columns) {
            w += c.width > 0 ? c.width : 0;
        }
        return w;
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: UI.Style.tone(0.08)
    }
}
