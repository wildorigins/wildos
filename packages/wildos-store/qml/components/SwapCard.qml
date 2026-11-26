import QtQuick
import QtQuick.Templates as T
import WildOS.Store
import WildOS.UI as UI

// "Instead of Photoshop": the Windows app on top, the free one that replaces
// it, and why it is a good swap, as in Store.dc.html.
T.AbstractButton {
    id: card

    property string appKey
    readonly property var app: Catalog.app(appKey)

    signal activated(string key)

    implicitHeight: column.implicitHeight + 32
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: app ? qsTr("Instead of %1, %2").arg(app.insteadOf).arg(app.name) : ""
    onClicked: activated(appKey)

    AppMenuArea {
        appKey: card.appKey
    }

    background: Rectangle {
        radius: Metrics.radiusCard
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: card.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: card.down ? Metrics.buttonFill : card.hovered ? Metrics.pill : Metrics.card

        UI.FocusRing {
            visible: card.visualFocus
            targetRadius: UI.Style.radiusLg
        }
    }

    contentItem: Item {
        Column {
            id: column
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 14

            Row {
                spacing: 8

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.app ? qsTr("Instead of %1").arg(card.app.insteadOf) : ""
                    size: 12
                    color: Metrics.inkSecondary
                    width: Math.min(implicitWidth, column.width - 20)
                }

                UI.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "arrow"
                    size: 12
                    strokeWidth: 2.2
                    color: Metrics.inkSecondary
                }
            }

            Row {
                width: parent.width
                spacing: 14

                AppIcon {
                    id: icon
                    size: 56
                    source: card.app ? card.app.iconSource : ""
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - icon.width - 14
                    spacing: 3

                    UI.Label {
                        width: parent.width
                        text: card.app ? card.app.name : ""
                        size: 14.5
                        font.weight: UI.Style.weightSemiBold
                    }

                    UI.Label {
                        width: parent.width
                        text: card.app ? card.app.tagline : ""
                        size: 13
                        color: Metrics.inkSecondary
                    }
                }
            }

            Item {
                width: parent.width
                height: action.height

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    width: parent.width - action.width - 12

                    UI.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "shield"
                        size: 13
                        strokeWidth: 2.4
                        color: UI.Style.accent
                    }

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 19
                        text: card.app ? card.app.cardNote : ""
                        size: 12
                        color: UI.Style.accentText
                    }
                }

                StoreButton {
                    id: action
                    anchors.right: parent.right
                    width: implicitWidth
                    app: card.app
                }
            }
        }
    }
}
