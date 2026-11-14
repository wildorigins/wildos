import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Report
import WildOS.UI as UI

// Report a problem (ReportDescribe.dc, ReportReview.dc, ReportSend.dc): the
// title bar, the steps on the left, the step itself and a footer with the
// report's state and the buttons.
UI.AppWindow {
    id: window

    property int step: 0
    property int reached: 0
    readonly property string helpUrl: "https://help.wildos.io/troubleshooting/report-a-problem/"

    width: 1204
    height: 760
    minimumWidth: 980
    minimumHeight: 640
    deep: true
    title: qsTr("Report a problem")

    function go(to) {
        if (to === 1 && step === 0) {
            Report.prepare();
        }
        if (to === 2) {
            Report.suggestTitle();
        }
        step = to;
        reached = Math.max(reached, to);
    }

    Connections {
        target: Report
        // Hidden rather than minimised: a Wayland window can't bring
        // itself back from minimised, but it can show itself again.
        function onHideForCapture() {
            window.hide();
        }
        function onCaptureDone() {
            window.show();
            window.raise();
            window.requestActivate();
        }
        function onRaiseRequested() {
            window.raise();
            window.requestActivate();
        }
        function onMessage(text) {
            toast.show(text);
        }
    }

    Component.onCompleted: what.focusDescription()
    onClosing: Qt.quit()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            frame: window.frame
            showTitle: false
            buttons.buttonHeight: 44

            // The title bar puts its content 16px in; the board's icon is 14.
            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/WildOS/Report/data/wildos-report.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Report a problem")
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StepNav {
                Layout.preferredWidth: 236
                Layout.fillHeight: true
                current: window.step
                reached: window.reached
                onStepClicked: index => window.go(index)
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.step

                    PageWhat {
                        id: what
                    }
                    PageIncluded {
                        onLearnMore: Qt.openUrlExternally(window.helpUrl)
                    }
                    PageSend {
                        id: send
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    color: Look.footer

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Look.hairline
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 22
                        anchors.rightMargin: 22
                        spacing: 8

                        UI.Label {
                            Layout.fillWidth: true
                            size: 12.5
                            color: UI.Style.inkMuted
                            text: window.step === 0 ? Report.crashNote
                                : window.step === 1 ? Report.sizeText
                                : Report.savedText.length > 0 ? Report.savedText : Report.sizeText
                        }
                        UI.LinkButton {
                            visible: window.step === 2 && Report.savedPath.length > 0
                            text: qsTr("Show in folder")
                            size: 13
                            onClicked: Report.showSaved()
                        }
                        Item {
                            width: 6
                            height: 1
                        }
                        ActionButton {
                            text: window.step === 0 ? qsTr("Cancel") : qsTr("Back")
                            onClicked: window.step === 0 ? window.close() : window.go(window.step - 1)
                        }
                        ActionButton {
                            visible: window.step < 2
                            primary: true
                            text: qsTr("Next")
                            enabled: window.step === 0 ? Report.description.trim().length > 0 : !Report.busy
                            onClicked: window.go(window.step + 1)
                        }
                        ActionButton {
                            visible: window.step === 2
                            primary: true
                            iconName: send.method === "github" ? "external" : send.method === "save" ? "save" : "copy"
                            text: send.method === "github" ? qsTr("Open GitHub")
                                : send.method === "save" ? qsTr("Save report")
                                : qsTr("Copy description")
                            enabled: !Report.busy
                            onClicked: {
                                if (send.method === "github") {
                                    Report.openGitHub();
                                } else if (send.method === "save") {
                                    saveDialog.selectedFile = Report.downloadsFolder() + "/" + Report.suggestedName();
                                    saveDialog.open();
                                } else {
                                    Report.copyDescription();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save the report")
        fileMode: FileDialog.SaveFile
        currentFolder: Report.downloadsFolder()
        nameFilters: [qsTr("Zip files (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: Report.saveTo(selectedFile)
    }

    HandoffDialog {
        id: handoffDialog
        stage: Report.handoff
        zipName: Report.handoffZip
        issueNumber: Report.postedNumber
        watching: Report.watching
        onShowReport: Report.showSaved()
        onOpenAgain: Report.reopenGitHub()
        onViewIssue: Report.viewPosted()
        onCloseWindow: window.close()
        onClosed: Report.endHandoff()

        Connections {
            target: Report
            function onHandoffChanged() {
                if (Report.handoff.length > 0 && !handoffDialog.opened) {
                    handoffDialog.open();
                } else if (Report.handoff.length === 0 && handoffDialog.opened) {
                    handoffDialog.close();
                }
            }
        }
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 76
    }
}
