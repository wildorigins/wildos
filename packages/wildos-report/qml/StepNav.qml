import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The left column (ReportDescribe.dc): the three steps, each a 26px
// numbered disc that turns into a green tick once done, and the "Nothing
// is sent for you" card at the foot.
Rectangle {
    id: nav

    property int current: 0
    // The furthest step reached, so a step can be gone back to.
    property int reached: 0
    signal stepClicked(int index)

    readonly property var steps: [
        { title: qsTr("What happened"), note: qsTr("Describe the problem") },
        { title: qsTr("What's included"), note: qsTr("Check before it leaves") },
        { title: qsTr("Send"), note: qsTr("Your choice how") }
    ]

    color: Look.nav

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Look.navEdge
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        anchors.leftMargin: 10
        anchors.rightMargin: 11
        spacing: 4

        Repeater {
            model: nav.steps

            delegate: Rectangle {
                id: step
                required property var modelData
                required property int index
                readonly property bool active: nav.current === index
                readonly property bool done: index < nav.current

                Layout.fillWidth: true
                implicitHeight: 52
                radius: 8
                color: active ? UI.Style.layer(0.061, false, 0.037) : mouse.containsMouse && mouse.enabled ? UI.Style.tone(0.04) : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: modelData.title

                Row {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 26
                        radius: 13
                        color: step.active ? UI.Style.accent : step.done ? Look.green : "transparent"
                        border.width: step.active || step.done ? 0 : 1.5
                        border.color: UI.Style.tone(0.35)

                        UI.Label {
                            anchors.centerIn: parent
                            visible: !step.done
                            text: step.index + 1
                            size: 12.5
                            font.weight: UI.Style.weightBold
                            color: step.active ? UI.Style.textOnAccent : UI.Style.inkMuted
                        }
                        ReportIcon {
                            anchors.centerIn: parent
                            visible: step.done
                            name: "tick"
                            size: 15
                            strokeWidth: 2.6
                            color: Look.dark ? "#0b1f14" : "#ffffff"
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        UI.Label {
                            text: step.modelData.title
                            size: 13.5
                            font.weight: step.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                            color: step.active || step.done ? UI.Style.ink : UI.Style.inkMuted
                        }
                        UI.Label {
                            text: step.modelData.note
                            size: 12
                            color: UI.Style.inkSubtle
                        }
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: step.index <= nav.reached && !step.active
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: nav.stepClicked(step.index)
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            implicitHeight: card.implicitHeight + 24
            radius: UI.Style.radiusPanel
            color: Look.greenFill
            border.width: 1
            border.color: Look.greenEdge

            Row {
                id: card
                x: 12
                y: 12
                width: parent.width - 24
                spacing: 10

                ReportIcon {
                    name: "shield-check"
                    size: 18
                    color: Look.green
                }
                Column {
                    width: parent.width - 28
                    spacing: 3

                    UI.Label {
                        text: qsTr("Nothing is sent for you")
                        size: 12.5
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        width: parent.width
                        text: qsTr("There's no Wild OS server. You decide where it goes.")
                        size: 11.5
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }
}
