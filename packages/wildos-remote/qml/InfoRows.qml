import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The facts box of the identity questions (RemoteCheck.dc): label on the
// left, value on the right, hairlines between; a fingerprint gets a row of
// its own in monospace.
Rectangle {
    id: box

    // [{ label, value, mono, wide, colour }]
    property var rows: []

    implicitHeight: column.implicitHeight + 8
    radius: 12
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 4
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 0

        Repeater {
            model: box.rows
            Item {
                id: rowItem
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: modelData.wide ? wideColumn.implicitHeight + 22 : 37

                Rectangle {
                    visible: rowItem.index > 0
                    width: parent.width
                    height: 1
                    color: UI.Style.divider
                }
                UI.Label {
                    visible: !rowItem.modelData.wide
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.modelData.label
                    size: 14
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    visible: !rowItem.modelData.wide
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width * 0.62)
                    horizontalAlignment: Text.AlignRight
                    text: rowItem.modelData.value
                    size: 14
                    font.weight: UI.Style.weightSemiBold
                    font.family: rowItem.modelData.mono ? "JetBrains Mono" : UI.Style.fontFamily
                    color: rowItem.modelData.colour || UI.Style.ink
                }
                Column {
                    id: wideColumn
                    visible: rowItem.modelData.wide === true
                    y: 11
                    width: parent.width
                    spacing: 6
                    UI.Label {
                        text: rowItem.modelData.label
                        size: 14
                        color: UI.Style.inkMuted
                    }
                    TextEdit {
                        width: parent.width
                        text: rowItem.modelData.value
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: UI.Style.ink
                        selectionColor: UI.Style.textSelection
                        font.family: "JetBrains Mono"
                        font.pixelSize: UI.Style.px(12)
                        Keys.onPressed: event => {
                            if (valueMenu.isMenuKey(event)) {
                                valueMenu.openAtCaret();
                                event.accepted = true;
                            }
                        }

                        UI.TextMenu {
                            id: valueMenu
                        }
                    }
                }
            }
        }
    }
}
