pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import WildOS.Files
import WildOS.UI as UI

// The details view of FilesDetails.dc.html: Name, Date modified, Type and
// Size in 2.4 : 1.4 : 1.1 : 0.7 columns, 38px rows. Headers sort; the lines
// between them resize the columns.
FocusScope {
    id: details

    required property var folder
    required property var view
    property int renameRow: -1
    property var weights: Settings.columnWeights
    readonly property real innerWidth: width - 28 - 24
    readonly property real weightSum: weights[0] + weights[1] + weights[2] + weights[3]
    readonly property int rowStride: Metrics.rowHeight + 2

    function colWidth(i) {
        return innerWidth * weights[i] / weightSum;
    }

    function colX(i) {
        let x = 12;
        for (let k = 0; k < i; ++k) {
            x += colWidth(k);
        }
        return x;
    }

    function startRename(row) {
        list.positionViewAtIndex(row, ListView.Contain);
        renameRow = row;
    }

    function columns() {
        return 1;
    }

    function pageRows() {
        return Math.max(1, Math.floor(list.height / rowStride) - 1);
    }

    function itemRect(row) {
        const item = list.itemAtIndex(row);
        return item ? item.mapToItem(details, 0, 0, item.width, item.height) : Qt.rect(0, 0, 0, 0);
    }

    Keys.onPressed: event => view.handleKey(event, details)

    Connections {
        target: details.folder
        function onCurrentIndexChanged() {
            if (details.folder.currentIndex >= 0) {
                list.positionViewAtIndex(details.folder.currentIndex, ListView.Contain);
            }
        }
        function onUrlChanged() {
            details.renameRow = -1;
            list.positionViewAtBeginning();
        }
        function onModelReset() {
            details.renameRow = -1;
        }
    }

    Item {
        id: header
        x: 14
        y: 8
        width: parent.width - 28
        height: Metrics.fit(34, 12) + 1

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Metrics.rule
        }

        Repeater {
            model: details.folder ? details.folder.columnTitles : []

            delegate: Item {
                id: column

                required property string modelData
                required property int index
                readonly property bool sorted: details.folder.sortColumn === index

                x: details.colX(index) - (index === 0 ? 12 : 0)
                width: details.colWidth(index) + (index === 0 ? 12 : 0)
                height: parent.height

                Accessible.role: Accessible.ColumnHeader
                Accessible.name: modelData

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 3
                    anchors.bottomMargin: 4
                    radius: 5
                    color: headerMouse.containsMouse ? Metrics.hoverSoft : "transparent"
                }

                Row {
                    x: column.index === 0 ? 12 : 0
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: column.index === 3 ? parent.right : undefined
                    spacing: 6
                    layoutDirection: column.index === 3 ? Qt.RightToLeft : Qt.LeftToRight

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: column.modelData
                        font.pixelSize: UI.Style.px(12)
                        font.weight: column.sorted ? UI.Style.weightMedium : UI.Style.weightRegular
                        color: column.sorted ? UI.Style.ink : UI.Style.inkMuted
                    }

                    UI.Icon {
                        visible: column.sorted
                        anchors.verticalCenter: parent.verticalCenter
                        name: details.folder.sortDescending ? "chevron-down" : "chevron-up"
                        size: 11
                        strokeWidth: 2.4
                    }
                }

                MouseArea {
                    id: headerMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (column.sorted) {
                            details.folder.sortDescending = !details.folder.sortDescending;
                        } else {
                            details.folder.sortColumn = column.index;
                        }
                    }
                }

                // The grip between this column and the next.
                MouseArea {
                    visible: column.index < 3
                    x: parent.width - 4
                    width: 8
                    height: parent.height
                    cursorShape: Qt.SplitHCursor
                    hoverEnabled: true
                    property real startX
                    property var startWeights
                    onPressed: mouse => {
                        startX = mapToItem(header, mouse.x, 0).x;
                        startWeights = details.weights.slice();
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) {
                            return;
                        }
                        const dx = mapToItem(header, mouse.x, 0).x - startX;
                        const perPx = details.weightSum / details.innerWidth;
                        const minW = 70 * perPx;
                        const i = column.index;
                        let a = startWeights[i] + dx * perPx;
                        let b = startWeights[i + 1] - dx * perPx;
                        if (a < minW) {
                            b -= minW - a;
                            a = minW;
                        }
                        if (b < minW) {
                            a -= minW - b;
                            b = minW;
                        }
                        const w = startWeights.slice();
                        w[i] = a;
                        w[i + 1] = b;
                        details.weights = w;
                    }
                    onReleased: Settings.columnWeights = details.weights
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 1
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: Metrics.rule
                        visible: parent.containsMouse || parent.pressed
                    }
                }
            }
        }
    }

    ListView {
        id: list
        x: 14
        y: header.y + header.height + 2
        width: parent.width - 28
        height: parent.height - y - 8
        spacing: 2
        clip: true
        model: details.folder
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        ScrollBar.vertical: UI.ScrollBar {}
        Accessible.role: Accessible.Table
        Accessible.name: details.folder ? details.folder.title : ""

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => details.view.wheelLayout(event.angleDelta.y)
        }

        Background {
            id: background
            view: details.view
            target: details
            parent: list.contentItem
            width: list.width
            height: Math.max(list.height, list.contentHeight)
            z: -1
            rowsIn: function(rect) {
                const first = Math.max(0, Math.floor(rect.y / details.rowStride));
                const last = Math.min(list.count - 1, Math.floor((rect.y + rect.height) / details.rowStride));
                const rows = [];
                for (let i = first; i <= last; ++i) {
                    const top = i * details.rowStride;
                    if (top + Metrics.rowHeight >= rect.y && top <= rect.y + rect.height) {
                        rows.push(i);
                    }
                }
                return rows;
            }
        }

        delegate: ItemBase {
            id: row

            view: details.view
            // A delegate waiting in the reuse pool has index -1, which is
            // also "nothing being renamed".
            renaming: details.renameRow >= 0 && details.renameRow === index
            nameLabel: rowName
            width: ListView.view.width
            height: Metrics.rowHeight

            Rectangle {
                anchors.fill: parent
                radius: 6
                color: row.selected ? UI.Style.accentSelected : row.highlighted ? UI.Style.surfaceHover : "transparent"
                border.width: row.selected ? 1 : 0
                border.color: UI.Style.accentBorderStrong
            }

            FileIcon {
                id: fileIcon
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                size: 20
                iconName: row.iconName
                url: row.url
                faded: row.faded
            }

            UI.Label {
                id: rowName
                visible: !row.renaming
                anchors.left: fileIcon.right
                anchors.leftMargin: 10
                width: details.colWidth(0) - 30 - 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                color: row.selected ? UI.Style.inkStrong : UI.Style.ink
            }

            Loader {
                active: row.renaming
                anchors.left: fileIcon.right
                anchors.leftMargin: 6
                width: details.colWidth(0) - 26 - 12
                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: RenameField {
                    original: row.name
                    onCommit: text => {
                        details.renameRow = -1;
                        FileOps.rename(row.url, row.fileName, row.isDir, text);
                        details.forceActiveFocus();
                    }
                    onCancel: {
                        details.renameRow = -1;
                        details.forceActiveFocus();
                    }
                }
            }

            UI.Label {
                x: details.colX(1)
                width: details.colWidth(1) - 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.col2
                color: UI.Style.inkMuted
            }

            UI.Label {
                x: details.colX(2)
                width: details.colWidth(2) - 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.col3
                color: UI.Style.inkMuted
            }

            UI.Label {
                x: details.colX(3)
                width: details.colWidth(3)
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: row.sizeText
                color: UI.Style.inkMuted
            }
        }
    }

    EmptyState {
        anchors.fill: list
        folder: details.folder
    }
}
