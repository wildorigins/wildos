pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.UI as UI
import org.kde.plasma.networkmanagement as PlasmaNM

// VPN connections set up in Settings. Choosing one connects or disconnects
// it; adding one happens in Settings, where all its details can be entered.
FocusScope {
    id: page

    required property var quick
    readonly property var network: quick.network

    signal back()

    function focusFirst(): void {
        header.backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        PageHeader {
            id: header
            Layout.fillWidth: true
            title: "VPN"
            onBack: page.back()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: -4
                leftMargin: 4
                rightMargin: 4
                topMargin: 4
                bottomMargin: 4
                clip: true
                spacing: 4
                visible: count > 0
                model: page.network ? page.network.vpnModel : null
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: UI.ScrollBar {}

                delegate: ListRow {
                    id: row

                    required property var model
                    readonly property int connState: model.ConnectionState

                    width: ListView.view.width - 8
                    title: model.ItemUniqueName
                    active: connState === PlasmaNM.Enums.Activated
                    subtitle: connState === PlasmaNM.Enums.Activated ? qsTr("Connected")
                        : connState === PlasmaNM.Enums.Activating ? qsTr("Connecting")
                        : connState === PlasmaNM.Enums.Deactivating ? qsTr("Disconnecting")
                        : qsTr("Not connected")
                    icon: Component {
                        UI.Glyph {
                            path: Icons.vpn
                            color: row.active ? UI.Style.accent : UI.Style.iconInk
                            strokeWidth: 1.9
                        }
                    }

                    onClicked: {
                        if (connState === PlasmaNM.Enums.Deactivated) {
                            page.network.activate(model.ConnectionPath, model.DevicePath, model.SpecificPath);
                        } else {
                            page.network.deactivate(model.ConnectionPath, model.DevicePath);
                        }
                    }
                }
            }

            Placeholder {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 320)
                visible: !list.visible
                iconPath: Icons.vpn
                heading: qsTr("No VPN set up yet")
                explanation: qsTr("Your VPN provider gives you the details to add one in Settings.")
                actionText: qsTr("Add a VPN")
                onAction: page.quick.openSettings("network")
            }
        }

        UI.LinkButton {
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: -4
            visible: list.visible
            text: qsTr("VPN settings")
            onClicked: page.quick.openSettings("network")
        }
    }
}
