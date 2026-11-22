import QtQuick
import WildOS.Files
import WildOS.UI as UI

// The 30px status bar: how many items, how many selected and their size,
// and the details and icons switches.
Item {
    id: status

    required property var folder

    height: Metrics.statusHeight
    // This computer lists its six Home folders above the drives the model
    // holds, and counts both, as Windows does.
    readonly property int itemCount: folder ? folder.count + (folder.url.toString() === "wildos:computer" ? 6 : 0) : 0

    // one is the whole phrase for 1; many is a qsTr("%Ln items", "", n) numerus
    // string, so languages with more than two plural forms get theirs.
    function plural(n, one, many) {
        return n === 1 ? one : many;
    }

    Rectangle {
        width: parent.width
        height: 1
        color: UI.Style.divider
    }

    Row {
        x: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 18

        UI.Label {
            text: status.folder ? status.plural(status.itemCount, qsTr("1 item"), qsTr("%Ln items", "", status.itemCount)) : ""
            font.pixelSize: UI.Style.px(12)
            color: UI.Style.inkStatus
        }
        UI.Label {
            visible: status.folder && status.folder.selectedCount > 0
            text: status.folder ? status.plural(status.folder.selectedCount, qsTr("1 item selected"), qsTr("%Ln items selected", "", status.folder.selectedCount)) : ""
            font.pixelSize: UI.Style.px(12)
            color: UI.Style.inkStatus
        }
        UI.Label {
            visible: status.folder && status.folder.selectedCount > 0 && status.folder.selectionSize.length > 0
            text: status.folder ? status.folder.selectionSize : ""
            font.pixelSize: UI.Style.px(12)
            color: UI.Style.inkStatus
        }
        UI.Label {
            visible: status.folder && status.folder.searching
            text: qsTr("Searching folders inside…")
            font.pixelSize: UI.Style.px(12)
            color: UI.Style.inkStatus
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        UI.IconButton {
            width: 26
            height: 24
            radius: 5
            enabled: status.folder && status.folder.kind !== "computer"
            iconName: "list"
            iconSize: 14
            tip: qsTr("Details")
            checked: status.folder && status.folder.viewMode === "details"
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: status.folder.viewMode = "details"
        }
        UI.IconButton {
            width: 26
            height: 24
            radius: 5
            enabled: status.folder && status.folder.kind !== "computer"
            iconName: "grid"
            iconSize: 14
            tip: qsTr("Icons")
            checked: status.folder && ["xlarge", "large", "icons"].indexOf(status.folder.viewMode) >= 0
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: status.folder.viewMode = "icons"
        }
    }
}
