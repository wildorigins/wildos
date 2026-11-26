pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import "code/helpers.js" as Helpers
import WildOS.UI as UI

// A recent document: icon or picture preview, name without its extension,
// and the folder it lives in.
Item {
    id: row

    required property int index
    required property var model
    required property NavGrid grid

    readonly property bool isImage: Helpers.isImage(model.url)

    width: grid.cellWidth - grid.gap
    height: grid.rowHeight
    visible: index < grid.shownCount

    Accessible.role: Accessible.Button
    Accessible.name: title.text

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
        color: mouse.pressed ? UI.Style.surfacePressed : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
    }

    Item {
        id: iconBox
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 32
        height: 32

        AppIcon {
            anchors.fill: parent
            source: row.model.decoration
            visible: !preview.visible
        }

        Kirigami.ShadowedImage {
            id: preview
            anchors.fill: parent
            radius: 6
            source: row.isImage ? row.model.url : ""
            sourceSize.width: 64
            sourceSize.height: 64
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: row.isImage && status === Image.Ready
        }
    }

    Column {
        anchors.left: iconBox.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        UI.Label {
            id: title
            width: parent.width
            font.weight: UI.Style.weightMedium
            text: Helpers.withoutExtension(row.model.display)
        }

        UI.Label {
            width: parent.width
            size: 12
            color: UI.Style.inkMuted
            text: String(row.model.description ?? "").split("/").filter(part => part.length > 0).join(" › ")
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: row.grid.currentIndex = row.index
        onClicked: event => {
            if (event.button === Qt.RightButton) {
                row.grid.menuRequested(row.index, row, event.x, event.y);
            } else {
                row.grid.activated(row.index);
            }
        }
        onPressAndHold: event => row.grid.menuRequested(row.index, row, event.x, event.y)
    }

    UI.FocusRing {
        visible: row.GridView.isCurrentItem && row.grid.activeFocus && Nav.keyboard
    }
}
