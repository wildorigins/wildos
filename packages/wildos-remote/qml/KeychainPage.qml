import QtQuick
import QtQuick.Layouts
import WildOS.Remote
import WildOS.UI as UI

// SSH keys (RemoteKeychain.dc): the keys in ~/.ssh on the left, the chosen
// one on the right with its public half to copy or send, where its private
// half is and how it's protected, and the hosts that use it. What it says
// about protection is read from the key file itself.
Item {
    id: page

    property string selectedPath

    signal importRequested()
    signal newKeyRequested()
    signal sendRequested(string path, Item item)
    signal exportRequested(string path)
    signal deleteRequested(string path)
    signal passphraseRequested(string path, string mode)
    signal notice(string text)

    readonly property var keys: Keychain.keys
    readonly property var selected: {
        for (const k of keys) {
            if (k.path === selectedPath) {
                return k;
            }
        }
        return keys.length > 0 ? keys[0] : null;
    }
    readonly property var legacy: keys.filter(k => k.legacy)

    function protection(k) {
        if (k.securityKey) {
            return qsTr("Needs a touch on the key");
        }
        if (!k.encrypted) {
            return qsTr("No passphrase");
        }
        if (k.passphraseSaved) {
            return qsTr("Passphrase, saved in your keyring");
        }
        return qsTr("Passphrase");
    }
    function shortFingerprint(fp) {
        const body = fp.substring(7);
        return "SHA256:" + body.substring(0, 4) + "…" + body.substring(body.length - 4);
    }
    function usedText(k) {
        if (k.usedBy.length === 0) {
            return qsTr("Not set for a host");
        }
        if (k.usedBy.length === 1) {
            return qsTr("Used by %1").arg(k.usedBy[0]);
        }
        return qsTr("Used by %1 hosts").arg(k.usedBy.length);
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 36
        anchors.rightMargin: 36
        anchors.topMargin: 22
        anchors.bottomMargin: 26
        spacing: 28

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 560
            Layout.fillHeight: true
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Keychain")
                    size: 26
                    font.weight: UI.Style.weightBold
                }
                UI.Button {
                    size: "small"
                    text: qsTr("Import")
                    onClicked: page.importRequested()
                }
                UI.Button {
                    size: "small"
                    kind: "primary"
                    iconName: "plus"
                    text: qsTr("New key")
                    onClicked: page.newKeyRequested()
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: !Keychain.agent
                text: qsTr("No key agent is running in this session, so ssh asks for a key's passphrase each time it's used.")
                size: 13
                color: UI.Style.warning
                wrapMode: Text.Wrap
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14
                clip: true
                model: page.keys
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: keyCard
                    required property var modelData
                    readonly property bool current: page.selected && page.selected.path === modelData.path
                    width: ListView.view.width
                    height: 76
                    radius: 12
                    color: current ? UI.Style.surfaceSelected : keyMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceRaised
                    border.width: 1
                    border.color: current ? UI.Style.borderControl : UI.Style.borderSubtle
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.label

                    Rectangle {
                        x: 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42
                        height: 42
                        radius: 10
                        color: keyCard.modelData.legacy ? UI.Style.warningSoft : UI.Style.surfaceTile
                        UI.Glyph {
                            anchors.centerIn: parent
                            width: 19
                            height: 19
                            path: keyCard.modelData.securityKey ? RemoteIcons.securityKey : RemoteIcons.key
                            strokeWidth: 1.8
                            color: keyCard.modelData.legacy ? UI.Style.warning : UI.Style.iconInk
                        }
                    }
                    Column {
                        x: 72
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 72 - rightCol.width - 24
                        spacing: 4
                        Row {
                            spacing: 10
                            UI.Label {
                                text: keyCard.modelData.label
                                size: 15
                                font.weight: UI.Style.weightSemiBold
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: typeLabel.implicitWidth + 12
                                height: 20
                                radius: 5
                                color: UI.Style.tone(0.08)
                                UI.Label {
                                    id: typeLabel
                                    anchors.centerIn: parent
                                    text: keyCard.modelData.type + (keyCard.modelData.bits ? " " + keyCard.modelData.bits : "")
                                    size: 11
                                    font.weight: UI.Style.weightSemiBold
                                    color: UI.Style.inkMuted
                                }
                            }
                        }
                        UI.Label {
                            width: parent.width
                            text: page.protection(keyCard.modelData)
                            size: 13
                            color: keyCard.modelData.encrypted || keyCard.modelData.securityKey ? UI.Style.inkMuted : UI.Style.warning
                        }
                    }
                    Column {
                        id: rightCol
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        UI.Label {
                            anchors.right: parent.right
                            text: page.shortFingerprint(keyCard.modelData.fingerprint)
                            size: 12
                            font.family: "JetBrains Mono"
                            color: UI.Style.inkSubtle
                        }
                        UI.Label {
                            anchors.right: parent.right
                            text: page.usedText(keyCard.modelData)
                            size: 13
                            color: UI.Style.inkMuted
                        }
                    }
                    MouseArea {
                        id: keyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.selectedPath = keyCard.modelData.path
                    }
                }
                footer: Column {
                    width: ListView.view.width
                    topPadding: 14
                    spacing: 14
                    Rectangle {
                        visible: page.legacy.length > 0
                        width: parent.width
                        height: warnText.implicitHeight + 32
                        radius: 12
                        color: Qt.alpha(UI.Style.warning, 0.08)
                        border.width: 1
                        border.color: Qt.alpha(UI.Style.warning, 0.28)
                        UI.Icon {
                            x: 16
                            y: 17
                            name: "warning"
                            size: 17
                            color: UI.Style.warning
                        }
                        UI.Label {
                            id: warnText
                            x: 46
                            y: 16
                            width: parent.width - 62
                            textFormat: Text.StyledText
                            text: qsTr("<b>%1</b> still works, but newer keys are shorter and faster. Make a new key and send it to its hosts from here.").arg(page.legacy.length > 0 ? page.legacy[0].label : "")
                            size: 13
                            wrapMode: Text.Wrap
                            lineHeight: 1.35
                        }
                    }
                    Column {
                        visible: page.keys.length === 0
                        width: parent.width
                        topPadding: 40
                        spacing: 10
                        UI.Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("No keys yet")
                            size: 18
                            font.weight: UI.Style.weightSemiBold
                        }
                        UI.Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(parent.width, 420)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: qsTr("A key signs you in to servers without a password. Make one here, or import one you already have.")
                            size: 14
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }
        }

        // The chosen key.
        Rectangle {
            visible: page.selected !== null
            Layout.preferredWidth: 486
            Layout.fillHeight: true
            radius: 14
            color: UI.Style.surfaceRaised
            border.width: 1
            border.color: UI.Style.borderSubtle

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 12

                RowLayout {
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 46
                        radius: 11
                        color: UI.Style.surfaceTile
                        UI.Glyph {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            path: page.selected && page.selected.securityKey ? RemoteIcons.securityKey : RemoteIcons.key
                            strokeWidth: 1.8
                            color: UI.Style.iconInk
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        UI.Label {
                            text: page.selected ? page.selected.label : ""
                            size: 19
                            font.weight: UI.Style.weightBold
                        }
                        UI.Label {
                            text: page.selected ? qsTr("%1, made %2").arg(page.selected.type).arg(page.selected.made) : ""
                            size: 13
                            color: UI.Style.inkMuted
                        }
                    }
                }

                Caption { text: qsTr("Public key"); Layout.topMargin: 6 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: publicText.implicitHeight + 24
                    radius: 10
                    color: UI.Style.surfaceSunken
                    border.width: 1
                    border.color: UI.Style.borderSubtle
                    TextEdit {
                        id: publicText
                        x: 12
                        y: 12
                        width: parent.width - 24
                        text: page.selected ? page.selected.publicKey : ""
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: UI.Style.ink
                        selectionColor: UI.Style.textSelection
                        font.family: "JetBrains Mono"
                        font.pixelSize: UI.Style.px(12)
                        Keys.onPressed: event => {
                            if (publicMenu.isMenuKey(event)) {
                                publicMenu.openAtCaret();
                                event.accepted = true;
                            }
                        }

                        UI.TextMenu {
                            id: publicMenu
                        }
                    }
                }
                Row {
                    spacing: 8
                    UI.Button {
                        size: "small"
                        iconName: "copy"
                        text: qsTr("Copy")
                        onClicked: {
                            Keychain.copyPublic(page.selected.path);
                            page.notice(qsTr("Public key copied"));
                        }
                    }
                    UI.Button {
                        id: sendButton
                        size: "small"
                        iconName: "send"
                        text: qsTr("Send to a server")
                        onClicked: page.sendRequested(page.selected.path, sendButton)
                    }
                }

                Caption { text: qsTr("Private key"); Layout.topMargin: 6 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    UI.Icon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        name: page.selected && (page.selected.encrypted || page.selected.securityKey) ? "lock" : "warning"
                        size: 15
                        color: page.selected && (page.selected.encrypted || page.selected.securityKey) ? UI.Style.success : UI.Style.warning
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: {
                            const k = page.selected;
                            if (!k) {
                                return "";
                            }
                            const where = qsTr("It's the file %1 in your .ssh folder, readable only by you, and it's only copied if you export it.").arg(k.file);
                            if (k.securityKey) {
                                return qsTr("The key itself stays on your security key; this PC only holds a handle that needs the key and a touch to use. ") + where;
                            }
                            if (!k.encrypted) {
                                return qsTr("Not protected by a passphrase: anyone who gets a copy of the file can use it. ") + where;
                            }
                            if (k.passphraseSaved) {
                                return qsTr("Encrypted with its passphrase, which is saved in your keyring. When you connect, Remote unlocks it into your key agent, so you're not asked. ") + where;
                            }
                            return qsTr("Encrypted with its passphrase, which you're asked for when it's used. ") + where;
                        }
                        size: 13
                        color: UI.Style.inkStatus
                        wrapMode: Text.Wrap
                        lineHeight: 1.35
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: page.selected !== null && !page.selected.securityKey
                    UI.Button {
                        size: "small"
                        visible: page.selected && page.selected.encrypted && !page.selected.passphraseSaved
                        text: qsTr("Save passphrase in keyring")
                        onClicked: page.passphraseRequested(page.selected.path, "save")
                    }
                    UI.Button {
                        size: "small"
                        visible: page.selected && page.selected.passphraseSaved
                        text: qsTr("Forget saved passphrase")
                        onClicked: Keychain.forgetPassphrase(page.selected.path)
                    }
                    UI.Button {
                        size: "small"
                        text: page.selected && page.selected.encrypted ? qsTr("Change passphrase") : qsTr("Add a passphrase")
                        onClicked: page.passphraseRequested(page.selected.path, "change")
                    }
                    UI.Button {
                        size: "small"
                        visible: Keychain.agent && page.selected && !page.selected.inAgent
                        text: qsTr("Add to key agent")
                        onClicked: Keychain.addToAgent(page.selected.path, (ok, message) => page.notice(ok ? qsTr("Unlocked in your key agent until you sign out") : message))
                    }
                }
                UI.Label {
                    visible: page.selected !== null && page.selected.inAgent
                    text: qsTr("Unlocked in your key agent now")
                    size: 12
                    color: UI.Style.success
                }

                Caption { text: qsTr("Used by"); Layout.topMargin: 6 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: page.selected ? page.selected.usedBy : []
                        Rectangle {
                            required property string modelData
                            width: chipLabel.implicitWidth + 20
                            height: 28
                            radius: 7
                            color: UI.Style.tone(0.07)
                            UI.Label {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: parent.modelData
                                size: 13
                            }
                        }
                    }
                    UI.Label {
                        visible: page.selected !== null && page.selected.usedBy.length === 0
                        text: qsTr("No host is set to use it. Hosts left to choose their own key may still be offered it.")
                        width: parent.width
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    UI.Button {
                        size: "small"
                        text: qsTr("Export")
                        onClicked: page.exportRequested(page.selected.path)
                    }
                    Item { Layout.fillWidth: true }
                    UI.Button {
                        size: "small"
                        danger: true
                        text: qsTr("Delete")
                        onClicked: page.deleteRequested(page.selected.path)
                    }
                }
            }
        }
    }

    component Caption: UI.Label {
        size: 12
        font.weight: UI.Style.weightBold
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
        color: UI.Style.inkMuted
    }
}
