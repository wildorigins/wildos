import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Editing a rule (MailMsRuleEdit.dc): its name, conditions matched on any or
// all, actions, whether later rules still run, and whether to run it now on
// what is already in the Inbox, with how many messages that would be.
UI.Dialog {
    id: editor

    signal saved(var rule, bool runNow)
    signal deleted(string id)

    property var rule: ({})
    property var conditions: []
    property var actions: []
    property string match: "any"
    // Typing in a condition changes it in place (rebuilding the rows would
    // take the cursor away); this counts those changes for what reads them.
    property int revision: 0
    readonly property var account: MailEngine.accounts.find(a => a.id === rule.accountId) || ({})
    readonly property var folders: MailEngine.moveTargets(rule.accountId || "")
        .filter(f => f.role !== "outbox" && f.role !== "drafts")
        .map(f => ({ value: f.fullName, text: f.name }))
    readonly property var draft: revision >= 0 && Object.assign({}, rule, { name: nameField.text.trim(), match: match, conditions: conditions, actions: actions, stop: stopBox.checked })
    readonly property int matching: MailEngine.countRuleMatches(draft)

    readonly property var fieldChoices: [
        { value: "from", text: qsTr("From") },
        { value: "to", text: qsTr("Sent to") },
        { value: "subject", text: qsTr("Subject") },
        { value: "list", text: qsTr("Sent to a mailing list") },
        { value: "attachment", text: qsTr("Has an attachment") }
    ]
    readonly property var actionChoices: [
        { value: "move", text: qsTr("Move to folder") },
        { value: "read", text: qsTr("Mark as read") },
        { value: "flag", text: qsTr("Flag") },
        { value: "pin", text: qsTr("Pin to the top") },
        { value: "archive", text: qsTr("Archive") },
        { value: "delete", text: qsTr("Delete") }
    ]

    function edit(r) {
        rule = r;
        nameField.text = r.name || "";
        match = r.match || "any";
        conditions = JSON.parse(JSON.stringify(r.conditions || []));
        actions = JSON.parse(JSON.stringify(r.actions || []));
        stopBox.checked = r.stop !== false;
        runNowBox.checked = false;
        open();
        nameField.forceActiveFocus();
    }
    function setCondition(i, change) {
        const list = conditions.slice();
        list[i] = Object.assign({}, list[i], change);
        conditions = list;
    }
    function setAction(i, change) {
        const list = actions.slice();
        list[i] = Object.assign({}, list[i], change);
        actions = list;
    }
    readonly property bool complete: revision >= 0 && nameField.text.trim().length > 0 && conditions.length > 0 && actions.length > 0
        && conditions.every(c => c.field === "list" || c.field === "attachment" || (c.text || "").trim().length > 0)
        && actions.every(a => a.type !== "move" || (a.folder || "").length > 0)

    title: rule.id ? qsTr("Edit rule") : qsTr("New rule")
    text: qsTr("For new mail in %1").arg(account.email || "")

    ColumnLayout {
        Layout.fillWidth: true
        Layout.preferredWidth: 560
        spacing: 12

        UI.Label {
            text: qsTr("Name")
            size: 13
            font.weight: UI.Style.weightMedium
        }
        UI.TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("For example, Bills and statements")
        }

        RowLayout {
            spacing: 8
            UI.Label {
                text: qsTr("If")
                size: 13
                font.weight: UI.Style.weightMedium
            }
            UI.ComboBox {
                model: [{ value: "any", text: qsTr("any") }, { value: "all", text: qsTr("all") }]
                currentIndex: editor.match === "all" ? 1 : 0
                onActivated: function (i) {
                    editor.match = i === 1 ? "all" : "any";
                }
            }
            UI.Label {
                text: qsTr("of these are true")
                size: 13
            }
        }
        Repeater {
            model: editor.conditions
            delegate: RowLayout {
                id: conditionRow
                required property var modelData
                required property int index
                readonly property bool needsText: modelData.field !== "list" && modelData.field !== "attachment"
                Layout.fillWidth: true
                spacing: 8
                UI.ComboBox {
                    Layout.preferredWidth: 190
                    model: editor.fieldChoices
                    currentIndex: editor.fieldChoices.findIndex(c => c.value === conditionRow.modelData.field)
                    onActivated: function (i) {
                        editor.setCondition(conditionRow.index, { field: editor.fieldChoices[i].value });
                    }
                }
                UI.Label {
                    visible: conditionRow.needsText
                    text: qsTr("contains")
                    size: 13
                }
                UI.TextField {
                    visible: conditionRow.needsText
                    Layout.fillWidth: true
                    text: conditionRow.modelData.text || ""
                    placeholderText: conditionRow.modelData.field === "subject" ? qsTr("Words in the subject") : qsTr("A name or address")
                    onTextEdited: {
                        editor.conditions[conditionRow.index].text = text;
                        editor.revision++;
                    }
                }
                Item {
                    visible: !conditionRow.needsText
                    Layout.fillWidth: true
                }
                UI.IconButton {
                    iconName: "close"
                    tip: qsTr("Remove")
                    enabled: editor.conditions.length > 1
                    onClicked: editor.conditions = editor.conditions.filter((c, i) => i !== conditionRow.index)
                }
            }
        }
        UI.LinkButton {
            text: qsTr("Add a condition")
            onClicked: editor.conditions = editor.conditions.concat([{ field: "subject", text: "" }])
        }

        UI.Label {
            Layout.topMargin: 4
            text: qsTr("Then")
            size: 13
            font.weight: UI.Style.weightMedium
        }
        Repeater {
            model: editor.actions
            delegate: RowLayout {
                id: actionRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 8
                UI.ComboBox {
                    Layout.preferredWidth: 190
                    model: editor.actionChoices
                    currentIndex: editor.actionChoices.findIndex(c => c.value === actionRow.modelData.type)
                    onActivated: function (i) {
                        editor.setAction(actionRow.index, { type: editor.actionChoices[i].value });
                    }
                }
                UI.ComboBox {
                    visible: actionRow.modelData.type === "move"
                    Layout.fillWidth: true
                    accessibleName: qsTr("Folder")
                    model: editor.folders
                    currentIndex: editor.folders.findIndex(f => f.value === actionRow.modelData.folder)
                    onActivated: function (i) {
                        editor.setAction(actionRow.index, { folder: editor.folders[i].value });
                    }
                }
                Item {
                    visible: actionRow.modelData.type !== "move"
                    Layout.fillWidth: true
                }
                UI.IconButton {
                    iconName: "close"
                    tip: qsTr("Remove")
                    enabled: editor.actions.length > 1
                    onClicked: editor.actions = editor.actions.filter((a, i) => i !== actionRow.index)
                }
            }
        }
        UI.LinkButton {
            text: qsTr("Add an action")
            onClicked: editor.actions = editor.actions.concat([{ type: "read" }])
        }

        UI.CheckBox {
            id: stopBox
            Layout.topMargin: 4
            text: qsTr("Stop running other rules on this message")
        }
        RowLayout {
            spacing: 8
            UI.CheckBox {
                id: runNowBox
                text: qsTr("Also run it now on mail already in Inbox")
            }
            UI.Label {
                text: editor.matching === 1 ? qsTr("1 message matches") : qsTr("%1 messages match").arg(editor.matching)
                size: 12
                color: UI.Style.inkMuted
            }
        }
    }

    buttons: [
        UI.Button {
            visible: (editor.rule.id || "").length > 0
            text: qsTr("Delete rule")
            danger: true
            onClicked: {
                editor.deleted(editor.rule.id);
                editor.close();
            }
        },
        UI.Button {
            text: qsTr("Cancel")
            onClicked: editor.close()
        },
        UI.Button {
            kind: "primary"
            text: qsTr("Save")
            enabled: editor.complete
            onClicked: {
                editor.saved(editor.draft, runNowBox.checked);
                editor.close();
            }
        }
    ]
}
