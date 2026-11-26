pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import WildOS.Terminal
import WildOS.UI as UI

// A colour scheme as a small live terminal: a prompt, a listing in the
// scheme's own colours and its sixteen colour palette, in the font and
// cursor the terminal uses. The chosen one takes the accent.
T.AbstractButton {
    id: card

    required property var scheme

    width: 236
    height: preview.height + label.height + 20
    padding: 0
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: scheme.name
    Accessible.checkable: true
    Accessible.checked: checked

    // The choice card's states (UI.ChoiceCard): accent tint and edge when
    // chosen, hover and press fills otherwise.
    background: Rectangle {
        radius: UI.Style.radiusMd
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: card.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: card.checked ? UI.Style.accentTint : card.down ? UI.Style.surfacePressed : card.hovered ? UI.Style.surfaceHover : UI.Style.surfaceRest
        border.width: card.checked ? 2 : 1
        border.color: card.checked ? UI.Style.accent : UI.Style.borderGlass

        UI.FocusRing {
            visible: card.visualFocus
            targetRadius: UI.Style.radiusMd
        }
    }

    readonly property var c: scheme.colours
    readonly property font mono: Qt.font({ family: Config.fontFamily, pixelSize: 12 })

    function span(text, colour, bold) {
        const safe = text.replace(/&/g, "&amp;").replace(/</g, "&lt;");
        return "<font color=\"" + colour + "\">" + (bold ? "<b>" + safe + "</b>" : safe) + "</font>";
    }

    Rectangle {
        id: preview
        x: 8
        y: 8
        width: parent.width - 16
        height: lines.implicitHeight + 24
        radius: UI.Style.radiusSm
        color: card.scheme.background
        border.width: 1
        border.color: UI.Style.borderSubtle

        Column {
            id: lines
            x: 10
            y: 10
            width: parent.width - 20
            spacing: 3

            Text {
                textFormat: Text.StyledText
                font: card.mono
                text: card.span("you@wildos", card.c[10], true) + card.span(":", card.scheme.foreground) + card.span("~", card.c[12], true) + card.span("$ ls", card.scheme.foreground)
            }

            Text {
                textFormat: Text.StyledText
                font: card.mono
                text: card.span("Documents", card.c[12], true) + "  " + card.span("notes.txt", card.scheme.foreground) + "  " + card.span("run.sh", card.c[10], true)
            }

            Row {
                spacing: 0

                Text {
                    textFormat: Text.StyledText
                    font: card.mono
                    text: card.span("you@wildos", card.c[10], true) + card.span(":", card.scheme.foreground) + card.span("~", card.c[12], true) + card.span("$ ", card.scheme.foreground)
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Config.cursorShape === "block" ? 7 : Config.cursorShape === "underline" ? 7 : 2
                    height: Config.cursorShape === "underline" ? 2 : 14
                    anchors.verticalCenterOffset: Config.cursorShape === "underline" ? 6 : 0
                    color: card.scheme.foreground
                }
            }

            Item {
                width: 1
                height: 4
            }

            Grid {
                columns: 8
                spacing: 3

                Repeater {
                    model: 16

                    Rectangle {
                        required property int index
                        width: (lines.width - 7 * 3) / 8
                        height: 8
                        radius: 2
                        color: card.c[index]
                    }
                }
            }
        }
    }

    Row {
        id: label
        x: 12
        anchors.top: preview.bottom
        anchors.topMargin: 6
        height: Math.max(26, UI.Style.px(13) + 10)
        spacing: 8

        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            text: card.scheme.name
            font.weight: card.checked ? UI.Style.weightSemiBold : UI.Style.weightRegular
        }

        UI.Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: card.scheme.id === "Homebrew"
            text: qsTr("Default")
            size: 12
            color: UI.Style.inkMuted
        }
    }

    UI.Icon {
        visible: card.checked
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: label.verticalCenter
        name: "check"
        size: 16
        strokeWidth: 2.4
        color: UI.Style.accent
    }
}
