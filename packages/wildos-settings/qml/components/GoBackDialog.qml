import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Going back to a restore point (RecoveryGoBack.dc): what goes back, what
// stays as it is, holding the update back, and Go back and restart. Opened
// from System > Recovery and from Updates' "Go back" on the latest Wild OS
// build.
RecoveryDialog {
    id: dialog

    property var point: ({})
    readonly property string label: point.label || ""
    readonly property bool before: label.startsWith("Before ")
    readonly property var apps: point.apps || []
    readonly property bool canHold: point.holdable === true && (point.build || 0) > 0
    property bool hold: true

    function openFor(id) {
        point = Recovery.point(id);
        hold = true;
        open();
    }

    function lowerFirst(text) {
        return text.length > 0 ? text.charAt(0).toLowerCase() + text.slice(1) : text;
    }

    function whenText(date) {
        if (!Format.isValid(date)) {
            return "";
        }
        return Format.dayAndTime(date);
    }

    function goesBackSystem() {
        if (point.system && point.driver) {
            return qsTr("System files and the graphics driver");
        }
        if (point.driver) {
            return qsTr("The graphics driver");
        }
        if (point.system || apps.length === 0) {
            return apps.length === 0 && !point.system ? qsTr("System files and apps, as they were then") : qsTr("System files");
        }
        return "";
    }

    function goesBackApps() {
        if (apps.length === 0) {
            return "";
        }
        const shown = apps.slice(0, 3);
        const more = apps.length - shown.length;
        const names = more > 0 ? qsTr("%1 and %2").arg(shown.join(", ")).arg(Format.count(more, qsTr("1 more app"), qsTr("%n more apps", "", more)))
            : shown.length === 1 ? shown[0] : qsTr("%1 and %2").arg(shown.slice(0, -1).join(", ")).arg(shown[shown.length - 1]);
        return apps.length === 1 ? qsTr("%1, to its earlier version").arg(names) : qsTr("%1, to their earlier versions").arg(names);
    }

    title: before ? qsTr("Go back to %1?").arg(lowerFirst(label)) : qsTr("Go back to this restore point?")
    dialogWidth: 560

    UI.Label {
        Layout.fillWidth: true
        text: {
            const when = dialog.whenText(dialog.point.when);
            const quick = Recovery.backend === "btrfs";
            if (dialog.before) {
                const what = dialog.lowerFirst(dialog.label);
                return quick ? qsTr("The PC goes back to how it was %1, just %2. It takes about a minute and restarts once.").arg(when).arg(what)
                    : qsTr("The PC goes back to how it was %1, just %2. It takes a few minutes and restarts once.").arg(when).arg(what);
            }
            return quick ? qsTr("The PC goes back to how it was %1. It takes about a minute and restarts once.").arg(when)
                : qsTr("The PC goes back to how it was %1. It takes a few minutes and restarts once.").arg(when);
        }
        size: 13.5
        lineHeight: 1.5
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 10

            UI.Label {
                text: qsTr("GOES BACK")
                size: 12.5
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkSubtle
            }

            Repeater {
                model: [dialog.goesBackSystem(), dialog.goesBackApps()].filter(t => t.length > 0)

                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 9

                    PathIcon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        name: "undo-2"
                        size: 15
                        strokeWidth: 2.2
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
            spacing: 10

            UI.Label {
                text: qsTr("STAYS AS IT IS")
                size: 12.5
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkSubtle
            }

            Repeater {
                model: [qsTr("Your files, Mail, Calendar and Notes"), qsTr("Your settings, passwords and Wi-Fi"), qsTr("Windows mode and its apps")]

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
        visible: dialog.canHold
        implicitHeight: holdRow.implicitHeight + 24
        radius: 10
        color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.03)
        border.width: 1
        border.color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)

        RowLayout {
            id: holdRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 12

            UI.CheckBox {
                Layout.alignment: Qt.AlignTop
                checked: dialog.hold
                Accessible.name: qsTr("Hold back the build %1 update for 2 weeks").arg(dialog.point.build || "")
                onToggled: dialog.hold = checked
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Hold back the build %1 update for 2 weeks").arg(dialog.point.build || "")
                    size: 13
                    wrapMode: Text.Wrap
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("So it doesn't install again straight away. Security fixes still come through.")
                    size: 12
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        PathIcon {
            name: "undo-2"
            size: 14
            strokeWidth: 2
            color: UI.Style.inkSubtle
        }

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Changed your mind later? Today's version is saved as a restore point too.")
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: Recovery.error.length > 0 && !Recovery.busy
        text: Recovery.error
        size: 12.5
        color: UI.Style.danger
        wrapMode: Text.Wrap
    }

    buttons: [
        UI.Button {
            implicitHeight: 32
            text: qsTr("Cancel")
            enabled: !Recovery.busy
            onClicked: dialog.close()
        },
        UI.Button {
            implicitHeight: 32
            kind: "primary"
            iconName: "restore"
            text: qsTr("Go back and restart")
            busy: Recovery.busy
            onClicked: Recovery.goBack(dialog.point.id, dialog.canHold && dialog.hold)
        }
    ]

    Connections {
        target: Recovery
        function onFinished(what) {
            if (what === "go-back" && dialog.opened) {
                dialog.close();
                Recovery.restart();
            }
        }
    }
}
