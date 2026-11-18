import QtQuick
import QtQuick.Layouts
import WildOS.Terminal
import WildOS.UI as UI

// Interaction: the clipboard, closing and how much history a tab keeps.
PageScroll {
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Copy and paste")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                ToggleRow {
                    iconName: "copy"
                    title: qsTr("Ctrl+C copies and Ctrl+V pastes")
                    description: qsTr("As in Windows. With nothing selected, Ctrl+C still stops the running command.")
                    value: Config.ctrlCopyPaste
                    onSwitched: v => Config.ctrlCopyPaste = v
                }

                ToggleRow {
                    iconName: "copy"
                    title: qsTr("Copy text as soon as it is selected")
                    value: Config.copyOnSelect
                    onSwitched: v => Config.copyOnSelect = v
                }

                ToggleRow {
                    iconName: "paste"
                    title: qsTr("Paste with a right-click")
                    description: qsTr("Shift and right-click then opens the menu instead.")
                    value: Config.pasteOnRightClick
                    onSwitched: v => Config.pasteOnRightClick = v
                }

                ToggleRow {
                    iconName: "warning"
                    title: qsTr("Check before pasting more than one line")
                    description: qsTr("Each line of pasted text can run as a command.")
                    value: Config.warnMultilinePaste
                    onSwitched: v => Config.warnMultilinePaste = v
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Tabs and history")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                ToggleRow {
                    iconName: "close"
                    title: qsTr("Ask before closing a window with several tabs")
                    value: Config.confirmCloseTabs
                    onSwitched: v => Config.confirmCloseTabs = v
                }

                UI.SettingRow {
                    iconName: "list"
                    title: qsTr("Lines to keep for scrolling back")
                    description: qsTr("Older output is dropped once a tab has this many lines.")

                    Choice {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Lines to keep for scrolling back")
                        options: [
                            { value: 1000, text: "1,000" },
                            { value: 10000, text: "10,000" },
                            { value: 50000, text: "50,000" },
                            { value: 100000, text: "100,000" },
                            { value: -1, text: qsTr("All of them") }
                        ]
                        value: Config.scrollback
                        onChosen: v => Config.scrollback = v
                    }
                }
            }
        }
    }
}
