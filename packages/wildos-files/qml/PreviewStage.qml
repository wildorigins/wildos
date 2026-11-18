import QtQuick
import QtQuick.Effects
import QtQuick.Window
import WildOS.Files
import WildOS.UI as UI

// Development only (WILDOS_FILES_PREVIEW=1): Files drawn over the Wild OS
// wallpaper at the artboard's size and place, with the blur and shadow the
// compositor adds on a real desktop, so screenshots compare one to one
// with Main.dc.html and FilesDetails.dc.html.
Window {
    id: stage

    property var startUrls: []

    width: 1440
    height: 900
    visible: true
    color: UI.Style.lakeNavy
    title: qsTr("Files preview")

    Image {
        id: wallpaper
        anchors.fill: parent
        source: "file:///usr/share/wallpapers/WildOS/contents/images/misty-lake.jpg"
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignVCenter
    }

    Rectangle {
        id: shape
        width: view.width
        height: view.height
        radius: UI.Style.windowRadius
        visible: false
        layer.enabled: true
    }

    // What KWin's blur effect does behind the window: the wallpaper under
    // it, blurred, cut to the window's rounded shape.
    Item {
        x: view.x
        y: view.y
        width: view.width
        height: view.height
        clip: true
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: shape
        }

        MultiEffect {
            x: -view.x
            y: -view.y
            width: stage.width
            height: stage.height
            source: wallpaper
            blurEnabled: true
            blur: 1.0
            blurMax: 64
            saturation: 0.3
        }
    }

    // The window's glass sheet, which UI.AppWindow draws in the real app.
    Rectangle {
        x: view.x - 1
        y: view.y - 1
        width: view.width + 2
        height: view.height + 2
        radius: UI.Style.windowRadius
        color: UI.Style.glassWindow
        border.width: 1
        border.color: UI.Style.windowEdge
    }

    FilesView {
        id: view
        x: 110
        y: 54
        width: 980
        height: 620
        startUrls: stage.startUrls
        Component.onCompleted: FileOps.activeWindow = stage
    }
}
