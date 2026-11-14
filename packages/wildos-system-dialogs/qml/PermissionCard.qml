import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import WildOS.UI as UI
import WildOS.SystemDialogs.Backend

// The permission prompt (DialogPermission.dc): "Wild OS security" over a
// 20px question, the asking app with its verified state and what it wants
// in one line, the password, "Show more details", and Yes and No side by
// side with Yes first, as Windows' prompt has them. 500 wide, radius 16,
// 22 / 24 padding, 16 apart; the footer 14 / 24 with a hairline above.
Item {
    id: card

    property bool detailsShown: false

    readonly property var who: Agent.people[Agent.person] || ({ text: "", value: "" })
    readonly property bool waiting: Agent.waitSeconds > 0

    function takeFocus() {
        password.forceActiveFocus();
    }

    // An empty password is a fingerprint, for a person with one enrolled
    // (see Agent::fingerprint), unless the reader is already listening.
    readonly property bool canSubmit: (password.text.length > 0 || (Agent.fingerprint && !Agent.listening)) && !card.waiting

    function submit() {
        if (card.canSubmit && !Agent.busy) {
            Agent.submit(password.text);
        }
    }

    width: 500
    implicitHeight: column.implicitHeight

    Connections {
        target: Agent
        // A new request, or another try after a wrong password: the field
        // starts empty.
        function onSerialChanged() {
            password.text = "";
            card.detailsShown = false;
        }
        function onStateChanged() {
            if (!Agent.busy && Agent.error.length > 0) {
                password.selectAll();
                password.forceActiveFocus();
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: UI.Style.glassDialog
        border.width: 1
        border.color: UI.Style.borderGlass
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: UI.Style.shadowFlyout
            shadowVerticalOffset: 30
            shadowBlur: 1.0
            blurMax: 64
        }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.topMargin: 22
            Layout.bottomMargin: 22
            spacing: 16

            Row {
                spacing: 10

                UI.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "shield"
                    size: 18
                    color: UI.Style.accentText
                }

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Wild OS security"
                    size: 13
                    color: UI.Style.accentText
                }
            }

            UI.Label {
                Layout.fillWidth: true
                text: "Allow " + (Agent.appName.length > 0 ? Agent.appName : "this app") + " to make changes to this computer?"
                size: 20
                font.weight: UI.Style.weightSemiBold
                lineHeightMode: Text.FixedHeight
                lineHeight: Math.round(UI.Style.px(20) * 1.3)
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                Accessible.role: Accessible.Heading
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: appRow.implicitHeight + 28
                radius: UI.Style.radiusMd
                color: UI.Style.surfaceRaised

                RowLayout {
                    id: appRow
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    AppPicture {
                        Layout.alignment: Qt.AlignTop
                        iconName: Agent.iconName
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        UI.Label {
                            Layout.fillWidth: true
                            text: Agent.appName.length > 0 ? Agent.appName : "An app"
                            size: 14
                            font.weight: UI.Style.weightSemiBold
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            UI.Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                visible: Agent.trust !== "checking"
                                name: Agent.trust === "verified" ? "check" : "warning"
                                size: 12
                                strokeWidth: Agent.trust === "verified" ? 2.6 : 2.2
                                color: Agent.trust === "verified" ? UI.Style.success : UI.Style.warning
                            }

                            UI.Spinner {
                                visible: Agent.trust === "checking"
                                size: 12
                            }

                            UI.Label {
                                Layout.fillWidth: true
                                text: Agent.trust === "verified" ? "Verified: part of Wild OS"
                                    : Agent.trust === "unverified" ? "Not verified. " + Agent.why
                                    : "Checking where it comes from"
                                size: 12
                                color: Agent.trust === "unverified" ? UI.Style.warning : UI.Style.inkAlpha(0.78, 0.72)
                                wrapMode: Text.Wrap
                                elide: Text.ElideNone
                            }
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: Agent.purpose
                            size: 12
                            color: UI.Style.inkMuted
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: Agent.people.length > 1
                spacing: 6

                UI.Label {
                    text: "Administrator"
                    size: 13
                    font.weight: UI.Style.weightMedium
                }

                UI.ComboBox {
                    Layout.fillWidth: true
                    model: Agent.people
                    currentIndex: Agent.person
                    Accessible.name: "Administrator"
                    onActivated: index => Agent.choose(index)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                UI.Label {
                    id: passwordLabel
                    text: Agent.forSelf ? "Your password (" + card.who.text + ")"
                        : Agent.people.length > 1 ? "Password for " + card.who.text
                        : "Administrator password (" + card.who.text + ")"
                    size: 13
                    font.weight: UI.Style.weightMedium
                }

                UI.TextField {
                    id: password
                    UI.NoPaste {}
                    Layout.fillWidth: true
                    implicitHeight: 40
                    echoMode: TextInput.Password
                    enabled: !card.waiting
                    readOnly: Agent.busy
                    error: Agent.error.length > 0
                    Accessible.name: passwordLabel.text
                    onAccepted: card.submit()
                    onTextChanged: {
                        if (text.length > 0) {
                            Agent.typing();
                        }
                    }
                    Keys.onEscapePressed: Agent.cancel()
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: Agent.error.length > 0 || card.waiting
                    text: card.waiting
                        ? "Too many wrong tries. Try again in " + Agent.waitSeconds + (Agent.waitSeconds === 1 ? " second." : " seconds.")
                        : Agent.error
                    size: 12
                    color: UI.Style.danger
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    Accessible.role: Accessible.AlertMessage
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0 && Agent.error.length === 0 && !card.waiting
                    text: Agent.info.length > 0 ? Agent.info
                        : Agent.fingerprint && !Agent.busy && !Agent.listening ? "Or leave it empty and select Yes, then touch the fingerprint reader."
                        : ""
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            UI.LinkButton {
                Layout.leftMargin: -4
                text: card.detailsShown ? "Hide details" : "Show more details"
                onClicked: card.detailsShown = !card.detailsShown
            }

            GridLayout {
                Layout.fillWidth: true
                visible: card.detailsShown
                columns: 2
                columnSpacing: 12
                rowSpacing: 6

                Repeater {
                    model: card.detailsShown ? Agent.details : []

                    delegate: Item {
                        id: entry
                        required property var modelData
                        required property int index
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        implicitHeight: entryRow.implicitHeight

                        RowLayout {
                            id: entryRow
                            width: parent.width
                            spacing: 12

                            UI.Label {
                                Layout.preferredWidth: 76
                                Layout.alignment: Qt.AlignTop
                                text: entry.modelData.label
                                size: 12
                                color: UI.Style.inkMuted
                            }

                            TextEdit {
                                Layout.fillWidth: true
                                text: entry.modelData.value
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                color: UI.Style.ink
                                selectionColor: UI.Style.textSelection
                                font.family: UI.Style.fontFamily
                                font.letterSpacing: UI.Style.tracking(UI.Style.px(12))
                                font.pixelSize: UI.Style.px(12)
                                Keys.onPressed: event => {
                                    if (valueMenu.isMenuKey(event)) {
                                        valueMenu.openAtCaret();
                                        event.accepted = true;
                                    }
                                }

                                UI.TextMenu {
                                    id: valueMenu
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: UI.Style.divider
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 14
            Layout.bottomMargin: 14
            columns: 2
            columnSpacing: 10
            uniformCellWidths: true

            UI.Button {
                Layout.fillWidth: true
                implicitHeight: 40
                kind: "primary"
                text: "Yes"
                busy: Agent.busy
                enabled: card.canSubmit
                onClicked: card.submit()
            }

            UI.Button {
                Layout.fillWidth: true
                implicitHeight: 40
                text: "No"
                onClicked: Agent.cancel()
            }
        }
    }
}
