import QtQuick

// The face's main temperature: one number with its degree ring, or, when
// the screen shows both temperatures (FaceBase.paired), the CPU's and the
// GPU's side by side at a smaller size, each with its own name above it.
// The names repeat the face's own label (`label`) in its font and place,
// and that label is hidden meanwhile.
Item {
    id: reading

    property string value: "54"
    property string sample: "54"
    property real cx: 320
    property real baseY: 0
    property real size: 150
    property int weight: 250
    property real spacing: 0
    property string family: "Albert Sans"
    property color colour: "#ffffff"
    property bool degree: true
    property bool ringShown: degree
    property real degreeOffset: 0.66 * size
    property real degreeRise: 0.6 * size
    property real degreeRadius: 0.055 * size
    property real degreeStroke: 0.03 * size
    property real fitWidth: 0

    // The second number; empty shows the one.
    property string pair: ""
    property var label: null
    property string firstName: qsTr("CPU")
    property string secondName: qsTr("GPU")
    readonly property bool paired: pair !== ""

    // At this share of the single size two numbers of two digits and their
    // rings fit where one stood.
    property real pairScale: 0.6
    readonly property real pairSize: size * pairScale
    readonly property real pairStep: Math.max(pairSize * 0.92, fitWidth > 0 ? fitWidth / 4 : 0)
    // The smaller numbers sit on a baseline raised by half the height they
    // lost, so the pair stays centred where the one number was.
    readonly property real pairBaseY: baseY - (size - pairSize) * 0.36

    // What a face reads to place things against the single number.
    readonly property real grown: one.grown
    readonly property real ringX: one.ringX
    readonly property real numberRight: one.numberRight
    readonly property real reach: one.reach
    readonly property real fit: one.fit

    Degrees {
        id: one
        visible: !reading.paired
        value: reading.value
        sample: reading.sample
        cx: reading.cx
        baseY: reading.baseY
        size: reading.size
        weight: reading.weight
        spacing: reading.spacing
        family: reading.family
        colour: reading.colour
        degree: reading.degree
        ringShown: reading.ringShown
        degreeOffset: reading.degreeOffset
        degreeRise: reading.degreeRise
        degreeRadius: reading.degreeRadius
        degreeStroke: reading.degreeStroke
        fitWidth: reading.fitWidth
    }

    Repeater {
        model: reading.paired ? [reading.value, reading.pair] : []

        Degrees {
            required property string modelData
            required property int index
            value: modelData
            cx: reading.cx + (index === 0 ? -reading.pairStep : reading.pairStep)
            baseY: reading.pairBaseY
            size: reading.pairSize
            weight: reading.weight
            spacing: reading.spacing * reading.pairScale
            family: reading.family
            colour: reading.colour
            degree: reading.degree
            ringShown: reading.degree
            fitWidth: reading.pairStep * 1.9
        }
    }

    Repeater {
        model: reading.paired && reading.label ? [reading.firstName, reading.secondName] : []

        FaceText {
            required property string modelData
            required property int index
            cx: reading.cx + (index === 0 ? -reading.pairStep : reading.pairStep)
            baseY: reading.label.baseY
            size: reading.label.size
            spacing: reading.label.spacing
            font.family: reading.label.font.family
            font.weight: reading.label.font.weight
            color: reading.label.color
            opacity: reading.label.opacity
            text: modelData
        }
    }

    Binding {
        when: reading.paired && reading.label !== null
        target: reading.label
        property: "visible"
        value: false
    }
}
