import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Settings, Rules (MailMsSetRules.dc): each account's rules in the order
// they run, switched on and off and dragged into order, with New rule and
// Run rules now. A rule opens in the editor (MailMsRuleEdit.dc).
ColumnLayout {
    id: page

    property string account: MailEngine.accounts.length > 0 ? MailEngine.accounts[0].id : ""
    readonly property var rules: MailEngine.rules.filter(r => r.accountId === account)
    property string dragging: ""

    spacing: 14

    // Rules are kept as one list for every account; this account's keep
    // their order among themselves.
    function replaceAccountRules(list) {
        const others = MailEngine.rules.filter(r => r.accountId !== account);
        MailEngine.setRules(others.concat(list));
    }
    function updateRule(rule) {
        const list = rules.map(r => r.id === rule.id ? rule : r);
        if (!rules.some(r => r.id === rule.id)) {
            list.push(rule);
        }
        replaceAccountRules(list);
    }
    function removeRule(id) {
        replaceAccountRules(rules.filter(r => r.id !== id));
    }
    function moveRule(id, to) {
        const list = rules.slice();
        const from = list.findIndex(r => r.id === id);
        to = Math.max(0, Math.min(list.length - 1, to));
        if (from < 0 || from === to) {
            return;
        }
        const [rule] = list.splice(from, 1);
        list.splice(to, 0, rule);
        replaceAccountRules(list);
    }

    // What a rule does, in words, for its row.
    function describe(rule) {
        const fields = { from: qsTr("From"), to: qsTr("Sent to"), subject: qsTr("Subject has"), list: qsTr("Sent to a mailing list"), attachment: qsTr("Has an attachment") };
        const when = (rule.conditions || []).map(c => c.field === "list" || c.field === "attachment" ? fields[c.field] : fields[c.field] + " " + c.text)
            .join(rule.match === "all" ? qsTr(" and ") : qsTr(" or "));
        const folders = MailEngine.moveTargets(rule.accountId);
        const does = (rule.actions || []).map(a => {
            switch (a.type) {
            case "move": return (folders.find(f => f.fullName === a.folder) || { name: a.folder }).name;
            case "read": return qsTr("Mark as read");
            case "flag": return qsTr("Flag");
            case "pin": return qsTr("Pin to the top");
            case "archive": return qsTr("Archive");
            case "delete": return qsTr("Delete");
            }
            return "";
        }).join(", ");
        return when + "  →  " + does;
    }

    ListModel {
        id: order
    }
    function syncOrder() {
        order.clear();
        for (const r of rules) {
            order.append({ ruleId: r.id });
        }
    }
    onRulesChanged: if (dragging.length === 0) syncOrder()
    Component.onCompleted: syncOrder()

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        UI.Button {
            text: qsTr("New rule")
            iconName: "plus"
            kind: "primary"
            enabled: page.account.length > 0
            onClicked: editor.edit({
                id: "", name: "", enabled: true, accountId: page.account, match: "any",
                conditions: [{ field: "from", text: "" }], actions: [{ type: "move", folder: "" }], stop: true
            })
        }
        UI.Button {
            text: qsTr("Run rules now")
            iconName: "refresh"
            enabled: page.rules.length > 0
            onClicked: MailEngine.runRules(page.account)
        }
        Item {
            Layout.fillWidth: true
        }
        UI.Label {
            visible: MailEngine.accounts.length > 1
            text: qsTr("Account")
            size: 13
        }
        UI.ComboBox {
            visible: MailEngine.accounts.length > 1
            Layout.preferredWidth: 260
            model: MailEngine.accounts.map(a => ({ value: a.id, text: a.email }))
            currentIndex: MailEngine.accounts.findIndex(a => a.id === page.account)
            onActivated: function (i) {
                page.account = MailEngine.accounts[i].id;
            }
        }
    }

    UI.Label {
        visible: page.rules.length === 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        size: 13
        color: UI.Style.inkMuted
        text: qsTr("No rules yet. A rule can move mail from someone to a folder, mark it read, flag it, pin it, archive it or delete it.")
    }

    ListView {
        id: list
        Layout.fillWidth: true
        implicitHeight: contentHeight
        interactive: false
        spacing: 6
        model: order
        move: Transition {
            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
        }
        displaced: Transition {
            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
        }

        delegate: Rectangle {
            id: row
            required property string ruleId
            required property int index
            readonly property var rule: page.rules.find(r => r.id === ruleId) || ({})
            readonly property bool lifted: page.dragging === ruleId
            width: list.width
            height: 60
            radius: UI.Style.radiusMd
            color: UI.Style.surfaceRaised
            border.width: activeFocus ? 2 : 1
            border.color: activeFocus ? UI.Style.accent : UI.Style.borderSubtle
            z: lifted ? 10 : 0
            scale: lifted && !UI.Style.reducedMotion ? 1.01 : 1
            Behavior on scale {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
            activeFocusOnTab: true
            Accessible.role: Accessible.ListItem
            Accessible.name: rule.name || ""
            Keys.onPressed: function (event) {
                if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Up) {
                    page.moveRule(ruleId, index - 1);
                    event.accepted = true;
                } else if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Down) {
                    page.moveRule(ruleId, index + 1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    editor.edit(row.rule);
                    event.accepted = true;
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 10

                Item {
                    implicitWidth: 16
                    implicitHeight: 40
                    visible: page.rules.length > 1
                    Accessible.name: qsTr("Drag to re-order")
                    MailIcon {
                        anchors.centerIn: parent
                        name: "grip"
                        size: 14
                        color: UI.Style.iconMuted
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        preventStealing: true
                        property int target: 0
                        onPressed: {
                            page.dragging = row.ruleId;
                            target = row.index;
                        }
                        onPositionChanged: function (mouse) {
                            const y = mapToItem(list, mouse.x, mouse.y).y;
                            const at = list.indexAt(10, y);
                            if (at >= 0 && at !== row.index) {
                                order.move(row.index, at, 1);
                                target = at;
                            }
                        }
                        onReleased: {
                            const id = page.dragging;
                            page.dragging = "";
                            page.moveRule(id, target);
                            page.syncOrder();
                        }
                        onCanceled: {
                            page.dragging = "";
                            page.syncOrder();
                        }
                    }
                }
                UI.Toggle {
                    checked: row.rule.enabled !== false
                    accessibleName: qsTr("Rule on")
                    onToggled: page.updateRule(Object.assign({}, row.rule, { enabled: checked }))
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    UI.Label {
                        Layout.fillWidth: true
                        text: row.rule.name || qsTr("Rule")
                        size: 13.5
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideRight
                        opacity: row.rule.enabled !== false ? 1 : 0.6
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: page.describe(row.rule)
                        size: 12
                        color: UI.Style.inkMuted
                        elide: Text.ElideRight
                    }
                }
                UI.Chip {
                    text: qsTr("On this PC")
                }
                UI.IconButton {
                    iconName: "edit"
                    tip: qsTr("Edit rule")
                    onClicked: editor.edit(row.rule)
                }
                UI.IconButton {
                    id: more
                    iconName: "more"
                    tip: qsTr("More")
                    onClicked: {
                        ruleMenu.ruleId = row.ruleId;
                        ruleMenu.popup(more, 0, more.height);
                    }
                }
            }
            TapHandler {
                onDoubleTapped: editor.edit(row.rule)
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: page.rules.length > 0
        text: qsTr("Rules run from the top down on mail as it arrives. Drag to change the order. They run on this PC, and catch up when Mail next opens.")
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.WordWrap
    }

    UI.Menu {
        id: ruleMenu
        property string ruleId
        entries: {
            const at = page.rules.findIndex(r => r.id === ruleMenu.ruleId);
            return [
                { text: qsTr("Run now"), icon: "refresh", run: () => MailEngine.runRule(ruleMenu.ruleId) },
                { text: qsTr("Move up"), icon: "chevron-up", enabled: at > 0, run: () => page.moveRule(ruleMenu.ruleId, at - 1) },
                { text: qsTr("Move down"), icon: "chevron-down", enabled: at >= 0 && at < page.rules.length - 1, run: () => page.moveRule(ruleMenu.ruleId, at + 1) },
                { text: qsTr("Delete rule"), icon: "delete", danger: true, run: () => page.removeRule(ruleMenu.ruleId) }
            ];
        }
    }

    RuleEditor {
        id: editor
        onSaved: function (rule, runNow) {
            const id = rule.id.length > 0 ? rule.id : Date.now().toString(36) + Math.random().toString(36).slice(2, 8);
            const saved = Object.assign({}, rule, { id: id });
            page.updateRule(saved);
            if (runNow) {
                MailEngine.runRule(id);
            }
        }
        onDeleted: function (id) {
            page.removeRule(id);
        }
    }
}
