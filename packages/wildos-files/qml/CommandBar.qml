import QtQuick
import WildOS.Files
import QtQuick.Templates as T
import WildOS.UI as UI

// The 48px command bar: New, the clipboard and file commands, Sort, View,
// More, the trust chip and the details pane switch.
Item {
    id: bar

    required property var folder
    required property var view
    readonly property bool inBin: folder && folder.kind === "bin"
    readonly property bool inComputer: folder && folder.kind === "computer"
    readonly property int selected: folder ? folder.selectedCount : 0

    // In a narrow window the buttons with words give them up rather than
    // run under one another: first Sort, View and Details (1), then New
    // and the Recycle Bin's own buttons (2). The word becomes the tooltip.
    // Each level remembers the width it ran out of room at, and comes back
    // once the bar is that wide again.
    property int compact: 0
    property var neededAt: [0, 0]
    readonly property real needed: left.x + left.width + 16 + detailsButton.width + 12

    function fit() {
        if (width <= 0) {
            return;
        }
        if (compact < 2 && needed > width) {
            const widths = neededAt.slice();
            widths[compact] = needed;
            neededAt = widths;
            compact += 1;
        } else if (compact > 0 && width >= neededAt[compact - 1]) {
            compact -= 1;
        }
    }

    height: Metrics.commandHeight
    // Fitting changes the very widths it is worked out from, so it runs
    // once they have settled rather than in the middle of the change.
    onWidthChanged: Qt.callLater(bar.fit)
    onNeededChanged: Qt.callLater(bar.fit)
    Component.onCompleted: Qt.callLater(bar.fit)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: UI.Style.borderSubtle
    }

    component CommandButton: UI.Button {
        id: commandButton
        // The button's word, and the level of squeeze at which it goes.
        property string label
        property int dropAt: 2
        readonly property bool wordless: bar.compact >= dropAt
        kind: "text"
        height: Metrics.buttonHeight
        text: wordless ? "" : label
        Accessible.name: label

        UI.Tooltip {
            text: commandButton.label
            visible: commandButton.wordless && commandButton.hovered && !commandButton.down
        }
    }

    component CommandIcon: UI.IconButton {
        height: Metrics.buttonHeight
        iconSize: 17
        stroke: 1.8
    }

    component Separator: Rectangle {
        width: 13
        height: 22
        color: "transparent"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 22
            color: Metrics.separator
        }
    }

    Row {
        id: left
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        CommandButton {
            id: newButton
            visible: !bar.inBin
            iconName: "new"
            iconSize: 18
            iconStroke: 2
            iconColor: UI.Style.accent
            label: qsTr("New")
            dropDown: true
            enabled: bar.folder && bar.folder.writable
            onClicked: bar.view.openNewMenu(newButton)
        }

        CommandButton {
            visible: bar.inBin
            iconName: "bin"
            label: qsTr("Empty Recycle Bin")
            enabled: FileOps.binCount > 0
            onClicked: FileOps.emptyBin()
        }

        CommandButton {
            visible: bar.inBin
            iconName: "restore"
            label: bar.selected > 0 ? qsTr("Restore") : qsTr("Restore all")
            enabled: bar.folder && bar.folder.count > 0
            onClicked: bar.view.restore()
        }

        Separator {}

        CommandIcon {
            visible: !bar.inBin
            iconName: "cut"
            tip: qsTr("Cut (Ctrl+X)")
            enabled: bar.selected > 0 && bar.folder.writable && !bar.inComputer
            onClicked: bar.view.cut()
        }
        CommandIcon {
            visible: !bar.inBin
            iconName: "copy"
            tip: qsTr("Copy (Ctrl+C)")
            enabled: bar.selected > 0 && !bar.inComputer
            onClicked: bar.view.copy()
        }
        CommandIcon {
            visible: !bar.inBin
            iconName: "paste"
            tip: qsTr("Paste (Ctrl+V)")
            enabled: FileOps.canPaste && bar.folder && bar.folder.writable
            onClicked: bar.view.paste()
        }
        CommandIcon {
            visible: !bar.inBin
            iconName: "rename"
            tip: qsTr("Rename (F2)")
            enabled: bar.selected === 1 && bar.folder.writable && !bar.inComputer
            onClicked: bar.view.rename()
        }
        CommandIcon {
            id: shareButton
            visible: !bar.inBin
            iconName: "share"
            tip: qsTr("Share")
            enabled: bar.selected > 0 && !bar.inComputer && !bar.folder.selectionHasFolders()
            onClicked: bar.view.openShareMenu(shareButton)
        }
        CommandIcon {
            iconName: "delete"
            tip: bar.inBin ? qsTr("Delete for good") : qsTr("Delete (Delete)")
            enabled: bar.selected > 0 && !bar.inComputer
            onClicked: bar.view.remove(bar.inBin)
        }

        Separator {}

        CommandButton {
            id: sortButton
            iconName: "sort"
            enabled: !bar.inComputer
            label: qsTr("Sort")
            dropAt: 1
            dropDown: true
            onClicked: bar.view.openSortMenu(sortButton)
        }
        CommandButton {
            id: viewButton
            iconName: "view"
            label: qsTr("View")
            dropAt: 1
            dropDown: true
            onClicked: bar.view.openViewMenu(viewButton)
        }
        CommandIcon {
            id: moreButton
            iconName: "more"
            tip: qsTr("More options")
            onClicked: bar.view.openMoreMenu(moreButton)
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        // The trust chip: what this location is and who can open it. Hidden
        // when the window is too narrow for it and the commands.
        T.AbstractButton {
            id: chip
            visible: bar.folder && bar.folder.scope.length > 0 && left.width + chipRow.implicitWidth + 160 < bar.width
            anchors.verticalCenter: parent.verticalCenter
            width: chipRow.implicitWidth + 22
            height: 30
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: bar.folder ? bar.folder.scope : ""
            onClicked: bar.view.showScope(chip)

            background: Rectangle {
                radius: 15
                color: chip.hovered ? Metrics.chipHover : UI.Style.accentSoft
                border.width: 1
                border.color: UI.Style.accentBorder
                Behavior on color {
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }

                UI.FocusRing {
                    visible: chip.visualFocus
                    targetRadius: 15
                }
            }

            contentItem: Item {
                Row {
                    id: chipRow
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    UI.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "shield"
                        size: 14
                        strokeWidth: 2.2
                        color: UI.Style.accent
                    }

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: bar.folder ? bar.folder.scope : ""
                        size: 12
                        font.weight: UI.Style.weightMedium
                        color: Metrics.chipText
                    }
                }
            }
        }

        CommandButton {
            id: detailsButton
            iconName: "pane"
            label: qsTr("Details")
            dropAt: 1
            checked: Settings.detailsPane
            onClicked: Settings.detailsPane = !Settings.detailsPane
        }
    }
}
