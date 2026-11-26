import QtQuick
import QtQuick.Effects
import org.kde.kwin as KWin
import org.kde.kirigami as Kirigami
import WildOS.UI as UI

// One window in the switcher: icon and title above a live preview.
// The selected window wears the accent ring and a soft accent fill.
Item {
    id: item

    property string title
    property var iconSource
    property var windowUuid
    property bool canClose: false
    property bool selected: false

    signal activated()
    signal closeRequested()

    readonly property bool hovered: mouse.containsMouse || closeMouse.containsMouse

    Accessible.role: Accessible.ListItem
    Accessible.name: title
    Accessible.selected: selected

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusLg
        color: item.selected ? UI.Style.accentSoft
             : item.hovered ? UI.Style.surfaceHover
             : UI.Style.surfaceHoverClear
        border.width: 2
        border.color: item.selected ? UI.Style.accent : "transparent"

        Behavior on color {
            enabled: !item.selected
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: item.activated()
    }

    Row {
        id: header

        x: 12
        y: 12
        width: parent.width - 24
        height: Math.max(18, implicitHeight)
        spacing: 8

        Kirigami.Icon {
            id: appIcon
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            source: item.iconSource
        }

        UI.Label {
            width: header.width - appIcon.width - header.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: item.title
            font.weight: UI.Style.weightMedium
        }
    }

    Rectangle {
        id: well

        x: 12
        y: header.y + header.height + 10
        width: parent.width - 24
        height: 150
        radius: 10
        color: UI.Style.glassWindow

        // Stands in for the preview when there is none: windows KWin cannot
        // preview, and software rendering (virtual machines without 3D),
        // where KWin draws no thumbnails at all.
        Kirigami.Icon {
            anchors.centerIn: parent
            width: 48
            height: 48
            source: item.iconSource
            visible: thumbnail.implicitWidth <= 0 || GraphicsInfo.api === GraphicsInfo.Software
        }

        Item {
            id: preview

            anchors.fill: parent
            anchors.margins: 1
            visible: false
            clip: true
            layer.enabled: true

            // The whole window, scaled down to fit the well and centred, as
            // Windows shows it: a tall or wide window is never cropped and
            // never reaches past its card.
            KWin.WindowThumbnail {
                id: thumbnail

                readonly property real fitScale: implicitWidth > 0 && implicitHeight > 0
                    ? Math.min(preview.width / implicitWidth, preview.height / implicitHeight)
                    : 1

                wId: item.windowUuid
                width: implicitWidth > 0 ? Math.floor(implicitWidth * fitScale) : preview.width
                height: implicitHeight > 0 ? Math.floor(implicitHeight * fitScale) : preview.height
                x: Math.round((preview.width - width) / 2)
                y: Math.round((preview.height - height) / 2)
            }
        }

        Rectangle {
            id: previewMask
            anchors.fill: preview
            radius: well.radius - 1
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: preview
            source: preview
            maskEnabled: true
            maskSource: previewMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Rectangle {
            anchors.fill: parent
            radius: well.radius
            color: "transparent"
            border.width: 1
            border.color: UI.Style.borderGlass
        }

        // Red under the pointer, like every other close button in Wild OS.
        Rectangle {
            id: closeButton

            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            width: 26
            height: 26
            radius: 13
            visible: item.canClose && item.hovered
            color: closeMouse.pressed ? UI.Style.closePressed
                 : closeMouse.containsMouse ? UI.Style.closeHover
                 : UI.Style.glassMenu
            border.width: 1
            border.color: closeMouse.containsMouse ? "transparent" : UI.Style.borderMenu

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Close %1").arg(item.title)

            UI.Icon {
                anchors.centerIn: parent
                name: "close"
                size: 12
                strokeWidth: 2
                color: closeMouse.containsMouse ? UI.Style.inkOnWallpaper : UI.Style.iconInk
            }

            MouseArea {
                id: closeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: item.closeRequested()
            }
        }
    }
}
