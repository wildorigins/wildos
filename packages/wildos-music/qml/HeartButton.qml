import QtQuick
import QtQuick.Templates as T
import WildOS.Music
import WildOS.UI as UI

// Like and unlike. A liked heart is filled in the board's pink; one not
// liked is an outline in the row's quiet ink.
T.Button {
    id: control

    property var paths: []
    property real iconSize: 15
    property real side: 24
    property color restColor: UI.Style.inkSubtle
    readonly property bool liked: Playlists.revision >= 0 && Playlists.allLiked(paths)

    implicitWidth: side
    implicitHeight: side
    padding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    enabled: paths.length > 0
    opacity: enabled ? 1 : 0.4
    Accessible.role: Accessible.CheckBox
    Accessible.checked: liked
    Accessible.name: liked ? qsTr("Remove from Liked songs") : qsTr("Add to Liked songs")
    onClicked: Playlists.setLikedAll(paths, !liked)

    background: Rectangle {
        radius: UI.Style.radiusSm
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.down ? UI.Style.surfacePressed : control.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        MusicIcon {
            anchors.centerIn: parent
            name: "heart"
            size: control.iconSize
            strokeWidth: control.iconSize >= 18 ? 1.8 : 2
            filled: control.liked
            color: control.liked ? MusicStyle.liked : control.hovered ? UI.Style.iconInk : control.restColor
        }
    }

    UI.Tooltip {
        text: control.Accessible.name
        visible: control.hovered && !control.down
    }
}
