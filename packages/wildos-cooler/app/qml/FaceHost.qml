import QtQuick
import WildOS.Cooler.Faces

// A face from WildOS.Cooler.Faces at this item's size. Only FacePreview
// loads this file, and only when that module is built into the window.
Item {
    id: host

    property alias faceId: view.faceId
    property alias time: view.time
    property alias readings: view.readings
    property alias showMark: view.showMark
    // "Use my theme colours", with the theme's accent and whether that is
    // Glacier. Which colours a face then gets is the faces' own rule.
    property bool useTheme: false
    property color accent
    property bool glacier: true

    FaceView {
        id: view
        anchors.fill: parent
        themeColours: host.useTheme ? Faces.themeColours(host.accent, host.glacier) : null
    }
}
