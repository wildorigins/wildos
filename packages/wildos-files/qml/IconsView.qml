pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import WildOS.Files
import WildOS.UI as UI

// Every layout from the View menu except Details and Content, as on
// Windows: extra large, large and medium icons (the design's 100 by 108
// tiles with 58px icons and an 8px gap), small icons in rows, a list that
// flows down in columns, and tiles with the type and size. This computer's
// drives show as wide tiles with a space bar in Tiles.
FocusScope {
    id: icons

    required property var folder
    required property var view
    property string mode: "icons"
    property int renameRow: -1

    readonly property bool flowDown: mode === "list"
    readonly property bool vertical: mode === "xlarge" || mode === "large" || mode === "icons"
    readonly property bool driveTiles: mode === "tiles" && folder && folder.kind === "computer"
    readonly property real lineSmall: Math.ceil(UI.Style.px(12) * 1.3)
    readonly property real lineBody: Math.ceil(UI.Style.px(13) * 1.3)
    readonly property real iconSize: ({ "xlarge": 256, "large": 96, "icons": 58, "small": 16, "list": 16, "tiles": driveTiles ? 40 : 48 })[mode] ?? 58
    readonly property real tileWidth: ({ "xlarge": 272, "large": 140, "icons": 100, "small": 236, "list": 248, "tiles": 260 })[mode] ?? 100
    readonly property real tileHeight: {
        if (vertical) {
            // The icon, a 6px gap, two lines of name and the padding.
            return Math.max(({ "xlarge": 304, "large": 150, "icons": 108 })[mode], iconSize + 6 + 2 * lineSmall + 18);
        }
        if (mode === "tiles") {
            return Math.max(72, lineBody + 2 * lineSmall + 18);
        }
        return Math.max(26, lineBody + 8);
    }

    function startRename(row) {
        grid.positionViewAtIndex(row, GridView.Contain);
        renameRow = row;
    }

    function rowsPerColumn() {
        return Math.max(1, Math.floor(grid.height / grid.cellHeight));
    }

    // Up and Down move by this many items, Left and Right by across().
    function columns() {
        return flowDown ? 1 : Math.max(1, Math.floor(grid.width / grid.cellWidth));
    }

    function across() {
        return flowDown ? rowsPerColumn() : 1;
    }

    function pageRows() {
        return flowDown ? rowsPerColumn() : Math.max(1, Math.floor(grid.height / grid.cellHeight) - 1) * columns();
    }

    function itemRect(row) {
        const item = grid.itemAtIndex(row);
        return item ? item.mapToItem(icons, 0, 0, item.width, item.height) : Qt.rect(0, 0, 0, 0);
    }

    function cellAt(i) {
        if (flowDown) {
            const rows = rowsPerColumn();
            return Qt.point(Math.floor(i / rows) * grid.cellWidth, (i % rows) * grid.cellHeight);
        }
        const cols = columns();
        return Qt.point((i % cols) * grid.cellWidth, Math.floor(i / cols) * grid.cellHeight);
    }

    Keys.onPressed: event => view.handleKey(event, icons)

    Connections {
        target: icons.folder
        function onCurrentIndexChanged() {
            if (icons.folder.currentIndex >= 0) {
                grid.positionViewAtIndex(icons.folder.currentIndex, GridView.Contain);
            }
        }
        function onUrlChanged() {
            icons.renameRow = -1;
            grid.positionViewAtBeginning();
        }
        function onModelReset() {
            icons.renameRow = -1;
        }
    }

    GridView {
        id: grid
        x: 14
        y: icons.vertical ? 12 : 8
        width: parent.width - 28
        height: parent.height - y - (icons.flowDown ? 8 : 0)
        flow: icons.flowDown ? GridView.FlowTopToBottom : GridView.FlowLeftToRight
        // 8 px between tiles, so a selected tile's highlight keeps clear of
        // its neighbours.
        cellWidth: icons.tileWidth + 8
        cellHeight: icons.tileHeight + 8
        clip: true
        model: icons.folder
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        bottomMargin: icons.flowDown ? 0 : 12
        ScrollBar.vertical: UI.ScrollBar {
            visible: !icons.flowDown
        }
        ScrollBar.horizontal: UI.ScrollBar {
            visible: icons.flowDown
            orientation: Qt.Horizontal
        }
        Accessible.role: Accessible.List
        Accessible.name: icons.folder ? icons.folder.title : ""

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => icons.view.wheelLayout(event.angleDelta.y)
        }

        Background {
            view: icons.view
            target: icons
            parent: grid.contentItem
            width: Math.max(grid.width, grid.contentWidth)
            height: Math.max(grid.height, grid.contentHeight + 12)
            z: -1
            rowsIn: function(rect) {
                const rows = [];
                for (let i = 0; i < grid.count; ++i) {
                    const p = icons.cellAt(i);
                    if (p.x + icons.tileWidth >= rect.x && p.x <= rect.x + rect.width
                            && p.y + icons.tileHeight >= rect.y && p.y <= rect.y + rect.height) {
                        rows.push(i);
                    }
                }
                return rows;
            }
        }

        delegate: ItemBase {
            id: tile

            view: icons.view
            // A delegate waiting in the reuse pool has index -1, which is
            // also "nothing being renamed".
            renaming: icons.renameRow >= 0 && icons.renameRow === index
            corner: icons.vertical || icons.mode === "tiles" ? 8 : 6
            nameLabel: icons.vertical ? tileName : icons.driveTiles && tile.isDrive ? driveName : rowName
            width: icons.tileWidth
            height: icons.tileHeight

            Rectangle {
                anchors.fill: parent
                radius: tile.corner
                color: tile.selected ? UI.Style.accentSelected : tile.highlighted ? UI.Style.surfaceHover : "transparent"
                border.width: tile.selected ? 1 : 0
                border.color: UI.Style.accentBorderStrong
            }

            Column {
                visible: icons.vertical
                anchors.centerIn: parent
                width: parent.width - 8
                spacing: 6

                FileIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: icons.iconSize
                    iconName: tile.iconName
                    url: tile.url
                    preview: tile.preview && icons.vertical
                    stamp: tile.modified
                    faded: tile.faded
                }

                UI.Label {
                    id: tileName
                    visible: !tile.renaming
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: UI.Style.px(12)
                    lineHeight: 1.1
                    text: tile.name
                    color: tile.selected ? UI.Style.inkStrong : UI.Style.ink
                }

                Loader {
                    active: tile.renaming && icons.vertical
                    visible: active
                    width: parent.width + 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    sourceComponent: RenameField {
                        original: tile.name
                        multiLine: true
                        onCommit: text => icons.commitRename(tile, text)
                        onCancel: icons.cancelRename()
                    }
                }
            }

            Item {
                visible: !icons.vertical && !(icons.driveTiles && tile.isDrive)
                anchors.fill: parent

                FileIcon {
                    id: rowIcon
                    x: icons.mode === "tiles" ? 10 : 6
                    anchors.verticalCenter: parent.verticalCenter
                    size: icons.iconSize
                    iconName: tile.iconName
                    url: tile.url
                    preview: tile.preview && icons.mode === "tiles"
                    stamp: tile.modified
                    faded: tile.faded
                }

                Column {
                    anchors.left: rowIcon.right
                    anchors.leftMargin: icons.mode === "tiles" ? 10 : 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter

                    UI.Label {
                        id: rowName
                        visible: !tile.renaming
                        width: parent.width
                        text: tile.name
                        color: tile.selected ? UI.Style.inkStrong : UI.Style.ink
                    }

                    Loader {
                        active: tile.renaming && !icons.vertical
                        visible: active
                        width: parent.width
                        sourceComponent: RenameField {
                            original: tile.name
                            onCommit: text => icons.commitRename(tile, text)
                            onCancel: icons.cancelRename()
                        }
                    }

                    UI.Label {
                        visible: icons.mode === "tiles" && text.length > 0
                        width: parent.width
                        text: tile.col3
                        font.pixelSize: UI.Style.px(12)
                        color: UI.Style.inkMuted
                    }

                    UI.Label {
                        visible: icons.mode === "tiles" && text.length > 0
                        width: parent.width
                        text: tile.sizeText
                        font.pixelSize: UI.Style.px(12)
                        color: UI.Style.inkMuted
                    }
                }
            }

            Item {
                visible: icons.driveTiles && tile.isDrive
                anchors.fill: parent

                FileIcon {
                    id: driveIcon
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    size: icons.iconSize
                    iconName: tile.iconName
                }

                Column {
                    anchors.left: driveIcon.right
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5

                    UI.Label {
                        id: driveName
                        width: parent.width
                        text: tile.name
                        color: tile.selected ? UI.Style.inkStrong : UI.Style.ink
                    }

                    Rectangle {
                        visible: tile.usage >= 0
                        width: parent.width
                        height: 6
                        radius: 3
                        color: UI.Style.surfaceTrack
                        Rectangle {
                            width: Math.max(height, parent.width * Math.max(0, Math.min(1, tile.usage)))
                            height: parent.height
                            radius: 3
                            color: tile.usage > 0.9 ? UI.Style.warning : UI.Style.accent
                        }
                    }

                    UI.Label {
                        width: parent.width
                        text: tile.subtitle
                        font.pixelSize: UI.Style.px(12)
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }

    function commitRename(tile, text) {
        renameRow = -1;
        FileOps.rename(tile.url, tile.fileName, tile.isDir, text);
        forceActiveFocus();
    }

    function cancelRename() {
        renameRow = -1;
        forceActiveFocus();
    }

    EmptyState {
        anchors.fill: grid
        folder: icons.folder
    }
}
