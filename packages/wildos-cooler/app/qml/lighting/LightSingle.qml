import QtQuick
import WildOS.Cooler

// A zone drawn as one light (10.11 "Top light", 10.16): its colour in the
// middle with two rings of glow. `known` false draws only an outline, for
// a light Wild OS can see is there but cannot read or set.
Item {
    id: single

    property color colour: "black"
    property real size: 54
    property bool known: true

    readonly property bool lit: known && colour.r + colour.g + colour.b > 0.06

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        visible: single.lit
        anchors.centerIn: parent
        width: single.size * 0.92
        height: width
        radius: width / 2
        color: Qt.rgba(single.colour.r, single.colour.g, single.colour.b, 0.14)
    }

    Rectangle {
        visible: single.lit
        anchors.centerIn: parent
        width: single.size * 0.64
        height: width
        radius: width / 2
        color: Qt.rgba(single.colour.r, single.colour.g, single.colour.b, 0.3)
    }

    Rectangle {
        anchors.centerIn: parent
        width: single.size * 0.4
        height: width
        radius: width / 2
        color: single.lit ? single.colour : single.known ? LightWords.unlit : "transparent"
        border.width: single.lit ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, single.known ? 0.14 : 0.32)
    }
}
