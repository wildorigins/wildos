pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtCore
import WildOS.Files
import WildOS.UI as UI

// This computer, as ThisComputer.dc.html: the Home folders as a grid of
// tiles, then the drives as cards with how full each one is. The drives are
// the folder model's rows, so selection, the context menu, Properties and
// the keyboard work as in any other folder; a click picks, a double click
// or Enter opens, as in File Explorer.
FocusScope {
    id: computer

    required property var folder
    required property var view

    readonly property var folders: [
        { name: qsTr("Desktop"), location: StandardPaths.DesktopLocation },
        { name: qsTr("Documents"), location: StandardPaths.DocumentsLocation },
        { name: qsTr("Downloads"), location: StandardPaths.DownloadLocation },
        { name: qsTr("Music"), location: StandardPaths.MusicLocation },
        { name: qsTr("Pictures"), location: StandardPaths.PicturesLocation },
        { name: qsTr("Videos"), location: StandardPaths.MoviesLocation }
    ]

    // Up and Down move a row of drive cards, Left and Right one card.
    function columns() {
        return 2;
    }

    function across() {
        return 1;
    }

    function pageRows() {
        return 4;
    }

    function itemRect(row) {
        const card = drives.itemAt(row);
        return card ? card.mapToItem(computer, 0, 0, card.width, card.height) : Qt.rect(0, 0, 0, 0);
    }

    function startRename(row) {
    }

    Keys.onPressed: event => view.handleKey(event, computer)

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + 44
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: UI.ScrollBar {}

        // A click on the empty space clears the selection, as elsewhere.
        MouseArea {
            id: space
            width: flick.width
            height: Math.max(flick.height, flick.contentHeight)
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                computer.view.keyboardUsed = false;
                computer.forceActiveFocus();
                computer.folder.clearSelection();
            }
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton) {
                    computer.view.openFolderMenu(space, mouse.x, mouse.y);
                }
            }
        }

        Column {
            id: column
            x: 26
            y: 22
            width: flick.width - 52
            spacing: 14

            UI.Label {
                text: qsTr("Folders")
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkStatus
                Accessible.role: Accessible.Heading
            }

            Grid {
                id: folderGrid
                width: parent.width
                columns: 3
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: computer.folders

                    delegate: T.AbstractButton {
                        id: tile

                        required property var modelData
                        readonly property url url: StandardPaths.writableLocation(modelData.location)

                        width: (folderGrid.width - 2 * folderGrid.columnSpacing) / 3
                        height: Math.max(56, label.implicitHeight + 24)
                        hoverEnabled: true
                        focusPolicy: Qt.TabFocus
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.name
                        onDoubleClicked: computer.folder.openUrl(url)
                        Keys.onReturnPressed: computer.folder.openUrl(url)
                        Keys.onEnterPressed: computer.folder.openUrl(url)

                        background: Rectangle {
                            radius: 10
                            color: tile.down ? UI.Style.surfacePressed : tile.hovered ? UI.Style.surfaceHover : UI.Style.surfaceRest
                            Behavior on color {
                                ColorAnimation { duration: UI.Style.hoverDuration }
                            }

                            UI.FocusRing {
                                visible: tile.visualFocus
                                targetRadius: 10
                            }
                        }

                        contentItem: Item {
                            UI.ThemeIcon {
                                id: icon
                                x: 14
                                anchors.verticalCenter: parent.verticalCenter
                                width: 32
                                height: 32
                                name: "folder"
                            }

                            UI.Label {
                                id: label
                                anchors.left: icon.right
                                anchors.leftMargin: 12
                                anchors.right: parent.right
                                anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                text: tile.modelData.name
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: point => computer.view.openPlaceMenu(tile, point.position.x, point.position.y,
                                                                           { name: tile.modelData.name, url: tile.url, kind: "place", removable: false, udi: "" })
                        }

                        DropArea {
                            anchors.fill: parent
                            onEntered: drag => drag.accept(drag.proposedAction)
                            onDropped: drop => computer.view.dropUrls(drop, tile.url)
                        }
                    }
                }
            }

            UI.Label {
                topPadding: 8
                text: qsTr("Drives")
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkStatus
                Accessible.role: Accessible.Heading
            }

            Grid {
                id: driveGrid
                width: parent.width
                columns: 2
                columnSpacing: 12
                rowSpacing: 12

                Repeater {
                    id: drives
                    model: computer.folder

                    delegate: T.AbstractButton {
                        id: card

                        required property int index
                        required property string name
                        required property string iconName
                        required property string subtitle
                        required property string chip
                        required property real usage
                        required property bool selected
                        required property bool removable
                        required property url url

                        readonly property bool current: computer.folder.currentIndex === index && computer.activeFocus && computer.view.keyboardUsed

                        width: (driveGrid.width - driveGrid.columnSpacing) / 2
                        height: Math.max(78, texts.implicitHeight + 28)
                        hoverEnabled: true
                        focusPolicy: Qt.NoFocus
                        Accessible.role: Accessible.ListItem
                        Accessible.name: name + (chip.length > 0 ? ", " + chip : "") + ", " + subtitle
                        Accessible.selected: selected

                        onPressed: {
                            computer.view.keyboardUsed = false;
                            computer.forceActiveFocus();
                        }
                        onClicked: computer.folder.select(index, 0)
                        onDoubleClicked: computer.folder.activate(index)

                        background: Rectangle {
                            radius: UI.Style.radiusMd
                            color: card.selected ? UI.Style.accentSelected : card.down ? UI.Style.surfacePressed : card.hovered ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                            border.width: 1
                            border.color: card.selected ? UI.Style.accentBorderStrong : UI.Style.borderSubtle
                            Behavior on color {
                                ColorAnimation { duration: UI.Style.hoverDuration }
                            }

                            UI.FocusRing {
                                visible: card.current
                                targetRadius: UI.Style.radiusMd
                            }
                        }

                        contentItem: Item {
                            UI.ThemeIcon {
                                id: driveIcon
                                x: 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 40
                                height: 40
                                name: card.iconName
                            }

                            Column {
                                id: texts
                                anchors.left: driveIcon.right
                                anchors.leftMargin: 14
                                anchors.right: eject.visible ? eject.left : parent.right
                                anchors.rightMargin: eject.visible ? 8 : 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Row {
                                    width: parent.width
                                    spacing: 8

                                    UI.Label {
                                        id: driveName
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.min(implicitWidth, parent.width - (chipPill.visible ? chipPill.width + 8 : 0))
                                        text: card.name
                                        size: 14
                                        font.weight: UI.Style.weightSemiBold
                                    }

                                    Rectangle {
                                        id: chipPill
                                        visible: card.chip.length > 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: chipText.implicitWidth + 16
                                        height: Math.max(20, chipText.implicitHeight + 4)
                                        radius: height / 2
                                        color: UI.Style.surfaceTile

                                        UI.Label {
                                            id: chipText
                                            anchors.centerIn: parent
                                            text: card.chip
                                            size: 11
                                            color: UI.Style.inkAlpha(0.85, 0.80)
                                        }
                                    }
                                }

                                UI.ProgressBar {
                                    visible: card.usage >= 0
                                    width: parent.width
                                    barHeight: 6
                                    value: Math.max(0, Math.min(1, card.usage))
                                    warning: card.usage > 0.9
                                }

                                UI.Label {
                                    width: parent.width
                                    text: card.subtitle
                                    size: 12
                                    wrapMode: Text.Wrap
                                    color: UI.Style.inkMuted
                                }
                            }

                            UI.IconButton {
                                id: eject
                                visible: card.removable && card.url.toString().length > 0 && (card.hovered || hovered || card.selected)
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "eject"
                                tip: qsTr("Eject %1").arg(card.name)
                                focusPolicy: Qt.NoFocus
                                onClicked: FileOps.ejectDrive(computer.folder.udiAt(card.index))
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: point => {
                                if (!card.selected) {
                                    computer.folder.select(card.index, 0);
                                }
                                computer.view.openItemMenu(card, point.position.x, point.position.y);
                            }
                        }

                        DropArea {
                            anchors.fill: parent
                            enabled: card.url.toString().length > 0
                            onEntered: drag => drag.accept(drag.proposedAction)
                            onDropped: drop => computer.view.dropUrls(drop, card.url)
                        }
                    }
                }
            }
        }
    }
}
