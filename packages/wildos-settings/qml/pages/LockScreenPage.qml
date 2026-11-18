import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Lock screen: the picture behind the sign-in panel, written to
// kscreenlockerrc, and a way into when the screen locks.
PageScroll {
    id: page

    RowLayout {
        Layout.fillWidth: true
        spacing: 20

        Preview {
            Layout.preferredWidth: 320
            Layout.preferredHeight: 190
            wallpaper: System.lockWallpaper
            lockScreen: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Lock screen picture")
                size: 15
                font.weight: UI.Style.weightSemiBold
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Shown behind the clock when your computer is locked.")
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            UI.Button {
                Layout.topMargin: 6
                visible: System.lockWallpaper !== System.wallpaper
                text: qsTr("Use my desktop background")
                onClicked: System.setLockWallpaper(System.wallpaper)
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Pictures")
        }

        WallpaperGrid {
            current: System.lockWallpaper
            onPicked: path => System.setLockWallpaper(path)
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "lock"
                title: qsTr("Screen lock")
                description: qsTr("When the screen locks and asks for your password")
                chevron: true
                onClicked: Navigator.open("screenlock")
            }
        }
    }
}
