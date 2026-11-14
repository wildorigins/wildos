pragma ComponentBehavior: Bound

import QtQuick
import WildOS.Files
import QtQuick.Templates as T
import WildOS.UI as UI

// The breadcrumb address bar. Each part opens its folder; each chevron
// lists the folders inside it. Clicking the empty part, Ctrl+L or Alt+D
// turns it into a text box for typing a path.
FocusScope {
    id: address

    required property var folder
    required property var view
    property bool editing: false

    implicitHeight: Metrics.fieldHeight

    function edit() {
        editing = true;
        input.text = folder ? folder.editText : "";
        input.selectAll();
        input.forceActiveFocus();
    }

    function finish() {
        editing = false;
    }

    // A part of the path, a chevron that lists folders, or the recent
    // locations button: flat until hovered, radius 5 inside the field.
    component CrumbButton: T.AbstractButton {
        id: crumbButton
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.role: Accessible.Button

        background: Rectangle {
            radius: 5
            color: crumbButton.down ? UI.Style.surfacePressed : crumbButton.hovered ? UI.Style.surfaceHover : "transparent"
            Behavior on color {
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusSm
        color: UI.Style.surfaceSunken
        border.width: 1
        border.color: address.editing ? UI.Style.accentBorderStrong : UI.Style.borderField
    }

    MouseArea {
        anchors.fill: parent
        onClicked: address.edit()
    }

    Row {
        id: crumbs
        visible: !address.editing
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        // Long paths keep their right end, the part you are in.
        readonly property real room: address.width - 10 - 8 - 26 - 8
        clip: true

        Image {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            sourceSize: Qt.size(16, 16)
            source: address.folder ? "image://icon/" + encodeURIComponent(address.folder.iconName) : ""
        }

        Repeater {
            model: address.folder ? address.folder.crumbs : []

            delegate: Row {
                id: crumb

                required property var modelData
                required property int index
                readonly property var all: address.folder.crumbs
                // Hide the leading crumbs that do not fit; the chevron
                // before the first one shown still lists them.
                visible: index >= address.firstShown

                spacing: 4
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined

                CrumbButton {
                    id: chevron
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    height: Metrics.fit(26, 13)
                    Accessible.name: "Folders in " + (crumb.index > 0 ? crumb.all[crumb.index - 1].name : "this computer")
                    onClicked: address.showChildren(chevron, crumb.index - 1)

                    UI.Icon {
                        anchors.centerIn: parent
                        name: "chevron"
                        size: 12
                        strokeWidth: 2.2
                        color: UI.Style.iconMuted
                    }
                }

                CrumbButton {
                    anchors.verticalCenter: parent.verticalCenter
                    height: Metrics.fit(26, 13)
                    width: label.implicitWidth + 12
                    Accessible.name: crumb.modelData.name
                    onClicked: address.folder.openUrl(crumb.modelData.url)

                    UI.Label {
                        id: label
                        anchors.centerIn: parent
                        text: crumb.modelData.name
                    }

                    DropArea {
                        anchors.fill: parent
                        onDropped: drop => address.view.dropUrls(drop, crumb.modelData.url)
                    }
                }
            }
        }

        CrumbButton {
            id: lastChevron
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: Metrics.fit(26, 13)
            Accessible.name: "Folders in " + (address.folder ? address.folder.title : "")
            onClicked: address.showChildren(lastChevron, address.folder.crumbs.length - 1)

            UI.Icon {
                anchors.centerIn: parent
                name: "chevron"
                size: 12
                strokeWidth: 2.2
                color: UI.Style.iconMuted
            }
        }
    }

    // How many leading crumbs to drop so the rest fit: each crumb is about
    // its text plus 34px of padding and chevron.
    property int firstShown: {
        if (!folder) {
            return 0;
        }
        const list = folder.crumbs;
        let total = 16 + 20 + 16;
        let first = list.length;
        for (let i = list.length - 1; i >= 0; --i) {
            total += metrics.advanceWidth(list[i].name) + 12 + 20;
            if (total > crumbs.room && i < list.length - 1) {
                break;
            }
            first = i;
        }
        return first;
    }

    FontMetrics {
        id: metrics
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
        font.pixelSize: UI.Style.px(13)
    }

    function showChildren(anchorItem, crumbIndex) {
        let url;
        if (crumbIndex < 0) {
            url = "wildos:computer";
        } else {
            url = folder.crumbs[crumbIndex].url;
        }
        const list = folder.subfolders(url);
        const entries = list.map(item => ({
            text: item.name,
            icon: "folder",
            run: () => address.folder.openUrl(item.url)
        }));
        if (entries.length === 0) {
            entries.push({ text: "No folders inside", enabled: false });
        }
        view.openMenu(entries, anchorItem, true, 0, 0, 240);
    }

    TextInput {
        id: input
        visible: address.editing
        anchors.left: parent.left
        anchors.right: recent.left
        anchors.leftMargin: 12
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        color: UI.Style.inkField
        selectionColor: UI.Style.textSelection
        selectedTextColor: UI.Style.ink
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
        font.pixelSize: UI.Style.px(13)
        clip: true
        selectByMouse: true
        Accessible.role: Accessible.EditableText
        Accessible.name: "Address"
        onAccepted: {
            if (address.folder.openText(text)) {
                address.finish();
                address.view.focusContent();
            } else {
                address.view.showMessage("Wild OS cannot find “" + text + "”. Check the spelling and try again.");
            }
        }
        Keys.onEscapePressed: {
            address.finish();
            address.view.focusContent();
        }
        Keys.onPressed: event => {
            if (addressMenu.isMenuKey(event)) {
                addressMenu.openAtCaret();
                event.accepted = true;
            }
        }
        // The text menu holds focus while it is open; the address is still
        // being typed.
        onActiveFocusChanged: if (!activeFocus && !addressMenu.opened) address.finish()

        UI.TextMenu {
            id: addressMenu
            onFocusLeft: address.finish()
        }
    }

    CrumbButton {
        id: recent
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 26
        Accessible.name: "Recent locations"
        onClicked: {
            const list = address.folder ? address.folder.recent : [];
            const entries = list.map((item, i) => ({
                text: item.name,
                icon: "folder",
                run: () => address.folder.goToRecent(i)
            }));
            if (entries.length === 0) {
                entries.push({ text: "Nowhere else yet", enabled: false });
            }
            const width = Math.min(420, address.width);
            address.view.openMenu(entries, address, false, address.width - width, address.height + 4, width);
        }

        UI.Icon {
            anchors.centerIn: parent
            name: "chevron-down"
            size: 12
            strokeWidth: 2.2
        }
    }
}
