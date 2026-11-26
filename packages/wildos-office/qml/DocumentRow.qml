import QtQuick
import QtQuick.Templates as T
import WildOS.Office
import WildOS.UI as UI

// One document in Recent, Pinned or the search results (WriteStart.dc):
// the app icon, the name over its folder, when it last changed, and the
// row's menu behind "..." or a right click.
T.AbstractButton {
    id: row

    required property var modelData
    property bool removable: true
    readonly property bool lit: hovered || visualFocus || menu.visible

    signal openRequested(string path, string name)

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: 48
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ListItem
    Accessible.name: modelData.name + ", " + modelData.folder + ", " + modelData.when

    onClicked: openRequested(modelData.path, modelData.name)
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onMenuPressed: menu.popup(more, 0, more.height)

    background: Rectangle {
        radius: 8
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: row.lit
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.down ? UI.Style.surfacePressed : row.lit ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        UI.FocusRing {
            visible: row.visualFocus
        }
    }

    contentItem: Item {}

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => menu.popup(row, eventPoint.position.x, eventPoint.position.y)
    }

    AppIcon {
        id: icon
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        size: 26
    }

    Column {
        anchors.left: icon.right
        anchors.leftMargin: 14
        anchors.right: when.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        UI.Label {
            width: parent.width
            text: row.modelData.name
            size: 13.5
            font.weight: UI.Style.weightMedium
        }
        UI.Label {
            width: parent.width
            text: row.modelData.folder
            size: 12
            color: UI.Style.inkMuted
        }
    }

    UI.Label {
        id: when
        anchors.right: more.left
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 140
        text: row.modelData.when
        size: 12.5
        color: UI.Style.inkMuted
    }

    UI.IconButton {
        id: more
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 30
        height: 30
        iconName: "more"
        iconColor: UI.Style.iconMuted
        tip: qsTr("More options")
        focusPolicy: Qt.NoFocus
        onClicked: menu.popup(more, 0, more.height + 4)
    }

    UI.Menu {
        id: menu
        entries: {
            const list = [
                { text: qsTr("Open"), icon: "open", bold: true, run: () => row.openRequested(row.modelData.path, row.modelData.name) },
                { text: qsTr("Open file location"), icon: "open-folder", run: () => Office.showInFolder(row.modelData.path) },
                { separator: true },
                row.modelData.pinned
                    ? { text: qsTr("Unpin"), icon: "unpin", run: () => Office.unpin(row.modelData.path) }
                    : { text: qsTr("Pin"), icon: "pin", run: () => Office.pin(row.modelData.path) }
            ];
            if (row.removable) {
                list.push({ text: qsTr("Remove from list"), icon: "close", run: () => Office.forget(row.modelData.path) });
            }
            return list;
        }
    }
}
