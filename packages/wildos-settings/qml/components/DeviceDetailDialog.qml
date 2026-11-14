import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// One device of System > Devices: what is wrong in plain words when it
// needs you, then what it really is, the driver running it and where it is
// connected, and any figures of its own (a drive's health, a battery's).
// The fix button is the same as on its row.
RecoveryDialog {
    id: dialog

    property var row: ({})
    readonly property bool needsYou: row.state === "attention"
    readonly property var facts: {
        const out = [];
        const add = (label, value) => {
            if (value) {
                out.push({ label: label, value: value });
            }
        };
        add("Model", row.model);
        add("Made by", row.maker);
        add("Driver", row.driver ? row.driver : row.kind === "printer" ? "" : "None");
        add("Connected", row.connection);
        for (const e of (row.extra || [])) {
            add(e.label, e.value);
        }
        add("Hardware ID", row.hardwareId);
        return out;
    }

    signal act(var row)

    function openFor(r) {
        row = r;
        open();
    }

    title: row.name || ""
    iconName: row.icon || "chip"
    dialogWidth: 560

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        PathIcon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            name: dialog.needsYou ? "triangle-alert" : dialog.row.state === "off" ? "power-round" : "status-ok"
            size: 15
            strokeWidth: 2.2
            color: dialog.needsYou ? (UI.Style.dark ? "#ffc46a" : UI.Style.warning)
                : dialog.row.state === "off" ? UI.Style.inkMuted
                : (UI.Style.dark ? "#5fd38d" : "#1d7a4c")
        }

        UI.Label {
            Layout.fillWidth: true
            text: dialog.needsYou ? dialog.row.problem : (dialog.row.state === "off" ? "" : "Working. ") + dialog.row.line + "."
            size: 13.5
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.topMargin: 6
        columns: 2
        columnSpacing: 18
        rowSpacing: 8

        Repeater {
            model: dialog.facts

            UI.Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 0
                Layout.alignment: Qt.AlignTop
                text: modelData.label
                size: 13
                color: UI.Style.inkMuted
            }
        }

        Repeater {
            model: dialog.facts

            UI.Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 1
                Layout.fillWidth: true
                text: modelData.value
                size: 13
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }

    buttons: [
        UI.Button {
            visible: dialog.needsYou && (dialog.row.action || "") !== ""
            implicitHeight: 32
            text: dialog.row.actionLabel || ""
            onClicked: {
                const r = dialog.row;
                dialog.close();
                dialog.act(r);
            }
        },
        UI.Button {
            implicitHeight: 32
            kind: "primary"
            text: "Close"
            onClicked: dialog.close()
        }
    ]
}
