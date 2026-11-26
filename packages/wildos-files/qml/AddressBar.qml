pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
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
    // The folders the address being typed could be going to, and the one
    // picked with Up and Down (none until one of those is pressed).
    property var suggestions: []
    property int suggestion: -1

    implicitHeight: Metrics.fieldHeight

    function edit() {
        editing = true;
        suggestions = [];
        suggestion = -1;
        input.text = folder ? folder.editText : "";
        input.selectAll();
        input.forceActiveFocus();
    }

    function finish() {
        editing = false;
        suggestions = [];
        suggestion = -1;
    }

    function go(text) {
        if (folder.openText(text)) {
            finish();
            view.focusContent();
        } else {
            view.showMessage(qsTr("Wild OS cannot find “%1”. Check the spelling and try again.").arg(text));
        }
    }

    function pick(step) {
        const n = suggestions.length;
        if (n === 0) {
            return false;
        }
        // Up from the first, or Down from the last, goes back to what was typed.
        suggestion = (suggestion + 1 + step + n + 1) % (n + 1) - 1;
        return true;
    }

    // A part of the path, a chevron that lists folders, or the recent
    // locations button: flat until hovered, radius 5 inside the field.
    component CrumbButton: T.AbstractButton {
        id: crumbButton
        // For the ones that are only an arrow. It stays away once clicked,
        // so it never covers the menu the button opened.
        property string tip
        property bool tipDismissed: false
        // Lit while something is dragged over it, like a folder.
        property bool lit: false
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.role: Accessible.Button
        onClicked: tipDismissed = true
        onHoveredChanged: if (!hovered) tipDismissed = false

        UI.Tooltip {
            text: crumbButton.tip
            visible: crumbButton.hovered && !crumbButton.tipDismissed && crumbButton.tip.length > 0
        }

        background: Rectangle {
            radius: 5
            // Fades in on hover and clears at once, so moving along a list
            // never lights two items. It comes before the colour so it sees
            // the change in hover first.
            Behavior on color {
                enabled: crumbButton.hovered
                ColorAnimation { duration: UI.Style.hoverDuration }
            }
            color: crumbButton.down ? UI.Style.surfacePressed : crumbButton.hovered || crumbButton.lit ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: UI.Style.radiusSm
        color: UI.Style.surfaceSunken
        border.width: 1
        border.color: address.editing ? UI.Style.accentBorderStrong : UI.Style.borderField
        Behavior on border.color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
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
                    Accessible.name: qsTr("Folders in %1").arg(crumb.index > 0 ? crumb.all[crumb.index - 1].name : qsTr("this computer"))
                    tip: Accessible.name
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
                    lit: crumbDrop.containsDrag
                    onClicked: address.folder.openUrl(crumb.modelData.url)

                    UI.Label {
                        id: label
                        anchors.centerIn: parent
                        text: crumb.modelData.name
                    }

                    DropArea {
                        id: crumbDrop
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
            Accessible.name: qsTr("Folders in %1").arg(address.folder ? address.folder.title : "")
            tip: Accessible.name
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
            entries.push({ text: qsTr("No folders inside"), enabled: false });
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
        Accessible.name: qsTr("Address")
        onTextEdited: {
            address.suggestions = address.folder ? address.folder.completions(text) : [];
            address.suggestion = -1;
        }
        onAccepted: address.go(address.suggestion >= 0 ? address.suggestions[address.suggestion] : text)
        Keys.onDownPressed: event => event.accepted = address.pick(1)
        Keys.onUpPressed: event => event.accepted = address.pick(-1)
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

    // The folders the typed address could be going to, in the menu glass
    // under the bar. The keyboard stays in the address.
    T.Popup {
        id: completer
        y: address.height + 4
        width: address.width
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: 6
        visible: address.editing && address.suggestions.length > 0 && input.activeFocus
        closePolicy: T.Popup.NoAutoClose

        background: Rectangle {
            radius: UI.Style.radiusMd
            color: UI.Style.glassMenu
            border.width: 1
            border.color: UI.Style.borderMenu
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: UI.Style.shadowMenu
                shadowVerticalOffset: 12
                shadowBlur: 1.0
                blurMax: 32
            }
        }

        contentItem: Column {
            spacing: 2
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Suggested folders")

            Repeater {
                model: address.suggestions

                delegate: T.AbstractButton {
                    id: option

                    required property string modelData
                    required property int index
                    readonly property bool lit: index === address.suggestion

                    width: parent ? parent.width : 0
                    implicitHeight: Metrics.fit(UI.Style.menuItemHeight, 13)
                    padding: 0
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData
                    Accessible.selected: lit
                    onClicked: address.go(modelData)

                    background: Rectangle {
                        radius: UI.Style.radiusMenuRow
                        // Resting the pointer on one does not pick it: Enter
                        // still goes to what was typed.
                        color: option.down ? UI.Style.surfacePressed : option.lit || option.hovered ? UI.Style.surfaceHover : "transparent"
                    }

                    contentItem: Item {
                        UI.Icon {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            name: "folder"
                            size: 16
                            strokeWidth: 1.8
                        }

                        UI.Label {
                            x: 36
                            width: parent.width - x - 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: option.modelData
                            // A long address keeps its end, the folder's name.
                            elide: Text.ElideLeft
                        }
                    }
                }
            }
        }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: UI.Style.motionFast }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Math.round(UI.Style.motionFast / 2) }
        }
    }

    CrumbButton {
        id: recent
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 26
        Accessible.name: qsTr("Recent locations")
        tip: Accessible.name
        onClicked: {
            const list = address.folder ? address.folder.recent : [];
            const entries = list.map((item, i) => ({
                text: item.name,
                icon: "folder",
                run: () => address.folder.goToRecent(i)
            }));
            if (entries.length === 0) {
                entries.push({ text: qsTr("Nowhere else yet"), enabled: false });
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
