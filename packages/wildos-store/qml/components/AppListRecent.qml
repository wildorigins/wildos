import QtQuick
import QtQuick.Templates as T
import WildOS.Store
import WildOS.UI as UI

// A row under "Updated recently": the 40px icon, the name, when it was
// updated and the app's button.
T.AbstractButton {
    id: row

    property string appKey
    readonly property var app: Catalog.app(appKey)

    signal activated(string key)

    implicitHeight: Math.max(40, Math.ceil(name.implicitHeight), button.implicitHeight) + 22
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: app ? qsTr("%1, updated %2").arg(app.name).arg(app.updatedText) : ""
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
            radius: UI.Style.radiusSm
            // Fades in on hover and clears at once, so moving along a list
            // never lights two items. It comes before the colour so it sees
            // the change in hover first.
            Behavior on color {
                enabled: row.hovered
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
            color: row.down ? Metrics.pill : row.hovered ? Metrics.rowHover : "transparent"

            UI.FocusRing {
                visible: row.visualFocus
                targetRadius: UI.Style.radiusSm
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Metrics.divider
        }
    }

    contentItem: Item {
        AppIcon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            size: 40
            source: row.app ? row.app.iconSource : ""
        }

        UI.Label {
            id: name
            anchors.left: icon.right
            anchors.leftMargin: 14
            anchors.right: when.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: row.app ? row.app.name : ""
            size: 14
            font.weight: UI.Style.weightSemiBold
        }

        UI.Label {
            id: when
            anchors.right: button.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 110
            text: row.app ? row.app.updatedText : ""
            size: 13
            color: Metrics.inkTertiary
        }

        StoreButton {
            id: button
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            app: row.app
        }
    }
}
