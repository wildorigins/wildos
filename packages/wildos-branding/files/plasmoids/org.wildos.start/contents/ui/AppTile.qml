pragma ComponentBehavior: Bound

import QtQuick
import "code/helpers.js" as Helpers
import WildOS.UI as UI

// One pinned app: a 42px icon over a 12px label in a 92px cell. In a grid
// that can be rearranged it is dragged to another tile's place; a line on
// that tile shows which side it will land.
Item {
    id: tile

    required property int index
    required property var model
    required property NavGrid grid

    readonly property string label: Helpers.appName(model.favoriteId, model.display)

    width: grid.cellWidth - grid.gap
    height: grid.rowHeight

    readonly property bool dragged: grid.dragFrom === index
    readonly property bool dropHere: grid.dragFrom >= 0 && grid.dragTo === index && !dragged

    opacity: dragged ? 0.4 : 1

    Accessible.role: Accessible.Button
    Accessible.name: label

    Rectangle {
        anchors.fill: parent
        radius: Style.radiusItem
        color: mouse.pressed ? UI.Style.surfacePressed : mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 8

        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 42
            height: 42
            source: tile.model.decoration
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
                tile.grid.dragStart(tile.index, tile.model.decoration);
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
