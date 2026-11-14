import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Security, as a plain card: what security-status reports (through
// Protection), one row per protection with its state, and Check now. Home
// shows the three that matter most, anything that needs a look first, with
// a last line for the rest; Privacy and security shows them all at 40px.
// Only a protection that needs a look changes colour: an amber icon and
// state. One that is fine gets the green tick; one that is neither (not
// used in the live session, not supported here, or a warning the person
// chose to ignore) gets a quiet icon, so a tick never claims more than is
// true.
HomeCard {
    id: card

    // Home's card: three rows and a last line. Off on Privacy and security.
    property bool compact: true
    property date now: new Date()

    readonly property var levelNames: ({ standard: "Standard", strict: "Strict", maximum: "Maximum" })

    // On Home the three most important, needs-a-look first; on Privacy and
    // security the three the design names plus anything else needing a look.
    // 32-bit apps, when on, come straight after anything needing a look:
    // the card should not read as all on without saying so.
    readonly property var shown: compact
        ? Protection.attention.concat(Protection.tiles.filter(item => item.key === "compat32"))
            .concat(Protection.tiles.filter(item => !Protection.attention.includes(item) && item.key !== "compat32")).slice(0, 3)
        : Protection.tiles
    // The last line names the one people most often ask about first.
    readonly property var order: ["devices", "apps", "secureboot", "network", "encryption", "updates", "compat32"]
    readonly property var hidden: Protection.items.filter(item => !shown.includes(item))
        .sort((a, b) => order.indexOf(a.key) - order.indexOf(b.key))
    readonly property var hiddenAttention: hidden.filter(item => !Protection.good(item))

    function tone(item) {
        if (!Protection.good(item)) {
            return "warn";
        }
        // 32-bit apps on is a choice that lowers protection, never a tick.
        if (Protection.ignored.includes(item.key) || item.key === "compat32") {
            return "quiet";
        }
        const v = Protection.status[item.key];
        return v === "on" || v === "current" ? "ok" : "quiet";
    }

    function rowValue(item) {
        if (!compact && item.key === "updates" && Protection.status.updates === "current" && System.updatePolicy.auto === true) {
            return "Up to date, installed automatically";
        }
        return Protection.value(item);
    }

    readonly property string summary: {
        if (!Protection.checked) {
            return "Couldn't check.";
        }
        const n = Protection.attention.length;
        // 32-bit apps lower protection by choice, so the card says so
        // rather than "All on".
        if (n === 0 && Protection.compat32) {
            return "32-bit apps allowed.";
        }
        return n === 0 ? "All on." : n === 1 ? "1 needs a look." : n + " need a look.";
    }

    iconName: "shield-check"
    headerPadding: compact ? 11 : 13
    title: "Security"
    caption: {
        card.now;
        if (Protection.busy) {
            return Protection.checkStep + ".";
        }
        if (compact) {
            const level = levelNames[System.protectionLevel];
            // With something to look at the line is longer, so the level
            // takes fewer words.
            if (!level) {
                return summary;
            }
            return Protection.allOn ? summary + " Protection level: " + level : summary + " " + level + " protection";
        }
        const when = Format.ago(System.securityChecked);
        const note = Protection.checkNote.length > 0 ? " " + Protection.checkNote : "";
        return (when.length > 0 ? summary + " Checked " + when + "." : summary) + note;
    }

    Timer {
        interval: 30000
        running: card.visible
        repeat: true
        onTriggered: card.now = new Date()
    }

    trailing: UI.Button {
        size: "small"
        text: Protection.busy ? "Checking" : "Check now"
        busy: Protection.busy
        implicitHeight: 30
        onClicked: Protection.checkNow()
    }

    Repeater {
        model: card.shown

        CardRow {
            required property var modelData
            readonly property string rowTone: card.tone(modelData)
            rowHeight: card.compact ? 30 : 40
            titleSize: card.compact ? 13 : 13.5
            valueSize: card.compact ? 12.5 : 13
            iconSize: card.compact ? 16 : 17
            iconName: rowTone === "ok" ? "status-ok" : rowTone === "warn" ? "status-alert" : "info"
            iconColor: rowTone === "ok" ? UI.Style.success : rowTone === "warn" ? UI.Style.warning : UI.Style.iconMuted
            title: modelData.title
            value: card.rowValue(modelData)
            valueColor: rowTone === "warn" ? UI.Style.warning : UI.Style.inkMuted
            // The firewall has a page of its own to change it on.
            onClicked: modelData.key === "firewall" && Firewall.available ? Navigator.open("firewall") : protectionDialog.show(modelData)
        }
    }

    // Nothing to list yet (the first read is still running, or the status
    // tool is missing): one line that says so, never an empty card.
    CardRow {
        visible: card.shown.length === 0
        chevron: false
        enabled: !Protection.busy
        rowHeight: card.compact ? 30 : 40
        title: Protection.busy ? "Results show here in a moment" : "Wild OS couldn't read your protection"
        titleColor: UI.Style.inkMuted
        value: Protection.busy ? "" : "Check again"
        valueColor: UI.Style.accentText
        onClicked: Protection.checkNow()
    }

    footer: CardRow {
        visible: card.compact && card.hidden.length > 0
        link: true
        title: {
            const rest = card.hiddenAttention;
            if (rest.length === 1) {
                return rest[0].title + " needs a look";
            }
            if (rest.length > 1) {
                return rest.length + " more need a look";
            }
            const others = card.hidden.length - 1;
            const allOn = card.hidden.every(item => item.key !== "compat32" && (Protection.status[item.key] === "on" || Protection.status[item.key] === "current"));
            const head = card.hidden.length > 0 ? card.hidden[0].title : "";
            const more = others > 0 ? head + " and " + others + " more" : head;
            return more + (allOn ? (others > 0 ? ", all on" : ", on") : ", all fine");
        }
        titleColor: card.hiddenAttention.length > 0 ? UI.Style.warning : UI.Style.accentText
        onClicked: Navigator.open("privacy")
    }

    ProtectionDialog {
        id: protectionDialog
    }
}
