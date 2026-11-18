pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import WildOS.Files
import WildOS.UI as UI

// The Content layout from Windows: one row per item with a larger icon,
// the name and type on the left and the date and size on the right.
FocusScope {
    id: content

    required property var folder
    required property var view
    property int renameRow: -1
    readonly property bool computer: folder && folder.kind === "computer"
    readonly property var titles: folder ? folder.columnTitles : []
    readonly property int rowHeight: Math.max(Metrics.fit(56, 13), Math.ceil(UI.Style.px(13) * 1.3) + Math.ceil(UI.Style.px(12) * 1.3) + 18)

    function startRename(row) {
        list.positionViewAtIndex(row, ListView.Contain);
        renameRow = row;
    }

    function columns() {
        return 1;
    }

    function pageRows() {
        return Math.max(1, Math.floor(list.height / (rowHeight + 2)) - 1);
    }

    function itemRect(row) {
        const item = list.itemAtIndex(row);
        return item ? item.mapToItem(content, 0, 0, item.width, item.height) : Qt.rect(0, 0, 0, 0);
    }

    Keys.onPressed: event => view.handleKey(event, content)

    Connections {
        target: content.folder
        function onCurrentIndexChanged() {
            if (content.folder.currentIndex >= 0) {
                list.positionViewAtIndex(content.folder.currentIndex, ListView.Contain);
            }
        }
        function onUrlChanged() {
            content.renameRow = -1;
            list.positionViewAtBeginning();
        }
        function onModelReset() {
            content.renameRow = -1;
        }
    }

    ListView {
        id: list
        x: 14
        y: 8
        width: parent.width - 28
        height: parent.height - 16
        spacing: 2
        clip: true
        model: content.folder
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        ScrollBar.vertical: UI.ScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: content.folder ? content.folder.title : ""

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => content.view.wheelLayout(event.angleDelta.y)
        }

        Background {
            view: content.view
            target: content
            parent: list.contentItem
            width: list.width
            height: Math.max(list.height, list.contentHeight)
            z: -1
            rowsIn: function(rect) {
                const stride = content.rowHeight + 2;
                const first = Math.max(0, Math.floor(rect.y / stride));
                const last = Math.min(list.count - 1, Math.floor((rect.y + rect.height) / stride));
                const rows = [];
                for (let i = first; i <= last; ++i) {
                    const top = i * stride;
                    if (top + content.rowHeight >= rect.y && top <= rect.y + rect.height) {
                        rows.push(i);
                    }
                }
                return rows;
            }
        }

        delegate: ItemBase {
            id: row

            view: content.view
            // A delegate waiting in the reuse pool has index -1, which is
            // also "nothing being renamed".
            renaming: content.renameRow >= 0 && content.renameRow === index
            nameLabel: rowName
            width: ListView.view.width
            height: content.rowHeight

            readonly property string kindText: content.computer ? row.col2 : row.col3
            readonly property var details: {
                const lines = [];
                const t = content.titles;
                if (content.computer) {
                    if (row.col3.length > 0) {
                        lines.push(t[2] + ": " + row.col3);
                    }
                } else if (row.col2.length > 0) {
                    lines.push(t[1] + ": " + row.col2);
                }
                if (row.sizeText.length > 0) {
                    lines.push(t[3] + ": " + row.sizeText);
                }
                return lines;
            }

            Rectangle {
                anchors.fill: parent
                radius: 6
                color: row.selected ? UI.Style.accentSelected : row.highlighted ? UI.Style.surfaceHover : "transparent"
                border.width: row.selected ? 1 : 0
                border.color: UI.Style.accentBorderStrong
            }

            FileIcon {
                id: icon
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                size: 40
                iconName: row.iconName
                url: row.url
                preview: row.preview
                stamp: row.modified
                faded: row.faded
            }

            Column {
                anchors.left: icon.right
                anchors.leftMargin: 12
                width: Math.max(80, parent.width * 0.55 - x)
                anchors.verticalCenter: parent.verticalCenter

                UI.Label {
                    id: rowName
                    visible: !row.renaming
                    width: parent.width
                    text: row.name
                    color: row.selected ? UI.Style.inkStrong : UI.Style.ink
                }

                Loader {
                    active: row.renaming
                    visible: active
                    width: parent.width
                    sourceComponent: RenameField {
                        original: row.name
                        onCommit: text => {
                            content.renameRow = -1;
                            FileOps.rename(row.url, row.fileName, row.isDir, text);
                            content.forceActiveFocus();
                        }
                        onCancel: {
                            content.renameRow = -1;
                            content.forceActiveFocus();
                        }
                    }
                }

                UI.Label {
                    visible: text.length > 0
                    width: parent.width
                    text: row.kindText
                    font.pixelSize: UI.Style.px(12)
                    color: UI.Style.inkMuted
                }
            }

            Column {
                x: parent.width * 0.55 + 12
                width: parent.width - x - 14
                anchors.verticalCenter: parent.verticalCenter

                Repeater {
                    model: row.details
                    delegate: UI.Label {
                        required property string modelData
                        width: parent ? parent.width : 0
                        text: modelData
                        font.pixelSize: UI.Style.px(12)
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.fill: list
        folder: content.folder
    }
}
