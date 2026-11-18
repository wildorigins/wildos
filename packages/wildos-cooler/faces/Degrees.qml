import QtQuick

// One big number with the degree ring the board draws beside it (Reading
// sets one or, with both temperatures on, two of these). The board
// places the ring for its two digit example; here it follows the number's
// real width, and the whole reading shrinks when it would outgrow fitWidth,
// so one and three digit values sit as well as the example does.
Item {
    id: reading

    property string value: "54"
    // The board's example for this number, which the ring's place is taken from.
    property string sample: "54"
    property real cx: 320
    property real baseY: 0
    property real size: 150
    property int weight: 250
    property real spacing: 0
    property string family: "Albert Sans"
    property color colour: "#ffffff"
    property bool degree: true
    // Off when a face draws the ring itself, outside an effect on the number.
    property bool ringShown: degree
    property real degreeOffset: 0.66 * size
    property real degreeRise: 0.6 * size
    property real degreeRadius: 0.055 * size
    property real degreeStroke: 0.03 * size
    property real fitWidth: 0

    readonly property real grown: (valueMetrics.advanceWidth - sampleMetrics.advanceWidth) / 2
    readonly property real ringX: cx + degreeOffset + grown
    // Right edge of the number itself, for a face that sets a unit after it.
    readonly property real numberRight: cx + valueMetrics.advanceWidth / 2
    readonly property real reach: Math.max(valueMetrics.advanceWidth / 2,
                                           degree ? degreeOffset + grown + degreeRadius + degreeStroke / 2 : 0)
    readonly property real fit: fitWidth > 0 && reach * 2 > fitWidth ? fitWidth / (reach * 2) : 1

    transform: Scale {
        origin.x: reading.cx
        origin.y: reading.baseY - reading.size * 0.35
        xScale: reading.fit
        yScale: reading.fit
    }

    TextMetrics {
        id: valueMetrics
        font: number.font
        text: reading.value
    }

    TextMetrics {
        id: sampleMetrics
        font: number.font
        text: reading.sample
    }

    FaceText {
        id: number
        cx: reading.cx
        baseY: reading.baseY
        size: reading.size
        spacing: reading.spacing
        font.family: reading.family
        font.weight: reading.weight
        color: reading.colour
        text: reading.value
    }

    Rectangle {
        visible: reading.ringShown && reading.value !== ""
        x: reading.ringX - width / 2
        y: reading.baseY - reading.degreeRise - height / 2
        width: reading.degreeRadius * 2 + reading.degreeStroke
        height: width
        radius: width / 2
        color: "transparent"
        border.width: reading.degreeStroke
        border.color: reading.colour
    }
}
