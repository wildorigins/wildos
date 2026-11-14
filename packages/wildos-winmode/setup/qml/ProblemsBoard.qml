import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// Board 9.7 as drawn, for design reviews only (--preview problems): the
// three problem cards side by side over the wallpaper.
Rectangle {
    id: board

    property string backdrop

    color: UI.Style.lakeNavy

    Image {
        anchors.fill: parent
        source: board.backdrop
        fillMode: Image.PreserveAspectCrop
    }

    Rectangle {
        anchors.fill: parent
        color: UI.Style.wallpaperDim
    }

    Column {
        x: 60
        y: 60
        width: 900
        spacing: 4

        UI.Label {
            text: "When setup can't go ahead"
            size: 15
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkOnWallpaper
        }

        UI.Label {
            width: parent.width
            text: "Each problem says what's wrong in plain words, what to do about it, and offers the one button that helps. Nothing is ever left half installed."
            size: 13
            color: UI.Style.inkOnWallpaper
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    Row {
        x: 60
        y: 170
        spacing: 30

        ProblemCard {
            kind: "virtualisation"
        }

        ProblemCard {
            kind: "space"
        }

        ProblemCard {
            kind: "download"
            offline: true
            received: 3.8e9
        }
    }
}
