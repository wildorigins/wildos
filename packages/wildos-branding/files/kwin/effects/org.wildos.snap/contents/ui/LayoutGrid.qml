pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

import "layouts.js" as Layouts

// The snap layout picker of Snap.dc: four miniature screens, two to a row,
// each split into the zones a window can be dropped into. Hovering a zone
// fills it with the accent. The panel around it is SnapPanel.
Item {
    id: picker

    // Zones respond to the pointer. Off while a window is being dragged,
    // because the drag holds the pointer and the owner hit-tests instead.
    property bool interactive: true

    property int activeLayout: -1
    property int activeZone: -1

    signal zoneChosen(int layoutIndex, int zoneIndex)

    // Snap.dc: a 300px panel with 14px padding leaves 272px for two tiles
    // 8px apart. Each zone sits 6px inside its tile with a 2px clear edge,
    // so neighbouring zones are 4px apart.
    readonly property int tileWidth: 132
    readonly property int tileHeight: 64
    readonly property int tileGap: 8
    readonly property int tilePadding: 6
    readonly property int zoneInset: 2
    readonly property int padding: 0
    readonly property int rows: Math.ceil(Layouts.layouts.length / Layouts.columns)

    implicitWidth: Layouts.columns * tileWidth + (Layouts.columns - 1) * tileGap + padding * 2
    implicitHeight: rows * tileHeight + (rows - 1) * tileGap + padding * 2

    Accessible.role: Accessible.List
    Accessible.name: qsTr("Snap layouts")

    function tileOrigin(index: int): point {
        return Qt.point(padding + (index % Layouts.columns) * (tileWidth + tileGap),
                        padding + Math.floor(index / Layouts.columns) * (tileHeight + tileGap));
    }

    // Zone rectangles inside a tile: the zone's share of the tile less its
    // clear edge on every side.
    function zoneBox(zone: var): rect {
        const innerW = tileWidth - tilePadding * 2;
        const innerH = tileHeight - tilePadding * 2;
        const left = tilePadding + zone[0] * innerW + zoneInset;
        const top = tilePadding + zone[1] * innerH + zoneInset;
        const right = tilePadding + (zone[0] + zone[2]) * innerW - zoneInset;
        const bottom = tilePadding + (zone[1] + zone[3]) * innerH - zoneInset;
        return Qt.rect(Math.round(left), Math.round(top), Math.round(right) - Math.round(left), Math.round(bottom) - Math.round(top));
    }

    // Which zone is under a point given in the picker's own coordinates.
    // Points in the gaps between zones count for the nearest zone, so a drag
    // never flickers between "a zone" and "nothing" as it crosses a gap.
    function zoneAt(x: real, y: real): var {
        for (let i = 0; i < Layouts.layouts.length; ++i) {
            const o = tileOrigin(i);
            if (x < o.x || y < o.y || x >= o.x + tileWidth || y >= o.y + tileHeight) {
                continue;
            }
            const zones = Layouts.layouts[i].zones;
            let best = -1;
            let bestDistance = Infinity;
            for (let z = 0; z < zones.length; ++z) {
                const b = zoneBox(zones[z]);
                const dx = Math.max(o.x + b.x - x, 0, x - (o.x + b.x + b.width));
                const dy = Math.max(o.y + b.y - y, 0, y - (o.y + b.y + b.height));
                const d = dx * dx + dy * dy;
                if (d < bestDistance) {
                    bestDistance = d;
                    best = z;
                }
            }
            return { layout: i, zone: best };
        }
        return null;
    }

    function select(layoutIndex: int, zoneIndex: int): void {
        activeLayout = layoutIndex;
        activeZone = zoneIndex;
    }

    function clear(): void {
        select(-1, -1);
    }

    // Arrow keys: left and right walk zone by zone through every layout,
    // up and down move to the layout above or below.
    function moveBy(dx: int, dy: int): void {
        const count = Layouts.layouts.length;
        if (activeLayout < 0) {
            select(0, 0);
            return;
        }
        if (dx !== 0) {
            let layout = activeLayout;
            let zone = activeZone + dx;
            if (zone < 0) {
                layout = (layout - 1 + count) % count;
                zone = Layouts.layouts[layout].zones.length - 1;
            } else if (zone >= Layouts.layouts[layout].zones.length) {
                layout = (layout + 1) % count;
                zone = 0;
            }
            select(layout, zone);
        } else if (dy !== 0) {
            const layout = activeLayout + dy * Layouts.columns;
            if (layout >= 0 && layout < count) {
                select(layout, Math.min(activeZone, Layouts.layouts[layout].zones.length - 1));
            }
        }
    }

    Repeater {
        model: Layouts.layouts.length

        delegate: Rectangle {
            id: tile

            required property int index
            readonly property var layout: Layouts.layouts[index]
            readonly property bool current: picker.activeLayout === index

            x: picker.tileOrigin(index).x
            y: picker.tileOrigin(index).y
            width: picker.tileWidth
            height: picker.tileHeight
            radius: UI.Style.radiusSm
            color: current ? UI.Style.surfaceHover : UI.Style.surfaceRaised

            Behavior on color {
                ColorAnimation { duration: UI.Style.quickDuration }
            }

            Accessible.role: Accessible.Grouping
            Accessible.name: layout.name

            Repeater {
                model: tile.layout.zones.length

                delegate: Rectangle {
                    id: zoneItem

                    required property int index
                    readonly property rect box: picker.zoneBox(tile.layout.zones[index])
                    readonly property bool active: tile.current && picker.activeZone === index

                    x: box.x
                    y: box.y
                    width: box.width
                    height: box.height
                    radius: 4
                    color: active ? UI.Style.accent : UI.Style.tone(0.16)

                    Accessible.role: Accessible.Button
                    Accessible.name: tile.layout.labels[index]
                    Accessible.selected: active

                    Behavior on color {
                        ColorAnimation { duration: 120 }
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -picker.zoneInset
                        enabled: picker.interactive
                        hoverEnabled: true
                        onEntered: picker.select(tile.index, zoneItem.index)
                        onExited: {
                            if (picker.activeLayout === tile.index && picker.activeZone === zoneItem.index) {
                                picker.clear();
                            }
                        }
                        onClicked: picker.zoneChosen(tile.index, zoneItem.index)
                    }
                }
            }
        }
    }
}
