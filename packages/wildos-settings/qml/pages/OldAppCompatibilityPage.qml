import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Old app compatibility: how much older apps may see of what you type in
// other apps, the keyboard half of KDE's Legacy X11 App Support. Each choice
// writes kwinrc and asks KWin to reload, so it applies at once.
PageScroll {
    id: page

    readonly property var choices: [
        { level: 0, title: qsTr("Never"), description: qsTr("Older apps only see what you type into them. Recommended.") },
        { level: 1, title: qsTr("Ctrl, Alt, Shift and the Windows key"), description: qsTr("Lets them notice those keys pressed on their own") },
        { level: 2, title: qsTr("Shortcuts"), description: qsTr("Also keys pressed while holding Ctrl, Alt or the Windows key, for global shortcuts") },
        { level: 3, title: qsTr("Everything"), description: qsTr("Every key you press, in any app. Only for an app you fully trust.") }
    ]

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Older apps (built for X11) can be allowed to see keys you press while you use other apps, which a few need for a shortcut such as push to talk, but it also lets them see what you type, passwords included.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Let older apps see keys typed in other apps")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.choices

                    UI.SettingRow {
                        id: choice
                        required property var modelData
                        readonly property bool selected: OlderApps.keys === modelData.level
                        title: modelData.title
                        description: modelData.description
                        descriptionColor: modelData.level === 3 ? UI.Style.warning : UI.Style.inkMuted
                        interactive: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: selected
                        onClicked: OlderApps.keys = modelData.level

                        leading: UI.RadioButton {
                            checked: choice.selected
                            focusPolicy: Qt.NoFocus
                            Accessible.ignored: true
                            onClicked: OlderApps.keys = choice.modelData.level
                        }
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "mouse"
                title: qsTr("Mouse buttons too")
                description: OlderApps.keys === 0 ? qsTr("Choose something other than Never first")
                    : qsTr("Lets older apps see mouse clicks in other apps as well")
                status: OlderApps.mouse ? qsTr("On") : qsTr("Off")
                enabled: OlderApps.keys > 0

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: OlderApps.mouse
                    Accessible.name: qsTr("Mouse buttons too")
                    onToggled: OlderApps.mouse = checked
                }
            }

            UI.SettingRow {
                iconName: "display"
                title: qsTr("Screen")
                description: qsTr("Older apps only see their own windows and those of other older apps. Seeing the rest of your screen needs your permission each time.")
            }
        }
    }
}
