import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Windows apps, laid out as WinAppsSettings.dc.html. The App Bridge runs
// each Windows app in a Wine prefix of its own; this page holds its two
// settings, lists the apps it has set up and can remove them. Until the
// bridge is installed the switches are held back and say so.
PageScroll {
    id: page

    property var removing: null
    readonly property var windowsModeApps: WindowsMode.state === "ready" ? (WindowsMode.status.apps || []) : []

    UI.Label {
        Layout.fillWidth: true
        Layout.maximumWidth: 720
        text: qsTr("Windows apps run in their own sandbox. Each one only reaches what you allow, and you can change that at any time.")
        size: 14
        lineHeightMode: Text.FixedHeight
        lineHeight: 22 * UI.Style.textScale
        color: UI.Style.inkStatus
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                title: qsTr("Windows app support")
                description: WindowsApps.installed
                    ? qsTr("Runs Windows apps in a sandbox. Turn off to block every Windows app.")
                    : qsTr("The part of Wild OS that runs Windows apps is not installed on this computer yet.")
                enabled: WindowsApps.installed
                status: (WindowsApps.installed && WindowsApps.enabled) ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: WindowsApps.installed && WindowsApps.enabled
                    Accessible.name: qsTr("Windows app support")
                    onToggled: WindowsApps.setEnabled(checked)
                }
            }

            UI.SettingRow {
                title: qsTr("Starting permissions for new apps")
                description: qsTr("What a newly installed Windows app can reach until you change it")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Starting permissions")
                    enabled: WindowsApps.installed && WindowsApps.enabled
                    model: [
                        { value: "documents-offline", text: qsTr("Documents only, no internet") },
                        { value: "documents-online", text: qsTr("Documents and internet") },
                        { value: "own-offline", text: qsTr("Its own folder only, no internet") }
                    ]
                    readonly property var choice: WindowsApps.startingPermissions
                    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
                    onActivated: index => WindowsApps.setStartingPermissions(model[index].value)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Installed Windows apps")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: WindowsApps.apps

                    UI.SettingRow {
                        id: app
                        required property var modelData
                        minHeight: 68
                        iconTile: true
                        iconName: "windowsapps"
                        themeIcon: modelData.icon
                        title: modelData.title

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Uninstall")
                            onClicked: {
                                page.removing = app.modelData;
                                appDialog.open();
                            }
                        }

                        below: Repeater {
                            model: app.modelData.permissions

                            UI.Chip {
                                required property var modelData
                                text: modelData.text
                                granted: modelData.granted
                            }
                        }
                    }
                }

                // Apps installed inside Windows mode, as its Start menu lists
                // them. They are removed inside Windows, like any program.
                Repeater {
                    model: page.windowsModeApps

                    UI.SettingRow {
                        required property var modelData
                        minHeight: 68
                        iconTile: true
                        iconName: "windowsapps"
                        themeIcon: modelData.icon
                        title: modelData.name
                        description: qsTr("Runs in Windows mode. To uninstall it, open the Windows desktop from the Windows mode page.")
                    }
                }

                UI.SettingRow {
                    visible: WindowsApps.apps.length === 0 && page.windowsModeApps.length === 0
                    title: qsTr("No Windows apps yet")
                    description: WindowsApps.installed
                        ? qsTr("Open a Windows installer, an .exe or .msi file, and Wild OS offers to set it up here.")
                        : qsTr("Once Windows app support is installed, apps you set up appear here.")
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("When a Windows app won't run")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                // Windows mode, the background Windows VM, has a page of
                // its own (WinModeSettings.dc).
                UI.SettingRow {
                    title: qsTr("Windows mode")
                    description: WindowsMode.state === "ready" ? qsTr("Set up. Office and other Windows programs open in their own windows.")
                        : WindowsMode.state === "setup" ? qsTr("Setting up in the background")
                        : qsTr("Runs the hardest apps in a real copy of Windows, using the licence built into this PC")
                    chevron: true
                    onClicked: Navigator.open("windowsmode")
                }
            }
        }
    }

    UI.Dialog {
        id: appDialog
        title: page.removing ? qsTr("Uninstall %1?").arg(page.removing.title) : ""
        text: qsTr("Uninstalling removes the app and everything saved inside its sandbox. Files it saved to your Documents stay where they are. You can bring it back from the Recycle Bin.")
        buttons: [
            UI.Button {
                text: qsTr("Uninstall")
                kind: "primary"
                onClicked: {
                    WindowsApps.uninstall(page.removing.name);
                    appDialog.close();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: appDialog.close()
            }
        ]
    }
}
