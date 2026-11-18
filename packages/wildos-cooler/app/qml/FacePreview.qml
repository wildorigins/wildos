import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// One face on a round screen at any size. The faces are the QML module
// WildOS.Cooler.Faces, the same components the cooler's own screen is
// drawn from; where that module is not there the screen is a plain dark
// disc with the face's name.
//
// `time` is the face's clock in seconds. A face moves only when it changes,
// so a page steps the one preview that should move and leaves the rest.
Item {
    id: preview

    property string faceId
    property real time: 0
    readonly property bool drawn: loader.status === Loader.Ready

    implicitWidth: 80
    implicitHeight: width

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Colours.screenOff
    }

    Loader {
        id: loader
        anchors.fill: parent
        active: CoolerApp.facesAvailable
        source: Qt.resolvedUrl("FaceHost.qml")

        Binding {
            target: loader.item
            property: "faceId"
            value: preview.faceId
            when: preview.drawn
        }
        Binding {
            target: loader.item
            property: "time"
            value: preview.time
            when: preview.drawn
        }
        Binding {
            target: loader.item
            property: "readings"
            value: CoolerApp.readings
            when: preview.drawn
        }
        Binding {
            target: loader.item
            property: "showMark"
            value: CoolerApp.showMark
            when: preview.drawn
        }
        Binding {
            target: loader.item
            property: "useTheme"
            value: CoolerApp.themeColours
            when: preview.drawn
        }
        Binding {
            target: loader.item
            property: "accent"
            value: UI.Style.accent
            when: preview.drawn
        }
        Binding {
            target: loader.item
            property: "glacier"
            value: UI.Style.glacier
            when: preview.drawn
        }
    }

    Text {
        visible: !preview.drawn
        anchors.centerIn: parent
        width: parent.width * 0.8
        horizontalAlignment: Text.AlignHCenter
        text: Format.faceName(preview.faceId)
        elide: Text.ElideRight
        font.family: UI.Style.fontFamily
        font.pixelSize: Math.max(9, Math.round(preview.width * 0.075))
        font.weight: UI.Style.weightMedium
        color: Qt.rgba(1, 1, 1, 0.7)
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Colours.screenEdge
    }
}
