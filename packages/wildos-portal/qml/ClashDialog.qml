pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The name clash of DialogCopy.dc (8.2): "Budget 2026" is already in
// Documents, then three plain choices with the dates and sizes that tell
// the files apart, a box to answer the same for the rest of the copy, and
// Cancel and Continue. 580 wide, 18px heading, cards with a 32px icon.
UI.AppWindow {
    id: win

    required property var clash
    property string choice: clash.canReplace ? "replace" : clash.canKeepBoth ? "both" : "skip"

    title: clash.windowTitle
    deep: true
    width: 580
    height: content.implicitHeight
    minimumWidth: 580
    maximumWidth: 580
    minimumHeight: content.implicitHeight
    maximumHeight: content.implicitHeight

    onClosing: clash.answer("cancel", false)

    function go() {
        clash.answer(choice, every.checked);
    }

    Shortcut {
        sequence: "Escape"
        onActivated: win.clash.answer("cancel", false)
    }

    // Enter means Continue wherever the focus is, as in a Windows dialog,
    // except on Cancel itself.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: !cancelButton.activeFocus
        onActivated: win.go()
    }

    component Choice: UI.ChoiceCard {
        id: card

        required property string value
        property string iconName: win.clash.iconName

        Layout.fillWidth: true
        topPadding: 14
        bottomPadding: 14
        leftPadding: 16
        rightPadding: 16
        radius: UI.Style.radiusMd
        checked: win.choice === value
        onClicked: win.choice = value
        onDoubleClicked: {
            win.choice = value;
            win.go();
        }
        Component.onCompleted: if (checked) forceActiveFocus()

        contentItem: RowLayout {
            spacing: 14

            UI.ThemeIcon {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                name: card.iconName
                fallback: win.clash.folders ? "folder" : "text-x-generic"
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                UI.Label {
                    Layout.fillWidth: true
                    text: card.title
                    size: 14
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: card.description
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    lineHeight: 1.45
                }
            }
        }
    }

    ColumnLayout {
        id: content
        width: parent.width
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: win.frame
            showTitle: false
            buttons.showMinimise: false
            buttons.showMaximise: false

            Row {
                x: -2
                height: parent.height
                spacing: 10

                UI.ThemeIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    name: "folder"
                }

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.clash.windowTitle
                    font.weight: UI.Style.weightMedium
                    color: win.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: UI.Style.divider
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 18
            Layout.bottomMargin: 18
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: win.clash.heading
                size: 18
                font.weight: UI.Style.weightSemiBold
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            UI.Label {
                Layout.fillWidth: true
                text: win.clash.summary
                color: UI.Style.inkStatus
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            Choice {
                visible: win.clash.canReplace
                value: "replace"
                title: win.clash.replaceTitle
                description: win.clash.replaceText
            }

            Choice {
                visible: win.clash.canSkip
                value: "skip"
                title: win.clash.skipTitle
                description: win.clash.skipText
            }

            Choice {
                visible: win.clash.canKeepBoth
                value: "both"
                title: qsTr("Keep both")
                description: win.clash.keepBothText
            }

            UI.CheckBox {
                id: every
                visible: win.clash.multiple
                text: qsTr("Do this for every clash in this copy")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 59
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            UI.Button {
                id: cancelButton
                text: qsTr("Cancel")
                onClicked: win.clash.answer("cancel", false)
            }

            UI.Button {
                kind: "primary"
                text: qsTr("Continue")
                onClicked: win.go()
            }
        }
    }
}
