import QtQuick
import QtQuick.Templates as T
import WildOS.TaskManager
import WildOS.UI as UI

// One row of the Processes table (TaskManager.dc): group headings 34px
// ("Apps (5)", 12px weight 600), entries and their processes 36px. An
// entry has its chevron, 18px icon and name at 12 (its processes at 34),
// the status in 12px and four 92px number cells, each shaded in the accent
// by how much it uses.
T.ItemDelegate {
    id: row

    // The statuses Processes sets, in English there so it can compare them;
    // listed here for the translations and shown with qsTr().
    readonly property var statusNames: [QT_TR_NOOP("Sandboxed"), QT_TR_NOOP("Suspended"), QT_TR_NOOP("Paused"), QT_TR_NOOP("Working")]

    required property int index
    required property string key
    required property int kind
    required property int depth
    required property string name
    required property string iconName
    required property string status
    required property real cpu
    required property real memory
    required property real disk
    required property real network
    required property bool expandable
    required property bool expanded
    required property int count
    required property string chip

    property bool selected: false
    // The selected row while the keyboard is what moves the selection.
    property bool focused: false
    property real memoryTotal: 1
    readonly property bool heading: kind === Processes.Header

    signal chosen(string key)
    signal expandToggled(string key)
    signal menuRequested(string key, point at)

    implicitHeight: heading ? 34 : 37
    padding: 0
    // The hairline under a row is outside its 36px of content.
    bottomPadding: heading ? 0 : 1
    hoverEnabled: !heading
    focusPolicy: Qt.NoFocus
    Accessible.role: heading ? Accessible.StaticText : Accessible.Row
    Accessible.name: heading ? name + " (" + count + ")" : name

    onClicked: if (!heading) chosen(key)
    // A single process has nothing to open, as in Windows.
    onDoubleClicked: if (!heading && expandable) expandToggled(key)

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: !row.heading
        onTapped: eventPoint => {
            row.chosen(row.key);
            row.menuRequested(row.key, eventPoint.position);
        }
    }

    background: Rectangle {
        color: row.heading ? "transparent" : row.selected ? UI.Style.tone(0.08) : row.hovered ? UI.Style.surfaceRaised : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        Rectangle {
            visible: !row.heading
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: UI.Style.tone(0.04)
        }

        UI.FocusRing {
            visible: row.focused
            anchors.margins: 2
            targetRadius: 4
        }
    }

    contentItem: Item {
        UI.Label {
            visible: row.heading
            x: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            height: implicitHeight
            text: row.name + " (" + row.count + ")"
            size: 12
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkAlpha(0.75, 0.70)
        }

        Item {
            id: nameCell
            visible: !row.heading
            width: parent.width - 120 - 4 * 92
            height: parent.height

            T.AbstractButton {
                id: chevron
                x: (row.depth > 0 ? 34 : 12) - 4
                width: 20
                height: parent.height
                visible: row.expandable
                focusPolicy: Qt.NoFocus
                Accessible.name: row.expanded ? qsTr("Collapse") : qsTr("Expand")
                onClicked: row.expandToggled(row.key)

                UI.Icon {
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.expanded ? "chevron-down" : "chevron-right"
                    size: 12
                    strokeWidth: 2
                }
            }

            Row {
                x: (row.depth > 0 ? 34 : 12) + 12 + 8
                width: parent.width - x - 8
                height: parent.height
                spacing: 8

                AppIcon {
                    width: 18
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    icon: row.iconName
                }

                UI.Label {
                    id: nameLabel
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - 26 - (chipItem.visible ? chipItem.width + 8 : 0))
                    text: row.name
                }

                UI.Chip {
                    id: chipItem
                    visible: row.chip.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.chip
                }
            }
        }

        UI.Label {
            visible: !row.heading
            x: nameCell.width + 12
            width: 120 - 12
            height: parent.height
            text: qsTr(row.status)
            size: 12
            color: UI.Style.inkMuted
        }

        Row {
            visible: !row.heading
            anchors.right: parent.right
            height: parent.height

            Repeater {
                model: [
                    { kind: "cpu", value: row.cpu, text: Format.cpu(row.cpu) },
                    { kind: "memory", value: row.memory, text: Format.memory(row.memory) },
                    { kind: "disk", value: row.disk, text: Format.disk(row.disk) },
                    { kind: "network", value: row.network, text: Format.network(row.network) }
                ]

                Rectangle {
                    required property var modelData
                    width: 92
                    height: parent.height
                    color: Colours.heatColour(Colours.heat(modelData.kind, modelData.value, row.memoryTotal))

                    UI.Label {
                        anchors.fill: parent
                        anchors.rightMargin: 12
                        anchors.leftMargin: 12
                        horizontalAlignment: Text.AlignRight
                        text: parent.modelData.text
                        size: 12
                        font.features: { "tnum": 1 }
                    }
                }
            }
        }
    }
}
