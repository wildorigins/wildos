import QtQuick
import QtQuick.Layouts
import WildOS.Music
import WildOS.UI as UI

// An album, artist or song in a grid of covers (Home, Albums, Artists,
// Search): the cover, then its name and a quieter second line. Artists are
// round and centred. A play button rises over the cover under the pointer.
Item {
    id: tile

    property url cover
    property bool round: false
    property string placeholder: "album"
    property string title
    property string subtitle
    property real coverSize: 168

    signal clicked()
    signal playClicked()
    signal menuRequested(real x, real y)

    implicitWidth: coverSize
    implicitHeight: coverSize + 10 + titleLabel.implicitHeight + 2 + subtitleLabel.implicitHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: subtitle.length > 0 ? qsTr("%1, %2").arg(title).arg(subtitle) : title
    Accessible.onPressAction: tile.clicked()
    Keys.onReturnPressed: tile.clicked()
    Keys.onEnterPressed: tile.clicked()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            tile.menuRequested(tile.coverSize / 2, tile.coverSize / 2);
            event.accepted = true;
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -8
        radius: UI.Style.radiusMd
        color: mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        // On the plate itself, which already stands 8px clear of the cover.
        UI.FocusRing {
            anchors.margins: 0
            targetRadius: UI.Style.radiusMd - 4
            visible: tile.activeFocus
        }
    }

    Cover {
        id: art
        width: tile.coverSize
        height: tile.coverSize
        source: tile.cover
        round: tile.round
        radius: 10
        placeholder: tile.placeholder
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.margins: -8
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function (event) {
            if (event.button === Qt.RightButton) {
                tile.menuRequested(event.x - 8, event.y - 8);
            } else {
                tile.clicked();
            }
        }
    }

    // White in both schemes, like the big play button in the player bar.
    Rectangle {
        id: playDisc
        anchors.right: art.right
        anchors.bottom: art.bottom
        anchors.rightMargin: tile.round ? art.width * 0.1 : 10
        anchors.bottomMargin: tile.round ? art.width * 0.1 : 10
        width: 40
        height: 40
        radius: 20
        color: MusicStyle.playDisc
        opacity: mouse.containsMouse || playMouse.containsMouse ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionFast }
        }

        MusicIcon {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 1
            name: "play"
            size: 16
            strokeWidth: 2.4
            filled: true
            color: MusicStyle.playGlyph
        }

        MouseArea {
            id: playMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.playClicked()
        }
    }

    UI.Label {
        id: titleLabel
        anchors.top: art.bottom
        anchors.topMargin: 10
        width: tile.coverSize
        horizontalAlignment: tile.round ? Text.AlignHCenter : Text.AlignLeft
        text: tile.title
        size: 13.5
        font.weight: UI.Style.weightSemiBold
    }
    UI.Label {
        id: subtitleLabel
        anchors.top: titleLabel.bottom
        anchors.topMargin: 2
        width: tile.coverSize
        horizontalAlignment: tile.round ? Text.AlignHCenter : Text.AlignLeft
        text: tile.subtitle
        size: 12.5
        color: UI.Style.inkMuted
    }
}
