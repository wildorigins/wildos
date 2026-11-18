import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Default apps, after Windows 11: one row per kind of thing, each with the
// app that opens it. Only apps installed and able to open that kind of file
// are offered.
PageScroll {
    id: page

    Component.onCompleted: DefaultApps.reload()

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            Repeater {
                model: DefaultApps.categories

                UI.SettingRow {
                    id: row
                    required property var modelData
                    iconName: modelData.icon
                    title: modelData.title
                    description: modelData.apps.length === 0 ? qsTr("No app installed can open these") : modelData.description

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.modelData.apps.length > 0
                        Accessible.name: row.modelData.title
                        model: row.modelData.apps.some(a => a.value === row.modelData.current) || row.modelData.current.length === 0
                            ? row.modelData.apps
                            : row.modelData.apps.concat([{ value: row.modelData.current, text: row.modelData.current.replace(/\.desktop$/, "") }])
                        readonly property var choice: row.modelData.current
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => DefaultApps.setDefault(row.modelData.id, model[index].value)
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
                iconName: "document"
                title: qsTr("Choose defaults by file type")
                description: qsTr("Pick the app for .pdf, .docx and every other kind of file")
                chevron: true
                onClicked: Navigator.open("filetypes")
            }

            UI.SettingRow {
                visible: System.storeApp.length > 0
                iconName: "store"
                title: qsTr("Get more apps")
                description: qsTr("Find another browser, music player or photo app in the Store")
                chevron: true
                onClicked: System.openStore()
            }
        }
    }
}
