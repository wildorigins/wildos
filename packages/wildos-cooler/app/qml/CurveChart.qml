import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// The speed curve (10.10): speed in per cent against the liquid's
// temperature from 20 to 60 degrees, the line running from the accent to
// rose over a fading fill, a round handle on every point between the two
// ends, and a dashed line at the liquid's temperature now with the speed
// the curve gives there.
//
// Drag a handle, or move a focused one with the arrow keys. A point never
// passes its neighbours, and the curve never falls as the liquid warms:
// lowering a point lowers those to its left with it, raising it raises
// those to its right. `edited` carries the new curve when the handle is let
// go.
Item {
    id: chart

    // [{ temperature, percent }], the two ends included.
    property var points: []
    property real nowTemperature: 0
    // "Now: 34°, fans at 36%", with %1 the temperature and %2 the speed.
    property string nowText
    property string accessibleName

    signal edited(var points)

    readonly property real plotLeft: 46
    readonly property real plotRight: width - 14
    readonly property real plotTop: 12
    readonly property real plotBottom: 220
    readonly property int coldest: 20
    readonly property int hottest: 60

    // The curve as drawn: `points`, or the one being dragged.
    property var draft: []
    property int dragging: -1
    onPointsChanged: if (dragging < 0) draft = points.map(p => ({ temperature: p.temperature, percent: p.percent }))

    function xOf(temperature) {
        return plotLeft + (temperature - coldest) / (hottest - coldest) * (plotRight - plotLeft);
    }
    function yOf(percent) {
        return plotBottom - percent / 100 * (plotBottom - plotTop);
    }
    function speedAt(temperature) {
        if (draft.length === 0) {
            return 0;
        }
        for (let i = 1; i < draft.length; ++i) {
            const a = draft[i - 1];
            const b = draft[i];
            if (temperature <= b.temperature) {
                const share = b.temperature === a.temperature ? 0 : Math.max(0, (temperature - a.temperature) / (b.temperature - a.temperature));
                return a.percent + (b.percent - a.percent) * share;
            }
        }
        return draft[draft.length - 1].percent;
    }
    function move(index, temperature, percent) {
        const next = draft.map(p => ({ temperature: p.temperature, percent: p.percent }));
        next[index].temperature = Math.max(next[index - 1].temperature + 1, Math.min(next[index + 1].temperature - 1, Math.round(temperature)));
        const speed = Math.max(0, Math.min(100, Math.round(percent)));
        next[index].percent = speed;
        for (let i = 0; i < next.length; ++i) {
            next[i].percent = i < index ? Math.min(next[i].percent, speed) : Math.max(next[i].percent, speed);
        }
        draft = next;
    }

    implicitHeight: 250
    implicitWidth: 760

    readonly property real nowShown: Math.max(coldest, Math.min(hottest, nowTemperature))
    readonly property real nowSpeed: speedAt(nowShown)

    Repeater {
        model: 5

        Item {
            id: gridLine
            required property int index
            readonly property int percent: index * 25
            y: chart.yOf(percent)
            width: chart.width

            Rectangle {
                x: chart.plotLeft
                y: -0.5
                width: chart.plotRight - chart.plotLeft
                height: 1
                color: UI.Style.tone(0.08)
            }

            Words {
                x: 0
                y: 4 - ascentBox
                width: 36
                horizontalAlignment: Text.AlignRight
                text: qsTr("%1%").arg(gridLine.percent)
                size: 11.5
                color: UI.Style.inkSubtle
            }
        }
    }

    Repeater {
        model: 5

        Words {
            required property int index
            readonly property int temperature: chart.coldest + index * 10
            x: chart.xOf(temperature) - width / 2
            y: 242 - ascentBox
            text: Format.degrees(temperature)
            size: 11.5
            color: UI.Style.inkSubtle
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        readonly property color lineStart: UI.Style.accent
        readonly property color lineEnd: Colours.fans
        readonly property color nowColour: Colours.liquid
        // Everything the picture is made from: any change paints it again.
        readonly property var inputs: [chart.draft, chart.nowShown, width, lineStart, lineEnd, nowColour]
        onInputsChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const points = chart.draft;
            if (points.length < 2) {
                return;
            }
            let top = chart.plotBottom;
            ctx.beginPath();
            for (let i = 0; i < points.length; ++i) {
                const x = chart.xOf(points[i].temperature);
                const y = chart.yOf(points[i].percent);
                top = Math.min(top, y);
                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.lineTo(chart.plotRight, chart.plotBottom);
            ctx.lineTo(chart.plotLeft, chart.plotBottom);
            ctx.closePath();
            const fill = ctx.createLinearGradient(0, top, 0, chart.plotBottom);
            fill.addColorStop(0, Colours.alpha(lineStart, 0.32));
            fill.addColorStop(1, Colours.alpha(lineStart, 0));
            ctx.fillStyle = fill;
            ctx.fill();

            ctx.beginPath();
            for (let i = 0; i < points.length; ++i) {
                const x = chart.xOf(points[i].temperature);
                const y = chart.yOf(points[i].percent);
                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            const line = ctx.createLinearGradient(chart.plotLeft, 0, chart.plotRight, 0);
            line.addColorStop(0, lineStart);
            line.addColorStop(1, lineEnd);
            ctx.strokeStyle = line;
            ctx.lineWidth = 3;
            ctx.lineJoin = "round";
            ctx.lineCap = "round";
            ctx.stroke();

            const nowX = chart.xOf(chart.nowShown);
            ctx.strokeStyle = nowColour;
            ctx.lineWidth = 1.5;
            ctx.lineCap = "butt";
            for (let y = chart.plotTop; y < chart.plotBottom; y += 9) {
                ctx.beginPath();
                ctx.moveTo(nowX, y);
                ctx.lineTo(nowX, Math.min(chart.plotBottom, y + 4));
                ctx.stroke();
            }
        }
    }

    Repeater {
        model: Math.max(0, chart.draft.length - 2)

        Item {
            id: handle

            required property int index
            readonly property int point: index + 1
            readonly property var value: chart.draft[point] ?? ({ temperature: 0, percent: 0 })
            readonly property bool held: chart.dragging === point

            x: chart.xOf(value.temperature) - width / 2
            y: chart.yOf(value.percent) - height / 2
            width: 28
            height: 28
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("%1, point %2: %3 at %4").arg(chart.accessibleName).arg(point).arg(qsTr("%1%").arg(value.percent)).arg(Format.degrees(value.temperature))

            function nudge(temperature, percent) {
                chart.move(point, value.temperature + temperature, value.percent + percent);
                chart.edited(chart.draft);
            }
            Keys.onLeftPressed: nudge(-1, 0)
            Keys.onRightPressed: nudge(1, 0)
            Keys.onUpPressed: nudge(0, 1)
            Keys.onDownPressed: nudge(0, -1)

            Rectangle {
                anchors.centerIn: parent
                width: 15.5
                height: 15.5
                radius: width / 2
                color: UI.Style.ink
                scale: handle.held ? 1.15 : area.containsMouse ? 1.08 : 1
                Behavior on scale {
                    NumberAnimation { duration: UI.Style.motionFast }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 10.5
                    height: 10.5
                    radius: width / 2
                    color: UI.Style.windowOpaque
                }
            }

            UI.FocusRing {
                visible: handle.activeFocus && !handle.held
                anchors.margins: 2
                targetRadius: 12
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: handle.held ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                preventStealing: true
                onPressed: {
                    chart.dragging = handle.point;
                    handle.forceActiveFocus();
                }
                onPositionChanged: mouse => {
                    if (!pressed) {
                        return;
                    }
                    const at = mapToItem(chart, mouse.x, mouse.y);
                    const temperature = chart.coldest + (at.x - chart.plotLeft) / (chart.plotRight - chart.plotLeft) * (chart.hottest - chart.coldest);
                    const percent = (chart.plotBottom - at.y) / (chart.plotBottom - chart.plotTop) * 100;
                    chart.move(handle.point, temperature, percent);
                }
                onReleased: {
                    chart.dragging = -1;
                    chart.edited(chart.draft);
                }
                onCanceled: {
                    chart.dragging = -1;
                    chart.draft = chart.points.map(p => ({ temperature: p.temperature, percent: p.percent }));
                }
            }

            UI.Tooltip {
                text: qsTr("%1 at %2").arg(qsTr("%1%").arg(handle.value.percent)).arg(Format.degrees(handle.value.temperature))
                visible: handle.held || area.containsMouse
            }
        }
    }

    Rectangle {
        x: chart.xOf(chart.nowShown) - 6
        y: chart.yOf(chart.nowSpeed) - 6
        width: 12
        height: 12
        radius: 6
        color: Colours.liquid
    }

    Words {
        readonly property real after: chart.xOf(chart.nowShown) + 10
        x: after + width <= chart.plotRight ? after : chart.xOf(chart.nowShown) - 10 - width
        y: 24 - ascentBox
        text: chart.nowText.arg(Format.degrees(chart.nowTemperature)).arg(qsTr("%1%").arg(Math.round(chart.nowSpeed)))
        size: 12
        color: Colours.liquid
    }
}
