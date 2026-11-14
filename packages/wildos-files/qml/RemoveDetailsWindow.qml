import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import WildOS.Files
import WildOS.UI as UI

// "Choose what is removed", from Settings > Privacy and security >
// Advanced protection: what Remove personal details takes out of the copy
// it saves. A window of its own, laid out as Properties is. Each tick is
// kept as soon as it changes, so Close is the only button.
UI.AppWindow {
    id: win

    deep: true
    width: 480
    height: 560
    minimumWidth: 420
    minimumHeight: 480
    title: "Choose what is removed"

    component Choice: ColumnLayout {
        id: choice
        property alias text: box.text
        property alias checked: box.checked
        property string detail
        signal toggled(bool on)
        Layout.fillWidth: true
        spacing: 2

        UI.CheckBox {
            id: box
            Layout.fillWidth: true
            font.weight: UI.Style.weightMedium
            onToggled: choice.toggled(checked)
        }
        UI.Label {
            Layout.fillWidth: true
            Layout.leftMargin: 30
            text: choice.detail
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: win.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: win.frame
            buttons.showMinimise: false
            buttons.showMaximise: false

            Row {
                x: -2
                height: parent.height
                spacing: 10

                UI.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "eye-off"
                    size: 16
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.title
                    font.weight: UI.Style.weightMedium
                    color: win.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.divider
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            spacing: 18

            UI.Label {
                Layout.fillWidth: true
                text: "Right-click a file in Files and choose Remove personal details to save a copy without what is ticked here. The file itself stays as it is."
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            Choice {
                text: "Location"
                detail: "GPS position and place names, such as the city and country, in photos, videos and audio."
                checked: Protection.removeLocation
                onToggled: on => Protection.removeLocation = on
            }
            Choice {
                text: "Device"
                detail: "The phone or camera's make, model and serial numbers, the lens, the camera maker's own notes, and the program that made or last saved the file. In documents, the printer it was set up for."
                checked: Protection.removeDevice
                onToggled: on => Protection.removeDevice = on
            }
            Choice {
                text: "Author names"
                detail: "Author, artist, owner and copyright names, and in documents who last saved it, the company, and the names on comments and tracked changes."
                checked: Protection.removeAuthors
                onToggled: on => Protection.removeAuthors = on
            }

            UI.Label {
                Layout.fillWidth: true
                text: "Works with photos, MP4 and MOV videos, MP3, FLAC and Ogg audio, PDFs, and Word, Excel, PowerPoint and OpenDocument files. Anything that shows in the picture or the text itself is kept, and so is the date it was made. Pictures inside a PDF keep their own details."
                size: 12
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            Item { Layout.fillHeight: true }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            Item { Layout.fillWidth: true }

            UI.Button {
                kind: "primary"
                text: "Close"
                onClicked: win.close()
            }
        }
    }

    Shortcut {
        sequences: ["Escape"]
        onActivated: win.close()
    }
}
