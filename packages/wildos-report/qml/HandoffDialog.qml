import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// What happens after Open GitHub. The issue is finished in the browser,
// where this app can't see; it only learns it was posted by finding it on
// GitHub, so until then it says what to do there rather than claiming
// anything was sent. `stage` is Report.handoff: opening, waiting, posted.
UI.Dialog {
    id: dialog

    property string stage
    property string zipName
    property int issueNumber
    property bool watching

    signal showReport()
    signal openAgain()
    signal viewIssue()
    signal closeWindow()

    readonly property bool posted: stage === "posted"

    title: stage === "opening" ? qsTr("Opening GitHub in your browser")
        : posted ? qsTr("Issue submitted")
        : qsTr("Finish in your browser")
    iconName: posted ? "check" : ""
    text: stage === "opening" ? qsTr("Saving the report to Downloads, then opening a new issue with your title and description filled in.")
        : posted ? qsTr("Thank you. Your report is issue #%1 on GitHub. It's safe to close this window.").arg(issueNumber)
        : qsTr("Your title and description are filled in on the new issue page. To send it:")
    closePolicy: stage === "opening" ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape

    RowLayout {
        visible: dialog.stage === "opening"
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.bottomMargin: 8
        spacing: 10

        UI.Spinner {
            size: 18
        }
        UI.Label {
            Layout.fillWidth: true
            text: qsTr("This takes a few seconds.")
            color: UI.Style.inkMuted
        }
    }

    ColumnLayout {
        visible: dialog.stage === "waiting"
        Layout.fillWidth: true
        spacing: 8

        Repeater {
            model: [
                qsTr("Sign in to GitHub if it asks."),
                qsTr("Drag %1 from your Downloads folder into the description box.").arg(dialog.zipName),
                qsTr("Submit the issue.")
            ]

            delegate: RowLayout {
                id: stepRow

                required property string modelData
                required property int index

                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 20
                    implicitHeight: 20
                    radius: 10
                    color: UI.Style.accentAlpha(0.14, 0.10)

                    UI.Label {
                        anchors.centerIn: parent
                        text: stepRow.index + 1
                        size: 11.5
                        font.weight: UI.Style.weightSemiBold
                        color: UI.Style.accentText
                    }
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: stepRow.modelData
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    color: UI.Style.inkStatus
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 16

            UI.LinkButton {
                text: qsTr("Show the report in its folder")
                size: 13
                onClicked: dialog.showReport()
            }
            UI.LinkButton {
                text: qsTr("Open the issue page again")
                size: 13
                onClicked: dialog.openAgain()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 10

            UI.Spinner {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                visible: dialog.watching
                size: 14
            }
            UI.Label {
                Layout.fillWidth: true
                text: dialog.watching
                    ? qsTr("This window will say when your issue is up. Closing it now is safe too: everything is in your browser and in Downloads.")
                    : qsTr("Once you've submitted it, you can close this window.")
                size: 12.5
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                color: UI.Style.inkMuted
            }
        }
    }

    buttons: [
        UI.Button {
            visible: dialog.stage === "waiting"
            text: qsTr("Back to the report")
            onClicked: dialog.close()
        },
        UI.Button {
            visible: dialog.posted
            text: qsTr("View issue")
            iconName: "external"
            onClicked: dialog.viewIssue()
        },
        UI.Button {
            visible: dialog.stage !== "opening"
            kind: "primary"
            text: qsTr("Close window")
            onClicked: dialog.closeWindow()
        }
    ]
}
