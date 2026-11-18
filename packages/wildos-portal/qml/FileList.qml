pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WildOS.UI as UI

// The details list of DialogSave.dc: a 30px header (Name, Date modified
// 150, Type 130, Size 70, the last two wider when what they hold is
// longer) that sorts when clicked, then 36px rows with a
// 20px icon, the name in 13px and the rest in 12px muted. Click selects
// (Ctrl and Shift add when several may be opened), double click opens a
// folder or chooses the file, right click (or the Menu key) asks for the
// menu, Delete sends to the Recycle Bin and typing jumps to a name.
//
// Large icons, the other view, lays the same items out as Files' Large
// icons tiles (140 by 150, a 96px icon or a preview of the picture) so
// photos can be told apart by what is in them.
FocusScope {
    id: list

    required property var folder
    property string mode: "details"
    readonly property bool icons: mode === "icons" && !folder.isComputer
    property bool showSize: true
    property int renameRow: -1
    property string typed: ""
    // A folder that lists at once never flashes the spinner.
    property bool slowLoad: false

    readonly property real sizeWidth: Math.round(Math.max(70, Math.min(140, sizeProbe.implicitWidth + 6)))
    // The longest type name in the folder fits, as long as the name column
    // keeps room of its own.
    readonly property real typeWidth: Math.round(Math.max(90, Math.min(Math.max(130, typeProbe.implicitWidth + 6), 260,
        width - 268 - sizeWidth - (folder.isComputer ? 0 : 160))))

    signal activated(int row)
    // A right click or the Menu key: on `row`, or on the empty space when
    // it is -1, at a point in this item.
    signal menuRequested(int row, real x, real y, bool keyboard)
    signal emptyAction(string action)
    property string emptyText: ""
    property string emptyButton: ""
    property string emptyActionName: ""

    function startRename(row) {
        show(row);
        renameRow = row;
    }

    function show(row) {
        if (icons) {
            grid.positionViewAtIndex(row, GridView.Contain);
        } else {
            view.positionViewAtIndex(row, ListView.Contain);
        }
    }

    function requestMenu(row, item, x, y, keyboard) {
        const at = item.mapToItem(list, x, y);
        menuRequested(row, at.x, at.y, keyboard);
    }

    function requestMenuAtCurrent() {
        const cur = folder.currentIndex;
        const item = cur < 0 ? null : icons ? grid.itemAtIndex(cur) : view.itemAtIndex(cur);
        if (item && item.selected) {
            requestMenu(cur, item, Math.min(item.width / 2, 120), item.height / 2, true);
        } else {
            menuRequested(-1, 24, header.visible ? header.height + 12 : 24, true);
        }
    }

    function columns() {
        return icons ? Math.max(1, Math.floor(grid.width / grid.cellWidth)) : 1;
    }

    function pageRows() {
        if (icons) {
            return Math.max(1, Math.floor(grid.height / grid.cellHeight)) * columns();
        }
        return Math.max(1, Math.floor(view.height / 36) - 1);
    }

    Keys.onPressed: event => {
        const f = list.folder;
        const mods = event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier);
        const cur = f.currentIndex;
        if (event.key === Qt.Key_Down) {
            f.moveCurrent(cur < 0 ? 0 : Math.min(f.count - 1, cur + columns()), mods);
        } else if (event.key === Qt.Key_Up) {
            f.moveCurrent(Math.max(0, cur - columns()), mods);
        } else if (event.key === Qt.Key_Right && icons) {
            f.moveCurrent(cur + 1, mods);
        } else if (event.key === Qt.Key_Left && icons) {
            f.moveCurrent(Math.max(0, cur - 1), mods);
        } else if (event.key === Qt.Key_Home) {
            f.moveCurrent(0, mods);
        } else if (event.key === Qt.Key_End) {
            f.moveCurrent(f.count - 1, mods);
        } else if (event.key === Qt.Key_PageDown) {
            f.moveCurrent(cur + pageRows(), mods);
        } else if (event.key === Qt.Key_PageUp) {
            f.moveCurrent(Math.max(0, cur - pageRows()), mods);
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (cur >= 0) {
                list.activated(cur);
            }
        } else if (event.key === Qt.Key_Space && (event.modifiers & Qt.ControlModifier) && cur >= 0) {
            f.select(cur, Qt.ControlModifier);
        } else if (event.key === Qt.Key_F2 && cur >= 0 && f.writable) {
            list.startRename(cur);
        } else if (event.key === Qt.Key_Delete && !(event.modifiers & Qt.ShiftModifier) && f.selectedCount > 0 && f.writable) {
            f.trashSelected();
        } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            list.requestMenuAtCurrent();
        } else if (event.key === Qt.Key_C && mods === (Qt.ControlModifier | Qt.ShiftModifier)) {
            f.copyPaths();
        } else if (event.text.length === 1 && event.text >= " " && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
            typed += event.text;
            typeReset.restart();
            const row = f.findPrefix(typed, typed.length > 1 ? Math.max(0, cur) : cur + 1);
            if (row >= 0) {
                f.select(row, 0);
            }
        } else {
            return;
        }
        event.accepted = true;
    }

    Timer {
        id: typeReset
        interval: 900
        onTriggered: list.typed = ""
    }

    Timer {
        interval: 300
        running: list.folder.loading
        onTriggered: list.slowLoad = true
    }

    UI.Label {
        id: typeProbe
        visible: false
        size: 12
        text: list.folder.widestType
    }

    UI.Label {
        id: sizeProbe
        visible: false
        size: 12
        text: list.folder.widestSize
    }

    Connections {
        target: list.folder
        function onCurrentIndexChanged() {
            if (list.folder.currentIndex >= 0) {
                list.show(list.folder.currentIndex);
            }
        }
        function onUrlChanged() {
            list.renameRow = -1;
            view.positionViewAtBeginning();
            grid.positionViewAtBeginning();
        }
        function onFolderMade(row) {
            list.startRename(row);
        }
        function onLoadingChanged() {
            if (!list.folder.loading) {
                list.slowLoad = false;
            }
        }
    }

    onIconsChanged: {
        renameRow = -1;
        if (folder.currentIndex >= 0) {
            Qt.callLater(show, folder.currentIndex);
        }
    }

    Item {
        id: header
        x: 0
        width: parent.width
        height: 30
        visible: !list.icons

        component HeaderCell: Item {
            id: cell
            required property int column
            required property string label
            property bool alignRight: false
            readonly property bool sorted: list.folder.sortColumn === column
            height: header.height
            Accessible.role: Accessible.ColumnHeader
            Accessible.name: label

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: cell.alignRight ? parent.right : undefined
                spacing: 4
                layoutDirection: cell.alignRight ? Qt.RightToLeft : Qt.LeftToRight

                UI.Label {
                    text: cell.label
                    size: 12
                    color: UI.Style.inkMuted
                    font.weight: cell.sorted ? UI.Style.weightMedium : UI.Style.weightRegular
                }

                UI.Icon {
                    visible: cell.sorted
                    anchors.verticalCenter: parent.verticalCenter
                    name: list.folder.sortDescending ? "chevron-down" : "chevron-up"
                    size: 10
                    color: UI.Style.inkMuted
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (cell.sorted) {
                        list.folder.sortDescending = !list.folder.sortDescending;
                    } else {
                        list.folder.sortColumn = cell.column;
                    }
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10

            Item { Layout.preferredWidth: 20 }
            HeaderCell { Layout.fillWidth: true; column: 0; label: qsTr("Name") }
            HeaderCell { Layout.preferredWidth: 150; column: 1; label: qsTr("Date modified"); visible: !list.folder.isComputer }
            HeaderCell { Layout.preferredWidth: list.typeWidth; column: 2; label: qsTr("Type") }
            HeaderCell { Layout.preferredWidth: list.sizeWidth; column: 3; label: list.folder.isComputer ? qsTr("Free") : qsTr("Size"); alignRight: true }
        }
    }

    ListView {
        id: view
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !list.icons
        clip: true
        model: list.icons ? null : list.folder
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        activeFocusOnTab: false
        ScrollBar.vertical: UI.ScrollBar {}

        delegate: Item {
            id: row

            required property int index
            required property string name
            required property string fileName
            required property string iconName
            required property bool isDir
            required property string date
            required property string type
            required property string size
            required property bool selected
            required property bool hidden

            width: view.width
            height: 36
            Accessible.role: Accessible.Row
            Accessible.name: name
            Accessible.selected: selected

            Rectangle {
                anchors.fill: parent
                radius: UI.Style.radiusXs
                color: rowMouse.pressed ? UI.Style.surfacePressed : row.selected ? UI.Style.accentSelected
                    : rowMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
                border.width: list.activeFocus && list.folder.currentIndex === row.index ? 1 : 0
                border.color: UI.Style.accentBorderStrong
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                UI.ThemeIcon {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    name: row.iconName
                    fallback: row.isDir ? "folder" : "text-x-generic"
                    opacity: row.hidden ? 0.5 : 1
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        visible: list.renameRow !== row.index
                        text: row.name
                        opacity: row.hidden ? 0.6 : 1
                    }

                    Loader {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        active: list.renameRow === row.index
                        sourceComponent: UI.TextField {
                            id: renameField
                            height: 28
                            implicitHeight: 28
                            text: row.name
                            escapeClears: false
                            Component.onCompleted: {
                                forceActiveFocus();
                                selectAll();
                            }
                            property bool finished: false
                            function commit() {
                                if (finished) {
                                    return;
                                }
                                finished = true;
                                list.folder.renameItem(row.index, text);
                                list.renameRow = -1;
                                list.forceActiveFocus();
                            }
                            onAccepted: commit()
                            // The text menu holds focus while it is open; the rename is not over.
                            onActiveFocusChanged: if (!activeFocus && !textMenuOpen) commit()
                            onTextMenuFocusLeft: commit()
                            Keys.onEscapePressed: {
                                finished = true;
                                list.renameRow = -1;
                                list.forceActiveFocus();
                            }
                        }
                    }
                }

                UI.Label {
                    Layout.preferredWidth: 150
                    visible: !list.folder.isComputer
                    text: row.date
                    size: 12
                    color: UI.Style.inkMuted
                }

                UI.Label {
                    Layout.preferredWidth: list.typeWidth
                    text: row.type
                    size: 12
                    color: UI.Style.inkMuted
                }

                UI.Label {
                    Layout.preferredWidth: list.sizeWidth
                    horizontalAlignment: Text.AlignRight
                    text: row.size
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                enabled: list.renameRow !== row.index
                onPressed: mouse => {
                    list.forceActiveFocus();
                    // A right click on one of several selected items keeps
                    // them all, as in Files.
                    if (mouse.button === Qt.LeftButton) {
                        list.folder.select(row.index, mouse.modifiers);
                    } else if (!row.selected) {
                        list.folder.select(row.index, 0);
                    }
                }
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        list.requestMenu(row.index, row, mouse.x, mouse.y, false);
                    }
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        list.activated(row.index);
                    }
                }
            }
        }

        // Clicking below the last row clears the selection, as in Files.
        MouseArea {
            id: belowRows
            z: -1
            width: view.width
            height: Math.max(0, view.height - view.contentHeight)
            y: view.contentHeight
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: {
                list.forceActiveFocus();
                list.folder.clearSelection();
            }
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton) {
                    list.requestMenu(-1, belowRows, mouse.x, mouse.y, false);
                }
            }
        }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.topMargin: 6
        visible: list.icons
        clip: true
        // 8px between tiles, as in Files, so a selected tile's highlight
        // keeps clear of its neighbours.
        cellWidth: 148
        cellHeight: 158
        model: list.icons ? list.folder : null
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        activeFocusOnTab: false
        bottomMargin: 6
        ScrollBar.vertical: UI.ScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: list.folder.title

        delegate: Item {
            id: tile

            required property int index
            required property string name
            required property string iconName
            required property url url
            required property bool isDir
            required property bool selected
            required property bool hidden
            required property bool preview
            required property var stamp

            width: 140
            height: 150
            Accessible.role: Accessible.ListItem
            Accessible.name: name
            Accessible.selected: selected

            Rectangle {
                anchors.fill: parent
                radius: 8
                color: tileMouse.pressed ? UI.Style.surfacePressed : tile.selected ? UI.Style.accentSelected
                    : tileMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
                border.width: 1
                border.color: tile.selected ? UI.Style.accentBorderStrong
                    : list.activeFocus && list.folder.currentIndex === tile.index ? UI.Style.accentBorderStrong : "transparent"
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 8
                spacing: 6

                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 96
                    height: 96
                    opacity: tile.hidden ? 0.5 : 1

                    UI.ThemeIcon {
                        anchors.fill: parent
                        visible: thumb.status !== Image.Ready
                        name: tile.iconName
                        fallback: tile.isDir ? "folder" : "text-x-generic"
                    }

                    Image {
                        id: thumb
                        anchors.fill: parent
                        visible: status === Image.Ready
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize: Qt.size(192, 192)
                        source: tile.preview ? "image://thumb/" + tile.stamp + "/" + encodeURIComponent(tile.url.toString()) : ""
                        smooth: true
                        mipmap: true
                    }
                }

                UI.Label {
                    visible: list.renameRow !== tile.index
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    size: 12
                    lineHeight: 1.1
                    text: tile.name
                    opacity: tile.hidden ? 0.6 : 1
                }

                Loader {
                    width: parent.width
                    active: list.renameRow === tile.index
                    visible: active
                    sourceComponent: UI.TextField {
                        height: 28
                        implicitHeight: 28
                        text: tile.name
                        escapeClears: false
                        Component.onCompleted: {
                            forceActiveFocus();
                            selectAll();
                        }
                        property bool finished: false
                        function commit() {
                            if (finished) {
                                return;
                            }
                            finished = true;
                            list.folder.renameItem(tile.index, text);
                            list.renameRow = -1;
                            list.forceActiveFocus();
                        }
                        onAccepted: commit()
                        // The text menu holds focus while it is open; the rename is not over.
                        onActiveFocusChanged: if (!activeFocus && !textMenuOpen) commit()
                        onTextMenuFocusLeft: commit()
                        Keys.onEscapePressed: {
                            finished = true;
                            list.renameRow = -1;
                            list.forceActiveFocus();
                        }
                    }
                }
            }

            MouseArea {
                id: tileMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                enabled: list.renameRow !== tile.index
                onPressed: mouse => {
                    list.forceActiveFocus();
                    if (mouse.button === Qt.LeftButton) {
                        list.folder.select(tile.index, mouse.modifiers);
                    } else if (!tile.selected) {
                        list.folder.select(tile.index, 0);
                    }
                }
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        list.requestMenu(tile.index, tile, mouse.x, mouse.y, false);
                    }
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        list.activated(tile.index);
                    }
                }
            }
        }

        // Clicking between or below the tiles clears the selection.
        MouseArea {
            id: betweenTiles
            parent: grid.contentItem
            z: -1
            width: Math.max(grid.width, grid.contentWidth)
            height: Math.max(grid.height, grid.contentHeight)
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: {
                list.forceActiveFocus();
                list.folder.clearSelection();
            }
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton) {
                    list.requestMenu(-1, betweenTiles, mouse.x, mouse.y, false);
                }
            }
        }
    }

    UI.Spinner {
        anchors.centerIn: list.icons ? grid : view
        size: 24
        visible: list.slowLoad && list.folder.loading && list.folder.count === 0
        Accessible.role: Accessible.Indicator
        Accessible.name: qsTr("Loading")
    }

    ColumnLayout {
        anchors.centerIn: list.icons ? grid : view
        width: Math.min(view.width - 40, 360)
        visible: list.emptyText.length > 0 && list.folder.count === 0
        spacing: 12

        UI.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: list.emptyText
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Button {
            Layout.alignment: Qt.AlignHCenter
            visible: list.emptyButton.length > 0
            text: list.emptyButton
            onClicked: list.emptyAction(list.emptyActionName)
        }
    }
}
