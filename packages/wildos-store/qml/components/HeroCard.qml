import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import WildOS.Store
import WildOS.UI as UI

// The boards' feature card: a photograph, or the deep grey card it falls
// back to, darkened towards the bottom, with app icons at the top and an
// eyebrow, a title and a line of text at the bottom in white.
T.AbstractButton {
    id: card

    property string eyebrow
    property string title
    property string body
    property var keys: []
    property url photo
    // Where the photograph is cropped, as CSS object-position's second
    // value: 0 keeps the top, 1 the bottom.
    property real photoY: 0.5
    property bool iconsRight: false
    property real iconSize: 50
    property real titleSize: 20
    property real bodySize: 13
    property real shadeStrength: 0.75
    // Busy photographs on the small cards: the shade runs from the left,
    // under the text, rather than up from the bottom.
    property bool shadeFromLeft: false

    implicitHeight: 154
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: title + ". " + body

    background: Item {
        Item {
            id: art
            anchors.fill: parent
            visible: false
            layer.enabled: true

            Rectangle {
                anchors.fill: parent
                color: Metrics.heroBase
            }

            Image {
                id: photoImage
                visible: card.photo.toString().length > 0
                readonly property real aspect: implicitHeight > 0 ? implicitWidth / implicitHeight : 1.5
                width: Math.max(parent.width, parent.height * aspect)
                height: width / aspect
                x: (parent.width - width) / 2
                y: -(height - parent.height) * card.photoY
                source: card.photo
                sourceSize.width: 2000
                asynchronous: true
            }

            Rectangle {
                anchors.fill: parent
                visible: card.shadeFromLeft
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.9) }
                    GradientStop { position: 0.75; color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.1) }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: !card.shadeFromLeft
                gradient: Gradient {
                    GradientStop { position: 0.3; color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0) }
                    GradientStop { position: 1.0; color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, card.shadeStrength) }
                }
            }

            Rectangle {
                anchors.fill: parent
                // Fades in on hover and clears at once, so moving along a list
                // never lights two items. It comes before the colour so it sees
                // the change in hover first.
                Behavior on color {
                    enabled: card.hovered
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
                color: card.down ? Qt.rgba(1, 1, 1, 0.04) : card.hovered ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(1, 1, 1, 0)
            }
        }

        Rectangle {
            id: mask
            anchors.fill: parent
            radius: Metrics.radiusHero
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: art
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        UI.FocusRing {
            visible: card.visualFocus
            targetRadius: Metrics.radiusHero
        }
    }

    contentItem: Item {
        Row {
            id: icons
            x: card.iconsRight ? parent.width - width - 20 : 24
            y: card.iconsRight ? 18 : 22
            spacing: card.iconsRight ? 10 : 12
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Qt.rgba(0, 0, 0, 0.35)
                shadowVerticalOffset: 10
                shadowBlur: 0.6
                blurMax: 24
            }

            Repeater {
                model: card.keys

                AppIcon {
                    required property string modelData
                    readonly property var app: Catalog.app(modelData)
                    size: card.iconSize
                    source: app ? app.iconSource : ""
                }
            }
        }

        Column {
            x: 24
            width: parent.width - 48
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            spacing: 6

            UI.Label {
                visible: card.eyebrow.length > 0
                text: card.eyebrow.toUpperCase()
                size: 11.5
                font.weight: UI.Style.weightBold
                font.letterSpacing: 0.92
                color: Metrics.onPhotoAlpha(0.75)
            }

            UI.Label {
                width: parent.width
                text: card.title
                size: card.titleSize
                font.weight: UI.Style.weightBold
                font.letterSpacing: -0.015 * card.titleSize
                lineHeight: 1.15
                color: Metrics.onPhoto
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                Accessible.role: Accessible.Heading
            }

            UI.Label {
                visible: card.body.length > 0
                width: parent.width
                text: card.body
                size: card.bodySize
                lineHeight: 1.4
                color: Metrics.onPhotoAlpha(0.8)
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }
}
