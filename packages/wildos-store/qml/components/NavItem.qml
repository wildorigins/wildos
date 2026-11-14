import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A section in the Store's sidebar: 34px, a 17px line icon, 14px name. The
// current one gets the accent wash, an accent icon and a bolder name.
// Updates carries a count; Library a small ring while apps install.
T.AbstractButton {
    id: item

    property string iconName
    property bool current: false
    property int badge: 0
    property bool working: false
    // The Wild OS mark in place of a line icon.
    property bool mark: false

    implicitHeight: Math.max(34, Math.ceil(label.implicitHeight) + 14)
    // Read by the window, which widens the sidebar to fit the longest name.
    implicitWidth: 10 + 17 + 11 + Math.ceil(label.implicitWidth) + 10 + (badge > 0 ? badgePill.width + 11 : working ? 29 : 0)
    width: parent ? parent.width : implicitWidth
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: badge > 0 ? text + ", " + badge : working ? text + ", installing" : text
    Accessible.selected: current

    background: Rectangle {
        radius: UI.Style.radiusSm
        color: item.current ? Metrics.navSelected
             : item.down ? Metrics.pill
             : item.hovered ? Metrics.rowHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: item.visualFocus
            targetRadius: UI.Style.radiusSm
        }
    }

    contentItem: Item {
        StoreIcon {
            id: icon
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            visible: !item.mark
            name: item.iconName
            size: 17
            strokeWidth: 1.8
            color: item.current ? UI.Style.accent : Metrics.inkSecondary
        }

        WildOSMark {
            visible: item.mark
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            size: 17
        }

        UI.Label {
            id: label
            anchors.left: icon.right
            anchors.leftMargin: 11
            anchors.right: badgePill.visible ? badgePill.left : ring.visible ? ring.left : parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            size: 14
            font.weight: item.current ? UI.Style.weightSemiBold : UI.Style.weightMedium
        }

        Rectangle {
            id: badgePill
            visible: item.badge > 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(20, badgeText.implicitWidth + 12)
            height: 20
            radius: 10
            color: UI.Style.accent

            UI.Label {
                id: badgeText
                anchors.centerIn: parent
                text: item.badge > 99 ? "99+" : item.badge
                size: 11.5
                font.weight: UI.Style.weightBold
                color: UI.Style.textOnAccent
            }
        }

        Ring {
            id: ring
            visible: item.working && !badgePill.visible
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            size: 18
            mode: "spin"
            centre: "stop"
        }
    }
}
