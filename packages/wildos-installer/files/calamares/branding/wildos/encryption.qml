/* Encryption (InstallerEncryption.dc), a notesqml step. The disk password is
 * its own password, typed at every start before anything loads, so it
 * comes after the keyboard step. The answers go to global storage for the
 * partition module (wildosDiskPassword, wildosEncrypt, scrambled the way
 * Calamares scrambles passwords) and for the wildosrecovery job, which adds
 * the recovery key as a second key slot (wildosRecovery, wildosRecoveryKey).
 * Continue in the footer waits for Installer.encryptionReady.
 */
import io.calamares.core 1.0
import io.calamares.ui 1.0
import WildOS.Installer
import WildOS.UI as UI

import QtQuick
import QtQuick.Layouts

WPage {
    id: page

    property bool recovery: true
    property bool dontEncrypt: false
    readonly property string pw: password.text
    readonly property bool longEnough: pw.length >= 8
    readonly property bool matching: pw !== "" && confirm.text === pw
    // 0 to 4 bars: length first, then a mix of kinds of character.
    readonly property int strength: {
        if (pw.length === 0)
            return 0;
        var kinds = [/[a-z]/, /[A-Z]/, /[0-9]/, /[^A-Za-z0-9]/].filter(function(r) { return r.test(pw); }).length;
        var score = pw.length >= 16 ? 3 : pw.length >= 12 ? 2 : pw.length >= 8 ? 1 : 0;
        return Math.min(4, score + (kinds >= 3 ? 1 : 0));
    }
    readonly property bool ready: dontEncrypt || (longEnough && matching)

    title: qsTr("Protect your files")
    subtitle: qsTr("Wild OS encrypts the whole of its disk space. You will type this password each time the computer starts, before anything else loads.")
    gap: 18

    function store() {
        Global.insert("wildosEncrypt", !dontEncrypt);
        Global.insert("wildosDiskPassword", dontEncrypt ? "" : Installer.obscure(pw));
        Global.insert("wildosRecovery", !dontEncrypt && recovery);
        Global.insert("wildosRecoveryKey", Installer.obscure(Installer.recoveryKey));
        Installer.encryptionReady = ready;
    }
    onReadyChanged: store()
    onRecoveryChanged: store()
    onDontEncryptChanged: store()
    onPwChanged: store()

    function onActivate() {
        store();
        if (!dontEncrypt)
            password.input.forceActiveFocus();
    }
    function onLeave() { store(); }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        rowSpacing: 16
        columnSpacing: 18
        enabled: !page.dontEncrypt
        opacity: enabled ? 1 : 0.5

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 6
            WField {
                id: password
                Layout.fillWidth: true
                label: qsTr("Disk password")
                echoMode: TextInput.Password
                UI.NoPaste {}
            }
            RowLayout {
                spacing: 8
                Row {
                    spacing: 4
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            width: 40
                            height: 4
                            radius: 2
                            color: index < page.strength ? (page.strength <= 1 ? "#f5c27a" : "#7fe0b0")
                                                         : UI.Style.surfaceTrack
                        }
                    }
                }
                Text {
                    text: page.pw === "" ? qsTr("Use at least 8 characters")
                          : !page.longEnough ? qsTr("Too short")
                          : [qsTr("Weak"), qsTr("Weak"), qsTr("Fair"), qsTr("Good"), qsTr("Strong")][page.strength]
                    color: UI.Style.inkStatus
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 6
            WField {
                id: confirm
                Layout.fillWidth: true
                label: qsTr("Type it again")
                echoMode: TextInput.Password
                UI.NoPaste {}
            }
            RowLayout {
                spacing: 6
                visible: confirm.text !== ""
                WIcon {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    stroke: page.matching ? "#7fe0b0" : "#f5c27a"
                    strokeWidth: 2.4
                    path: page.matching ? "M5 12l5 5 9-10" : "M12 7v6 M12 16.6v0.4"
                }
                Text {
                    text: page.matching ? qsTr("Passwords match") : qsTr("Passwords do not match yet")
                    color: UI.Style.inkStatus
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: switches.implicitHeight + 2
        radius: UI.Style.radiusMd
        color: UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.borderSubtle

        ColumnLayout {
            id: switches
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 1
            spacing: 0

            Item {
                Layout.fillWidth: true
                implicitHeight: Math.max(64, recoveryText.implicitHeight + 24)
                opacity: page.dontEncrypt ? 0.5 : 1
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 16
                    ColumnLayout {
                        id: recoveryText
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("Create a recovery key")
                            color: UI.Style.ink
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(14)
                            font.pixelSize: 14
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("A one-off code to save or write down, in case you ever forget the password. Recommended.")
                            color: UI.Style.inkMuted
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(12)
                            font.pixelSize: 12
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }
                    }
                    WSwitch {
                        enabled: !page.dontEncrypt
                        accessibleName: qsTr("Create a recovery key")
                        checked: page.recovery && !page.dontEncrypt
                        onToggled: function(on) { page.recovery = on; }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: UI.Style.divider
            }
            Item {
                Layout.fillWidth: true
                implicitHeight: Math.max(64, plainText.implicitHeight + 24)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 16
                    ColumnLayout {
                        id: plainText
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("Don't encrypt")
                            color: UI.Style.ink
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(14)
                            font.pixelSize: 14
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Faster to start, but anyone with your laptop can read your files. Not recommended.")
                            color: UI.Style.inkMuted
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(12)
                            font.pixelSize: 12
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }
                    }
                    WSwitch {
                        accessibleName: qsTr("Don't encrypt")
                        checked: page.dontEncrypt
                        onToggled: function(on) { page.dontEncrypt = on; }
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: note.implicitHeight + 28
        radius: UI.Style.radiusMd
        color: page.dontEncrypt ? Qt.rgba(240 / 255, 180 / 255, 90 / 255, 0.08) : UI.Style.accentTint
        border.width: 1
        border.color: page.dontEncrypt ? Qt.rgba(240 / 255, 180 / 255, 90 / 255, 0.28) : Qt.rgba(143 / 255, 180 / 255, 1, 0.24)

        RowLayout {
            id: note
            anchors.fill: parent
            anchors.margins: 14
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 14
            WIcon {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                stroke: page.dontEncrypt ? "#f5c27a" : UI.Style.accentText
                path: page.dontEncrypt ? "M12 7v6 M12 16.6v0.4 M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0"
                                       : "M5 11h14v10H5z M8 11V8a4 4 0 0 1 8 0v3"
            }
            Text {
                Layout.fillWidth: true
                text: page.dontEncrypt
                      ? qsTr("Your files will not be encrypted. Anyone who has this computer, or takes its disk out, can read them.")
                      : qsTr("If you lose both the password and the recovery key, nobody can get your files back, not even us. That's what keeps them safe.")
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(13)
                font.pixelSize: 13
                lineHeight: 1.5
                wrapMode: Text.WordWrap
            }
        }
    }

    Item { Layout.fillHeight: true }
}
