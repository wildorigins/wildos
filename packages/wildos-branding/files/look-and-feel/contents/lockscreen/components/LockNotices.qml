import QtQuick
import org.kde.kirigami as Kirigami
import WildOS.UI as UI

import ".."

// What arrived while the screen was locked, under the clock. With "Quiet lock
// screen" on, one glass pill: the apps' icons and how many arrived. With it
// off, the newest as cards with the app, title and text, as many as fit above
// the sign-in block, then how many more.
Item {
    id: notices

    property Item backdrop: null
    // The room left between the clock and the sign-in block.
    property real availableHeight: 0

    readonly property int count: LockNotifications.arrived.count
    readonly property int cardHeight: 72
    readonly property int cardsShown: LockNotifications.quiet ? 0
        : Math.max(1, Math.min(3, count, Math.floor((availableHeight - 24 + 8) / (cardHeight + 8))))

    implicitWidth: 400
    implicitHeight: count === 0 ? 0 : LockNotifications.quiet ? 40 : content.implicitHeight
    visible: count > 0

    Accessible.role: Accessible.StaticText
    Accessible.name: summaryText

    readonly property string summaryText: count === 1 ? "1 new notification" : count + " new notifications"

    Item {
        id: pill
        visible: LockNotifications.quiet
        anchors.horizontalCenter: parent.horizontalCenter
        width: pillRow.implicitWidth + 28
        height: 40

        Glass {
            anchors.fill: parent
            backdrop: notices.backdrop
            tint: UI.Style.glassOnWallpaper
            blurRadius: 64
        }

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 8

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Repeater {
                    model: LockNotifications.appIcons
                    Kirigami.Icon {
                        required property string modelData
                        width: 18
                        height: 18
                        source: modelData
                        fallback: "preferences-desktop-notification-bell"
                    }
                }
            }

            UI.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: notices.summaryText
                color: UI.Style.inkOnWallpaper
            }
        }
    }

    Column {
        id: content
        visible: !LockNotifications.quiet
        width: parent.width
        spacing: 8

        Repeater {
            model: LockNotifications.arrived

            Item {
                id: card

                required property int index
                required property string appName
                required property string iconName
                required property string summary
                required property string body

                visible: index < notices.cardsShown
                width: content.width
                height: visible ? notices.cardHeight : 0

                Glass {
                    anchors.fill: parent
                    backdrop: notices.backdrop
                    tint: UI.Style.glassOnWallpaperField
                    radius: 18
                    blurRadius: 64
                }

                Kirigami.Icon {
                    id: appIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.top: parent.top
                    anchors.topMargin: 14
                    width: 20
                    height: 20
                    source: card.iconName
                    fallback: "preferences-desktop-notification-bell"
                }

                Column {
                    anchors.left: appIcon.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1

                    UI.Label {
                        width: parent.width
                        visible: text.length > 0
                        text: card.appName
                        size: 12
                        color: UI.Style.inkOnWallpaperMuted
                    }
                    UI.Label {
                        width: parent.width
                        visible: text.length > 0
                        text: card.summary
                        font.weight: UI.Style.weightSemiBold
                        color: UI.Style.inkOnWallpaper
                    }
                    UI.Label {
                        width: parent.width
                        visible: text.length > 0
                        // The server has already cut the body down to the
                        // few tags notifications may use.
                        textFormat: Text.StyledText
                        text: card.body
                        size: 12
                        color: UI.Style.inkOnWallpaper
                    }
                }
            }
        }

        UI.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: notices.count > notices.cardsShown
            text: notices.count - notices.cardsShown + " more"
            size: 12
            color: UI.Style.inkOnWallpaperMuted
        }
    }
}
