import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// System > Recovery (RecoveryPage.dc): restore points, taken before every
// update and driver install and on demand, each with Go back; then what to
// do if something goes wrong: Reset Wild OS, start-up options and Backup.
// Everything here is the restore-points tool of wildos-recovery, through
// Recovery and its one root helper.
PageScroll {
    id: page

    property bool showAll: false
    readonly property var shown: page.showAll ? Recovery.points : Recovery.points.slice(0, 3)

    function whenText(date) {
        const day = Format.day(date);
        const name = day === "today" ? qsTr("Today") : day === "yesterday" ? qsTr("Yesterday") : day.charAt(0).toUpperCase() + day.slice(1);
        return qsTr("%1, %2").arg(name).arg(Format.time(date));
    }

    function pointDetail(p) {
        const what = p.summary.length > 0 ? p.summary : p.kind === "manual" ? qsTr("Made by you") : "";
        return what.length > 0 ? page.whenText(p.when) + " · " + what : page.whenText(p.when);
    }

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: Recovery
        function onBusyChanged() {
            if (!Recovery.busy && Recovery.error.length > 0 && !goBack.opened && !resetDialog.opened) {
                notice.show(qsTr("Nothing was changed"), Recovery.error, true);
            }
        }
        function onFinished(what) {
            if (what === "startup") {
                Recovery.restart();
            }
        }
    }

    // The hero: restore points are on (or off), and make one now.
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: heroRow.implicitHeight + 32
        radius: 12
        color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.035)
        border.width: 1
        border.color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)

        RowLayout {
            id: heroRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 18

            Rectangle {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 52
                radius: 26
                color: Recovery.enabled ? Qt.rgba(95 / 255, 211 / 255, 141 / 255, 0.16) : UI.Style.warningSoft

                PathIcon {
                    anchors.centerIn: parent
                    name: "history"
                    size: 26
                    strokeWidth: 1.9
                    color: Recovery.enabled ? UI.Style.success : UI.Style.warning
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: !Recovery.available ? qsTr("Restore points aren't available")
                        : Recovery.enabled ? qsTr("Restore points are on") : qsTr("Restore points are off")
                    size: 17
                    font.weight: UI.Style.weightSemiBold
                    wrapMode: Text.Wrap
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: !Recovery.available ? qsTr("This disk's layout can't keep restore points.")
                        : Recovery.enabled ? qsTr("Saved before every update and driver install. Your own files are never touched.")
                        : qsTr("Nothing is saved before updates. Turn them on under Change.")
                    size: 13
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
            }

            UI.Button {
                visible: Recovery.available
                iconName: "plus"
                text: Recovery.busy && Recovery.busyText === qsTranslate("Recovery", "Making a restore point") ? qsTr("Making a restore point") : qsTr("Create a restore point")
                busy: Recovery.busy && Recovery.busyText === qsTranslate("Recovery", "Making a restore point")
                enabled: !Recovery.busy
                onClicked: Recovery.create()
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: Recovery.available
        spacing: 10

        SectionTitle {
            text: qsTr("Restore points")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.shown

                    UI.SettingRow {
                        id: row
                        required property var modelData
                        width: parent.width

                        leading: PathIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "history"
                            size: 20
                            strokeWidth: 1.8
                            color: UI.Style.ink
                        }
                        title: modelData.label
                        description: page.pointDetail(modelData)

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            implicitHeight: 32
                            text: qsTr("Go back")
                            enabled: !Recovery.busy && !Recovery.restartPending
                            onClicked: goBack.openFor(row.modelData.id)
                        }
                    }
                }

                UI.SettingRow {
                    visible: Recovery.points.length === 0
                    title: qsTr("No restore points yet")
                    description: qsTr("The first one is made now, and then before every update.")
                }

                UI.SettingRow {
                    visible: Recovery.restartPending
                    title: qsTr("Restart to finish going back")
                    description: qsTr("The earlier version is ready. It takes over at the next start.")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "primary"
                        text: qsTr("Restart now")
                        onClicked: Recovery.restart()
                    }
                }

                // The footer row: how many are kept, and the links.
                Item {
                    width: parent.width
                    height: 46

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: UI.Style.borderSubtle
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 18

                        UI.Label {
                            Layout.fillWidth: true
                            text: qsTr("Keeps the last %1, using up to %2 GB. The oldest go first.").arg(Recovery.keep).arg(Recovery.maxGb)
                            size: 12.5
                            color: UI.Style.inkMuted
                            elide: Text.ElideRight
                        }

                        UI.LinkButton {
                            visible: Recovery.points.length > 3
                            text: page.showAll ? qsTr("Show fewer") : qsTr("See all %1").arg(Recovery.points.length)
                            onClicked: page.showAll = !page.showAll
                        }

                        UI.LinkButton {
                            text: qsTr("Change")
                            onClicked: limits.start()
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("If something goes wrong")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    id: resetRow
                    width: parent.width
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "rotate-ccw"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    title: qsTr("Reset Wild OS")
                    description: qsTr("Back to how it was when installed. Keep your files, or remove everything.")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        implicitHeight: 32
                        text: qsTr("Reset")
                        enabled: !Recovery.busy
                        onClicked: resetDialog.start()
                    }
                }

                UI.SettingRow {
                    width: parent.width
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "power-round"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    title: qsTr("Start-up options")
                    description: Recovery.startupRecovery
                        ? qsTr("Earlier versions, safe mode and repair tools. Or hold Shift while the PC starts.")
                        : qsTr("Earlier versions, safe mode and repair tools. Available once the copy of Wild OS is on this PC.")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        implicitHeight: 32
                        text: qsTr("Restart now")
                        enabled: Recovery.startupRecovery && !Recovery.busy
                        onClicked: Recovery.restartToRecovery()
                    }
                }

                UI.SettingRow {
                    width: parent.width
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "hard-drive"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    title: qsTr("Back up your files")
                    description: Format.isValid(Recovery.lastBackup)
                        ? qsTr("Restore points cover the system only. Backup keeps your files, last done %1 at %2.").arg(Format.dayText(Recovery.lastBackup)).arg(Format.time(Recovery.lastBackup))
                        : qsTr("Restore points cover the system only. Backup keeps your files.")
                    enabled: Recovery.backupInstalled

                    UI.LinkButton {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Open Backup")
                        enabled: Recovery.backupInstalled
                        onClicked: Recovery.openBackup()
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        PathIcon {
            name: "lock-round"
            size: 14
            strokeWidth: 2
            color: UI.Style.inkSubtle
        }

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Restore points stay on this PC, inside the encrypted disk.")
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }

        UI.LinkButton {
            text: qsTr("Learn more")
            size: 13
            onClicked: Qt.openUrlExternally("https://help.wildos.io/troubleshooting/restore-points/")
        }
    }

    GoBackDialog {
        id: goBack
    }

    ResetDialog {
        id: resetDialog
    }

    // Change: how many to keep, how much space, or off.
    RecoveryDialog {
        id: limits

        property int keep: 5
        property int gb: 20
        property bool on: true

        function start() {
            keep = Recovery.keep;
            gb = Recovery.maxGb;
            on = Recovery.enabled;
            open();
        }

        title: qsTr("Restore points")
        dialogWidth: 480

        UI.SettingRow {
            Layout.fillWidth: true
            Layout.leftMargin: -18
            Layout.rightMargin: -18
            title: qsTr("Take restore points")
            description: qsTr("Before every update and driver install")

            UI.Toggle {
                anchors.verticalCenter: parent.verticalCenter
                checked: limits.on
                Accessible.name: qsTr("Take restore points")
                onToggled: limits.on = checked
            }
        }

        UI.SettingRow {
            Layout.fillWidth: true
            Layout.leftMargin: -18
            Layout.rightMargin: -18
            title: qsTr("How many to keep")
            enabled: limits.on

            UI.ComboBox {
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: qsTr("How many to keep")
                model: [3, 5, 10].map(n => ({ value: n, text: String(n) }))
                currentIndex: [3, 5, 10].indexOf(limits.keep)
                onActivated: index => limits.keep = [3, 5, 10][index]
            }
        }

        UI.SettingRow {
            Layout.fillWidth: true
            Layout.leftMargin: -18
            Layout.rightMargin: -18
            title: qsTr("Space they can use")
            enabled: limits.on

            UI.ComboBox {
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: qsTr("Space they can use")
                model: [10, 20, 40].map(n => ({ value: n, text: qsTr("%1 GB").arg(n) }))
                currentIndex: [10, 20, 40].indexOf(limits.gb)
                onActivated: index => limits.gb = [10, 20, 40][index]
            }
        }

        UI.Label {
            Layout.fillWidth: true
            text: Recovery.usedBytes > 0 ? qsTr("Using %1 GB now. The oldest go first when either limit is reached.").arg((Recovery.usedBytes / 1073741824).toFixed(1)) : qsTr("The oldest go first when either limit is reached.")
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
        }

        buttons: [
            UI.Button {
                implicitHeight: 32
                text: qsTr("Cancel")
                onClicked: limits.close()
            },
            UI.Button {
                implicitHeight: 32
                kind: "primary"
                text: qsTr("Save")
                busy: Recovery.busy
                onClicked: {
                    Recovery.setLimits(limits.keep, limits.gb, limits.on);
                    limits.close();
                }
            }
        ]
    }
}
