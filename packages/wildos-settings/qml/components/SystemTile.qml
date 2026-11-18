import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import WildOS.UI as UI

// The 40px tile beside each system on System > Start-up disk
// (SettingsStartupDisk.dc): the Wild OS logo on its dark tile, the Windows
// logo on blue, and a disk or a USB stick on a neutral tile for anything
// else. Every tile has the board's gloss, pale inner edge and soft shadow.
Item {
    id: tile

    // "wildos", "windows", "linux" or "usb".
    property string kind

    readonly property var fills: ({
        "wildos": { from: "#262b4a", to: "#10121f", end: 0.7 },
        "windows": { from: "#58b6ff", to: "#1565d8", end: 1 }
    })
    readonly property var fill: fills[kind] || { from: "#6d7484", to: "#3b404c", end: 1 }

    implicitWidth: 40
    implicitHeight: 40

    Item {
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.55)
            shadowVerticalOffset: 5
            shadowBlur: 1.0
            blurMax: 12
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeWidth: -1
                fillGradient: LinearGradient {
                    // The board's linear-gradient(150deg) across 40 by 40.
                    x1: 6.34; y1: -3.66; x2: 33.66; y2: 43.66
                    GradientStop { position: 0; color: tile.fill.from }
                    GradientStop { position: tile.fill.end; color: tile.fill.to }
                }
                PathRectangle { width: 40; height: 40; radius: 9 }
            }
        }

        // The Wild OS mark at 25px, in the logo's own blue to rose.
        Item {
            anchors.centerIn: parent
            visible: tile.kind === "wildos"
            width: 25
            height: 25

            Shape {
                width: 150
                height: 150
                preferredRendererType: Shape.CurveRenderer
                transform: [
                    Translate { x: -18; y: 10.5 },
                    Scale { xScale: 25 / 150; yScale: 25 / 150 }
                ]

                ShapePath {
                    strokeWidth: -1
                    fillGradient: LinearGradient {
                        x1: 0; y1: 17; x2: 0; y2: 113
                        GradientStop { position: 0; color: "#b4caff" }
                        GradientStop { position: 0.5; color: "#7d97ec" }
                        GradientStop { position: 1; color: "#f0a3ae" }
                    }
                    PathSvg {
                        path: "M37 17 L50.34 40 L64.75 64.84 L79.16 40 L92.5 17 L105.84 40 L134.26 89 L120.76 112.28 L92.5 63.55 L64.91 111.12 L23.66 40Z M148.5 17 L162 40 L148.5 63 L135 40Z"
                    }
                }
            }
        }

        // The Windows logo: four equal panes with a small gap.
        Grid {
            anchors.centerIn: parent
            visible: tile.kind === "windows"
            columns: 2
            spacing: 1.5

            Repeater {
                model: 4

                Rectangle {
                    width: 8.5
                    height: 8.5
                    color: "#ffffff"
                }
            }
        }

        PathIcon {
            anchors.centerIn: parent
            visible: tile.kind !== "wildos" && tile.kind !== "windows"
            name: tile.kind === "usb" ? "usb" : "hard-drive"
            size: 20
            strokeWidth: 1.8
            color: "#ffffff"
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeWidth: -1
                fillGradient: LinearGradient {
                    // The board's gloss, linear-gradient(160deg), gone by 42%.
                    x1: 11.23; y1: -4.1; x2: 28.77; y2: 44.1
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.28) }
                    GradientStop { position: 0.42; color: Qt.rgba(1, 1, 1, 0) }
                }
                PathRectangle { width: 40; height: 40; radius: 9 }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 9
            color: "transparent"
            border.width: 1.5
            border.color: Qt.rgba(1, 1, 1, 0.20)
        }
    }
}
