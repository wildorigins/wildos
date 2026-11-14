import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WildOS.Report
import WildOS.UI as UI

// Step 1, What happened (ReportDescribe.dc): which part, when, what went
// wrong, whether it happens again, and screenshots: a new one taken with
// Capture, or pictures already on the PC.
Item {
    id: page

    function focusDescription() {
        description.forceActiveFocus();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        spacing: 16

        UI.Label {
            text: qsTr("What happened?")
            size: 24
            font.weight: UI.Style.weightBold
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 7

                FieldLabel {
                    text: qsTr("Which part of Wild OS?")
                }
                UI.ComboBox {
                    id: partBox
                    Layout.fillWidth: true
                    implicitHeight: 32
                    model: Report.parts
                    currentIndex: Report.parts.findIndex(p => p.value === Report.part)
                    onActivated: Report.part = currentValue
                    leftPadding: 10
                    background: Rectangle {
                        radius: 6
                        color: partBox.hovered ? UI.Style.tone(0.07) : Look.field
                        border.width: 1
                        border.color: partBox.popup.visible || partBox.visualFocus ? UI.Style.accent : Look.fieldEdge
                    }
                    Accessible.name: qsTr("Which part of Wild OS?")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 7

                FieldLabel {
                    text: qsTr("When did it happen?")
                }
                Segmented {
                    Layout.fillWidth: true
                    value: Report.when
                    options: [
                        { value: "now", text: qsTr("Just now") },
                        { value: "today", text: qsTr("Earlier today") },
                        { value: "pick", text: qsTr("Pick a time") }
                    ]
                    onChosen: v => Report.when = v
                }
                RowLayout {
                    visible: Report.when === "pick"
                    spacing: 8

                    Segmented {
                        value: Report.pickDay === 0 ? "today" : "yesterday"
                        options: [
                            { value: "today", text: qsTr("Today") },
                            { value: "yesterday", text: qsTr("Yesterday") }
                        ]
                        onChosen: v => Report.pickDay = v === "today" ? 0 : 1
                    }
                    UI.Label {
                        text: qsTr("at")
                        color: UI.Style.inkMuted
                    }
                    UI.TextField {
                        id: timeField
                        escapeClears: false
                        implicitWidth: 72
                        implicitHeight: 32
                        text: Report.pickTime
                        inputMask: "99:99"
                        horizontalAlignment: Text.AlignHCenter
                        color: UI.Style.ink
                        font.family: UI.Style.fontFamily
                        font.pixelSize: UI.Style.px(13)
                        selectionColor: UI.Style.textSelection
                        selectedTextColor: UI.Style.ink
                        onEditingFinished: Report.pickTime = text
                        Accessible.name: qsTr("Time, in hours and minutes")
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
                                visible: timeField.activeFocus
                                color: UI.Style.accent
                            }
                        }
                    }
                    UI.Label {
                        text: qsTr("Logs from %1").arg(Report.windowText)
                        size: 12
                        color: UI.Style.inkSubtle
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 7

            FieldLabel {
                text: qsTr("What went wrong?")
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                radius: 6
                color: Look.field
                border.width: 1
                border.color: Look.fieldEdge
                clip: true

                ScrollView {
                    id: scroller
                    anchors.fill: parent
                    ScrollBar.vertical: UI.ScrollBar {}

                    UI.TextArea {
                        id: description
                        text: Report.description
                        onTextChanged: Report.description = text
                        wrapMode: TextEdit.Wrap
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10
                        color: UI.Style.ink
                        font.family: UI.Style.fontFamily
                        font.pixelSize: UI.Style.px(13)
                        font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                        selectionColor: UI.Style.textSelection
                        selectedTextColor: UI.Style.ink
                        background: null
                        Accessible.name: qsTr("What went wrong?")
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    visible: description.activeFocus
                    color: UI.Style.accent
                }
            }
            UI.Label {
                text: qsTr("What you did, what you expected, and what happened instead.")
                size: 12
                color: UI.Style.inkSubtle
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 7

            FieldLabel {
                text: qsTr("Can you make it happen again?")
            }
            Segmented {
                Layout.fillWidth: true
                value: Report.repeat
                options: [
                    { value: "always", text: qsTr("Every time") },
                    { value: "sometimes", text: qsTr("Sometimes") },
                    { value: "once", text: qsTr("Only once") }
                ]
                onChosen: v => Report.repeat = v
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 7

            FieldLabel {
                text: qsTr("Screenshots")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: Report.screenshots

                    delegate: Row {
                        id: shot
                        required property var modelData
                        required property int index
                        spacing: 10

                        Rectangle {
                            width: 92
                            height: 58
                            radius: 6
                            color: UI.Style.tone(0.08)
                            border.width: 1
                            border.color: Look.fieldEdge
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: shot.modelData.url
                                sourceSize.width: 184
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            UI.Label {
                                text: shot.modelData.label
                                size: 12.5
                            }
                            UI.LinkButton {
                                text: qsTr("Remove")
                                size: 12
                                onClicked: Report.removeScreenshot(shot.index)
                            }
                        }
                        Item {
                            width: 10
                            height: 1
                        }
                    }
                }

                Item {
                    width: addButtons.width
                    height: Report.screenshots.length > 0 ? 58 : addButtons.height

                    Row {
                        id: addButtons
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        ActionButton {
                            iconName: "image-plus"
                            text: Report.capturing ? qsTr("Waiting for Capture") : qsTr("Take a screenshot")
                            enabled: !Report.capturing
                            onClicked: Report.takeScreenshot()
                        }
                        ActionButton {
                            iconName: "folder"
                            text: qsTr("Choose a picture")
                            enabled: !Report.capturing
                            onClicked: Report.chooseScreenshotFiles(page.Window.window)
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
