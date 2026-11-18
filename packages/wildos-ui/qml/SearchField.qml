import QtQuick

// The design's search box: a sunken field with the magnifier at its right
// end (Settings.dc 36px, Main.dc and FilesDetails.dc 34px, both radius 8)
// and a clear button once something is typed. `pill: true` gives the Store
// and Start shape (Store.dc radius 17, magnifier on the left). From
// wildos-settings SearchField, with the Store SearchBox's clear button and
// Escape behaviour: the first Escape clears, the second is passed on.
// `iconLeading` puts the magnifier on the left of the square field too (the
// emoji panel's search, ClipboardEmoji.dc: 34px, 12px in, text 8px after).
TextField {
    id: field

    property bool pill: false
    property bool showIcon: true
    property bool iconLeading: pill
    // Start's larger pill draws a 17px magnifier 16px in (Start.dc).
    property real iconSize: pill ? 15 : 14
    property real iconInset: pill ? 14 : 12

    signal escapePressed()
    signal downPressed()

    escapeClears: false
    implicitHeight: Math.max(Style.controlHeight, contentHeight + 16)
    leftPadding: iconLeading && showIcon ? (pill ? 38 : iconInset + iconSize + 8) : 12
    rightPadding: 12 + (!iconLeading && showIcon ? 22 : 0) + (text.length > 0 ? 26 : 0)
    Accessible.role: Accessible.EditableText

    background: Rectangle {
        radius: field.pill ? field.height / 2 : Style.radiusSm
        color: Style.surfaceSunken
        border.width: 1
        border.color: field.editingNow ? Style.accentBorderStrong : Style.borderField
        Behavior on border.color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    Keys.onEscapePressed: event => {
        if (text.length > 0) {
            clear();
        } else {
            escapePressed();
        }
        event.accepted = true;
    }
    Keys.onDownPressed: downPressed()

    Icon {
        visible: field.showIcon
        x: field.iconLeading ? field.iconInset : field.width - field.iconInset - size
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: field.iconSize
        strokeWidth: 2
        color: Style.iconMuted
    }

    IconButton {
        visible: field.text.length > 0
        x: field.width - width - (field.iconLeading || !field.showIcon ? 6 : 32)
        anchors.verticalCenter: parent.verticalCenter
        width: 24
        height: 24
        radius: field.pill ? 12 : Style.radiusXs
        iconName: "close"
        iconSize: 12
        stroke: 2.2
        tip: qsTr("Clear")
        focusPolicy: Qt.NoFocus
        onClicked: {
            field.clear();
            field.forceActiveFocus();
        }
    }
}
