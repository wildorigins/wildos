import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Accounts, after Windows 11: who is signed in, then Your info, Sign-in
// options and Other users.
PageScroll {
    id: page

    UI.Card {
        Layout.fillWidth: true
        implicitHeight: 112

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 20

            UI.Avatar {
                size: 72
                source: System.userAvatar
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: System.userFullName
                    size: 18
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Local account, %1").arg(System.userName)
                    color: UI.Style.inkMuted
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: Users.me.admin ? qsTr("Administrator") : qsTr("Standard user")
                    size: 12
                    color: UI.Style.accentText
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Account settings")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "account"
                    title: qsTr("Your info")
                    description: qsTr("Your name and picture")
                    chevron: true
                    onClicked: Navigator.open("users")
                }

                UI.SettingRow {
                    iconName: "lock"
                    title: qsTr("Sign-in options")
                    description: qsTr("Password, automatic sign-in and screen lock")
                    chevron: true
                    onClicked: Navigator.open("signin")
                }

                UI.SettingRow {
                    iconName: "people"
                    title: qsTr("Other users")
                    description: Users.others.length === 0 ? qsTr("Add the people who share this computer")
                        : Users.others.length === 1 ? qsTr("1 other person uses this computer")
                        : qsTr("%1 other people use this computer").arg(Users.others.length)
                    chevron: true
                    onClicked: Navigator.open("otherusers")
                }
            }
        }
    }
}
