import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A small segmented control, as the firewall boards draw it
// (SettingsFirewall.dc, FirewallAllow.dc, FirewallActivity.dc): a 0.05
// well with a 0.10 edge and 2px inside it, 26px segments with 12px either
// side, the chosen one on a 0.12 fill in semibold, the others muted.
Rectangle {
    id: control

    // [{ value, text }]
    property var options: []
    property var value
    signal picked(var value)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: 32
    radius: 7
    color: UI.Style.tone(0.05)
    border.width: 1
    border.color: UI.Style.tone(0.10)
    opacity: enabled ? 1 : 0.5

    Row {
        id: row
        anchors.centerIn: parent

        Repeater {
            model: control.options

            T.AbstractButton {
                id: segment
                required property var modelData
                readonly property bool chosen: control.value === modelData.value

                implicitWidth: label.implicitWidth + 24
                implicitHeight: 26
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.text
                Accessible.checked: chosen
                onClicked: {
                    if (!chosen) {
                        control.picked(modelData.value);
                    }
                }

                background: Rectangle {
                    radius: 5
                    color: segment.chosen ? UI.Style.tone(0.12) : segment.hovered && segment.enabled ? UI.Style.tone(0.06) : "transparent"

                    UI.FocusRing {
                        visible: segment.visualFocus
                        targetRadius: 5
                    }
                }

                contentItem: UI.Label {
                    id: label
                    text: segment.modelData.text
                    size: 12.5
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.weight: segment.chosen ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: segment.chosen ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }
    }
}
