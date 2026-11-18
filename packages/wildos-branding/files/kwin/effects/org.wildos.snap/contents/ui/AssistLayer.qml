pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

import "layouts.js" as Layouts

// Snap assist on the screen it belongs to: the zone being filled holds the
// menu glass with the other windows to choose from, and the zones after it
// wait in the accent tint the drag preview uses. Presses on the glass stay
// here; presses anywhere else reach the scene and close assist.
Item {
    id: layer

    required property var owner
    required property var screen

    readonly property rect area: owner.areaFor(screen)
    readonly property var layout: owner.assistLayout >= 0 ? Layouts.layouts[owner.assistLayout] : null
    readonly property int zone: owner.assistZones.length > 0 ? owner.assistZones[0] : -1

    // A zone's preview rectangle in this layer's coordinates.
    function zoneBox(zoneIndex: int): rect {
        if (!layout || zoneIndex < 0 || !screen) {
            return Qt.rect(0, 0, 0, 0);
        }
        const r = owner.previewRect(layout.zones[zoneIndex], area);
        const g = screen.geometry;
        return Qt.rect(r.x - g.x, r.y - g.y, r.width, r.height);
    }

    Repeater {
        model: layer.owner.assistZones.slice(1)

        delegate: Rectangle {
            required property int modelData
            readonly property rect box: layer.zoneBox(modelData)

            x: box.x
            y: box.y
            width: box.width
            height: box.height
            radius: UI.Style.windowRadius
            color: UI.Style.accentAlpha(0.16, 0.12)
            border.width: 2
            border.color: UI.Style.accentAlpha(0.70, 0.60)
        }
    }

    UI.Glass {
        id: panel

        readonly property rect box: layer.zoneBox(layer.zone)
        readonly property int padding: 18
        readonly property int gap: 12
        // A card is its preview well, wellRatio as tall as it is wide, plus
        // cardChrome for the title row and the space around it.
        readonly property real wellRatio: 0.6
        readonly property int cardChrome: 52

        visible: layer.zone >= 0
        level: "menu"
        x: box.x
        y: box.y
        width: box.width
        height: box.height

        Accessible.role: Accessible.List
        Accessible.name: title.text

        opacity: 0
        scale: 0.97
        Component.onCompleted: {
            opacity = 1;
            scale = 1;
        }

        Behavior on opacity {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }

        MouseArea {
            anchors.fill: parent
        }

        UI.Label {
            id: title

            x: panel.padding
            y: panel.padding
            width: panel.width - panel.padding * 2
            text: layer.layout && layer.zone >= 0
                ? qsTr("Fill the %1").arg(layer.layout.labels[layer.zone].toLowerCase())
                : ""
            size: 12
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkStatus
        }

        UI.Label {
            id: hint

            x: panel.padding
            y: panel.height - panel.padding - height
            width: panel.width - panel.padding * 2
            text: qsTr("Pick a window, or press Esc to leave the rest empty.")
            size: 12
            wrapMode: Text.Wrap
            color: UI.Style.inkMuted
        }

        Flickable {
            id: scroller

            // The card width that fits every window into the zone at the
            // largest size, capped so two windows do not fill a whole half.
            // Ties go to more columns, which reads as a grid, not a list.
            readonly property var fit: {
                const n = Math.max(1, layer.owner.assistWindows.length);
                let best = { columns: 1, width: 0 };
                for (let c = 1; c <= n; ++c) {
                    const rows = Math.ceil(n / c);
                    const byWidth = (width - (c - 1) * panel.gap) / c;
                    const byHeight = ((height - (rows - 1) * panel.gap) / rows - panel.cardChrome) / panel.wellRatio;
                    const w = Math.min(byWidth, byHeight, 280);
                    if (w >= best.width) {
                        best = { columns: c, width: w };
                    }
                }
                best.width = Math.floor(Math.max(best.width, 120));
                return best;
            }

            x: panel.padding
            y: title.y + title.height + 12
            width: panel.width - panel.padding * 2
            height: hint.y - 12 - y
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, grid.height)
            boundsBehavior: Flickable.StopAtBounds
            interactive: grid.height > height

            Grid {
                id: grid

                x: Math.max(0, Math.round((scroller.width - width) / 2))
                y: Math.max(0, Math.round((scroller.height - height) / 2))
                columns: scroller.fit.columns
                spacing: panel.gap

                Repeater {
                    model: layer.owner.assistWindows

                    delegate: AssistCard {
                        required property var modelData
                        required property int index

                        width: scroller.fit.width
                        wellHeight: Math.round(width * panel.wellRatio)
                        window: modelData
                        selected: layer.owner.assistIndex === index
                        onHoveredChanged: {
                            if (hovered) {
                                layer.owner.assistIndex = index;
                            }
                        }
                        onActivated: layer.owner.assistChoose(modelData)
                    }
                }
            }
        }
    }
}
