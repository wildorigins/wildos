import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// System > Start-up disk (SettingsStartupDisk.dc): every system this PC can
// start, which one starts by default, restarting straight into another, and
// when the start-up manager shows. All of it is wildos-startup's, through
// StartupDisk and its two helpers. The page is read again each time it
// opens, because a USB stick may have been plugged in since.
PageScroll {
    id: page

    readonly property bool managerOn: StartupDisk.mode !== "off"
    readonly property var waits: [3, 5, 10, 15, 30]

    function sizeText(bytes) {
        const gb = bytes / 1e9;
        if (gb >= 1000) {
            const tb = Math.round(gb / 100) / 10;
            return qsTr("%1 TB").arg(tb.toLocaleString(Qt.locale(), "f", tb % 1 === 0 ? 0 : 1));
        }
        return qsTr("%1 GB").arg(Math.round(gb));
    }

    function placeText(system) {
        if (system.removable) {
            return qsTr("USB stick");
        }
        switch (system.disk_rank) {
        case 0: return qsTr("Main disk");
        case 1: return qsTr("Second disk");
        case 2: return qsTr("Third disk");
        default: return qsTr("Another disk");
        }
    }

    function detailText(system) {
        const place = page.placeText(system);
        if (system.current) {
            return qsTr("%1 · this is what you are using now").arg(place);
        }
        return system.disk_bytes > 0 ? place + " · " + page.sizeText(system.disk_bytes) : place;
    }

    function restartText(system) {
        return system.kind === "windows" ? qsTr("Restart into Windows") : qsTr("Restart into %1").arg(system.name);
    }

    Component.onCompleted: StartupDisk.refresh()

    Notice {
        id: notice
        Layout.fillWidth: true
    }

    Connections {
        target: StartupDisk
        function onFailed(message) {
            notice.show(qsTr("Nothing was changed"), message, true);
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("What this PC can start")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: StartupDisk.systems

                    // The board's own row: a 40px tile, the name in
                    // semibold over a 12.5px line, then the default mark,
                    // the restart button and the radio.
                    Item {
                        id: entry

                        required property var modelData
                        required property int index

                        width: parent.width
                        implicitHeight: Math.max(62, entryRow.implicitHeight + 20)

                        Rectangle {
                            visible: entry.index > 0
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 1
                            color: UI.Style.divider
                        }

                        RowLayout {
                            id: entryRow
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            spacing: 16

                            SystemTile {
                                kind: entry.modelData.kind
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                UI.Label {
                                    Layout.fillWidth: true
                                    text: entry.modelData.name
                                    size: 14
                                    font.weight: UI.Style.weightSemiBold
                                }

                                UI.Label {
                                    Layout.fillWidth: true
                                    text: page.detailText(entry.modelData)
                                    size: 12.5
                                    color: UI.Style.inkMuted
                                }
                            }

                            UI.Label {
                                visible: entry.modelData["default"]
                                text: qsTr("Starts by default")
                                size: 12.5
                                color: UI.Style.accentText
                            }

                            UI.Button {
                                visible: !entry.modelData.current
                                implicitHeight: 32
                                text: page.restartText(entry.modelData)
                                enabled: !StartupDisk.busy
                                onClicked: confirm.ask(entry.modelData.id, entry.modelData.name)
                            }

                            // A USB stick cannot be the default; the gap
                            // keeps its button in line with the others.
                            Item {
                                Layout.leftMargin: 26
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 28

                                UI.RadioButton {
                                    anchors.centerIn: parent
                                    visible: !entry.modelData.removable
                                    checked: entry.modelData["default"]
                                    enabled: !StartupDisk.busy
                                    Accessible.name: qsTr("Start %1 by default").arg(entry.modelData.name)
                                    onClicked: {
                                        if (!entry.modelData["default"]) {
                                            StartupDisk.setDefault(entry.modelData.id);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                UI.SettingRow {
                    visible: !StartupDisk.loaded
                    title: StartupDisk.unreadable ? qsTr("This PC's start-up settings could not be read")
                                                  : qsTr("Looking for what this PC can start")
                }

                Item {
                    visible: StartupDisk.loaded && StartupDisk.systems.length < 2
                    width: parent.width
                    height: 46

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: UI.Style.divider
                    }

                    UI.Label {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        text: qsTr("No other system found on this PC.")
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: StartupDisk.loaded
        spacing: 10

        SectionTitle {
            text: qsTr("The start-up manager")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "app-window"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    title: qsTr("Use the start-up manager")
                    description: qsTr("A picture menu of everything this PC can start.")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.managerOn
                        enabled: !StartupDisk.busy
                        onToggled: StartupDisk.setMode(checked ? "hold" : "off")
                    }
                }

                UI.SettingRow {
                    iconName: "keyboard"
                    title: qsTr("Show it")
                    description: qsTr("Hold Space while the PC starts, or see it at every start.")
                    enabled: page.managerOn

                    Segmented {
                        anchors.verticalCenter: parent.verticalCenter
                        options: [
                            { value: "hold", text: qsTr("Only when I hold Space") },
                            { value: "always", text: qsTr("At every start") }
                        ]
                        value: StartupDisk.mode === "always" ? "always" : "hold"
                        enabled: !StartupDisk.busy
                        onPicked: value => StartupDisk.setMode(value)
                    }
                }

                UI.SettingRow {
                    id: waitRow
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "timer"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                        opacity: waitRow.enabled ? 1 : UI.Style.disabledOpacity
                    }
                    title: qsTr("Wait before starting the default")
                    description: qsTr("Only used when the manager shows at every start.")
                    enabled: StartupDisk.mode === "always"

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 130
                        model: page.waits.map(n => ({ value: n, text: qsTr("%1 seconds").arg(n) }))
                        currentIndex: page.waits.indexOf(StartupDisk.wait)
                        enabled: !StartupDisk.busy
                        onActivated: index => StartupDisk.setWait(page.waits[index])
                    }
                }

                UI.SettingRow {
                    id: restoreRow
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "history"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                        opacity: restoreRow.enabled ? 1 : UI.Style.disabledOpacity
                    }
                    title: qsTr("Offer restore points there")
                    description: qsTr("Go back to an earlier day without starting Wild OS first.")
                    enabled: page.managerOn && StartupDisk.restoreAvailable

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: StartupDisk.restore && StartupDisk.restoreAvailable
                        enabled: !StartupDisk.busy
                        onToggled: StartupDisk.setRestore(checked)
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: StartupDisk.loaded
        spacing: 10

        SectionTitle {
            text: qsTr("Safety")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    leading: PathIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "lock-round"
                        size: 20
                        strokeWidth: 1.8
                        color: UI.Style.ink
                    }
                    title: qsTr("Ask for my password before starting another system")
                    description: qsTr("Stops someone else starting Windows or a USB stick on this PC. The PC's own start-up menu is separate. Set a firmware password to close that too.")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: StartupDisk.password
                        enabled: !StartupDisk.busy
                        onToggled: StartupDisk.setPassword(checked)
                    }
                }

                UI.SettingRow {
                    visible: StartupDisk.efi
                    iconName: "cog"
                    title: qsTr("Restart into the PC firmware settings")
                    description: qsTr("Sometimes called the BIOS. For changing how the hardware is set up.")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        implicitHeight: 32
                        text: qsTr("Restart")
                        enabled: !StartupDisk.busy
                        onClicked: confirm.ask("", "")
                    }
                }
            }
        }
    }

    // The helper restarts the PC at once, without the desktop asking open
    // apps to save first, so the question says so.
    UI.Dialog {
        id: confirm

        // Empty: the PC firmware settings.
        property string systemId
        property string systemName

        function ask(id, name) {
            systemId = id;
            systemName = name;
            open();
        }

        title: systemId.length > 0 ? qsTr("Restart into %1?").arg(systemName) : qsTr("Restart into the PC firmware settings?")
        text: qsTr("The PC restarts straight away. Save your work and close your apps first.")

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: confirm.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Restart now")
                onClicked: {
                    confirm.close();
                    if (confirm.systemId.length > 0) {
                        StartupDisk.restartInto(confirm.systemId);
                    } else {
                        StartupDisk.restartToFirmware();
                    }
                }
            }
        ]
    }
}
