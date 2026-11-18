import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A quick settings tile as the Tray flyouts design draws it: a 76 px button
// with the 20 px icon above the 12 px name, both inside the button, left
// aligned with 12 px padding and 6 px between them. On is the accent fill
// with on-accent ink and a semibold name, off the tile fill, 14 px corners.
// The connection's status ("Connected", "Not set up") is read out to screen
// readers rather than drawn, as the design shows names only.
//
// Every tile is one button. Wi-Fi, Bluetooth and VPN also carry a small
// chevron in their top right corner, level with the icon, that opens the
// tile's page; it lights up on its own when pointed at, so it reads as a
// second, smaller target inside the same shape rather than half a tile.
Item {
    id: tile

    property string label
    property string status
    property string iconPath
    property string iconFill
    property bool checked: false
    // A tile that only opens something (Ethernet) is a plain button to
    // screen readers, not a switch.
    property bool switchable: true
    property bool hasPage: false
    property string description
    property string pageName
    // Edit mode shows every tile with a badge that adds or removes it, and
    // lets a tile be dragged to another's place (or moved with Alt+Shift
    // and the left or right arrow).
    property bool editing: false
    property bool shown: true
    property bool dragging: false
    property bool dropTarget: false

    readonly property bool on: checked && !editing
    readonly property color inkColor: on ? UI.Style.textOnAccent : UI.Style.ink
    readonly property bool showChevron: hasPage && !editing
    property alias toggleButton: toggle
    property alias pageButton: page

    signal toggled()
    signal pageRequested()
    signal editToggled()
    // A right click or a press and hold opens the tile's menu
    // (MenusApps.dc "Quick settings tile").
    signal menuRequested(real x, real y)
    // A drag in edit mode, in this tile's coordinates; `drop` is false when
    // it was called off.
    signal dragStarted()
    signal dragMoved(real x, real y)
    signal dragEnded(bool drop)
    signal moveRequested(int step)

    // Grows with the text size so a larger name still fits under the icon.
    implicitHeight: Math.max(Metrics.tileButtonHeight, content.implicitHeight + 2 * 14)
    opacity: dragging ? 0.4 : editing && !shown ? 0.55 : 1

    DragHandler {
        id: drag
        enabled: tile.editing
        target: null
        onActiveChanged: {
            if (active) {
                tile.dragStarted();
            } else {
                tile.dragEnded(true);
            }
        }
        onCentroidChanged: {
            if (active) {
                tile.dragMoved(centroid.position.x, centroid.position.y);
            }
        }
        onCanceled: tile.dragEnded(false)
    }

    function activate(): void {
        if (editing) {
            editToggled();
        } else {
            toggled();
        }
    }

    function onAccent(alpha: real): color {
        const c = UI.Style.textOnAccent;
        return Qt.rgba(c.r, c.g, c.b, alpha);
    }

    HoverHandler {
        id: tileHover
    }

    // Flyouts.dc lifts a 0.08 tile to 0.12 under the pointer: the general
    // 0.09 hover would barely show on a tile that is already filled.
    Rectangle {
        anchors.fill: parent
        radius: Metrics.tileRadius
        color: {
            if (tile.on) {
                return toggle.down ? UI.Style.accentPressed
                    : tileHover.hovered ? UI.Style.accentHover : UI.Style.accent;
            }
            return toggle.down ? UI.Style.tone(0.16)
                : tileHover.hovered ? UI.Style.tone(0.12) : UI.Style.surfaceTile;
        }
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: toggle.visualFocus || (toggle.activeFocus && Nav.keyboard)
            targetRadius: Metrics.tileRadius
        }

        // Where a dragged tile would land.
        Rectangle {
            visible: tile.dropTarget
            anchors.fill: parent
            radius: Metrics.tileRadius
            color: "transparent"
            border.width: 2
            border.color: UI.Style.accent
        }
    }

    T.AbstractButton {
        id: toggle
        anchors.fill: parent
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: tile.label
        Accessible.description: tile.status.length > 0 ? tile.status : tile.description
        Accessible.role: tile.switchable && !tile.editing ? Accessible.CheckBox : Accessible.Button
        Accessible.checkable: tile.switchable && !tile.editing
        Accessible.checked: tile.checked
        onClicked: tile.activate()
        Keys.onPressed: event => {
            const step = event.key === Qt.Key_Left ? -1 : event.key === Qt.Key_Right ? 1 : 0;
            if (tile.editing && step !== 0 && (event.modifiers & Qt.AltModifier) && (event.modifiers & Qt.ShiftModifier)) {
                tile.moveRequested(step);
                event.accepted = true;
            }
        }
        onPressAndHold: {
            if (!tile.editing) {
                tile.menuRequested(width / 2, height / 2);
            }
        }

        TapHandler {
            enabled: !tile.editing
            acceptedButtons: Qt.RightButton
            onTapped: eventPoint => tile.menuRequested(eventPoint.position.x, eventPoint.position.y)
        }

        contentItem: Item {
            Column {
                id: content
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                UI.Glyph {
                    width: 20
                    height: 20
                    path: tile.iconPath
                    fillPath: tile.iconFill
                    color: tile.inkColor
                    strokeWidth: tile.on ? 2.2 : 1.9
                }

                UI.Label {
                    width: parent.width
                    text: tile.label
                    // A larger text size may need a second line rather than "Do not di...".
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    verticalAlignment: Text.AlignTop
                    size: 12
                    font.weight: tile.on ? UI.Style.weightSemiBold : UI.Style.weightMedium
                    color: tile.inkColor
                }
            }
        }
    }

    // The way into the tile's page: a 14 px chevron level with the icon, on
    // a 28 px fill that only shows while the pointer is on it. The hit area
    // reaches the tile's top and right edges so it is easy to catch.
    T.AbstractButton {
        id: page
        visible: tile.showChevron
        anchors.right: parent.right
        anchors.top: parent.top
        width: 40
        // Twice the icon row's centre, so the fill below is centred on it.
        height: 2 * (content.y + 10)
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: tile.pageName
        onClicked: tile.pageRequested()

        background: Item {
            Rectangle {
                id: chevronFill
                anchors.centerIn: parent
                width: 28
                height: 28
                radius: 8
                color: page.down ? (tile.on ? tile.onAccent(0.20) : UI.Style.tone(0.16))
                    : page.hovered ? (tile.on ? tile.onAccent(0.12) : UI.Style.tone(0.10))
                    : "transparent"
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                // Inside the tile, where an outside ring would cross its edge;
                // on-accent ink on an accent tile so it still shows.
                Rectangle {
                    visible: page.visualFocus || (page.activeFocus && Nav.keyboard)
                    anchors.fill: parent
                    radius: 8
                    color: "transparent"
                    border.width: 2
                    border.color: tile.on ? UI.Style.textOnAccent : UI.Style.accent
                }
            }
        }

        contentItem: Item {
            UI.Glyph {
                anchors.centerIn: parent
                width: 14
                height: 14
                path: Icons.chevronRight
                color: tile.inkColor
                strokeWidth: 2.2
            }
        }
    }

    // A small corner badge in edit mode: minus removes the tile, plus adds it.
    Rectangle {
        visible: tile.editing
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: -6
        width: 22
        height: 22
        radius: 11
        color: tile.shown ? UI.Style.glassMenu : UI.Style.accent
        border.width: 1
        border.color: UI.Style.borderControl

        UI.Glyph {
            anchors.centerIn: parent
            width: 14
            height: 14
            path: tile.shown ? Icons.minus : Icons.plus
            color: tile.shown ? UI.Style.ink : UI.Style.textOnAccent
            strokeWidth: 2.4
        }
    }
}
