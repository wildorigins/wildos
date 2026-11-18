import QtQuick
import WildOS.Files
import WildOS.UI as UI

// What a file or folder does under the pointer in either view: Windows
// selection rules, open on double click, the context menu, middle click
// for a new tab, dragging out and dropping into folders.
Item {
    id: item

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
        enabled: !item.renaming
        drag.target: item.isDrive ? null : proxy
        drag.threshold: 8
        property bool pendingSingle: false

        onPressed: m => {
            item.view.keyboardUsed = false;
            item.view.focusContent();
            if (m.button === Qt.MiddleButton) {
                return;
            }
            if (m.button === Qt.RightButton) {
                if (!item.selected) {
                    item.view.folder.select(item.index, 0);
                }
                return;
            }
            pendingSingle = item.selected && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier));
            if (!pendingSingle) {
                item.view.folder.select(item.index, m.modifiers);
            }
            proxy.Drag.mimeData = {
                "text/uri-list": item.view.folder.selectedUrls().map(u => u.toString()).join("\r\n")
            };
            const moving = item.view.folder.selectedCount;
            proxy.Drag.imageSourceSize = moving > 1 ? Qt.size(64, 64) : Qt.size(48, 48);
            proxy.Drag.imageSource = moving > 1
                ? "image://dragicon/" + moving + "/" + encodeURIComponent(String(UI.Style.accent)) + "/" + encodeURIComponent(String(UI.Style.textOnAccent)) + "/" + encodeURIComponent(item.iconName)
                : "image://icon/" + encodeURIComponent(item.iconName);
        }
        onReleased: m => {
            if (pendingSingle && !drag.active && m.button === Qt.LeftButton) {
                item.view.folder.select(item.index, 0);
            }
            pendingSingle = false;
        }
        onClicked: m => {
            if (m.button === Qt.MiddleButton) {
                if (item.isDir && !item.isDrive) {
                    item.view.newTab(item.url, false);
                }
            } else if (m.button === Qt.RightButton) {
                item.view.openItemMenu(item, m.x, m.y);
            }
        }
        onDoubleClicked: m => {
            if (m.button === Qt.LeftButton && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                item.view.folder.activate(item.index);
            }
        }
    }

    // Drawn on the item's own edge, not outside it as on a button: the
    // items sit 2px apart and the list clips at its sides.
    UI.FocusRing {
        visible: item.focusMarked
        anchors.margins: 0
        targetRadius: item.corner - 4
        z: 1
    }

    Loader {
        anchors.fill: parent
        active: item.hovered && !mouse.pressed && !item.renaming && item.nameLabel !== null && item.nameLabel.truncated
        sourceComponent: UI.Tooltip {
            text: item.name
            visible: true
        }
    }

    DropArea {
        id: drop
        anchors.fill: parent
        enabled: item.isDir && !item.selected
        onEntered: dragEvent => dragEvent.accept(dragEvent.proposedAction)
        onDropped: dropEvent => item.view.dropUrls(dropEvent, item.url)
    }
}
