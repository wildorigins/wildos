import QtQuick

// One face by its id, at whatever size this item is given. An id that is
// not in Faces shows nothing.
Item {
    id: view

    property string faceId: "liquid"
    property real time: 0
    property var readings: null
    property bool showMark: true
    property var themeColours: null
    property bool circular: true

    readonly property bool ready: loader.status === Loader.Ready

    implicitWidth: 640
    implicitHeight: 640

    Loader {
        id: loader
        anchors.fill: parent
        source: Faces.source(view.faceId)
    }

    Binding { target: loader.item; property: "time"; value: view.time; when: view.ready }
    Binding { target: loader.item; property: "readings"; value: view.readings; when: view.ready }
    Binding { target: loader.item; property: "showMark"; value: view.showMark; when: view.ready }
    Binding { target: loader.item; property: "themeColours"; value: view.themeColours; when: view.ready }
    Binding { target: loader.item; property: "circular"; value: view.circular; when: view.ready }
}
