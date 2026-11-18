import QtQuick
import QtQuick.Layouts
import WildOS.Remote
import WildOS.UI as UI

// The side column (RemoteHosts.dc): every host, favourites, the groups,
// then the tools, what Remote keeps where, and Settings at the foot, where
// Windows apps keep it.
Item {
    id: side

    property string page: "hosts"
    signal pageChosen(string page)
    signal newGroup()
    signal groupMenu(string name, Item item)

    readonly property var hosts: Hosts.hosts
    function count(test) {
        let n = 0;
        for (const h of hosts) {
            if (test(h)) {
                n++;
            }
        }
        return n;
    }
    function forwardCount() {
        let n = 0;
        for (const h of hosts) {
            if (h.kind === "terminal") {
                n += (h.ssh.forwards || []).length;
            }
        }
        return n;
    }

    implicitWidth: 236

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.divider
    }

    Flickable {
        anchors.fill: parent
        anchors.bottomMargin: footCard.height + settingsRow.height + 34
        contentHeight: column.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            x: 10
            y: 6
            width: side.width - 21
            spacing: 2

            NavRow {
                width: parent.width
                text: qsTr("All hosts")
                iconName: "monitor"
                count: side.hosts.length
                current: side.page === "hosts"
                onClicked: side.pageChosen("hosts")
            }
            NavRow {
                width: parent.width
                text: qsTr("Favourites")
                iconName: "star"
                count: side.count(h => h.favourite)
                current: side.page === "favourites"
                onClicked: side.pageChosen("favourites")
            }

            UI.Label {
                text: qsTr("Groups")
                size: 12
                color: UI.Style.inkSubtle
                font.weight: UI.Style.weightMedium
                leftPadding: 10
                topPadding: 14
                bottomPadding: 6
            }
            Repeater {
                model: Hosts.groups
                NavRow {
                    id: groupRow
                    required property string modelData
                    width: column.width
                    text: modelData
                    iconName: modelData === "Servers" ? "server" : modelData === "Home" ? "home" : "box"
                    count: side.count(h => h.group === modelData)
                    current: side.page === "group:" + modelData
                    onClicked: side.pageChosen("group:" + modelData)
                    onMenuRequested: side.groupMenu(modelData, groupRow)
                }
            }
            NavRow {
                width: parent.width
                text: qsTr("New group")
                iconName: "plus"
                muted: true
                count: -1
                onClicked: side.newGroup()
            }

            UI.Label {
                text: qsTr("Tools")
                size: 12
                color: UI.Style.inkSubtle
                font.weight: UI.Style.weightMedium
                leftPadding: 10
                topPadding: 14
                bottomPadding: 6
            }
            NavRow {
                width: parent.width
                text: qsTr("Keychain")
                glyph: RemoteIcons.key
                count: Keychain.keys.length
                current: side.page === "keychain"
                onClicked: side.pageChosen("keychain")
            }
            NavRow {
                width: parent.width
                text: qsTr("Snippets")
                glyph: RemoteIcons.snippets
                count: Snippets.snippets.length
                current: side.page === "snippets"
                onClicked: side.pageChosen("snippets")
            }
            NavRow {
                width: parent.width
                text: qsTr("Port forwarding")
                glyph: RemoteIcons.forward
                count: side.forwardCount()
                current: side.page === "forwarding"
                onClicked: side.pageChosen("forwarding")
            }
            NavRow {
                width: parent.width
                text: qsTr("Known hosts")
                glyph: RemoteIcons.knownHosts
                count: HostKeys.known.length
                current: side.page === "knownhosts"
                onClicked: side.pageChosen("knownhosts")
            }
        }
    }

    // What lives where, said plainly (RemoteStart.dc's "Passwords kept
    // safe" and RemoteHosts.dc's "No cloud sync" in one card).
    Rectangle {
        id: footCard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: settingsRow.top
        anchors.margins: 14
        anchors.rightMargin: 15
        anchors.bottomMargin: 10
        height: footText.implicitHeight + 28
        radius: 12
        color: UI.Style.surfaceRest
        border.width: 1
        border.color: UI.Style.borderSubtle

        UI.Glyph {
            x: 14
            y: 15
            width: 18
            height: 18
            path: RemoteIcons.noSync
            color: UI.Style.iconInk
            strokeWidth: 1.8
        }
        Column {
            id: footText
            x: 42
            y: 14
            width: parent.width - 54
            spacing: 3
            UI.Label {
                width: parent.width
                text: qsTr("No cloud sync")
                size: 13
                font.weight: UI.Style.weightSemiBold
            }
            UI.Label {
                width: parent.width
                text: qsTr("Hosts and keys live on this PC only. Passwords go in your keyring, and only if you tick it.")
                size: 12
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
                lineHeight: 1.3
            }
        }
    }

    NavRow {
        id: settingsRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 11
        anchors.bottomMargin: 10
        text: qsTr("Settings")
        iconName: "cog"
        count: -1
        current: side.page === "settings"
        onClicked: side.pageChosen("settings")
    }

    component NavRow: Rectangle {
        id: row
        property string text
        property string iconName
        property string glyph
        property int count: 0
        property bool current: false
        property bool muted: false
        signal clicked()
        signal menuRequested()

        height: 36
        radius: UI.Style.radiusSm
        color: mouse.pressed ? UI.Style.surfacePressed : current ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onReturnPressed: row.clicked()
        Keys.onEnterPressed: row.clicked()
        Keys.onSpacePressed: row.clicked()

        UI.FocusRing {
            visible: row.activeFocus
            anchors.margins: -2
            targetRadius: row.radius
        }

        UI.Icon {
            visible: row.glyph.length === 0
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            name: row.iconName
            size: 17
            strokeWidth: 1.8
            color: row.current ? UI.Style.iconInk : UI.Style.iconMuted
        }
        UI.Glyph {
            visible: row.glyph.length > 0
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 17
            height: 17
            path: row.glyph
            strokeWidth: 1.8
            color: row.current ? UI.Style.iconInk : UI.Style.iconMuted
        }
        UI.Label {
            x: 38
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 80
            text: row.text
            size: 14
            font.weight: row.current ? UI.Style.weightSemiBold : UI.Style.weightMedium
            color: row.muted ? UI.Style.inkMuted : UI.Style.ink
        }
        UI.Label {
            visible: row.count >= 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: row.count
            size: 12
            color: UI.Style.inkSubtle
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: m => m.button === Qt.RightButton ? row.menuRequested() : row.clicked()
        }
    }
}
