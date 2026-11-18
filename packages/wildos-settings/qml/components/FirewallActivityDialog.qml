import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// What the firewall blocked (FirewallActivity.dc): the latest connections
// it turned away, today or over the week it keeps them, each with when,
// who sent it as far as the network says (your router, another PC on this
// network, the internet) and what it was for. Allow is offered for a
// sender on a network nearby, never the internet or the router; it opens
// Allow an app or port for that device and port.
RecoveryDialog {
    id: dialog

    property string range: "today"
    property date now: new Date()
    readonly property int shownMax: 5
    readonly property var entries: Firewall.blocked.filter(b => range === "week" || b.when.toDateString() === now.toDateString())
    readonly property var shown: entries.slice(0, shownMax)

    signal allowRequested(var entry)

    function who(kind) {
        return kind === "router" ? qsTr("Your router")
            : kind === "local" ? qsTr("Another PC on this network")
            : kind === "internet" ? qsTr("The internet")
            : qsTr("Another local network");
    }

    function countLine() {
        const n = entries.length;
        if (n === 0) {
            return "";
        }
        if (range === "week") {
            return n > shownMax ? qsTr("%1 in 7 days · showing the latest %2").arg(n).arg(shownMax) : qsTr("%1 in 7 days").arg(n);
        }
        return n > shownMax ? qsTr("%1 today · showing the latest %2").arg(n).arg(shownMax) : qsTr("%1 today").arg(n);
    }

    title: qsTr("What the firewall blocked")
    iconName: "shield-round-check"
    dialogWidth: 680

    onOpened: {
        now = new Date();
        range = "today";
        Firewall.refreshListening();
    }

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Each of these tried to connect to this PC and was turned away. Allow one only if you recognise it and meant it to connect.")
        size: 13
        lineHeightMode: Text.FixedHeight
        lineHeight: 19.5
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Segmented {
            options: [{ value: "today", text: qsTr("Today") }, { value: "week", text: qsTr("7 days") }]
            value: dialog.range
            onPicked: v => dialog.range = v
        }

        Item {
            Layout.fillWidth: true
        }

        UI.Label {
            text: dialog.countLine()
            size: 12.5
            color: UI.Style.inkMuted
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: list.implicitHeight
        radius: 10
        color: UI.Style.tone(0.04)
        border.width: 1
        border.color: UI.Style.tone(0.07)
        clip: true

        Column {
            id: list
            width: parent.width

            Repeater {
                model: dialog.shown

                Item {
                    id: entryRow
                    required property var modelData
                    required property int index
                    readonly property string service: Firewall.serviceName(modelData.port, modelData.proto)
                    readonly property bool canAllow: modelData.kind === "local" || modelData.kind === "other"
                    width: list.width
                    height: 52

                    Rectangle {
                        visible: entryRow.index > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: UI.Style.tone(0.07)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 14

                        UI.Label {
                            // The board's 40px fits a 24-hour clock; "10:42 PM" needs more.
                            Layout.preferredWidth: Format.twelveHour() ? 62 : 40
                            text: dialog.range === "week" && entryRow.modelData.when.toDateString() !== dialog.now.toDateString()
                                ? Format.shortDate(entryRow.modelData.when).slice(0, 5) : Format.time(entryRow.modelData.when)
                            size: 12.5
                            color: UI.Style.inkMuted
                            font.features: ({ "tnum": 1 })
                        }

                        PathIcon {
                            name: entryRow.modelData.kind === "router" ? "router"
                                : entryRow.modelData.kind === "internet" ? "globe-round" : "monitor-round"
                            size: 18
                            strokeWidth: 1.8
                            color: UI.Style.ink
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 210
                            Layout.fillWidth: false
                            spacing: 1

                            UI.Label {
                                Layout.fillWidth: true
                                text: dialog.who(entryRow.modelData.kind)
                                size: 13
                                elide: Text.ElideRight
                            }

                            UI.Label {
                                Layout.fillWidth: true
                                text: entryRow.modelData.source
                                size: 12
                                color: UI.Style.inkMuted
                                font.features: ({ "tnum": 1 })
                                elide: Text.ElideMiddle
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            UI.Label {
                                Layout.fillWidth: true
                                text: entryRow.service.length > 0 ? entryRow.service : (entryRow.modelData.proto === "udp" ? qsTr("UDP port %1") : qsTr("Port %1")).arg(entryRow.modelData.port)
                                size: 13
                                elide: Text.ElideRight
                            }

                            UI.Label {
                                Layout.fillWidth: true
                                text: entryRow.service.length > 0 ? (entryRow.modelData.proto === "udp" ? qsTr("UDP port %1") : qsTr("Port %1")).arg(entryRow.modelData.port) : qsTr("Nothing on this PC is known to use it")
                                size: 12
                                color: UI.Style.inkMuted
                                elide: Text.ElideRight
                            }
                        }

                        Item {
                            Layout.preferredWidth: 76
                            Layout.fillHeight: true

                            UI.Button {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: entryRow.canAllow
                                implicitHeight: 32
                                text: qsTr("Allow")
                                enabled: Firewall.enabled && !Firewall.busy
                                Accessible.name: qsTr("Allow %1 on port %2").arg(dialog.who(entryRow.modelData.kind).toLowerCase()).arg(entryRow.modelData.port)
                                onClicked: {
                                    const entry = entryRow.modelData;
                                    dialog.close();
                                    dialog.allowRequested(entry);
                                }
                            }

                            UI.Label {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !entryRow.canAllow
                                text: qsTr("Blocked")
                                size: 12.5
                                color: UI.Style.inkSubtle
                            }
                        }
                    }
                }
            }

            Item {
                visible: dialog.shown.length === 0
                width: list.width
                height: 52

                UI.Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: dialog.range === "week" ? qsTr("Nothing has tried to connect in the last 7 days.") : qsTr("Nothing has tried to connect today.")
                    size: 13
                    color: UI.Style.inkMuted
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
            text: qsTr("Kept on this PC for 7 days, then deleted. Nothing is sent anywhere.")
            size: 12.5
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    buttons: [
        UI.Button {
            implicitHeight: 32
            text: qsTr("Clear the list")
            enabled: Firewall.blocked.length > 0
            onClicked: Firewall.clearBlocked()
        },
        UI.Button {
            implicitHeight: 32
            kind: "primary"
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    ]
}
