pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.Terminal
import WildOS.UI as UI

// Keyboard shortcuts: every key Terminal itself answers to, the same as
// Windows Terminal's where it has one.
PageScroll {
    id: page

    readonly property var groups: [
        { title: qsTr("Tabs"), keys: [
            [qsTr("New tab"), "Ctrl+Shift+T"],
            [qsTr("New tab with profile 1 to 9"), "Ctrl+Shift+1 to 9"],
            [qsTr("Duplicate tab"), "Ctrl+Shift+D"],
            [qsTr("Close tab or pane"), "Ctrl+Shift+W"],
            [qsTr("Next tab"), "Ctrl+Tab"],
            [qsTr("Previous tab"), "Ctrl+Shift+Tab"],
            [qsTr("Go to tab 1 to 8, or the last tab"), "Ctrl+Alt+1 to 9"],
            [qsTr("Open the new tab menu"), "Ctrl+Shift+Space"]
        ] },
        { title: qsTr("Panes"), keys: [
            [qsTr("Split right"), "Alt+Shift+="],
            [qsTr("Split down"), "Alt+Shift+-"],
            [qsTr("Move to the pane beside"), "Alt+Arrows"]
        ] },
        { title: qsTr("Text"), keys: [
            [qsTr("Copy"), Config.ctrlCopyPaste ? "Ctrl+Shift+C or Ctrl+C" : "Ctrl+Shift+C"],
            [qsTr("Paste"), Config.ctrlCopyPaste ? "Ctrl+Shift+V or Ctrl+V" : "Ctrl+Shift+V"],
            [qsTr("Make text bigger"), "Ctrl+="],
            [qsTr("Make text smaller"), "Ctrl+-"],
            [qsTr("Reset text size"), "Ctrl+0"],
            [qsTr("Open a link"), "Ctrl+click"]
        ] },
        { title: qsTr("Window"), keys: [
            [qsTr("New window"), "Ctrl+Shift+N"],
            [qsTr("Settings"), "Ctrl+,"],
            [qsTr("Full screen"), "F11"]
        ] }
    ]

    // The shortcuts above are split into keys, so only the shown key names
    // are translated.
    function keyName(key) {
        switch (key) {
        case "Shift":
            return qsTr("Shift", "keyboard key");
        case "Space":
            return qsTr("Space", "keyboard key");
        case "Arrows":
            return qsTr("Arrows", "the arrow keys");
        case "click":
            return qsTr("click", "a mouse click");
        default:
            return key;
        }
    }

    Repeater {
        model: page.groups

        ColumnLayout {
            id: group
            required property var modelData
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: group.modelData.title
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: group.modelData.keys

                        UI.SettingRow {
                            id: row
                            required property var modelData
                            title: modelData[0]

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Repeater {
                                    model: row.modelData[1].split(" ")

                                    Item {
                                        id: word
                                        required property string modelData
                                        readonly property bool joiner: modelData === "or" || modelData === "to"
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: joiner ? joinText.implicitWidth : keys.implicitWidth
                                        height: Math.max(26, UI.Style.px(12) + 10)

                                        UI.Label {
                                            id: joinText
                                            visible: word.joiner
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: word.modelData === "or" ? qsTr("or", "between two shortcuts") : qsTr("to", "a range of keys like 1 to 9")
                                            size: 12
                                            color: UI.Style.inkMuted
                                        }

                                        Row {
                                            id: keys
                                            visible: !word.joiner
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 4

                                            Repeater {
                                                model: word.modelData.split("+")

                                                Rectangle {
                                                    id: cap
                                                    required property string modelData
                                                    width: Math.max(height, capText.implicitWidth + 16)
                                                    height: Math.max(26, UI.Style.px(12) + 10)
                                                    radius: UI.Style.radiusXs
                                                    color: UI.Style.tone(0.07)
                                                    border.width: 1
                                                    border.color: UI.Style.borderMenu

                                                    UI.Label {
                                                        id: capText
                                                        anchors.centerIn: parent
                                                        text: page.keyName(cap.modelData)
                                                        size: 12
                                                        font.weight: UI.Style.weightMedium
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
