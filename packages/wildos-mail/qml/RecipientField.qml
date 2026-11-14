import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// To, Cc or Bcc (MailMsCompose.dc): each address becomes a chip with the
// person's initials once a comma, Enter or Tab follows it. A focus scope,
// so focusing the field puts the cursor in it.
FocusScope {
    id: field

    property string label
    property string placeholder
    property var people: []
    // Where Tab goes from here.
    property Item next: null
    readonly property string text: people.concat(input.text.trim().length > 0 ? [input.text.trim()] : []).join(", ")
    default property alias trailing: extras.data

    signal edited()

    function setText(value) {
        people = value.split(/\s*,\s*/).filter(s => s.length > 0);
        input.text = "";
    }
    function commit() {
        const parts = input.text.split(/[,;]/).map(s => s.trim()).filter(s => s.length > 0);
        if (parts.length > 0) {
            people = people.concat(parts);
            input.text = "";
            edited();
        }
    }
    function nameOf(address) {
        const m = address.match(/^\s*"?([^"<]*?)"?\s*<([^>]+)>\s*$/);
        return m && m[1].length > 0 ? m[1] : address;
    }
    function addressOf(address) {
        const m = address.match(/<([^>]+)>/);
        return m ? m[1] : address;
    }

    implicitHeight: Math.max(40, flow.implicitHeight + 12)

    MouseArea {
        anchors.fill: parent
        z: -1
        cursorShape: Qt.IBeamCursor
        onClicked: input.forceActiveFocus()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: UI.Style.borderSubtle
    }

    UI.Label {
        id: caption
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        width: field.label.length > 0 ? 56 : 0
        text: field.label
        size: 13
        color: UI.Style.inkMuted
    }

    Flow {
        id: flow
        anchors.left: caption.right
        anchors.leftMargin: field.label.length > 0 ? 10 : 0
        anchors.right: extras.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Repeater {
            model: field.people

            delegate: Rectangle {
                id: chip
                required property string modelData
                required property int index
                height: 26
                width: chipRow.implicitWidth + 9
                radius: 13
                color: UI.Style.accentChip

                Row {
                    id: chipRow
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    SenderBadge {
                        anchors.verticalCenter: parent.verticalCenter
                        size: 20
                        initials: Apps.initialsOf(field.nameOf(chip.modelData))
                        colourIndex: Apps.colourOf(field.addressOf(chip.modelData))
                    }
                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: field.nameOf(chip.modelData)
                        size: 13
                    }
                    MailIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "close"
                        size: 12
                        color: UI.Style.inkMuted

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const list = field.people.slice();
                                list.splice(chip.index, 1);
                                field.people = list;
                                field.edited();
                            }
                        }
                    }
                }

                UI.Tooltip {
                    visible: chipHover.hovered
                    text: field.addressOf(chip.modelData)
                }
                HoverHandler {
                    id: chipHover
                }
            }
        }

        TextInput {
            id: input
            focus: true
            width: 200
            height: 26
            verticalAlignment: TextInput.AlignVCenter
            color: UI.Style.ink
            selectionColor: UI.Style.textSelection
            font.family: UI.Style.fontFamily
            font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
            font.pixelSize: UI.Style.px(13)
            clip: true
            onTextEdited: {
                if (/[,;]/.test(text)) {
                    field.commit();
                }
                field.edited();
            }
            // The text menu holds focus while it is open; what is typed so
            // far is not a whole address yet.
            onEditingFinished: if (!inputMenu.opened) field.commit()

            Text {
                visible: input.text.length === 0 && field.people.length === 0
                anchors.verticalCenter: parent.verticalCenter
                text: field.placeholder
                color: UI.Style.inkPlaceholder
                font: input.font
            }
            Keys.onReturnPressed: field.commit()
            Keys.onTabPressed: function (event) {
                field.commit();
                if (field.next) {
                    field.next.forceActiveFocus();
                    event.accepted = true;
                }
            }
            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_Backspace && text.length === 0 && field.people.length > 0) {
                    field.people = field.people.slice(0, -1);
                    field.edited();
                    event.accepted = true;
                } else if (inputMenu.isMenuKey(event)) {
                    inputMenu.openAtCaret();
                    event.accepted = true;
                }
            }

            UI.TextMenu {
                id: inputMenu
                onFocusLeft: field.commit()
            }
        }
    }

    Row {
        id: extras
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12
    }
}
