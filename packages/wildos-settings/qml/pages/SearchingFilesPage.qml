import QtQuick
import QtCore
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Searching files, after Windows 11's "Searching Windows": whether files
// are indexed so Start and Files find them at once, whether what is inside
// them is read too, which folders are searched and which are left out.
// Everything stays on this computer.
PageScroll {
    id: page

    property string error

    Connections {
        target: FileSearch
        function onFailed(reason) {
            page.error = reason.length > 0 ? reason : qsTr("The index was not rebuilt.");
        }
    }

    Component.onCompleted: FileSearch.reload()

    StatusCard {
        visible: !FileSearch.available
        iconName: "search"
        warning: true
        title: qsTr("File indexing isn't available")
        description: qsTr("The indexer is not installed on this computer. Files can still search a folder as you type, it just takes longer.")
    }

    UI.Label {
        Layout.fillWidth: true
        visible: FileSearch.available
        text: qsTr("Indexing keeps a list of your files so Start and Files can find them straight away. The index never leaves this computer.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true
        visible: FileSearch.available

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "search"
                title: qsTr("Index my files")
                description: qsTr("Find files by name and content as soon as you type")
                status: FileSearch.enabled ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: FileSearch.enabled
                    Accessible.name: qsTr("Index my files")
                    onToggled: FileSearch.enabled = checked
                }
            }

            UI.SettingRow {
                iconName: "document-lines"
                title: qsTr("What to index")
                description: qsTr("Reading inside files finds words in documents too, and uses more disk space")
                enabled: FileSearch.enabled

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("What to index")
                    model: [{ value: true, text: qsTr("Names and contents") }, { value: false, text: qsTr("Names only") }]
                    currentIndex: FileSearch.contents ? 0 : 1
                    onActivated: index => FileSearch.contents = model[index].value
                }
            }

            UI.SettingRow {
                iconName: "hidden"
                title: qsTr("Include hidden files and folders")
                description: qsTr("Hidden files are mostly settings kept by apps")
                status: FileSearch.hidden ? qsTr("On") : qsTr("Off")
                enabled: FileSearch.enabled

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: FileSearch.hidden
                    Accessible.name: qsTr("Include hidden files and folders")
                    onToggled: FileSearch.hidden = checked
                }
            }

            UI.SettingRow {
                iconName: "refresh"
                title: qsTr("Rebuild the index")
                description: page.error.length > 0 ? page.error
                    : FileSearch.indexSize.length > 0 ? qsTr("The index takes up %1. Rebuild it if search misses files you know are there.").arg(FileSearch.indexSize)
                    : qsTr("Rebuild it if search misses files you know are there")
                descriptionColor: page.error.length > 0 ? UI.Style.danger : UI.Style.inkMuted
                enabled: FileSearch.enabled

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: FileSearch.rebuilding ? qsTr("Rebuilding") : qsTr("Rebuild")
                    busy: FileSearch.rebuilding
                    enabled: !FileSearch.rebuilding
                    onClicked: {
                        page.error = "";
                        FileSearch.rebuild();
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: FileSearch.available
        spacing: 10

        SectionTitle {
            text: qsTr("Folders that are indexed")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: FileSearch.folders

                    UI.SettingRow {
                        id: folderRow
                        required property string modelData
                        iconName: "folder"
                        title: FileSearch.display(modelData)
                        enabled: FileSearch.enabled

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Remove")
                            onClicked: FileSearch.removeFolder(folderRow.modelData)
                        }
                    }
                }

                UI.SettingRow {
                    iconName: "plus"
                    title: qsTr("Index another folder")
                    description: FileSearch.folders.length === 0 ? qsTr("No folders are indexed") : ""
                    enabled: FileSearch.enabled

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Add a folder")
                        onClicked: {
                            folderDialog.excluding = false;
                            folderDialog.open();
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: FileSearch.available
        spacing: 10

        SectionTitle {
            text: qsTr("Excluded folders")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: FileSearch.excluded

                    UI.SettingRow {
                        id: excludedRow
                        required property string modelData
                        iconName: "folder"
                        title: FileSearch.display(modelData)
                        enabled: FileSearch.enabled

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Remove")
                            onClicked: FileSearch.include(excludedRow.modelData)
                        }
                    }
                }

                UI.SettingRow {
                    iconName: "plus"
                    title: qsTr("Add an excluded folder")
                    description: qsTr("Search never looks inside it")
                    enabled: FileSearch.enabled

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Add a folder")
                        onClicked: {
                            folderDialog.excluding = true;
                            folderDialog.open();
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog

        property bool excluding: false

        title: excluding ? qsTr("Choose a folder to exclude") : qsTr("Choose a folder to index")
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        onAccepted: {
            if (excluding) {
                FileSearch.exclude(selectedFolder.toString());
            } else {
                FileSearch.addFolder(selectedFolder.toString());
            }
        }
    }
}
