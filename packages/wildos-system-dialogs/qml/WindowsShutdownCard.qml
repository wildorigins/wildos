import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import WildOS.UI as UI

// "Your Windows apps are still open" (WinModeShutdown.dc, board 9.9),
// asked by Windows mode's shutdown guard as the session ends: 640 wide,
// radius 18, a 20px title, the open documents with their state, the
// accent card saying what keeping does, and a footer with "Close without
// saving" at the left. After 60 seconds the safe choice is taken.
//
// The taskbar panel's Restart and Shut down for Windows alone ask the same
// way (actions windows-restart and windows-shutdown) when an app may have
// unsaved work, with no countdown: restarting can't keep the work, so Go
// back is the main button; shutting down offers Save to disk instead,
// which keeps it and still frees the memory.
Item {
    id: card

    // shutdown, restart or signout; windows-restart or windows-shutdown
    property string action: "shutdown"
    property var apps: []
    property string reopen
    property int seconds: 0

    signal keep()
    signal discard()
    signal back()

    readonly property bool windowsOnly: action.startsWith("windows-")
    readonly property bool canKeep: action !== "windows-restart"
    readonly property string keepText: action === "windows-shutdown" ? qsTr("Save to disk instead")
        : action === "restart" ? qsTr("Keep my work and restart") : action === "signout" ? qsTr("Keep my work and sign out") : qsTr("Keep my work and shut down")
    // reopen is the session service's "Excel opens with Budget 2027 just as
    // you left them.", or empty when it names no one app.
    readonly property string keepDetail: action === "windows-shutdown"
        ? qsTr("Windows mode is saved to disk as it is, in about 5 seconds, and uses no memory until you open a Windows app.")
        : card.reopen.length > 0
        ? qsTr("Windows mode is saved to disk as it is, in about 5 seconds. Next time, %1").arg(card.reopen)
        : qsTr("Windows mode is saved to disk as it is, in about 5 seconds. Next time, your Windows apps open just as you left them.")
    readonly property string discardText: action === "windows-restart" ? qsTr("Restart without saving")
        : action === "windows-shutdown" ? qsTr("Shut down without saving") : qsTr("Close without saving")
    readonly property string body: action === "windows-restart"
        ? qsTr("Restarting Windows closes them, and changes that aren't saved are lost. Save your work in them first, then restart.")
        : action === "windows-shutdown"
        ? qsTr("Shutting down Windows closes them, and changes that aren't saved are lost. Save your work in them first, or save Windows mode to disk instead, which keeps everything as it is.")
        : action === "signout"
        ? qsTr("Nothing will be lost. Wild OS can keep them exactly as they are, unsaved changes included, and reopen them next time you sign in.")
        : qsTr("Nothing will be lost. Wild OS can keep them exactly as they are, unsaved changes included, and reopen them next time you switch on.")
    readonly property string title: qsTr("Your Windows apps are still open")

    width: 640
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.AlertMessage
    Accessible.name: title

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusDialog
        color: UI.Style.glassDialog
        border.width: 1
        border.color: UI.Style.borderGlass
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: UI.Style.shadowFlyout
            shadowVerticalOffset: 30
            shadowBlur: 1.0
            blurMax: 64
        }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 28
            Layout.bottomMargin: 20
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                UI.Label {
                    Layout.fillWidth: true
                    text: card.title
                    size: 20
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: card.body
                    size: 13
                    color: UI.Style.inkAlpha(0.82, 0.76)
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    lineHeight: 1.55
                }
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: card.apps

                        UI.ListRow {
                            required property var modelData
                            minHeight: 58
                            title: modelData.document
                            description: modelData.app
                            leading: UI.ThemeIcon {
                                width: 30
                                height: 30
                                name: modelData.icon
                                fallback: "wildos-windows-app"
                            }

                            UI.Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.state === "unsaved" ? qsTr("Unsaved changes") : modelData.state === "saved" ? qsTr("Saved") : qsTr("Open")
                                size: 12
                                color: modelData.state === "unsaved" ? UI.Style.warning : UI.Style.inkMuted
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: card.canKeep
                Layout.fillWidth: true
                implicitHeight: keepRow.implicitHeight + 32
                radius: UI.Style.radiusMd
                color: UI.Style.accentTint
                border.width: 1
                border.color: UI.Style.accentBorder

                RowLayout {
                    id: keepRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 16

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 44
                        implicitHeight: 44
                        radius: UI.Style.radiusMd
                        color: UI.Style.surfaceControl

                        UI.Glyph {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            // The design's floppy disk: what "keep" means.
                            path: "M5 3h11l3 3v15H5zM8 3v5h7V3M8 21v-7h8v7"
                            strokeWidth: 1.9
                            color: UI.Style.accentText
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        UI.Label {
                            text: card.keepText
                            size: 15
                            font.weight: UI.Style.weightSemiBold
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: card.keepDetail
                            size: 13
                            color: UI.Style.inkStatus
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                            lineHeight: 1.45
                        }
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: card.seconds > 0
                text: card.seconds === 1 ? qsTr("If you don't choose, Wild OS keeps your work in 1 second.") : qsTr("If you don't choose, Wild OS keeps your work in %1 seconds.").arg(card.seconds)
                size: 12
                color: UI.Style.inkMuted
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            bottomLeftRadius: UI.Style.radiusDialog
            bottomRightRadius: UI.Style.radiusDialog
            color: UI.Style.shade(0.25, 0.04)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: UI.Style.borderSubtle
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 10

                UI.LinkButton {
                    id: discardLink
                    text: card.discardText
                    onClicked: card.discard()
                }

                Item {
                    Layout.fillWidth: true
                }

                UI.Button {
                    id: backButton
                    kind: card.canKeep ? "secondary" : "primary"
                    text: qsTr("Go back")
                    onClicked: card.back()
                }

                UI.Button {
                    id: keepButton
                    visible: card.canKeep
                    kind: "primary"
                    text: card.keepText
                    onClicked: card.keep()
                }
            }
        }
    }

    Keys.onEscapePressed: card.back()
    // Enter presses the button that has focus, which is the safe one to
    // start with; the buttons themselves only take Space.
    function enter() {
        if (discardLink.activeFocus) {
            card.discard();
        } else if (keepButton.activeFocus && card.canKeep) {
            card.keep();
        } else {
            card.back();
        }
    }
    Keys.onReturnPressed: card.enter()
    Keys.onEnterPressed: card.enter()
    function takeFocus() {
        (card.canKeep ? keepButton : backButton).forceActiveFocus();
    }
}
