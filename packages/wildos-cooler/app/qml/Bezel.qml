import QtQuick
import QtQuick.Effects
import WildOS.Cooler

// The cooler's round body with its screen in the middle, as the boards
// picture it at three sizes (10.9: 308 and 92, 10.12: 124): a dark disc lit
// from the upper left with a hairline of light along its top edge, a soft
// shadow under it, and the face on the screen.
Item {
    id: bezel

    property real size: 92
    property alias faceId: face.faceId
    property alias time: face.time
    readonly property real inset: Math.round(size * 0.065)

    implicitWidth: size
    implicitHeight: size

    Item {
        id: body
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Colours.bezelShadow
            shadowVerticalOffset: Math.round(bezel.size * 0.042)
            shadowBlur: 1.0
            blurMax: Math.max(8, Math.round(bezel.size * 0.123))
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Colours.bezelEdge
        }

        // A disc looks the same turned, so turning it turns only its
        // top-to-bottom gradient: light from the upper left (145 degrees).
        Rectangle {
            y: 1
            width: parent.width
            height: parent.height
            radius: width / 2
            rotation: -35
            gradient: Gradient {
                GradientStop { position: 0; color: Colours.bezelLight }
                GradientStop { position: 0.64; color: Colours.bezelDark }
                GradientStop { position: 1; color: Colours.bezelDark }
            }
        }
    }

    FacePreview {
        id: face
        x: bezel.inset
        y: bezel.inset
        width: bezel.size - 2 * bezel.inset
        height: width
    }
}
