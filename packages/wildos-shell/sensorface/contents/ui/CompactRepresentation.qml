/*
    SPDX-FileCopyrightText: 2019 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2019 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2019 Arjen Hiemstra <ahiemstra@heimr.nl>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import org.kde.ksysguard.sensors as Sensors
import org.kde.ksysguard.faces as Faces
import org.kde.ksysguard.formatter as Formatter

// KDE's Text Only face with each sensor's label drawn bold in the sensor's
// colour. Its LegendDelegate shows labels as plain text in the theme colour,
// so on the taskbar the arrows on Network Speed were hard to tell apart (#89).
Faces.CompactSensorFace {
    id: root

    Layout.minimumWidth: horizontalFormFactor ? Math.max(layout.preferredWidth, defaultMinimumSize) : defaultMinimumSize
    Layout.minimumHeight: verticalFormFactor ? Math.max(layout.implicitHeight, defaultMinimumSize) : defaultMinimumSize
    Layout.preferredWidth: horizontalFormFactor ? layout.preferredWidth : -1
    Layout.maximumWidth: horizontalFormFactor ? Math.max(layout.preferredWidth, defaultMinimumSize) : -1

    FontMetrics {
        id: defaultMetrics
        font: Kirigami.Theme.defaultFont
    }

    FontMetrics {
        id: smallMetrics
        font: Kirigami.Theme.smallFont
    }

    readonly property bool useSmallFont: (horizontalFormFactor && height < defaultMetrics.lineSpacing + 2) || (verticalFormFactor && width < defaultMetrics.averageCharacterWidth * 6 + Kirigami.Units.smallSpacing)
    readonly property real lineHeight: useSmallFont ? smallMetrics.lineSpacing : defaultMetrics.lineSpacing
    readonly property font textFont: useSmallFont ? Kirigami.Theme.smallFont : Kirigami.Theme.defaultFont

    clip: true

    contentItem: GridLayout {
        id: layout

        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: 1

        flow: GridLayout.TopToBottom
        rows: root.verticalFormFactor ? -1 : Math.max(Math.floor(root.height / (root.lineHeight + rowSpacing)), 1)

        property real preferredWidth: {
            if (repeater.count == 0) {
                return 0
            }

            const columns = Math.ceil(repeater.count / rows)
            let width = 0
            for (let i = 0; i < columns; ++i) {
                const item = repeater.itemAt(i)
                width += item ? item.implicitWidth : 0
            }
            width += (columns - 1) * columnSpacing
            return Math.ceil(width)
        }

        Repeater {
            id: repeater
            model: root.controller.highPrioritySensorIds

            RowLayout {
                id: entry

                required property string modelData

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                spacing: Kirigami.Units.smallSpacing

                readonly property string label: root.controller.sensorLabels[entry.modelData] || sensor.shortName
                readonly property color labelColor: root.colorSource.map[entry.modelData] || Kirigami.Theme.textColor
                readonly property bool arrow: label === "↓" || label === "↑"

                Text {
                    Layout.preferredHeight: root.lineHeight
                    verticalAlignment: Text.AlignVCenter
                    text: entry.label
                    textFormat: Text.PlainText
                    color: entry.labelColor
                    font: {
                        let boldFont = Qt.font(root.textFont)
                        boldFont.weight = Font.Black
                        return boldFont
                    }
                    visible: text.length > 0 && !entry.arrow
                }

                // The font's arrows are hairlines even at its heaviest
                // weight, so the up and down arrows are drawn (#89).
                Canvas {
                    id: arrowShape
                    visible: entry.arrow
                    readonly property real side: Math.round(root.lineHeight * 0.7)
                    Layout.preferredWidth: Math.round(side * 0.8)
                    Layout.preferredHeight: side
                    Layout.alignment: Qt.AlignVCenter
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = width, h = height
                        const stem = Math.max(2, Math.round(w * 0.36))
                        const head = Math.round(h * 0.5)
                        ctx.fillStyle = entry.labelColor
                        ctx.beginPath()
                        if (entry.label === "↓") {
                            ctx.moveTo((w - stem) / 2, 0)
                            ctx.lineTo((w + stem) / 2, 0)
                            ctx.lineTo((w + stem) / 2, h - head)
                            ctx.lineTo(w, h - head)
                            ctx.lineTo(w / 2, h)
                            ctx.lineTo(0, h - head)
                            ctx.lineTo((w - stem) / 2, h - head)
                        } else {
                            ctx.moveTo(w / 2, 0)
                            ctx.lineTo(w, head)
                            ctx.lineTo((w + stem) / 2, head)
                            ctx.lineTo((w + stem) / 2, h)
                            ctx.lineTo((w - stem) / 2, h)
                            ctx.lineTo((w - stem) / 2, head)
                            ctx.lineTo(0, head)
                        }
                        ctx.closePath()
                        ctx.fill()
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Connections {
                        target: entry
                        function onLabelColorChanged() { arrowShape.requestPaint() }
                        function onLabelChanged() { arrowShape.requestPaint() }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: Formatter.Formatter.maximumLength(sensor.unit, root.textFont)
                    Layout.preferredHeight: root.lineHeight
                    verticalAlignment: Text.AlignVCenter
                    // Next to its label rather than across the room from it;
                    // the column still keeps the width of the longest value,
                    // so the taskbar does not shift as speeds change.
                    horizontalAlignment: Text.AlignLeft
                    text: sensor.formattedValue
                    color: Kirigami.Theme.textColor
                    font: {
                        let tnumFont = Qt.font(root.textFont)
                        tnumFont.features = Object.assign({}, tnumFont.features, { "tnum": 1 })
                        return tnumFont
                    }
                }

                Sensors.Sensor {
                    id: sensor
                    sensorId: entry.modelData
                    updateRateLimit: root.controller.updateRateLimit
                }
            }
        }
    }
}
