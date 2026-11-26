pragma ComponentBehavior: Bound

import QtQuick
import "code/helpers.js" as Helpers
import WildOS.UI as UI

// One pinned app, or a folder of them: a 42px icon over a 12px label in a
// 92px cell. A folder's icon is its first four apps, small, on a tile, as
// on Windows 11. In a grid that can be rearranged it is dragged to another
// tile's place, where a line shows which side it will land, or onto the
// middle of another tile, which lights up, to put the two in a folder.
Item {
    id: tile

    required property int index
    // An entry from HomeView.tiles: { kind: "app", row } or
    // { kind: "folder", folder, name, rows }.
    required property var modelData
    required property NavGrid grid
    required property var home

    readonly property bool isFolder: modelData.kind === "folder"
    // The app's row in the pinned apps model, for its icon, name and menu.
    readonly property var app: isFolder ? null : home.appData(modelData.row)
    readonly property var icon: isFolder ? "folder" : (app ? app.decoration : "")
    readonly property string label: isFolder ? modelData.name
        : Helpers.appName(String(app ? app.favoriteId : ""), String(app ? app.display : ""))

    width: grid.cellWidth - grid.gap
    height: grid.rowHeight

    readonly property bool dragged: grid.dragFrom === index
    readonly property bool dropHere: grid.dragFrom >= 0 && grid.dragTo === index && !dragged && grid.dragOnto < 0
    readonly property bool dropOnto: grid.dragOnto === index

    opacity: dragged ? 0.4 : 1

    Accessible.role: Accessible.Button
    Accessible.name: isFolder ? qsTr("Folder %1").arg(label) : label

    Rectangle {
        anchors.fill: parent
        radius: Style.radiusItem
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: mouse.containsMouse
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: tile.dropOnto ? Style.accentTint
            : mouse.pressed ? UI.Style.surfacePressed : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        border.width: tile.dropOnto ? 1 : 0
        border.color: UI.Style.accentBorderStrong
    }

    Column {
        anchors.centerIn: parent
        spacing: 8

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 42
            height: 42

            AppIcon {
                anchors.fill: parent
                visible: !tile.isFolder
                source: tile.icon
            }

            Rectangle {
                anchors.fill: parent
                visible: tile.isFolder
                radius: 8
                color: UI.Style.surfaceHover
                border.width: 1
                border.color: UI.Style.contrastDivider

                Grid {
                    anchors.centerIn: parent
                    columns: 2
                    spacing: 3

                    Repeater {
                        model: tile.isFolder ? tile.modelData.rows.slice(0, 4) : []
                        delegate: AppIcon {
                            required property int modelData
                            width: 16
                            height: 16
                            source: {
                                const data = tile.home.appData(modelData);
                                return data ? data.decoration : "";
                            }
                        }
                    }
                }
            }
        }

        UI.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, tile.width)
            horizontalAlignment: Text.AlignHCenter
            size: 12
            text: tile.label
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // A grid with more rows than it shows scrolls when dragged up or
        // down, so there only a sideways drag rearranges.
        preventStealing: tile.grid.reorder && !tile.grid.interactive

        property point pressedAt: Qt.point(0, 0)
        // A drag that ends back on its own tile is not a click.
        property bool wasDragged: false

        onPressed: event => {
            tile.grid.currentIndex = tile.index;
            pressedAt = Qt.point(event.x, event.y);
            wasDragged = false;
        }
        onPositionChanged: event => {
            if (!tile.grid.reorder || !(pressedButtons & Qt.LeftButton)) {
                return;
            }
            if (tile.grid.dragFrom < 0
                    && Math.abs(event.x - pressedAt.x) + Math.abs(event.y - pressedAt.y) > Qt.styleHints.startDragDistance) {
                wasDragged = true;
                tile.grid.dragStart(tile.index, tile.icon);
            }
            if (tile.grid.dragFrom >= 0) {
                const at = mapToItem(tile.grid.contentItem, event.x, event.y);
                tile.grid.dragMove(at.x, at.y);
            }
        }
        onReleased: {
            if (tile.grid.dragFrom >= 0) {
                tile.grid.dragEnd(true);
            }
        }
        onCanceled: {
            if (tile.grid.dragFrom >= 0) {
                tile.grid.dragEnd(false);
            }
        }
        onClicked: event => {
            if (wasDragged) {
                return;
            }
            if (event.button === Qt.RightButton) {
                tile.grid.menuRequested(tile.index, tile, event.x, event.y);
            } else {
                tile.grid.activated(tile.index);
            }
        }
        onPressAndHold: event => {
            if (!wasDragged) {
                tile.grid.menuRequested(tile.index, tile, event.x, event.y);
            }
        }
    }

    Rectangle {
        visible: tile.dropHere
        x: tile.grid.dragTo > tile.grid.dragFrom ? parent.width - 1 : -2
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: parent.height - 24
        radius: 1.5
        color: UI.Style.accent
    }

    UI.FocusRing {
        visible: tile.GridView.isCurrentItem && tile.grid.activeFocus && Nav.keyboard
    }
}
