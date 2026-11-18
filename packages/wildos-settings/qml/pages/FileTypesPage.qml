import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Choose defaults by file type, after Windows 11: search for an extension
// such as .pdf or .docx, or a kind of file, and pick the app that opens it.
// Only apps that say they can open that type are offered.
PageScroll {
    id: page

    property var results: []

    function refresh() {
        results = DefaultApps.fileTypes(search.text);
    }

    // Searching every type on each letter would lag the field.
    Timer {
        id: searchTimer
        interval: 250
        onTriggered: page.refresh()
    }

    Connections {
        target: DefaultApps
        function onChanged() {
            page.refresh();
        }
    }

    UI.SearchField {
        id: search
        Layout.fillWidth: true
        Layout.maximumWidth: 420
        placeholderText: qsTr("Enter a file type, such as .pdf")
        onTextChanged: searchTimer.restart()
        Component.onCompleted: forceActiveFocus()
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                visible: page.results.length === 0
                iconName: "search"
                title: search.text.trim().length === 0 ? qsTr("Search for a file type") : qsTr("No file types match")
                description: search.text.trim().length === 0 ? qsTr("Type an extension, such as .docx or .mp3, or a kind of file, such as spreadsheet")
                    : qsTr("Check the spelling, or try the extension on its own")
            }

            Repeater {
                model: page.results

                UI.SettingRow {
                    id: row
                    required property var modelData
                    themeIcon: modelData.icon
                    title: modelData.extensions
                    description: modelData.apps.length === 0 ? qsTr("%1. No app installed can open these.").arg(modelData.description)
                        : modelData.description

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.modelData.apps.length > 0
                        Accessible.name: qsTr("Default app for %1").arg(row.modelData.extensions)
                        model: row.modelData.current.length === 0 ? [{ value: "", text: qsTr("Choose an app") }].concat(row.modelData.apps)
                            : row.modelData.apps.some(a => a.value === row.modelData.current) ? row.modelData.apps
                            : row.modelData.apps.concat([{ value: row.modelData.current, text: row.modelData.current.replace(/\.desktop$/, "") }])
                        readonly property var choice: row.modelData.current
                        currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                        onActivated: index => {
                            if (model[index].value.length > 0) {
                                DefaultApps.setDefaultForType(row.modelData.mime, model[index].value);
                            }
                        }
                    }
                }
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: page.results.length >= 40
        text: qsTr("Only the first 40 matches are shown. Type more of the name to find others.")
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }
}
