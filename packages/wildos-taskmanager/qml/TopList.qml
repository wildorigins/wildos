import QtQuick
import WildOS.TaskManager
import WildOS.UI as UI

// "Using the most CPU" (TaskManagerPerf.dc): a 0.04 card, radius 10, with
// the three entries using a resource most, each with its icon, a 90px bar
// of its share and the amount in 12px.
Rectangle {
    id: card

    property string title
    property string metric
    property real total: 1
    property color colour: UI.Style.accent
    readonly property var entries: SystemStats.revision >= 0 && metric.length > 0 ? Processes.top(metric, 3) : []

    function amount(value) {
        switch (metric) {
        case "cpu":
        case "gpu":
            return Format.cpu(value);
        case "memory":
        case "winmode":
            return Format.memory(value);
        case "disk":
            return Format.disk(value);
        case "network":
            return Format.network(value);
        }
        return "";
    }

    implicitHeight: column.implicitHeight + 24
    radius: 10
    color: UI.Style.surfaceRest

    Column {
        id: column
        x: 14
        y: 12
        width: parent.width - 28
        spacing: 8

        UI.Label {
            width: parent.width
            text: card.title
            size: 12
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkStatus
        }

        UI.Label {
            visible: card.entries.length === 0
            width: parent.width
            text: qsTr("Nothing is using it right now.")
            size: 12
            color: UI.Style.inkMuted
        }

        Repeater {
            model: card.entries.length

            Item {
                required property int index
                readonly property var modelData: card.entries[index] || ({})
                width: column.width
                height: Math.max(16, nameLabel.implicitHeight)

                AppIcon {
                    width: 16
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter
                    icon: modelData.icon
                }

                UI.Label {
                    id: nameLabel
                    x: 26
                    width: parent.width - x - 10 - 90 - 10 - valueLabel.width
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    size: 12
                }

                Rectangle {
                    x: parent.width - valueLabel.width - 10 - 90
                    anchors.verticalCenter: parent.verticalCenter
                    width: 90
                    height: 4
                    radius: 2
                    color: UI.Style.tone(0.1)

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, modelData.value / Math.max(card.total, 0.0001)))
                        height: parent.height
                        radius: 2
                        color: card.colour
                    }
                }

                UI.Label {
                    id: valueLabel
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(40, implicitWidth)
                    horizontalAlignment: Text.AlignRight
                    text: card.amount(modelData.value)
                    size: 12
                    font.features: { "tnum": 1 }
                }
            }
        }
    }
}
