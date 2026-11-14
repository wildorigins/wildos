import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Welcome
import WildOS.UI as UI

// Setup step 3, from FirstAccount.dc.html (3.5). The account is made by the
// setup service as soon as Continue is pressed; steps 4 to 7 are then
// chosen for it. Coming back and changing something makes it again.
StepPage {
    id: page

    signal next()
    signal back()

    readonly property string state: FirstBoot.state
    readonly property bool working: state === "creating"
    property string picture: FirstBoot.pictures.length > 0 ? FirstBoot.pictures[0] : ""
    // What the account was last made with, so Back then Continue does not
    // make it again for nothing.
    property string madeWith: ""
    readonly property string choices: JSON.stringify([nameField.text, computerField.text, passwordField.text, diskSignIn.checked, hintField.text, picture])

    readonly property bool nameOk: nameField.text.trim().length > 0 && !/[:,=\\]/.test(nameField.text)
    readonly property bool computerOk: FirstBoot.validComputerName(computerField.text)
    readonly property bool passwordShort: passwordField.text.length > 0 && passwordField.text.length < 8
    readonly property bool mismatch: confirmField.text.length > 0 && confirmField.text !== passwordField.text
    readonly property bool hintBad: hintField.text.length > 0 && passwordField.text.length > 0 && hintField.text.indexOf(passwordField.text) >= 0
    readonly property bool ready: nameOk && computerOk && passwordField.text.length >= 8 && confirmField.text === passwordField.text && !hintBad

    gap: 18
    title: "Create your account"
    subtitle: "This account lives on this computer only. Wild OS never asks you to sign in online."
    note: state === "failed" ? FirstBoot.error
        : working ? "Creating your account."
        : hintBad ? "The hint can't contain the password itself."
        : "This is also the password for installing software and changing settings."
    noteColor: state === "failed" || hintBad ? UI.Style.danger : UI.Style.inkMuted
    Component.onCompleted: nameField.forceActiveFocus()

    Connections {
        target: FirstBoot
        function onAccountCreated() {
            page.madeWith = page.choices;
            page.next();
        }
    }

    function submit() {
        if (!ready || working) {
            return;
        }
        if (FirstBoot.state === "created" && madeWith === choices) {
            next();
            return;
        }
        FirstBoot.createAccount(nameField.text, computerField.text, passwordField.text, diskSignIn.checked, hintField.text, picture);
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 24
        enabled: !page.working

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 10

            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 88
                implicitHeight: 88

                Image {
                    id: avatar
                    anchors.fill: parent
                    source: page.picture.length > 0 ? "file://" + page.picture : ""
                    sourceSize: Qt.size(176, 176)
                    fillMode: Image.PreserveAspectCrop
                    visible: false
                    layer.enabled: true
                }

                Rectangle {
                    id: circle
                    anchors.fill: parent
                    radius: 44
                    visible: false
                    layer.enabled: true
                }

                MultiEffect {
                    anchors.fill: parent
                    source: avatar
                    maskEnabled: true
                    maskSource: circle
                    visible: page.picture.length > 0
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 44
                    visible: page.picture.length === 0
                    color: UI.Style.surfaceTile

                    UI.Icon {
                        anchors.centerIn: parent
                        name: "avatar"
                        size: 40
                        color: UI.Style.iconMuted
                    }
                }
            }

            UI.LinkButton {
                Layout.alignment: Qt.AlignHCenter
                visible: FirstBoot.pictures.length > 1
                text: "Choose picture"
                onClicked: pictureDialog.open()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            columns: 2
            columnSpacing: 18
            rowSpacing: 14

            Field {
                label: "Your name"
                UI.TextField {
                    id: nameField
                    Layout.fillWidth: true
                    size: "large"
                    maximumLength: 100
                    error: nameField.text.length > 0 && !page.nameOk
                    Accessible.name: "Your name"
                    onAccepted: page.submit()
                }
                hint: nameField.text.length > 0 && !page.nameOk ? "A name can't contain a colon, comma, equals sign or backslash." : ""
                hintError: true
            }

            Field {
                label: "Computer name"
                UI.TextField {
                    id: computerField
                    Layout.fillWidth: true
                    size: "large"
                    // Not the artboard's "alex-laptop": a
                    // neutral Windows-style name, never the person's.
                    text: FirstBoot.suggestedComputerName
                    maximumLength: 63
                    error: !page.computerOk
                    Accessible.name: "Computer name"
                    Accessible.description: "How it appears on your network"
                    onAccepted: page.submit()
                }
                hint: page.computerOk ? "How it appears on your network" : "Use letters, numbers and hyphens, not starting or ending with a hyphen."
                hintError: !page.computerOk
            }

            Field {
                label: "Password"
                UI.TextField {
                    id: passwordField
                    Layout.fillWidth: true
                    size: "large"
                    revealable: true
                    UI.NoPaste {}
                    error: page.passwordShort && !activeFocus
                    Accessible.name: "Password"
                    onAccepted: page.submit()
                }
                StrengthLine {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    password: passwordField.text
                    names: [nameField.text]
                }
                hint: page.passwordShort ? "Use at least 8 characters." : ""
                hintError: !passwordField.activeFocus
            }

            Field {
                label: "Type it again"
                UI.TextField {
                    id: confirmField
                    Layout.fillWidth: true
                    size: "large"
                    revealable: true
                    UI.NoPaste {}
                    error: page.mismatch && !activeFocus
                    Accessible.name: "Type the password again"
                    onAccepted: page.submit()
                }
                hint: page.mismatch ? "The passwords don't match." : ""
                hintError: !confirmField.activeFocus
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        enabled: !page.working

        Column {
            width: parent.width

            // Only with an encrypted disk: its password is then what stands
            // between someone at the computer and the account.
            UI.ListRow {
                visible: FirstBoot.encrypted
                minHeight: 60
                title: "Use my disk password to sign in"
                description: "One password at start-up instead of two. You'll go straight to the desktop after unlocking."

                UI.Toggle {
                    id: diskSignIn
                    anchors.verticalCenter: parent.verticalCenter
                    checked: FirstBoot.encrypted
                    Accessible.name: "Use my disk password to sign in"
                }
            }

            UI.ListRow {
                minHeight: 60
                title: "Password hint"
                description: "Shown on the sign-in screen after a wrong attempt"

                UI.TextField {
                    id: hintField
                    width: 220
                    anchors.verticalCenter: parent.verticalCenter
                    placeholderText: "Optional"
                    maximumLength: 200
                    error: page.hintBad
                    Accessible.name: "Password hint"
                }
            }
        }
    }

    UI.Dialog {
        id: pictureDialog
        title: "Choose a picture"
        text: "You can use one of your own photos later, in Settings > Accounts."

        Flow {
            Layout.fillWidth: true
            width: 5 * 64 + 4 * 12
            spacing: 12

            Repeater {
                model: FirstBoot.pictures

                T.AbstractButton {
                    id: choice
                    required property string modelData
                    width: 64
                    height: 64
                    hoverEnabled: true
                    Accessible.name: modelData.split("/").pop().replace(".png", "")
                    onClicked: {
                        page.picture = modelData;
                        pictureDialog.close();
                    }

                    Image {
                        id: thumb
                        anchors.fill: parent
                        source: "file://" + choice.modelData
                        sourceSize: Qt.size(128, 128)
                        fillMode: Image.PreserveAspectCrop
                        visible: false
                        layer.enabled: true
                    }

                    Rectangle {
                        id: thumbMask
                        anchors.fill: parent
                        radius: 32
                        visible: false
                        layer.enabled: true
                    }

                    MultiEffect {
                        anchors.fill: parent
                        source: thumb
                        maskEnabled: true
                        maskSource: thumbMask
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: 35
                        color: "transparent"
                        border.width: 2
                        border.color: page.picture === choice.modelData ? UI.Style.accent : choice.hovered ? UI.Style.borderControl : "transparent"
                    }

                    UI.FocusRing {
                        visible: choice.visualFocus
                        targetRadius: 32
                    }
                }
            }
        }

        buttons: [
            UI.Button {
                text: "Close"
                onClicked: pictureDialog.close()
            }
        ]
    }

    buttons: [
        UI.Button {
            text: "Back"
            enabled: !page.working
            onClicked: page.back()
        },
        UI.Button {
            text: "Continue"
            kind: "primary"
            enabled: page.ready
            busy: page.working
            onClicked: page.submit()
        }
    ]

    // A label over its field with the hint under it, as FirstAccount.dc
    // lays out each cell: 13px medium label, 6px gaps, 12px muted hint.
    component Field: ColumnLayout {
        property string label
        property string hint
        property bool hintError: false
        default property alias field: holder.data

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 1
        spacing: 6

        UI.Label {
            text: parent.label
            font.weight: UI.Style.weightMedium
        }

        ColumnLayout {
            id: holder
            Layout.fillWidth: true
            spacing: 0
        }

        UI.Label {
            Layout.fillWidth: true
            visible: parent.hint.length > 0
            text: parent.hint
            size: 12
            color: parent.hintError ? UI.Style.danger : UI.Style.inkMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }
}
