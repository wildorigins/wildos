pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// DialogSave.dc (8.1): the one Open and Save dialog every app gets. A 920
// by 620 glass window: title bar, a 52px tool row (back, forward, up, the
// path bar, search, New folder, the view switch), places on the left, the
// details list or Large icons, then the name and type fields on a darker band and the buttons, the main
// one at the right.
UI.AppWindow {
    id: win

    required property var chooser
    readonly property var folder: chooser.folder
    readonly property bool saving: chooser.mode === "save"
    readonly property bool choosingFolder: chooser.mode === "folder" || chooser.mode === "saveFiles"
    property bool collapsed: false
    property real expandedHeight: height
    // What the name field held before it was used to type a folder name,
    // so going into that folder does not lose the file's name.
    property string fileName: chooser.initialName

    title: chooser.title
    deep: true
    width: 920
    // Each extra choice an app adds (a check box, a drop-down) makes room
    // for itself rather than squeezing the list.
    height: 620 + 46 * chooser.choices.length
    minimumWidth: 640
    minimumHeight: collapsed ? content.implicitHeight : 420
    maximumHeight: collapsed ? content.implicitHeight : 16777215

    onClosing: chooser.cancel()

    function accept(text) {
        const before = folder.url;
        const result = chooser.accept(text);
        if (result.startsWith("replace:")) {
            replaceDialog.what = result.slice(8);
            replaceDialog.open();
        } else if (result.length > 0) {
            message.text = result;
            message.open();
        } else if (folder.url !== before) {
            nameField.text = saving ? fileName : "";
        }
    }

    function activate(row) {
        if (folder.isDirAt(row) && !(choosingFolder && folder.selectedCount > 1)) {
            folder.openUrl(folder.urlAt(row));
            return;
        }
        nameField.text = chooser.nameForSelection();
        accept(nameField.text);
    }

    function setCollapsed(on) {
        if (on === collapsed) {
            return;
        }
        if (on) {
            expandedHeight = height;
        }
        collapsed = on;
        if (!on) {
            height = expandedHeight;
        } else {
            height = content.implicitHeight;
        }
    }

    Component.onCompleted: {
        nameField.text = chooser.initialName;
        nameField.forceActiveFocus();
        if (saving) {
            // Typing a new name keeps the extension, as on Windows.
            const dot = chooser.suffixStart(nameField.text);
            if (dot > 0) {
                nameField.select(0, dot);
            } else {
                nameField.selectAll();
            }
        }
    }

    Connections {
        target: win.folder
        function onSelectionChanged() {
            if (!list.activeFocus) {
                return;
            }
            const name = win.chooser.nameForSelection();
            if (name.length > 0) {
                nameField.text = name;
            }
        }
        function onFailed(text) {
            message.text = text;
            message.open();
        }
    }

    Shortcut {
        sequences: ["Alt+Left", "Back"]
        enabled: win.folder.canGoBack && !win.collapsed
        onActivated: win.folder.back()
    }
    Shortcut {
        sequences: ["Alt+Right", "Forward"]
        enabled: win.folder.canGoForward && !win.collapsed
        onActivated: win.folder.forward()
    }
    Shortcut {
        sequence: "Alt+Up"
        enabled: win.folder.canGoUp && !win.collapsed
        onActivated: win.folder.up()
    }
    Shortcut {
        sequences: ["Ctrl+L", "Alt+D"]
        enabled: !win.collapsed
        onActivated: pathBar.edit()
    }
    Shortcut {
        sequences: ["Ctrl+F", "Ctrl+E"]
        enabled: !win.collapsed
        onActivated: search.forceActiveFocus()
    }
    Shortcut {
        sequences: ["F5", "Ctrl+R"]
        onActivated: win.folder.refresh()
    }
    Shortcut {
        sequence: "Ctrl+Shift+N"
        enabled: win.folder.writable && !win.collapsed
        onActivated: win.folder.newFolder()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !message.visible && !replaceDialog.visible && !pathBar.editing && list.renameRow < 0
        onActivated: win.chooser.cancel()
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: win.frame
            showTitle: false
            buttons.showMinimise: false
            buttons.showMaximise: false

            Row {
                x: -2
                height: parent.height
                spacing: 10

                UI.ThemeIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    name: win.chooser.appIcon.length > 0 ? win.chooser.appIcon : "folder"
                    fallback: "folder"
                }

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.chooser.title
                    font.weight: UI.Style.weightMedium
                    color: win.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 51
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            visible: !win.collapsed
            spacing: 6

            UI.IconButton {
                implicitWidth: 32
                implicitHeight: 32
                iconName: "back"
                tip: "Back"
                enabled: win.folder.canGoBack
                onClicked: win.folder.back()
            }
            UI.IconButton {
                implicitWidth: 32
                implicitHeight: 32
                iconName: "forward"
                tip: "Forward"
                enabled: win.folder.canGoForward
                onClicked: win.folder.forward()
            }
            UI.IconButton {
                implicitWidth: 32
                implicitHeight: 32
                iconName: "up"
                tip: "Up to the folder above"
                enabled: win.folder.canGoUp
                onClicked: win.folder.up()
            }

            PathBar {
                id: pathBar
                Layout.fillWidth: true
                folder: win.folder
                onDone: list.forceActiveFocus()
            }

            UI.SearchField {
                id: search
                Layout.preferredWidth: 220
                implicitHeight: 34
                placeholderText: "Search " + win.folder.title
                onTextChanged: win.folder.searchText = text
                onEscapePressed: list.forceActiveFocus()
                onDownPressed: list.forceActiveFocus()
                Connections {
                    target: win.folder
                    function onUrlChanged() {
                        search.text = "";
                    }
                }
            }

            UI.Button {
                text: "New folder"
                iconName: "new-folder"
                implicitHeight: 34
                visible: !win.folder.isComputer
                enabled: win.folder.writable
                onClicked: win.folder.newFolder()
            }

            // Files' view switch: Details, or Large icons with previews.
            Row {
                spacing: 0

                UI.IconButton {
                    implicitWidth: 32
                    implicitHeight: 32
                    enabled: !win.folder.isComputer
                    iconName: "list"
                    iconSize: 14
                    tip: "Details"
                    checked: win.chooser.viewMode === "details" || win.folder.isComputer
                    Accessible.checkable: true
                    Accessible.checked: checked
                    onClicked: win.chooser.viewMode = "details"
                }
                UI.IconButton {
                    implicitWidth: 32
                    implicitHeight: 32
                    enabled: !win.folder.isComputer
                    iconName: "grid"
                    iconSize: 14
                    tip: "Large icons"
                    checked: win.chooser.viewMode === "icons" && !win.folder.isComputer
                    Accessible.checkable: true
                    Accessible.checked: checked
                    onClicked: win.chooser.viewMode = "icons"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: !win.collapsed
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !win.collapsed
            spacing: 0

            Places {
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                folder: win.folder
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: UI.Style.divider
            }

            FileList {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                Layout.topMargin: 6
                Layout.bottomMargin: 6
                folder: win.folder
                mode: win.chooser.viewMode
                focus: true
                onActivated: row => win.activate(row)
                emptyText: {
                    if (win.folder.loading) {
                        return "";
                    }
                    if (win.folder.errorText.length > 0) {
                        return win.folder.errorText;
                    }
                    if (win.folder.searchText.length > 0) {
                        return "Nothing in " + win.folder.title + " matches \"" + win.folder.searchText + "\".";
                    }
                    if (win.choosingFolder) {
                        return "There are no folders in " + win.folder.title + ".";
                    }
                    return !win.saving && win.chooser.filters.length > 1 ? "There are no files of this type in " + win.folder.title + "."
                                                          : "This folder is empty.";
                }
                emptyButton: win.folder.errorText.length > 0 ? "Try again"
                    : win.folder.searchText.length > 0 ? "Clear search"
                    : (!win.choosingFolder && !win.saving && win.chooser.filters.length > 1 && win.chooser.allFilesIndex() >= 0 && win.chooser.currentFilter !== win.chooser.allFilesIndex()) ? "Show all types" : ""
                emptyActionName: emptyButton
                onEmptyAction: action => {
                    if (action === "Try again") {
                        win.folder.refresh();
                    } else if (action === "Clear search") {
                        search.text = "";
                    } else {
                        win.chooser.currentFilter = win.chooser.allFilesIndex();
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: fields.implicitHeight + 28
            color: UI.Style.surfaceBand

            Rectangle {
                width: parent.width
                height: 1
                color: UI.Style.borderSubtle
            }

            GridLayout {
                id: fields
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                columns: 2
                columnSpacing: 12
                rowSpacing: 10

                UI.Label {
                    Layout.preferredWidth: 110
                    text: win.chooser.nameLabel
                }

                UI.TextField {
                    id: nameField
                    Layout.fillWidth: true
                    Accessible.name: win.chooser.nameLabel
                    placeholderText: win.choosingFolder ? win.folder.title : ""
                    onAccepted: win.accept(text)
                    onTextEdited: if (win.saving) win.fileName = text
                    Keys.onDownPressed: list.forceActiveFocus()
                }

                UI.Label {
                    Layout.preferredWidth: 110
                    visible: typeBox.visible
                    text: win.chooser.typeLabel
                }

                UI.ComboBox {
                    id: typeBox
                    Layout.fillWidth: true
                    implicitHeight: 36
                    visible: !win.choosingFolder && win.chooser.filters.length > 0
                    model: win.chooser.filters
                    currentIndex: win.chooser.currentFilter
                    Accessible.name: win.chooser.typeLabel
                    onActivated: index => {
                        const from = win.chooser.currentFilter;
                        win.chooser.currentFilter = index;
                        if (win.saving) {
                            nameField.text = win.chooser.swapSuffix(nameField.text, from, index);
                            win.fileName = nameField.text;
                        }
                    }
                    Connections {
                        target: win.chooser
                        function onCurrentFilterChanged() {
                            typeBox.currentIndex = win.chooser.currentFilter;
                        }
                    }
                }

                Repeater {
                    model: win.chooser.choices

                    delegate: Item {
                        id: choice
                        required property var modelData
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        implicitHeight: choiceRow.implicitHeight

                        RowLayout {
                            id: choiceRow
                            width: parent.width
                            spacing: 12

                            UI.Label {
                                Layout.preferredWidth: 110
                                visible: choice.modelData.options.length > 0
                                text: choice.modelData.label
                            }

                            UI.ComboBox {
                                Layout.fillWidth: true
                                implicitHeight: 36
                                visible: choice.modelData.options.length > 0
                                model: choice.modelData.options
                                currentIndex: Math.max(0, indexOfValue(choice.modelData.value))
                                Accessible.name: choice.modelData.label
                                onActivated: index => win.chooser.setChoice(choice.modelData.id, valueAt(index))
                            }

                            UI.CheckBox {
                                Layout.leftMargin: 122
                                visible: choice.modelData.options.length === 0
                                text: choice.modelData.label
                                checked: choice.modelData.value === "true"
                                onToggled: win.chooser.setChoice(choice.modelData.id, checked ? "true" : "false")
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 59
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            spacing: 10

            Row {
                visible: win.saving
                spacing: 6

                UI.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: win.collapsed ? "chevron-down" : "chevron-up"
                    size: 14
                    color: UI.Style.accentText
                    TapHandler {
                        onTapped: win.setCollapsed(!win.collapsed)
                    }
                }

                UI.LinkButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.collapsed ? "Browse folders" : "Hide folders"
                    onClicked: win.setCollapsed(!win.collapsed)
                }
            }

            Item {
                Layout.fillWidth: true
            }

            UI.Button {
                text: "Cancel"
                onClicked: win.chooser.cancel()
            }

            UI.Button {
                kind: "primary"
                text: win.chooser.acceptLabel
                enabled: win.choosingFolder ? !win.folder.isComputer || win.folder.selectedCount > 0
                                            : nameField.text.trim().length > 0 || win.folder.selectedCount > 0
                onClicked: win.accept(nameField.text)
            }
        }
    }

    UI.Dialog {
        id: replaceDialog
        property string what
        readonly property bool count: /^[0-9]+$/.test(what)
        title: count ? "Replace " + what + " files?" : "Replace \"" + what + "\"?"
        text: count ? "Files with these names are already in " + win.folder.title + ". Replacing them overwrites what is in them."
                    : "There is already a file called \"" + what + "\" in " + win.folder.title + ". Replacing it overwrites what is in it."
        buttons: [
            UI.Button {
                text: "Cancel"
                onClicked: {
                    replaceDialog.close();
                    nameField.forceActiveFocus();
                }
            },
            UI.Button {
                kind: "primary"
                text: "Replace"
                onClicked: {
                    replaceDialog.close();
                    win.chooser.confirmReplace();
                }
            }
        ]
    }

    UI.Dialog {
        id: message
        title: win.chooser.title
        buttons: [
            UI.Button {
                kind: "primary"
                text: "OK"
                onClicked: {
                    message.close();
                    nameField.forceActiveFocus();
                }
            }
        ]
    }
}
