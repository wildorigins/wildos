import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WildOS.Report
import WildOS.UI as UI

// Step 3, Send (ReportSend.dc): open an issue on GitHub, save the report or
// copy the description, and the issue's title.
Item {
    id: page

    property string method: "github"

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: UI.ScrollBar {}

        ColumnLayout {
            id: column
            x: 28
            y: 24
            width: flick.width - 56
            spacing: 14

            UI.Label {
                text: qsTr("How do you want to send it?")
                size: 24
                font.weight: UI.Style.weightBold
            }

            SendChoice {
                Layout.fillWidth: true
                iconName: "github"
                title: qsTr("Open an issue on GitHub")
                note: qsTr("Recommended")
                description: qsTr("Opens the Wild OS issue page with your title and description filled in. The report is saved to Downloads, ready to drag in. Needs a free GitHub account.")
                checked: page.method === "github"
                onClicked: page.method = "github"
            }
            SendChoice {
                Layout.fillWidth: true
                iconName: "save"
                title: qsTr("Save the report")
                description: qsTr("Save it anywhere to send your own way, for example on the forum or by email.")
                checked: page.method === "save"
                onClicked: page.method = "save"
            }
            SendChoice {
                Layout.fillWidth: true
                iconName: "copy"
                title: qsTr("Copy the description only")
                description: qsTr("Just your words and the Wild OS version. No logs or details about this PC.")
                checked: page.method === "copy"
                onClicked: page.method = "copy"
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 7
                visible: page.method === "github"

                FieldLabel {
                    text: qsTr("Title")
                }
                UI.TextField {
                    id: titleField
                    // Escape would otherwise wipe the suggested title.
                    escapeClears: false
                    Layout.fillWidth: true
                    implicitHeight: 32
                    leftPadding: 10
                    rightPadding: 10
                    text: Report.title
                    onTextEdited: Report.title = text
                    color: UI.Style.ink
                    font.family: UI.Style.fontFamily
                    font.pixelSize: UI.Style.px(13)
                    font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                    selectionColor: UI.Style.textSelection
                    selectedTextColor: UI.Style.ink
                    placeholderText: qsTr("A short summary of the problem")
                    placeholderTextColor: UI.Style.inkPlaceholder
                    Accessible.name: qsTr("Title")
                    background: Rectangle {
                        radius: 6
                        color: Look.field
                        border.width: 1
                        border.color: Look.fieldEdge

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            radius: 1
                            visible: titleField.activeFocus
                            color: UI.Style.accent
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ReportIcon {
                        name: "info"
                        size: 14
                        color: UI.Style.inkSubtle
                    }
                    UI.Label {
                        text: qsTr("Look for an existing issue first. If someone has the same problem, add your report to theirs.")
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                    UI.LinkButton {
                        text: qsTr("Search issues")
                        size: 13
                        onClicked: Report.searchIssues()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
