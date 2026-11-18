import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// The message list (MailMicrosoft.dc, 392px): the folder's name and the
// filter button over one list, newest first, grouped by day. No Focused
// and Other, and no conversations unless they are turned on in Settings.
Rectangle {
    id: pane

    signal contextMenu(string key, real x, real y)
    signal openMessage(string key)
    readonly property bool listFocused: list.activeFocus

    radius: UI.Style.radiusPanel
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    readonly property bool inSearch: MailEngine.searching

    function moveSelection(step) {
        const count = list.count;
        if (count === 0) {
            return;
        }
        let i = MailEngine.messages.indexOf(MailEngine.selected);
        i = Math.max(0, Math.min(count - 1, i < 0 ? 0 : i + step));
        MailEngine.selected = MailEngine.messages.keyAt(i);
        list.positionViewAtIndex(i, ListView.Contain);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 16
            Layout.rightMargin: 10
            spacing: 8

            UI.Label {
                text: MailEngine.folderTitle
                size: 16
                font.weight: UI.Style.weightSemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: pane.width - 120
            }
            UI.Label {
                visible: pane.inSearch
                text: MailEngine.messages.count === 1 ? qsTr("1 message") : qsTr("%1 messages").arg(MailEngine.messages.count)
                size: 12
                color: UI.Style.inkMuted
            }
            UI.Spinner {
                visible: MailEngine.loading
                size: 14
            }
            Item {
                Layout.fillWidth: true
            }
            CommandButton {
                iconOnly: true
                iconName: "filter"
                tip: qsTr("Filter")
                menu: filterMenu
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            implicitHeight: 1
            color: UI.Style.borderSubtle
        }

        // Filtering keeps a chip on show, so a filtered list is never
        // mistaken for an empty folder.
        RowLayout {
            visible: MailEngine.messages.filter !== "all"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8

            UI.Chip {
                text: MailEngine.messages.filter === "unread" ? qsTr("Unread") : MailEngine.messages.filter === "flagged" ? qsTr("Flagged") : qsTr("Has attachments")
            }
            UI.LinkButton {
                text: qsTr("Show all")
                onClicked: MailEngine.messages.filter = "all"
            }
            Item {
                Layout.fillWidth: true
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 2
            clip: true
            model: MailEngine.messages
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            activeFocusOnTab: true
            reuseItems: true
            QQC.ScrollBar.vertical: UI.ScrollBar {}

            Keys.onUpPressed: pane.moveSelection(-1)
            Keys.onDownPressed: pane.moveSelection(1)
            Keys.onDeletePressed: if (MailEngine.selection.length > 0) MailEngine.removeKeys(MailEngine.selection)
            Keys.onReturnPressed: if (MailEngine.selected) pane.openMessage(MailEngine.selected)
            Keys.onPressed: function (event) {
                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_A) {
                    MailEngine.selectAll();
                    event.accepted = true;
                }
            }

            section.property: pane.inSearch ? "" : "section"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                required property string section
                width: list.width
                height: 32

                UI.Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: parent.section
                    size: 12
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkMuted
                }
            }

            delegate: MessageRowItem {
                width: list.width
                showFolder: pane.inSearch
                highlight: pane.inSearch ? MailEngine.searchText : ""
                onSelected: function (key) {
                    MailEngine.selected = key;
                    list.forceActiveFocus();
                }
                onMenuRequested: function (key, x, y) {
                    pane.contextMenu(key, x, y);
                }
                onOpenRequested: function (key) {
                    pane.openMessage(key);
                }
            }

            footer: Item {
                width: list.width
                height: pane.inSearch && list.count > 0 ? 44 : 8

                RowLayout {
                    visible: pane.inSearch && list.count > 0
                    anchors.centerIn: parent
                    spacing: 6

                    MailIcon {
                        name: "lock"
                        size: 13
                        color: UI.Style.inkMuted
                    }
                    UI.Label {
                        text: qsTr("Searched on this PC. Nothing is sent anywhere.")
                        size: 12
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }

    // Nothing in the folder (MailMsStates.dc).
    ColumnLayout {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 20
        visible: list.count === 0 && !MailEngine.loading && MailEngine.hasAccounts
        width: pane.width - 64
        spacing: 8

        Image {
            Layout.alignment: Qt.AlignHCenter
            visible: !pane.inSearch && MailEngine.messages.filter === "all"
            source: "qrc:/qt/qml/WildOS/Mail/data/caught-up.svg"
            sourceSize: Qt.size(120, 96)
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: pane.inSearch ? qsTr("Nothing found") : MailEngine.messages.filter !== "all" ? qsTr("Nothing here") : MailEngine.folderRole === "inbox" ? qsTr("You're all caught up") : qsTr("This folder is empty")
            size: 15
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: pane.inSearch ? qsTr("Try another word, or part of a name or address.") : MailEngine.folderRole === "inbox" && MailEngine.messages.filter === "all" ? qsTr("No new mail. Enjoy the quiet.") : ""
            visible: text.length > 0
            size: 13
            color: UI.Style.inkMuted
        }
    }

    UI.Menu {
        id: filterMenu
        entries: [
            { text: qsTr("All"), radio: true, checked: MailEngine.messages.filter === "all", run: () => MailEngine.messages.filter = "all" },
            { text: qsTr("Unread"), radio: true, checked: MailEngine.messages.filter === "unread", run: () => MailEngine.messages.filter = "unread" },
            { text: qsTr("Flagged"), radio: true, checked: MailEngine.messages.filter === "flagged", run: () => MailEngine.messages.filter = "flagged" },
            { text: qsTr("Has attachments"), radio: true, checked: MailEngine.messages.filter === "attachments", run: () => MailEngine.messages.filter = "attachments" }
        ]
    }
}
