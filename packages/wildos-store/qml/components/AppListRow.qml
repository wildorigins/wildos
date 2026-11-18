import QtQuick
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.Store
import WildOS.UI as UI

// An app in the boards' three-column lists: an optional place number, the
// 56px icon, name and one line, and the app's button at the right. A right
// click gives the app's menu; `showMore` adds a button that opens it too.
T.AbstractButton {
    id: row

    property string appKey
    property int number: 0
    property bool divider: true
    property string note
    property bool showMore: false
    property string subtitle: app ? app.tagline : ""
    readonly property var app: Catalog.app(appKey)

    signal activated(string key)

    implicitHeight: Math.max(56, Math.ceil(texts.implicitHeight), Math.ceil(button.implicitHeight)) + 22
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: app ? app.name + ", " + subtitle : ""
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
        UI.Label {
            id: place
            visible: row.number > 0
            width: visible ? 22 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: row.number
            size: 16
            font.weight: UI.Style.weightBold
            font.features: { "tnum": 1 }
            color: Metrics.inkSecondary
        }

        AppIcon {
            id: icon
            x: place.visible ? place.width + 13 : 0
            anchors.verticalCenter: parent.verticalCenter
            size: 56
            source: row.app ? row.app.iconSource : ""
        }

        Column {
            id: texts
            anchors.left: icon.right
            anchors.leftMargin: 13
            anchors.right: actions.left
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
                visible: text.length > 0
                text: row.subtitle
                size: 13
                color: Metrics.inkSecondary
            }
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            StoreButton {
                id: button
                anchors.verticalCenter: parent.verticalCenter
                app: row.app
                note: row.note
            }

            UI.IconButton {
                id: more
                visible: row.showMore
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                radius: 14
                iconName: "more"
                iconSize: 15
                tip: qsTr("More options")
                onClicked: {
                    const window = row.Window.window;
                    if (window && window.showAppMenu) {
                        window.showAppMenu(row.appKey, more, more.width / 2, more.height);
                    }
                }
            }
        }
    }
}
