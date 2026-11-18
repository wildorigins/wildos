import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import WildOS.UI as UI

// "Zoom wants your camera and microphone" (WinModeCalls.dc, right): a call
// app that has to run in Windows mode asks like any other app. Only what
// is allowed here is passed through to Windows, and only while that app
// is open. A PC without a camera or microphone has no row for it.
Item {
    id: card

    property string appName
    property var cameras: []
    property var microphones: []

    signal allowed(string camera, string microphone)
    signal denied()

    readonly property string title: cameras.length > 0 && microphones.length > 0 ? qsTr("%1 wants your camera and microphone").arg(appName)
        : cameras.length > 0 ? qsTr("%1 wants your camera").arg(appName) : qsTr("%1 wants your microphone").arg(appName)

    width: 500
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
            Layout.margins: 26
            Layout.bottomMargin: 18
            spacing: 14

            RowLayout {
                spacing: 14

                Rectangle {
                    implicitWidth: 44
                    implicitHeight: 44
                    radius: UI.Style.radiusMd
                    color: UI.Style.tone(0.07)

                    UI.Icon {
                        anchors.centerIn: parent
                        name: "video"
                        size: 22
                        strokeWidth: 1.9
                        color: UI.Style.accentText
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: card.title
                        size: 17
                        font.weight: UI.Style.weightSemiBold
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Accessible.role: Accessible.Heading
                    }

                    UI.Label {
                        text: qsTr("Running in Windows mode")
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Windows apps use the same camera, microphone and speakers as the rest of Wild OS. The light by your camera and the tray icon show when they're in use.")
                size: 13
                color: UI.Style.inkAlpha(0.82, 0.76)
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                lineHeight: 1.55
            }

            UI.Card {
                Layout.fillWidth: true
                visible: card.cameras.length > 0 || card.microphones.length > 0

                Column {
                    width: parent.width

                    UI.ListRow {
                        visible: card.cameras.length > 0
                        minHeight: 52
                        title: qsTr("Camera")
                        description: camera.currentText

                        UI.ComboBox {
                            id: camera
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Camera")
                            model: card.cameras
                            currentIndex: 0
                        }
                    }

                    UI.ListRow {
                        visible: card.microphones.length > 0
                        minHeight: 52
                        title: qsTr("Microphone")
                        description: microphone.currentText

                        UI.ComboBox {
                            id: microphone
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Microphone")
                            model: card.microphones
                            currentIndex: 0
                        }
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: card.cameras.length === 0 && card.microphones.length === 0
                text: qsTr("This PC has no camera or microphone connected, so there's nothing to pass through.")
                size: 13
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
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
                anchors.right: parent.right
                anchors.rightMargin: 26
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                UI.Button {
                    text: qsTr("Don't allow")
                    onClicked: card.denied()
                }

                UI.Button {
                    id: allowButton
                    kind: "primary"
                    text: qsTr("Allow while open")
                    enabled: card.cameras.length > 0 || card.microphones.length > 0
                    onClicked: card.allowed(card.cameras.length > 0 ? camera.currentValue : "", card.microphones.length > 0 ? microphone.currentValue : "")
                }
            }
        }
    }

    Keys.onEscapePressed: card.denied()
    function takeFocus() {
        allowButton.forceActiveFocus();
    }
}
