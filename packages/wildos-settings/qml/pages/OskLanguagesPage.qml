import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// The on-screen keyboard's languages, reached from Accessibility and from
// the keyboard itself (its language key's menu, Settings). They start as the
// languages of the keyboard layouts; the first change here makes the list
// the person's own, and a layout added later only adds its language.
PageScroll {
    id: page

    property var languages: Accessibility.oskLanguages()
    readonly property int onCount: languages.filter(l => l.on).length

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Choose the languages the on-screen keyboard types in. Its language key switches between them.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Languages")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.languages

                    UI.SettingRow {
                        id: row
                        required property var modelData
                        minHeight: 44
                        title: modelData.text
                        description: modelData.english !== modelData.text ? modelData.english : ""

                        leading: UI.CheckBox {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: row.modelData.on
                            // The keyboard always keeps one language.
                            enabled: !row.modelData.on || page.onCount > 1
                            Accessible.name: row.modelData.text
                            onToggled: page.languages = Accessibility.setOskLanguage(row.modelData.value, checked)
                        }
                    }
                }
            }
        }
    }
}
