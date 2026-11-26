import QtQuick
import QtQuick.Effects
import org.kde.kwin as KWin
import org.kde.kirigami as Kirigami
import WildOS.UI as UI

// One window snap assist offers: icon and title above a live preview, drawn
// as the Alt+Tab switcher draws its windows so the two read as one family.
// The selected window wears the accent ring and a soft accent fill.
Item {
    id: card

    property var window: null
    property bool selected: false
    property int wellHeight: 150
    readonly property bool hovered: mouse.containsMouse

    signal activated()

    height: wellHeight + 52

    Accessible.role: Accessible.Button
    Accessible.name: window ? window.caption : ""
    Accessible.selected: selected
    Accessible.onPressAction: card.activated()

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusLg
        color: card.selected ? UI.Style.accentSoft
             : card.hovered ? UI.Style.surfaceHover
             : UI.Style.surfaceHoverClear
        border.width: 2
        border.color: card.selected ? UI.Style.accent : "transparent"

        Behavior on color {
            enabled: !card.selected
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: card.activated()
    }

    Row {
        id: header

        x: 12
        y: 12
        width: parent.width - 24
        height: 18
        spacing: 8

        Kirigami.Icon {
            id: appIcon
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            source: card.window ? card.window.icon : ""
        }

        UI.Label {
            width: header.width - appIcon.width - header.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: card.window ? card.window.caption : ""
            font.weight: UI.Style.weightMedium
        }
    }

    Rectangle {
        id: well

        x: 12
        y: header.y + header.height + 10
        width: parent.width - 24
        height: card.wellHeight
        radius: 10
        color: UI.Style.glassWindow

        // Stands in for the preview when there is none: windows KWin cannot
        // preview, and software rendering (virtual machines without 3D),
        // where KWin draws no thumbnails at all.
        Kirigami.Icon {
            anchors.centerIn: parent
            width: 48
            height: 48
            source: card.window ? card.window.icon : ""
            visible: thumbnail.implicitWidth <= 0 || GraphicsInfo.api === GraphicsInfo.Software
        }

        Item {
            id: preview

            anchors.fill: parent
            anchors.margins: 1
            visible: false
            clip: true
            layer.enabled: true

            // The whole window scaled down to fit the well and centred, never
            // cropped.
            KWin.WindowThumbnail {
                id: thumbnail

                readonly property real fitScale: implicitWidth > 0 && implicitHeight > 0
                    ? Math.min(preview.width / implicitWidth, preview.height / implicitHeight)
                    : 1

                wId: card.window ? card.window.internalId : ""
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
    }
}
