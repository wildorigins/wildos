import QtQuick
import WildOS.Files
import WildOS.UI as UI

// What a file or folder does under the pointer in either view: Windows
// selection rules, open on double click, the context menu, middle click
// for a new tab, dragging out and dropping into folders.
Item {
    id: entry

    required property var view
    required property int index
    required property string name
    required property string fileName
    required property string iconName
    required property url url
    required property bool isDir
    required property bool isDrive
    required property string col2
    required property string col3
    required property string sizeText
    required property bool selected
    required property bool preview
    required property var modified
    required property bool faded
    required property string subtitle
    required property real usage
    property bool renaming: false
    // The label that draws the name. When it has to cut the name short,
    // resting the pointer on the item shows the whole of it.
    property Item nameLabel: null
    readonly property bool hovered: mouse.containsMouse
    readonly property bool highlighted: hovered || drop.containsDrag
    // Where the keyboard is, selected or not, once the keyboard has been
    // used: with Ctrl held the focus moves without changing the selection,
    // so it has to show on its own.
    readonly property bool focusMarked: view.keyboardUsed && view.contentFocused && !renaming && !!view.folder && view.folder.currentIndex === index
    // The corner of the item's highlight, which the focus outline follows.
    property real corner: 6

    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.selected: selected

    // The thing actually dragged; Qt moves it, the drag image is the icon,
    // or a pile of it with a count when several items go together. The
    // image is made on the press that could start a drag, not kept for
    // every item in the folder.
    Item {
        id: proxy
        width: 1
        height: 1
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
        Drag.proposedAction: Qt.MoveAction
        Drag.hotSpot.x: 24
        Drag.hotSpot.y: 24
        Drag.active: mouse.drag.active
        Drag.onDragFinished: {
            proxy.x = 0;
            proxy.y = 0;
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        enabled: !entry.renaming
        drag.target: entry.isDrive ? null : proxy
        drag.threshold: 8
        property bool pendingSingle: false

        onPressed: m => {
            entry.view.keyboardUsed = false;
            entry.view.focusContent();
            if (m.button === Qt.MiddleButton) {
                return;
            }
            if (m.button === Qt.RightButton) {
                if (!entry.selected) {
                    entry.view.folder.select(entry.index, 0);
                }
                return;
            }
            pendingSingle = entry.selected && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier));
            if (!pendingSingle) {
                entry.view.folder.select(entry.index, m.modifiers);
            }
            proxy.Drag.mimeData = {
                "text/uri-list": entry.view.folder.selectedUrls().map(u => u.toString()).join("\r\n")
            };
            const moving = entry.view.folder.selectedCount;
            proxy.Drag.imageSourceSize = moving > 1 ? Qt.size(64, 64) : Qt.size(48, 48);
            proxy.Drag.imageSource = moving > 1
                ? "image://dragicon/" + moving + "/" + encodeURIComponent(String(UI.Style.accent)) + "/" + encodeURIComponent(String(UI.Style.textOnAccent)) + "/" + encodeURIComponent(entry.iconName)
                : "image://icon/" + encodeURIComponent(entry.iconName);
        }
        onReleased: m => {
            if (pendingSingle && !drag.active && m.button === Qt.LeftButton) {
                entry.view.folder.select(entry.index, 0);
            }
            pendingSingle = false;
        }
        onClicked: m => {
            if (m.button === Qt.MiddleButton) {
                if (entry.isDir && !entry.isDrive) {
                    entry.view.newTab(entry.url, false);
                }
            } else if (m.button === Qt.RightButton) {
                entry.view.openItemMenu(entry, m.x, m.y);
            }
        }
        onDoubleClicked: m => {
            if (m.button === Qt.LeftButton && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                entry.view.folder.activate(entry.index);
            }
        }
    }

    // Drawn on the item's own edge, not outside it as on a button: the
    // items sit 2px apart and the list clips at its sides.
    UI.FocusRing {
        visible: entry.focusMarked
        anchors.margins: 0
        targetRadius: entry.corner - 4
        z: 1
    }

    Loader {
        anchors.fill: parent
        active: entry.hovered && !mouse.pressed && !entry.renaming && entry.nameLabel !== null && entry.nameLabel.truncated
        sourceComponent: UI.Tooltip {
            text: entry.name
            visible: true
        }
    }

    DropArea {
        id: drop
        anchors.fill: parent
        enabled: entry.isDir && !entry.selected
        onEntered: dragEvent => dragEvent.accept(dragEvent.proposedAction)
        onDropped: dropEvent => entry.view.dropUrls(dropEvent, entry.url)
    }
}
