import QtQuick
import WildOS.UI as UI

// A row of the table (AppArchive.dc): 38px, 16px in from the list's edges
// and 12px of padding, radius 6, the check box, an 18px icon or a 22px
// thumbnail at radius 4, the name, then Size and Packed (80, right
// aligned), Modified (140, 24 in) and Type (100), 12px apart.
Item {
    id: row

    required property int index
    required property int entry
    required property string name
    required property bool isDir
    required property string sizeText
    required property string packedText
    required property string modifiedText
    required property string typeText
    required property string kind
    required property bool checked
    required property string thumb
    // Rows can be dragged out to Files or the desktop.
    property bool draggable: true

    // The drag has begun; the window answers with carry() once the files
    // are there to be dragged.
    signal dragRequested()
    signal pressed(int modifiers)
    signal opened()
    signal toggled()
    signal menuRequested(real x, real y)

    implicitHeight: 38

    function carry(urls) {
        if (!mouse.drag.active) {
            return;
        }
        proxy.Drag.mimeData = { "text/uri-list": urls.join("\r\n") };
        proxy.ready = true;
    }

    // The thing actually dragged, as in Files: Qt moves it with the
    // pointer. The drag itself starts only once the files have been taken
    // out of the archive, so whatever they are dropped on finds them.
    Item {
        id: proxy
        property bool ready: false
        width: 1
        height: 1
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.proposedAction: Qt.CopyAction
        Drag.imageSource: ArchiveStyle.icon
        Drag.imageSourceSize: Qt.size(48, 48)
        Drag.hotSpot.x: 24
        Drag.hotSpot.y: 24
        Drag.active: mouse.drag.active && ready
        Drag.onDragFinished: {
            proxy.ready = false;
            proxy.x = 0;
            proxy.y = 0;
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        radius: 6
        color: mouse.pressed ? ArchiveStyle.rowPressed : row.checked ? ArchiveStyle.rowSelected : mouse.containsMouse ? ArchiveStyle.rowHover : "transparent"

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            // A drag here takes the row out, not the list along.
            preventStealing: true
            drag.target: row.draggable ? proxy : null
            drag.threshold: 8
            drag.onActiveChanged: {
                if (drag.active) {
                    if (pressedButtons & Qt.LeftButton) {
                        row.dragRequested();
                    }
                } else {
                    proxy.ready = false;
                    proxy.x = 0;
                    proxy.y = 0;
                }
            }
            onPressed: event => {
                if (event.button === Qt.RightButton && !row.checked) {
                    row.pressed(0);
                }
            }
            onClicked: event => {
                if (event.button === Qt.RightButton) {
                    row.menuRequested(event.x + 16, event.y);
                } else {
                    row.pressed(event.modifiers);
                }
            }
            onDoubleClicked: event => {
                if (event.button === Qt.LeftButton) {
                    row.opened();
                }
            }
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            CheckMark {
                anchors.verticalCenter: parent.verticalCenter
                checked: row.checked
                onClicked: row.toggled()
                Accessible.name: qsTr("Select %1").arg(row.name)
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: row.kind === "image" ? 22 : 18
                height: width

                Rectangle {
                    anchors.fill: parent
                    visible: row.kind === "image"
                    radius: 4
                    color: ArchiveStyle.thumbBlank
                    clip: true

                    Image {
                        anchors.fill: parent
                        visible: row.thumb.length > 0
                        source: row.thumb
                        sourceSize.width: 44
                        sourceSize.height: 44
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                    }
                    ArchiveIcon {
                        anchors.centerIn: parent
                        visible: row.thumb.length === 0
                        name: "image"
                        size: 14
                        color: ArchiveStyle.fileInk
                    }
                }
                ArchiveIcon {
                    anchors.centerIn: parent
                    visible: row.kind !== "image"
                    name: row.kind === "folder" ? "folder" : row.kind === "link" ? "link" : row.kind === "unsafe" ? "warning" : "file-text"
                    size: 18
                    strokeWidth: 1.8
                    color: row.kind === "folder" ? ArchiveStyle.folderInk : row.kind === "unsafe" ? UI.Style.danger : ArchiveStyle.fileInk
                }
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - 80 - 80 - 140 - 100 - 4 * 12
                text: row.name
                size: 13
                elide: Text.ElideMiddle
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: 80
                horizontalAlignment: Text.AlignRight
                text: row.sizeText
                size: 13
                color: UI.Style.inkMuted
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: 80
                horizontalAlignment: Text.AlignRight
                text: row.packedText
                size: 13
                color: UI.Style.inkMuted
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: 140
                leftPadding: 24
                text: row.modifiedText
                size: 13
                color: UI.Style.inkMuted
                elide: Text.ElideRight
            }
            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: 100
                text: row.typeText
                size: 13
                color: UI.Style.inkMuted
                elide: Text.ElideRight
            }
        }
    }
}
