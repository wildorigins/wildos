import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The left column of the sheet: the mark and name, then the steps, ticked
// off as they are done.
Item {
    id: sidebar

    property int current: 1
    property bool showNote: false
    // The first boot setup session has no later: there is no account yet.
    property bool canLeave: true

    // Wild OS asks once, but never traps anyone: this closes the welcome
    // without marking it done, so it comes back at the next sign-in.
    signal later()
    property var steps: [qsTr("Welcome"), qsTr("Make it feel like home"), qsTr("Bring your files across"), qsTr("Your privacy"), qsTr("Apps to get started")]

    implicitWidth: Metrics.sidebarWidth
    implicitHeight: steps.implicitHeight + 32 + 26

    // Clipped so only the left corners take the sheet's radius.
    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            width: parent.width + Metrics.radiusSheet
            height: parent.height
            radius: Metrics.radiusSheet
            color: UI.Style.surfaceSunken
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.borderSubtle
    }

    ColumnLayout {
        id: steps
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.topMargin: 32
        anchors.bottomMargin: 26
        spacing: 28

        Row {
            spacing: 12

            Logo {
                anchors.verticalCenter: parent.verticalCenter
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "Wild OS"
                size: 17
                font.weight: UI.Style.weightSemiBold
            }
        }

        Column {
            Layout.fillWidth: true
            spacing: 4
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Setup steps")

            Repeater {
                model: sidebar.steps

                Rectangle {
                    id: row

                    required property int index
                    required property string modelData
                    readonly property int number: index + 1
                    readonly property bool done: number < sidebar.current
                    readonly property bool active: number === sidebar.current

                    width: parent.width
                    height: Math.max(40, Math.ceil(stepLabel.implicitHeight) + 16)
                    radius: UI.Style.radiusSm
                    color: active ? UI.Style.surfaceSelected : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: done ? qsTr("%1, done").arg(modelData) : active ? qsTr("%1, current step").arg(modelData) : modelData

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        Rectangle {
                            width: 22
                            height: 22
                            radius: 11
                            anchors.verticalCenter: parent.verticalCenter
                            color: row.done ? UI.Style.accentAlpha(0.25, 0.19) : row.active ? UI.Style.accent : "transparent"
                            border.width: row.done || row.active ? 0 : 1.5
                            border.color: UI.Style.inkAlpha(0.45, 0.40)

                            UI.Icon {
                                anchors.centerIn: parent
                                visible: row.done
                                name: "checkmark"
                                size: 12
                                strokeWidth: 3
                                color: UI.Style.accentText
                            }

                            UI.Label {
                                anchors.centerIn: parent
                                visible: !row.done
                                text: row.number
                                size: 11
                                font.weight: row.active ? UI.Style.weightBold : UI.Style.weightRegular
                                color: row.active ? UI.Style.textOnAccent : UI.Style.ink
                            }
                        }

                        UI.Label {
                            id: stepLabel
                            anchors.verticalCenter: parent.verticalCenter
                            width: row.width - 10 - 22 - 12 - 8
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            text: row.modelData
                            font.weight: row.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                            color: row.done ? UI.Style.inkStatus : row.active ? UI.Style.ink : UI.Style.inkMuted
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        UI.Label {
            Layout.fillWidth: true
            visible: sidebar.showNote
            // Accent and taskbar layout both live on that Settings page.
            text: qsTr("You can change all of this later in Settings > Personalisation.")
            size: 12
            lineHeight: 18 * UI.Style.textScale
            lineHeightMode: Text.FixedHeight
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            color: UI.Style.inkMuted
        }

        UI.LinkButton {
            visible: sidebar.canLeave
            Layout.leftMargin: -4
            text: qsTr("Set up later")
            size: 12
            Accessible.description: qsTr("Closes the welcome. It opens again the next time you sign in.")
            onClicked: sidebar.later()
        }
    }
}
