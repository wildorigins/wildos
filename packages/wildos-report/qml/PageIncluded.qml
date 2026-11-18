import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WildOS.Report
import WildOS.UI as UI

// Step 2, What's included (ReportReview.dc): what the report holds, each
// with a tick and View, the count of personal details removed with their
// kinds, and on the right a preview of exactly what will leave the PC.
Item {
    id: page

    signal learnMore()

    // A line of the preview as rich text, with each placeholder the
    // redactor put in picked out in green.
    function marked(text) {
        const escaped = text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return escaped.replace(/\[(user|name|pc|(?:account|wifi|ip|mac|serial|id|email|file|folder|usb|word)-\d+)\]/g,
            "<span style=\"color:" + Look.green + "; background-color:" + Look.tokenFill + "; font-weight:500\">[$1]</span>");
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Flickable {
            id: list
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredWidth: 1.15
            contentHeight: left.implicitHeight + 48
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: UI.ScrollBar {}

            ColumnLayout {
                id: left
                x: 28
                y: 24
                width: list.width - 28 - 22
                spacing: 14

                UI.Label {
                    text: qsTr("What's included")
                    size: 24
                    font.weight: UI.Style.weightBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Untick anything you'd rather not share. The preview shows exactly what will leave this PC.")
                    size: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                    color: UI.Style.inkMuted
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Repeater {
                        model: Report.items

                        delegate: ItemRow {
                            required property var modelData
                            Layout.fillWidth: true
                            title: modelData.title
                            subtitle: modelData.subtitle
                            iconName: modelData.icon
                            included: modelData.included
                            viewing: Report.viewing === modelData.id
                            onToggled: on => Report.setIncluded(modelData.id, on)
                            onViewClicked: Report.view(modelData.id)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: removedColumn.implicitHeight + 28
                    radius: UI.Style.radiusPanel
                    color: Look.greenFill
                    border.width: 1
                    border.color: Look.greenEdge

                    ColumnLayout {
                        id: removedColumn
                        x: 16
                        y: 14
                        width: parent.width - 32
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ReportIcon {
                                name: "shield-check"
                                size: 18
                                color: Look.green
                            }
                            UI.Label {
                                Layout.fillWidth: true
                                text: Report.busy && Report.removed === 0 ? qsTr("Removing personal details")
                                    : Report.removed === 1 ? qsTr("1 personal detail removed")
                                    : qsTr("%1 personal details removed").arg(Report.removed)
                                size: 13.5
                                font.weight: UI.Style.weightSemiBold
                            }
                            UI.LinkButton {
                                id: addWordLink
                                text: qsTr("Add a word to remove")
                                size: 13
                                onClicked: {
                                    wordField.text = "";
                                    wordPopup.open();
                                    wordField.forceActiveFocus();
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: kinds.count > 0 || words.count > 0

                            Repeater {
                                id: kinds
                                model: Report.removedKinds

                                delegate: Rectangle {
                                    id: chip
                                    required property var modelData
                                    width: chipRow.implicitWidth + 20
                                    height: 24
                                    radius: 12
                                    color: Look.chip
                                    border.width: 1
                                    border.color: Look.hairline

                                    Row {
                                        id: chipRow
                                        anchors.centerIn: parent
                                        spacing: 6

                                        ReportIcon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "eye-off"
                                            size: 12
                                            strokeWidth: 2
                                            color: UI.Style.inkMuted
                                        }
                                        UI.Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: chip.modelData.label
                                            size: 12
                                        }
                                    }

                                    HoverHandler {
                                        id: chipHover
                                    }
                                    UI.Tooltip {
                                        visible: chipHover.hovered
                                        text: chip.modelData.count === 1 ? qsTr("1 removed") : qsTr("%1 removed").arg(chip.modelData.count)
                                    }
                                }
                            }

                            Repeater {
                                id: words
                                model: Report.customWords

                                delegate: Rectangle {
                                    id: word
                                    required property string modelData
                                    width: wordRow.implicitWidth + 16
                                    height: 24
                                    radius: 12
                                    color: Look.chip
                                    border.width: 1
                                    border.color: Look.hairline

                                    Row {
                                        id: wordRow
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: 10
                                        spacing: 6

                                        UI.Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("“%1”").arg(word.modelData)
                                            size: 12
                                        }
                                        Item {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 14
                                            height: 14

                                            ReportIcon {
                                                anchors.centerIn: parent
                                                name: "x"
                                                size: 11
                                                strokeWidth: 2.2
                                                color: removeHover.hovered ? UI.Style.ink : UI.Style.inkMuted
                                            }
                                            HoverHandler {
                                                id: removeHover
                                                cursorShape: Qt.PointingHandCursor
                                            }
                                            TapHandler {
                                                onTapped: Report.removeWord(word.modelData)
                                            }
                                            Accessible.role: Accessible.Button
                                            Accessible.name: qsTr("Stop removing %1").arg(word.modelData)
                                        }
                                    }
                                }
                            }
                        }

                        UI.LinkButton {
                            text: qsTr("Learn more about what's removed")
                            size: 13
                            onClicked: page.learnMore()
                        }
                    }

                    Popup {
                        id: wordPopup
                        x: parent.width - width
                        y: 44
                        width: 320
                        padding: 14
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                        background: Rectangle {
                            radius: 12
                            color: UI.Style.glassMenu
                            border.width: 1
                            border.color: UI.Style.borderMenu
                        }

                        contentItem: ColumnLayout {
                            spacing: 10

                            UI.Label {
                                Layout.fillWidth: true
                                text: qsTr("A word or name to take out wherever it appears, such as a company or a project.")
                                size: 12.5
                                wrapMode: Text.WordWrap
                                color: UI.Style.inkMuted
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                UI.TextField {
                                    id: wordField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Word to remove")
                                    onAccepted: addButton.clicked()
                                }
                                ActionButton {
                                    id: addButton
                                    primary: true
                                    text: qsTr("Remove it")
                                    enabled: wordField.text.trim().length >= 2
                                    onClicked: {
                                        Report.addWord(wordField.text);
                                        wordPopup.close();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            color: Look.pane

            Rectangle {
                width: 1
                height: parent.height
                color: Look.hairline
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 1
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    Layout.leftMargin: 16
                    Layout.rightMargin: 8
                    spacing: 10

                    ReportIcon {
                        name: "eye"
                        size: 16
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: Report.previewTitle
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.IconButton {
                        width: 30
                        height: 30
                        iconName: "copy"
                        iconSize: 15
                        iconColor: UI.Style.inkMuted
                        tip: qsTr("Copy")
                        enabled: !Report.busy
                        onClicked: Report.copyPreview()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Look.hairline
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: preview
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 10
                        topMargin: 14
                        bottomMargin: 14
                        visible: !Report.busy
                        clip: true
                        spacing: 9
                        boundsBehavior: Flickable.StopAtBounds
                        model: Report.preview
                        reuseItems: true
                        ScrollBar.vertical: UI.ScrollBar {}

                        delegate: Item {
                            id: line
                            required property string time
                            required property string source
                            required property string text
                            required property bool log

                            width: preview.width - 6
                            height: Math.max(message.implicitHeight, 15)

                            Text {
                                id: timeText
                                visible: line.log
                                width: line.log ? implicitWidth : 0
                                text: line.time
                                color: UI.Style.inkSubtle
                                font.family: Look.mono
                                font.pixelSize: UI.Style.px(11.5)
                            }
                            Text {
                                id: sourceText
                                visible: line.log
                                x: timeText.width + 10
                                width: line.log ? 64 : 0
                                text: line.source
                                elide: Text.ElideRight
                                color: Look.source
                                font.family: Look.mono
                                font.pixelSize: UI.Style.px(11.5)
                            }
                            Text {
                                id: message
                                x: line.log ? sourceText.x + sourceText.width + 10 : 0
                                width: line.width - x
                                text: page.marked(line.text)
                                textFormat: Text.RichText
                                wrapMode: Text.WrapAnywhere
                                color: UI.Style.ink
                                font.family: Look.mono
                                font.pixelSize: UI.Style.px(11.5)
                                lineHeight: 1.15
                            }
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        visible: Report.busy
                        spacing: 12

                        UI.Spinner {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: 24
                        }
                        UI.Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("Gathering the details and removing personal ones")
                            size: 12.5
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }
        }
    }
}
