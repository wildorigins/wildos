pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

// An open folder of pinned apps, over the pinned grid as Windows 11 opens
// one: its name on top, which is renamed by clicking it, and its apps
// below. Apps are rearranged by dragging, and dragged out of the panel to
// take them out of the folder. A click outside or Escape closes it.
FocusScope {
    id: view

    required property var home
    // The id of the open folder, or empty.
    property string folder: ""
    readonly property var tile: folder.length > 0 ? home.folderTile(folder) : null
    readonly property var entries: tile ? tile.apps.map((id, i) => ({ kind: "app", id: id, row: tile.rows[i] })) : []

    signal closed()

    function open(id: string): void {
        folder = id;
        grid.currentIndex = 0;
        grid.forceActiveFocus();
    }

    function close(): void {
        if (folder.length === 0) {
            return;
        }
        if (title.activeFocus) {
            home.renameFolder(folder, title.text);
        }
        folder = "";
        closed();
    }

    visible: tile !== null
    width: 4 * 104 + 2 * 20
    height: header.height + grid.height + 2 * 16

    // The rest of the page, dimmed; a click there closes the folder.
    MouseArea {
        parent: view.parent
        anchors.fill: parent
        z: view.z - 1
        visible: view.visible
        onClicked: view.close()

        Rectangle {
            anchors.fill: parent
            radius: Style.radiusItem
            color: UI.Style.wallpaperDim
            opacity: 0.35
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Style.radiusItem + 4
        // Solid: the pinned apps under it would show through glass.
        color: UI.Style.windowOpaque
        border.width: 1
        border.color: UI.Style.borderContextMenu
    }

    Item {
        id: header
        x: 20
        y: 16
        width: parent.width - 40
        height: 40

        // The name, edited where it is, as on Windows.
        TextInput {
            id: title
            anchors.centerIn: parent
            width: Math.min(parent.width, Math.max(contentWidth + 24, 120))
            horizontalAlignment: TextInput.AlignHCenter
            font.pixelSize: UI.Style.px(15)
            font.weight: Font.DemiBold
            color: UI.Style.ink
            selectionColor: UI.Style.textSelection
            selectedTextColor: UI.Style.ink
            maximumLength: 60
            selectByMouse: true
            text: view.tile ? view.tile.name : ""
            Accessible.role: Accessible.EditableText
            Accessible.name: qsTr("Folder name")
            onEditingFinished: {
                if (view.folder.length > 0) {
                    view.home.renameFolder(view.folder, text);
                }
            }
            Keys.onReturnPressed: grid.forceActiveFocus()
            Keys.onEnterPressed: grid.forceActiveFocus()
            Keys.onEscapePressed: {
                text = view.tile ? view.tile.name : "";
                grid.forceActiveFocus();
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -6
                z: -1
                radius: 6
                color: title.activeFocus ? UI.Style.surfaceSunken : titleHover.hovered ? UI.Style.surfaceHover : "transparent"
                border.width: title.activeFocus ? 1 : 0
                border.color: UI.Style.accentBorderStrong
            }

            HoverHandler {
                id: titleHover
                cursorShape: Qt.IBeamCursor
            }
        }
    }

    NavGrid {
        id: grid
        x: 20 - gap
        y: header.y + header.height + 8
        width: parent.width - 40 + gap
        columns: 4
        maxRows: 3
        rowHeight: 92 + UI.Style.px(12) - 12
        model: view.entries
        delegate: AppTile {
            grid: grid
            home: view.home
        }
        reorder: true
        onMoved: (from, to) => view.home.reorderFolder(view.folder, from, to)
        onDroppedOutside: from => {
            const entry = view.entries[from];
            if (entry) {
                view.home.takeOut(view.folder, entry.id);
            }
        }
        onActivated: index => {
            const entry = view.entries[index];
            if (entry && view.home.pins.trigger(entry.row, "", null)) {
                view.home.launcher.close();
            }
        }
        onMenuRequested: (index, item, x, y) => {
            const entry = view.entries[index];
            if (entry) {
                view.home.menuRequested(view.home.pins, entry.row, item, x, y, true);
            }
        }
        onExitUp: title.forceActiveFocus()
        onExitDown: {}
        Keys.onEscapePressed: view.close()
    }

    Keys.onEscapePressed: view.close()
}
