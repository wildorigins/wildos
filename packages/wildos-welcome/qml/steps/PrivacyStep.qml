import QtQuick
import QtQuick.Layouts
import WildOS.Welcome
import WildOS.UI as UI

// Step 6, from WelcomePrivacy.dc.html (3.8). Every switch reads and writes the
// real setting. A switch Wild OS cannot change on this computer stays
// locked at what is actually true and says why; the two things Wild OS has
// no switch for at all (location, web search from Start) are stated as
// plain facts rather than drawn as switches that never move.
StepPage {
    id: page

    signal next()
    signal back()

    // The last step (no step 7 on this PC): Continue reads Finish.
    property bool last: false
    // Step 7's checks are still running, so whether there is one isn't
    // known yet.
    property bool waiting: false
    readonly property bool finishing: FirstBoot.state === "finishing" || FirstBoot.state === "signing-in"

    title: qsTr("Your privacy")
    subtitle: qsTr("Here is everything that could leave this computer, in plain English. Change any of it later in Settings.")
    note: Privacy.error
    noteColor: UI.Style.warning
    Component.onCompleted: focusQuietly(continueButton)

    UI.Card {
        Layout.fillWidth: true
        tinted: true

        Item {
            width: parent.width
            implicitHeight: Math.max(68, bannerText.implicitHeight + 24)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 19
                anchors.rightMargin: 19
                spacing: 14

                UI.Icon {
                    name: "shield-check"
                    size: 22
                    strokeWidth: 2
                    color: UI.Style.accent
                }

                ColumnLayout {
                    id: bannerText
                    Layout.fillWidth: true
                    spacing: 2

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("No account, no advertising ID, no tracking")
                        size: 14
                        font.weight: UI.Style.weightSemiBold
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Everything below starts off unless it is needed to keep you safe.")
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                title: qsTr("Check for security updates")
                description: qsTr("Checks for fixes once a day. Nothing about you is sent.")

                UI.Toggle {
                    text: checked ? qsTr("On") : qsTr("Off")
                    checked: Privacy.updateChecks
                    locked: !Privacy.updateChecksAvailable || Privacy.busy
                    lockedReason: Privacy.busy ? qsTr("Saving your last change") : qsTr("The update checker isn't installed on this computer.")
                    Accessible.name: qsTr("Check for security updates")
                    onToggled: Privacy.setUpdateChecks(checked)
                }
            }

            UI.SettingRow {
                title: qsTr("Send crash reports")
                description: qsTr("Helps fix bugs. Sent only when an app crashes, with nothing personal in it.")

                UI.Toggle {
                    text: checked ? qsTr("On") : qsTr("Off")
                    checked: Privacy.crashReports
                    locked: !Privacy.crashReportsAvailable
                    lockedReason: qsTr("The crash reporter isn't installed on this computer.")
                    Accessible.name: qsTr("Send crash reports")
                    onToggled: Privacy.setCrashReports(checked)
                }
            }

            UI.SettingRow {
                title: qsTr("Location for weather and maps")
                description: Privacy.location
                    ? qsTr("On. Apps must still ask you before using it. Choose which in Settings, Privacy and security.")
                    : qsTr("Off. This computer has no location service, so no app can find out where you are.")
                status: Privacy.location ? qsTr("On") : qsTr("Off")
            }

            UI.SettingRow {
                title: qsTr("Search the web from Start")
                description: qsTr("Start searches this computer only. Nothing you type in Start leaves it.")
                status: qsTr("Off")
            }
        }
    }

    buttons: [
        UI.Button {
            text: qsTr("Back")
            onClicked: page.back()
        },
        UI.Button {
            id: continueButton
            text: page.last ? qsTr("Finish") : qsTr("Continue")
            kind: "primary"
            busy: page.waiting || page.finishing
            enabled: !page.waiting && !page.finishing
            onClicked: page.next()
        }
    ]
}
