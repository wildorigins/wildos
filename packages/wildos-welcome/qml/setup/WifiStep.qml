import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Welcome
import WildOS.UI as UI

// Setup step 2, from FirstWifi.dc.html (3.4), through NetworkManager.
// Networks joined here belong to the computer, so the new account is online
// as soon as it signs in. A computer already online through a cable skips
// the step (Main.qml), and nothing in setup needs the internet, so Skip for
// now always works.
StepPage {
    id: page

    signal next()
    signal back()

    readonly property var wifi: FirstBoot.wifi
    readonly property bool listing: wifi.ready && wifi.available && wifi.enabled
    property string openSsid: ""
    property string failedSsid: ""
    property string failReason: ""
    property bool autoConnect: true

    gap: 18
    title: "Connect to Wi-Fi"
    subtitle: "Wild OS needs the internet to fetch security updates and set up Windows programs."
    note: "You can skip this and connect later."
    Component.onCompleted: {
        wifi.active = true;
        focusQuietly(wifi.online ? continueButton : skipButton);
    }
    Component.onDestruction: wifi.active = false

    Connections {
        target: page.wifi
        function onConnectFinished(ssid, ok, reason) {
            if (ok) {
                if (!page.autoConnect) {
                    page.wifi.setAutoConnect(ssid, false);
                }
                page.openSsid = "";
                page.failedSsid = "";
                hiddenDialog.close();
            } else {
                page.failedSsid = ssid;
                page.failReason = reason;
            }
        }
    }

    function failText() {
        if (failReason === "password") {
            return "That password didn't work. Check it and try again.";
        }
        if (failReason === "range") {
            return "This network is out of range now.";
        }
        return "Wild OS couldn't connect to this network.";
    }

    // Where the list would be, when there is nothing to list.
    UI.Card {
        Layout.fillWidth: true
        visible: !page.listing

        UI.ListRow {
            minHeight: 60
            iconName: page.wifi.ready && (!page.wifi.available || !page.wifi.enabled) ? "wifi-off" : "wifi"
            title: !page.wifi.ready ? "Looking for Wi-Fi"
                : !page.wifi.available ? "This computer has no Wi-Fi"
                : "Wi-Fi is turned off"
            description: !page.wifi.ready ? "This takes a few seconds."
                : !page.wifi.available ? "Plug in a network cable if you want to connect, or skip this for now."
                : "Turn it on to see the networks around you."

            UI.Button {
                visible: page.wifi.ready && page.wifi.available && !page.wifi.enabled
                text: "Turn on"
                onClicked: page.wifi.setRadio(true)
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.listing

        Column {
            width: parent.width

            UI.ListRow {
                visible: networks.count === 0
                minHeight: 52
                title: page.wifi.scanning ? "Searching for networks" : "No networks found"
            }

            Repeater {
                id: networks
                model: page.wifi.networks

                Rectangle {
                    id: entry

                    required property var modelData
                    required property int index
                    readonly property var net: modelData
                    readonly property bool open: page.openSsid === net.ssid && !net.active
                    readonly property bool needsPassword: net.secure && !net.saved && !net.enterprise

                    width: parent.width
                    height: 52 + (open ? form.implicitHeight + 16 : 0)
                    color: open ? UI.Style.fillSubtle(0.10, 0.08) : row.hovered && row.enabled ? UI.Style.surfaceHover : "transparent"

                    Rectangle {
                        visible: entry.index > 0
                        width: parent.width
                        height: 1
                        color: UI.Style.divider
                    }

                    T.AbstractButton {
                        id: row
                        width: parent.width
                        height: 52
                        hoverEnabled: true
                        enabled: !entry.net.enterprise && !entry.net.active
                        Accessible.name: entry.net.ssid
                        onClicked: {
                            page.failedSsid = "";
                            if (entry.needsPassword) {
                                page.openSsid = entry.open ? "" : entry.net.ssid;
                            } else {
                                page.openSsid = "";
                                page.wifi.connectTo(entry.net.ssid, "");
                            }
                        }

                        UI.FocusRing {
                            visible: row.visualFocus
                            anchors.margins: 2
                            targetRadius: 6
                        }
                    }

                    RowLayout {
                        x: 18
                        width: parent.width - 36
                        height: 52
                        spacing: 14

                        // Four bars, 3px wide, lit up to the signal.
                        Item {
                            implicitWidth: 18
                            implicitHeight: 14

                            Row {
                                anchors.bottom: parent.bottom
                                spacing: 2

                                Repeater {
                                    model: [4, 7, 10, 13]

                                    Rectangle {
                                        required property int modelData
                                        required property int index
                                        anchors.bottom: parent.bottom
                                        width: 3
                                        height: modelData
                                        radius: 1
                                        color: entry.net.signal >= index * 25 + 5 ? UI.Style.ink : UI.Style.tone(0.25)
                                    }
                                }
                            }
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            text: entry.net.ssid
                            size: 14
                            font.weight: entry.open || entry.net.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }

                        UI.Spinner {
                            visible: page.wifi.connectingSsid === entry.net.ssid
                            size: 14
                        }

                        UI.Label {
                            visible: text.length > 0
                            text: entry.net.active ? "Connected"
                                : page.failedSsid === entry.net.ssid && !entry.open ? page.failText()
                                : entry.net.enterprise ? "Needs a work or school sign-in"
                                : !entry.net.secure ? "Open"
                                : entry.net.saved ? "Saved" : ""
                            size: 12
                            color: entry.net.active ? UI.Style.accentText : page.failedSsid === entry.net.ssid ? UI.Style.danger : UI.Style.inkMuted
                        }

                        UI.Icon {
                            visible: entry.net.secure && !entry.net.active
                            name: "lock"
                            size: 13
                            strokeWidth: 2
                            color: UI.Style.inkMuted
                        }
                    }

                    // Under its network, 31px in from the bars: password and
                    // Connect at 40px, then Connect automatically.
                    ColumnLayout {
                        id: form
                        visible: entry.open
                        x: 18 + 31
                        y: 52
                        width: parent.width - x - 18
                        spacing: 10
                        onVisibleChanged: if (visible) password.forceActiveFocus()

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            UI.TextField {
                                id: password
                                Layout.fillWidth: true
                                implicitHeight: 40
                                revealable: true
                                UI.NoPaste {}
                                placeholderText: "Network password"
                                enabled: page.wifi.connectingSsid === ""
                                onAccepted: if (connectButton.enabled) connectButton.clicked()
                            }

                            UI.Button {
                                id: connectButton
                                kind: "primary"
                                size: "large"
                                text: "Connect"
                                busy: page.wifi.connectingSsid === entry.net.ssid
                                enabled: password.text.length >= 8 && page.wifi.connectingSsid === ""
                                onClicked: {
                                    page.failedSsid = "";
                                    page.wifi.connectTo(entry.net.ssid, password.text);
                                }
                            }
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            visible: page.failedSsid === entry.net.ssid
                            text: page.failText()
                            size: 12
                            color: UI.Style.danger
                        }

                        UI.CheckBox {
                            text: "Connect automatically"
                            checked: page.autoConnect
                            onToggled: page.autoConnect = checked
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        UI.LinkButton {
            Layout.leftMargin: -4
            visible: page.listing
            text: "Join a hidden network"
            onClicked: hiddenDialog.open()
        }

        Item {
            Layout.fillWidth: true
        }

        UI.Label {
            text: page.wifi.wiredOnline ? "You're connected with a network cable." : "Using a network cable? You are already connected."
            color: UI.Style.inkMuted
        }
    }

    // The time zone lookup on the region page needs the internet, which most
    // people only have from this page on, so it is offered again here once
    // connected, while the zone is still only the language's guess. As
    // there, it runs only when pressed.
    RowLayout {
        readonly property bool guessed: FirstBoot.zoneSource === "language" || FirstBoot.zoneSource === "installer"

        Layout.fillWidth: true
        visible: page.wifi.online && (guessed || FirstBoot.zoneSource === "internet" || FirstBoot.lookupState === "failed")
        spacing: 8

        UI.Label {
            Layout.fillWidth: true
            readonly property string zone: FirstBoot.timeZone.split("/").pop().replace(/_/g, " ")
            text: FirstBoot.lookupState === "failed" ? "The online service didn't answer. You can change the time zone in Settings."
                : FirstBoot.zoneSource === "internet" ? "Time zone set to " + zone + " from your internet address."
                : "Time zone: " + zone + ", from your language. Finding it sends your internet address to an online service."
            size: 12
            color: FirstBoot.lookupState === "failed" ? UI.Style.danger : UI.Style.inkMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }

        UI.LinkButton {
            id: findZone

            // The region page has already set the time zone, so one found
            // from here is set as soon as it arrives.
            property bool asked: false

            visible: parent.guessed && FirstBoot.lookupState !== "failed"
            enabled: FirstBoot.lookupState !== "busy"
            text: FirstBoot.lookupState === "busy" ? "Looking it up" : "Find it for me"
            size: 12
            onClicked: {
                asked = true;
                FirstBoot.findTimeZone();
            }

            Connections {
                target: FirstBoot
                function onLookupChanged() {
                    if (findZone.asked && FirstBoot.lookupState === "done") {
                        findZone.asked = false;
                        FirstBoot.applyRegion();
                    }
                }
            }
        }
    }

    UI.Dialog {
        id: hiddenDialog
        title: "Join a hidden network"
        text: "Type the network's name exactly as it was set up on the router."
        onOpened: hiddenName.forceActiveFocus()
        onClosed: {
            hiddenName.text = "";
            hiddenPassword.text = "";
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            UI.TextField {
                id: hiddenName
                Layout.fillWidth: true
                placeholderText: "Network name"
            }

            UI.TextField {
                id: hiddenPassword
                Layout.fillWidth: true
                revealable: true
                UI.NoPaste {}
                placeholderText: "Password, if it has one"
            }

            UI.Label {
                Layout.fillWidth: true
                visible: page.failedSsid.length > 0 && page.failedSsid === hiddenName.text.trim()
                text: page.failText()
                size: 12
                color: UI.Style.danger
                wrapMode: Text.WordWrap
            }
        }

        buttons: [
            UI.Button {
                kind: "primary"
                text: "Connect"
                busy: page.wifi.connectingSsid.length > 0 && page.wifi.connectingSsid === hiddenName.text.trim()
                enabled: hiddenName.text.trim().length > 0 && (hiddenPassword.text.length === 0 || hiddenPassword.text.length >= 8)
                         && page.wifi.connectingSsid === ""
                onClicked: {
                    page.failedSsid = "";
                    page.wifi.connectHidden(hiddenName.text.trim(), hiddenPassword.text);
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: hiddenDialog.close()
            }
        ]
    }

    buttons: [
        UI.Button {
            id: skipButton
            visible: !page.wifi.online
            text: "Skip for now"
            onClicked: page.next()
        },
        UI.Button {
            text: "Back"
            onClicked: page.back()
        },
        UI.Button {
            id: continueButton
            text: "Continue"
            kind: "primary"
            enabled: page.wifi.online
            onClicked: page.next()
        }
    ]
}
