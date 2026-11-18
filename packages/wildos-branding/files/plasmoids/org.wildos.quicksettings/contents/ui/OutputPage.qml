pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// Where sound plays: the speakers, headphones and screens that are plugged
// in or connected. Choosing one makes it the output for everything, as the
// tray button's own menu does.
FocusScope {
    id: page

    required property var quick
    readonly property var audio: quick.audio

    signal back()

    function focusFirst(): void {
        header.backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        PageHeader {
            id: header
            Layout.fillWidth: true
            title: qsTr("Sound output")
            onBack: page.back()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: -4
                leftMargin: 4
                rightMargin: 4
                topMargin: 4
                bottomMargin: 4
                clip: true
                spacing: 4
                visible: count > 0
                model: page.audio ? page.audio.outputModel : null
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: UI.ScrollBar {}

                delegate: ListRow {
                    id: row

                    required property var model
                    // PulseAudio's word for the kind of device: "headphone",
                    // "headset", "speaker" and so on, when it knows.
                    readonly property string form: String(model.PulseObject && model.PulseObject.formFactor || "")

                    width: ListView.view.width - 8
                    title: model.Description
                    active: model.Default
                    subtitle: model.Default ? qsTr("Current output") : ""
                    icon: Component {
                        UI.Glyph {
                            path: row.form.indexOf("head") === 0 ? Icons.headphones : Icons.speaker + Icons.waveNear + Icons.waveFar
                            color: row.active ? UI.Style.accent : UI.Style.iconInk
                            strokeWidth: 1.9
                        }
                    }

                    onClicked: model.PulseObject.default = true
                }
            }

            Placeholder {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 320)
                visible: !list.visible
                iconPath: Icons.speaker + Icons.muteCross
                heading: qsTr("No sound output")
                explanation: qsTr("Plug in or connect speakers or headphones.")
            }
        }

        UI.LinkButton {
            Layout.leftMargin: -4
            text: qsTr("More sound settings")
            onClicked: page.quick.openSettings("sound")
        }
    }
}
