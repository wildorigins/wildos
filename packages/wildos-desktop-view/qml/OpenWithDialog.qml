pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Desktop
import WildOS.UI as UI

// "Open with, choose another app": every installed app, the suggested ones
// first, and a switch to make the choice stick for this kind of file.
DeskDialog {
    id: chooser

    property var urls: []
    property var suggested: []
    property var everything: []
    property string chosen: ""
    property bool always: false
    property string filter: ""
    readonly property var shown: {
        const f = filter.toLowerCase();
        const seen = {};
        const out = [];
        for (const app of suggested.concat(everything)) {
            if (seen[app.id] || (f.length > 0 && app.name.toLowerCase().indexOf(f) < 0)) {
                continue;
            }
            seen[app.id] = true;
            out.push(app);
        }
        return out;
    }

    title: qsTr("How do you want to open this file?")
    defaultAction: () => chooser.openChosen()

    function show(list) {
        urls = list;
        suggested = DeskOps.appsFor(list[0]);
        everything = DeskOps.allApps();
        chosen = suggested.length > 0 ? suggested[0].id : "";
        always = false;
        filter = "";
        search.text = "";
        present();
        search.forceActiveFocus();
    }

    function openChosen() {
        if (chosen.length === 0) {
            return;
        }
        close();
        DeskOps.openWith(urls, chosen, always);
    }

    UI.SearchField {
        id: search
        Layout.fillWidth: true
        placeholderText: qsTr("Find an app")
        Accessible.name: placeholderText
        onTextChanged: chooser.filter = text
        onEscapePressed: chooser.close()
        Keys.onReturnPressed: chooser.openChosen()
        Keys.onEnterPressed: chooser.openChosen()
        onDownPressed: apps.forceActiveFocus()
    }

    ListView {
        id: apps
        Layout.fillWidth: true
        Layout.preferredHeight: 260
        clip: true
        spacing: 2
        model: chooser.shown
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: UI.ScrollBar {}
        keyNavigationEnabled: true
        // Arrow keys move the choice; Enter opens it.
        onCurrentIndexChanged: if (activeFocus && currentIndex >= 0) chooser.chosen = chooser.shown[currentIndex].id
        Keys.onReturnPressed: chooser.openChosen()
        Keys.onEnterPressed: chooser.openChosen()

        delegate: T.ItemDelegate {
            id: app
            required property var modelData
            required property int index
            readonly property bool chosen: chooser.chosen === modelData.id
            width: ListView.view.width
            height: 40
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.name
            Accessible.selected: chosen
            onClicked: chooser.chosen = modelData.id
            onDoubleClicked: {
                chooser.chosen = modelData.id;
                chooser.openChosen();
            }

            background: Rectangle {
                radius: UI.Style.radiusXs
                // Fades in on hover and clears at once, so moving along a list
                // never lights two items. It comes before the colour so it sees
                // the change in hover first.
                Behavior on color {
                    enabled: app.hovered
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
                color: app.chosen ? UI.Style.accentSelected : app.down ? UI.Style.surfacePressed : app.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
            }

            contentItem: Item {
                UI.ThemeIcon {
                    id: appIcon
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    name: app.modelData.icon
                }

                UI.Label {
                    anchors.left: appIcon.right
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: app.modelData.name
                }
            }
        }
    }

    UI.CheckBox {
        text: qsTr("Always use this app for this kind of file")
        checked: chooser.always
        onToggled: chooser.always = checked
    }
    buttons: [
        UI.Button {
            kind: "primary"
            text: qsTr("Open")
            enabled: chooser.chosen.length > 0
            onClicked: chooser.openChosen()
        },
        UI.Button {
            text: qsTr("Cancel")
            onClicked: chooser.close()
        }
    ]
}
