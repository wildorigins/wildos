import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// Reset Wild OS (RecoveryReset.dc): three steps in one dialog. Choose what
// happens to your files, see what is removed and kept, confirm. The reset
// itself runs in start-up recovery from the copy of Wild OS kept on the PC,
// so the PC restarts into it. "Remove everything" asks a second time.
RecoveryDialog {
    id: dialog

    property int step: 0
    property string how: "keep"
    property bool sure: false

    function start() {
        step = 0;
        how = "keep";
        sure = false;
        open();
    }

    title: qsTr("Reset Wild OS")
    iconName: "rotate-ccw"
    dialogWidth: 520

    // Step 1: choose.
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.step === 0
        spacing: 14

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Choose what happens to your files.")
            size: 13.5
            wrapMode: Text.Wrap
        }

        Repeater {
            model: [
                { key: "keep", icon: "folder-check", title: qsTr("Keep my files"),
                  text: qsTr("The system and apps go back to how they were when installed. Your home folder, Mail and Windows mode stay.") },
                { key: "everything", icon: "trash-round", title: qsTr("Remove everything"),
                  text: Recovery.wipeFits ? qsTr("Wipes the whole disk and sets up encryption again with a new key. For selling or giving the PC away.")
                      : qsTr("Needs at least 4 GB of memory, because Wild OS runs from memory while the disk is wiped.") }
            ]

            T.AbstractButton {
                id: choice
                required property var modelData
                readonly property bool chosen: dialog.how === modelData.key
                Layout.fillWidth: true
                enabled: modelData.key === "keep" || Recovery.wipeFits
                opacity: enabled ? 1 : 0.55
                implicitHeight: choiceRow.implicitHeight + topPadding + bottomPadding
                padding: 14
                topPadding: 13
                bottomPadding: 13
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.title
                Accessible.checked: chosen
                onClicked: dialog.how = modelData.key

                background: Rectangle {
                    radius: 10
                    color: choice.chosen ? Qt.rgba(UI.Style.accent.r, UI.Style.accent.g, UI.Style.accent.b, 0.08)
                        : choice.hovered ? UI.Style.surfaceHover : (UI.Style.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.03))
                    border.width: choice.chosen ? 2 : 1
                    border.color: choice.chosen ? UI.Style.accent : (UI.Style.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.08))

                    UI.FocusRing {
                        visible: choice.visualFocus
                        targetRadius: parent.radius
                    }
                }

                contentItem: RowLayout {
                    id: choiceRow
                    spacing: 12

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        width: 18
                        height: 18
                        radius: 9
                        color: choice.chosen ? UI.Style.accent : "transparent"
                        border.width: choice.chosen ? 0 : 1.5
                        border.color: UI.Style.inkSubtle

                        Rectangle {
                            visible: choice.chosen
                            anchors.centerIn: parent
                            width: 7
                            height: 7
                            radius: 3.5
                            color: UI.Style.textOnAccent
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        RowLayout {
                            spacing: 8

                            PathIcon {
                                name: choice.modelData.icon
                                size: 16
                                color: UI.Style.ink
                            }

                            UI.Label {
                                text: choice.modelData.title
                                size: 14
                                font.weight: UI.Style.weightSemiBold
                            }
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: choice.modelData.text
                            size: 12.5
                            lineHeight: 1.45
                            color: UI.Style.inkMuted
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
    }

    // Step 2: what happens.
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.step === 1
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 20

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 10

                UI.Label {
                    text: qsTr("REMOVED")
                    size: 12.5
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkSubtle
                }

                Repeater {
                    model: dialog.how === "keep"
                        ? [qsTr("Apps you installed. A list goes on your Desktop so you can get them back."), qsTr("System settings, back to default")]
                        : [qsTr("Everything on this PC: your files, apps, accounts and Windows mode"), qsTr("The disk password and recovery key; you choose new ones")]

                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 9

                        PathIcon {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                            name: "x-mark"
                            size: 15
                            strokeWidth: 2.4
                            color: UI.Style.warning
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: modelData
                            size: 13
                            lineHeight: 1.4
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                visible: dialog.how === "keep"
                spacing: 10

                UI.Label {
                    text: qsTr("KEPT")
                    size: 12.5
                    font.weight: UI.Style.weightSemiBold
                    color: UI.Style.inkSubtle
                }

                Repeater {
                    model: [qsTr("Your home folder"), qsTr("Mail, Calendar and Notes"), qsTr("Windows mode and its apps"), qsTr("Wi-Fi and your password")]

                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 9

                        PathIcon {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                            name: "check"
                            size: 15
                            strokeWidth: 2.4
                            color: UI.Style.success
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: modelData
                            size: 13
                            lineHeight: 1.4
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            radius: 8
            color: Qt.rgba(95 / 255, 211 / 255, 141 / 255, 0.08)
            border.width: 1
            border.color: Qt.rgba(95 / 255, 211 / 255, 141 / 255, 0.25)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                PathIcon {
                    name: "hard-drive"
                    size: 16
                    color: UI.Style.success
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: Format.isValid(Recovery.lastBackup) ? qsTr("Last backup %1 at %2").arg(Format.dayText(Recovery.lastBackup)).arg(Format.time(Recovery.lastBackup))
                        : Recovery.backupInstalled ? qsTr("No backup yet") : qsTr("Back up anything that matters to you first")
                    size: 12.5
                    elide: Text.ElideRight
                }

                UI.LinkButton {
                    visible: Recovery.backupInstalled
                    text: qsTr("Back up now")
                    onClicked: Recovery.openBackup()
                }
            }
        }
    }

    // Step 3: confirm. "Remove everything" asks a second time here.
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.step === 2
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            width: 64
            height: 64
            radius: 32
            color: dialog.how === "keep" ? Qt.rgba(UI.Style.accent.r, UI.Style.accent.g, UI.Style.accent.b, 0.14) : Qt.rgba(1, 196 / 255, 106 / 255, 0.14)

            PathIcon {
                anchors.centerIn: parent
                name: dialog.how === "keep" ? "rotate-ccw" : "trash-round"
                size: 30
                strokeWidth: 1.8
                color: dialog.how === "keep" ? UI.Style.accent : UI.Style.warning
            }
        }

        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: dialog.how === "keep" ? qsTr("Ready to reset") : qsTr("Remove everything?")
            size: 18
            font.weight: UI.Style.weightMedium
        }

        UI.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 420
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            text: dialog.how === "keep"
                ? qsTr("Takes about 15 minutes and the PC restarts once. It uses the copy of Wild OS kept on this PC, so it works without the internet.")
                : qsTr("Every file on this PC is deleted for good, then Wild OS sets up again from the copy kept on this PC. You choose a new disk password and get a new recovery key.")
            size: 13
            lineHeight: 1.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.CheckBox {
            Layout.alignment: Qt.AlignHCenter
            visible: dialog.how === "everything"
            text: qsTr("I understand my files will be deleted")
            checked: dialog.sure
            onToggled: dialog.sure = checked
        }

        UI.Label {
            Layout.fillWidth: true
            visible: !Recovery.resetReady
            horizontalAlignment: Text.AlignHCenter
            text: Recovery.resetWhy === "missing" || Recovery.resetWhy === ""
                ? qsTr("The copy of Wild OS isn't on this PC yet. It arrives with the next Wild OS release.")
                : qsTr("The copy of Wild OS on this PC didn't pass its check, so it can't be used. A new one arrives with the next release.")
            size: 12.5
            color: UI.Style.danger
            wrapMode: Text.Wrap
        }

        UI.Label {
            Layout.fillWidth: true
            visible: Recovery.error.length > 0 && !Recovery.busy
            horizontalAlignment: Text.AlignHCenter
            text: Recovery.error
            size: 12.5
            color: UI.Style.danger
            wrapMode: Text.Wrap
        }
    }

    buttons: [
        UI.Button {
            implicitHeight: 32
            text: dialog.step === 0 || dialog.step === 2 ? qsTr("Cancel") : qsTr("Back")
            enabled: !Recovery.busy
            onClicked: dialog.step === 1 ? dialog.step = 0 : dialog.close()
        },
        UI.Button {
            implicitHeight: 32
            kind: "primary"
            danger: dialog.step === 2 && dialog.how === "everything"
            text: dialog.step < 2 ? qsTr("Next") : dialog.how === "keep" ? qsTr("Reset") : qsTr("Remove everything")
            busy: Recovery.busy
            enabled: dialog.step < 2 || (Recovery.resetReady && (dialog.how === "keep" || dialog.sure))
            onClicked: {
                if (dialog.step < 2) {
                    dialog.step += 1;
                } else {
                    Recovery.reset(dialog.how);
                }
            }
        }
    ]

    Connections {
        target: Recovery
        function onFinished(what) {
            if (what === "reset" && dialog.opened) {
                dialog.close();
                Recovery.restart();
            }
        }
    }
}
