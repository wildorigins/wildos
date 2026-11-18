import QtQuick
import QtQuick.Effects
import WildOS.UI as UI

// The date over a large time, as on the lock screen, both in the regional
// format and the clock chosen in Settings (the sign-in screen, which cannot
// read a person's settings, keeps the regional format's own); the time
// leaves out AM and PM, as Windows does here. The time
// box is exactly 136px tall like the design's line-height: 1, so the glyphs
// are shifted up by half the font's extra leading.
Item {
    id: clock

    property date now: new Date()

    readonly property string dateFormat: Qt.locale().dateFormat(Locale.LongFormat).replace(/[,.\s]*y+[.年]?/, "").trim()
        .replace(/^dddd, d MMMM$/, "dddd d MMMM")

    implicitWidth: Math.max(dateLabel.implicitWidth, timeLabel.implicitWidth)
    implicitHeight: dateLabel.height + 6 + timeBox.height

    Timer {
        // Ticking every second keeps the minute change on time without
        // having to align a longer timer to the wall clock.
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: clock.now = new Date()
    }

    FontMetrics {
        id: timeMetrics
        font: timeLabel.font
    }

    Item {
        id: content
        anchors.fill: parent

        Text {
            id: dateLabel
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.locale(qsTr("en_GB", "locale for day and month names")).toString(clock.now, clock.dateFormat)
            font.family: UI.Style.fontFamily
            font.letterSpacing: UI.Style.tracking(UI.Style.px(22))
            font.pixelSize: UI.Style.px(22)
            font.weight: UI.Style.weightRegular
            color: UI.Style.inkOnWallpaper
        }

        Item {
            id: timeBox
            anchors.top: dateLabel.bottom
            anchors.topMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            width: timeLabel.implicitWidth
            height: 136

            Text {
                id: timeLabel
                anchors.horizontalCenter: parent.horizontalCenter
                y: (136 - timeMetrics.height) / 2
                text: UI.TimeFormat.withoutMarker(clock.now)
                font.family: UI.Style.fontFamily
                renderType: Text.CurveRendering
                font.pixelSize: 136
                font.weight: UI.Style.weightLight
                font.letterSpacing: -0.04 * 136
                color: UI.Style.inkOnWallpaper
            }
        }

        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: UI.Style.textShadowOnWallpaper
            shadowBlur: 1.0
            blurMax: 48
            shadowVerticalOffset: 2
            shadowHorizontalOffset: 0
        }
    }
}
