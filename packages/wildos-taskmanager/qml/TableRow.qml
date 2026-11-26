import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A row of the Startup apps, Windows apps and Services tables: the
// Processes rows' hover (0.05), selection (0.08) and 0.04 hairline, 44px so
// a switch fits. Put the cells in its default slot.
T.ItemDelegate {
    id: row

    property bool selected: false
    // The selected row while the keyboard is what moves the selection.
    property bool focused: false
    default property alias cells: content.data

    signal menuRequested(point at)

    implicitHeight: 44
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.Row

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => {
            row.clicked();
            row.menuRequested(eventPoint.position);
        }
    }

    background: Rectangle {
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: row.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: row.selected ? UI.Style.tone(0.08) : row.hovered ? UI.Style.surfaceRaised : Qt.alpha(UI.Style.surfaceRaised, 0)

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: UI.Style.tone(0.04)
        }

        UI.FocusRing {
            visible: row.focused
            anchors.margins: 2
            targetRadius: 4
        }
    }

    contentItem: Row {
        id: content
    }
}
