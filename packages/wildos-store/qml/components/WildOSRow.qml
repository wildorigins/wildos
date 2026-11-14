import QtQuick
import QtQuick.Templates as T
import WildOS.Store
import WildOS.UI as UI

// One of Wild OS's own apps in the Wild OS section (StoreWildOS.dc.html):
// the 52px icon, name and one line, and at the right Open, or Reinstall
// with the icon dimmed and "Removed" under it. Under the button: what the
// app takes up, "Reinstalling" while it comes back, or what went wrong.
T.AbstractButton {
    id: row

    property string appKey
    property bool divider: true
    readonly property var app: Catalog.app(appKey)
    readonly property bool working: !!app && (app.busy || app.queued)
    readonly property bool removed: !!app && !app.installed && !working
    readonly property string sizeText: Catalog.stateRevision >= 0 && app ? Catalog.sizeText(Catalog.appSize(appKey)) : ""

    signal activated(string key)

    implicitHeight: Math.max(52, Math.ceil(texts.implicitHeight), Math.ceil(button.implicitHeight)) + 20
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: app ? app.name + ", " + app.tagline + (removed ? ", removed" : "") : ""
    onClicked: activated(appKey)

    AppMenuArea {
        appKey: row.appKey
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -8
            anchors.rightMargin: -8
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            radius: UI.Style.radiusSm
            color: row.down ? Metrics.pill : row.hovered ? Metrics.rowHover : "transparent"
            Behavior on color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }

            UI.FocusRing {
                visible: row.visualFocus
                targetRadius: UI.Style.radiusSm
            }
        }

        Rectangle {
            visible: row.divider
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Metrics.divider
        }
    }

    contentItem: Item {
        AppIcon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            size: 52
            source: row.app ? row.app.iconSource : ""
            opacity: row.removed ? 0.45 : 1
            Behavior on opacity {
                NumberAnimation { duration: UI.Style.hoverDuration }
            }
        }

        Column {
            id: texts
            anchors.left: icon.right
            anchors.leftMargin: 13
            anchors.right: button.left
            anchors.rightMargin: 13
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            UI.Label {
                width: parent.width
                text: row.app ? row.app.name : ""
                size: 14.5
                font.weight: UI.Style.weightSemiBold
            }

            UI.Label {
                width: parent.width
                text: row.app ? row.app.tagline : ""
                size: 13
                color: Metrics.inkSecondary
            }
        }

        StoreButton {
            id: button
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            app: row.app
            getLabel: "Reinstall"
            note: row.working ? "Reinstalling" : row.removed ? "Removed" : row.sizeText
            noteColor: row.removed ? UI.Style.warning : Metrics.inkTertiary
        }
    }
}
