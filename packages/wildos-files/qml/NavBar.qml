import QtQuick
import WildOS.Files
import WildOS.UI as UI

// The 50px navigation row: back, forward, up, refresh, the address bar
// and the search box.
Item {
    id: nav

    required property var folder
    required property var view
    property alias address: address
    property alias search: searchBox

    height: Metrics.navHeight

    Rectangle {
        anchors.fill: parent
        color: Metrics.navBar
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: UI.Style.borderSubtle
        }
    }

    component NavButton: UI.IconButton {
        width: 34
        height: Metrics.buttonHeight
    }

    Row {
        id: buttons
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        NavButton {
            iconName: "back"
            tip: qsTr("Back (Alt+Left)")
            enabled: nav.folder && nav.folder.canGoBack
            onClicked: nav.folder.back()
        }
        NavButton {
            iconName: "forward"
            tip: qsTr("Forward (Alt+Right)")
            enabled: nav.folder && nav.folder.canGoForward
            onClicked: nav.folder.forward()
        }
        NavButton {
            iconName: "up"
            tip: qsTr("Up one level (Alt+Up)")
            enabled: nav.folder && nav.folder.canGoUp
            onClicked: nav.folder.up()
        }
        NavButton {
            iconName: "refresh"
            tip: qsTr("Refresh (F5)")
            onClicked: nav.folder.refresh()
        }
    }

    AddressBar {
        id: address
        anchors.left: buttons.right
        anchors.leftMargin: 4 + 6
        anchors.right: searchBox.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        height: Metrics.fieldHeight
        folder: nav.folder
        view: nav.view
    }

    UI.SearchField {
        id: searchBox
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(250, Math.max(160, nav.width * 0.26))
        height: Metrics.fieldHeight
        // Searching the drives list itself means nothing; open a drive first.
        enabled: nav.folder && nav.folder.kind !== "computer"
        placeholderText: nav.folder ? qsTr("Search %1").arg(nav.folder.title) : qsTr("Search")
        Accessible.name: placeholderText

        // Typing filters straight away; the deeper search through the
        // folders inside starts a moment later in the model.
        onTextChanged: if (nav.folder && nav.folder.searchText !== text) nav.folder.searchText = text
        onEscapePressed: nav.view.focusContent()
        onDownPressed: nav.view.focusContent()
        Keys.onReturnPressed: nav.view.focusContent()
        Keys.onEnterPressed: nav.view.focusContent()

        Connections {
            target: nav.folder
            function onSearchTextChanged() {
                if (searchBox.text !== nav.folder.searchText) {
                    searchBox.text = nav.folder.searchText;
                }
            }
        }
    }

    onFolderChanged: searchBox.text = folder ? folder.searchText : ""
}
