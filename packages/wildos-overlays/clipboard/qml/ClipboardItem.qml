pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI

// One copied thing (ClipboardEmoji.dc): a card, radius 10 on a 0.06 fill,
// 12px by 14px inside. Text up to three lines, a picture 70px tall, a path
// in a monospaced face; under it when and from where in 11px. On the right
// a pin (accent when pinned) above a menu.
T.AbstractButton {
    id: card

    required property var item
    required property bool pinned
    required property string meta
    required property string shownText
    property bool current: false

    signal pasteRequested()
    signal pinToggled()
    signal copyRequested()
    signal removeRequested()

    implicitHeight: Math.max(content.implicitHeight, side.implicitHeight) + 24
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.Button
    Accessible.name: (item.kind === "image" ? qsTr("Picture") : shownText) + ", " + meta + (pinned ? qsTr(", pinned") : "")

    onClicked: pasteRequested()

    background: Rectangle {
        radius: 10
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: card.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: card.down ? UI.Style.surfacePressed : card.hovered || card.current ? UI.Style.surfaceHover : UI.Style.surfaceControl

        UI.FocusRing {
            visible: card.current
            targetRadius: 10
        }
    }

    ColumnLayout {
        id: content
        x: 14
        y: 12
        width: card.width - 28 - side.width - 10
        spacing: 6

        UI.Label {
            Layout.fillWidth: true
            visible: card.item.kind !== "image"
            text: card.shownText.replace(/\s+/g, " ").trim()
            wrapMode: card.item.kind === "url" ? Text.NoWrap : Text.Wrap
            maximumLineCount: card.item.kind === "url" ? 1 : 3
            lineHeightMode: Text.FixedHeight
            lineHeight: Math.round(UI.Style.px(13) * 1.45)
            elide: card.item.kind === "url" ? Text.ElideMiddle : Text.ElideRight
            font.family: card.item.kind === "url" ? "monospace" : UI.Style.fontFamily
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            visible: card.item.kind === "image"
            radius: 6
            color: UI.Style.surfaceSunken
            clip: true

            Image {
                anchors.fill: parent
                source: card.item.kind === "image" ? card.item.image : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 720
                asynchronous: true
                smooth: true
            }
        }

        UI.Label {
            Layout.fillWidth: true
            text: card.meta
            size: 11
            color: UI.Style.inkPlaceholder
        }
    }

    Column {
        id: side
        x: card.width - 14 - width + 5
        y: 12 - 5
        spacing: -4

        UI.IconButton {
            width: 24
            height: 24
            radius: UI.Style.radiusXs
            glyph: "M12 17v4M8 3h8l-1 6 3 3v2H6v-2l3-3z"
            iconSize: 14
            stroke: 1.8
            iconColor: card.pinned ? UI.Style.accentText : UI.Style.inkPlaceholder
            tip: card.pinned ? qsTr("Unpin") : qsTr("Pin, to keep it after a restart")
            focusPolicy: Qt.NoFocus
            onClicked: card.pinToggled()
        }

        UI.IconButton {
            id: moreButton
            width: 24
            height: 24
            radius: UI.Style.radiusXs
            iconName: "more-vertical"
            iconSize: 14
            iconColor: UI.Style.inkAlpha(0.7, 0.66)
            tip: qsTr("More")
            focusPolicy: Qt.NoFocus
            onClicked: menu.popup(moreButton, moreButton.width - menu.width, moreButton.height + 4)

            UI.Menu {
                id: menu
                minimumWidth: 180

                UI.MenuItem {
                    text: qsTr("Copy only")
                    iconName: "copy"
                    onTriggered: card.copyRequested()
                }
                UI.MenuItem {
                    text: card.pinned ? qsTr("Unpin") : qsTr("Pin")
                    iconName: card.pinned ? "unpin" : "pin"
                    onTriggered: card.pinToggled()
                }
                UI.MenuSeparator {}
                UI.MenuItem {
                    text: qsTr("Delete")
                    iconName: "delete"
                    danger: true
                    onTriggered: card.removeRequested()
                }
            }
        }
    }
}
