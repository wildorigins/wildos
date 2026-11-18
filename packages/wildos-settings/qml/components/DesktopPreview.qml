import QtQuick
import QtQuick.Effects
import WildOS.UI as UI

// The current wallpaper with a window and the taskbar sketched on it, as at
// the top of Personalisation.dc.html. The sketch uses the real window and
// taskbar glass, so it follows the scheme too.
Rectangle {
    id: preview

    property string wallpaper: System.wallpaper
    // The window and taskbar drawn over the picture.
    property bool sketch: true

    implicitWidth: 320
    implicitHeight: 190
    radius: UI.Style.radiusMd
    color: UI.Style.surfaceSunken
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Current wallpaper, %1").arg(System.wallpaperName)

    Image {
        id: picture
        anchors.fill: parent
        source: System.thumbnail(preview.wallpaper)
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: 640
        asynchronous: true
        visible: false
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: preview.radius
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: picture
        maskEnabled: true
        maskSource: mask
    }

    Rectangle {
        visible: preview.sketch
        x: 70
        y: 30
        width: 150
        height: 95
        radius: UI.Style.radiusXs
        color: UI.Style.glassWindow
        border.width: 1
        border.color: UI.Style.windowEdge
    }

    Rectangle {
        visible: preview.sketch
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: 120
        height: 16
        radius: UI.Style.radiusXs
        color: UI.Style.glassBar
    }

    Rectangle {
        anchors.fill: parent
        radius: preview.radius
        color: "transparent"
        border.width: 1
        border.color: UI.Style.borderGlass
    }
}
