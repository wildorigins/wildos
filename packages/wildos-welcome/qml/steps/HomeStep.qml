import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Welcome
import WildOS.UI as UI

// Step 4, from Welcome.dc.html (3.6): taskbar layout, appearance and accent.
// Each choice is applied the moment it is picked, so the desktop behind
// the sheet changes with it.
StepPage {
    id: page

    signal next()
    signal back()

    // The first step of an account's own welcome has nothing before it.
    property bool canGoBack: true

    title: qsTr("Make it feel like home")
    subtitle: qsTr("Coming from Windows? Pick the layout you're used to.")
    gap: 24
    Component.onCompleted: focusQuietly(continueButton)

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        UI.Label {
            text: qsTr("Taskbar")
            size: 14
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Taskbar layout")

            Repeater {
                model: [
                    { layout: 0, name: qsTr("Centred"), hint: qsTr("Like Windows 11") },
                    { layout: 1, name: qsTr("Left-aligned"), hint: qsTr("Like Windows 10") }
                ]

                UI.ChoiceCard {
                    id: card

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    padding: 10
                    bottomPadding: 14
                    radius: UI.Style.radiusLg
                    title: modelData.name
                    description: modelData.hint
                    checkable: false
                    checked: Setup.taskbarLayout === modelData.layout
                    enabled: Setup.taskbarLayoutKnown
                    onClicked: Setup.setTaskbarLayout(modelData.layout)

                    TaskbarPreview {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        leftAligned: card.modelData.layout === 1
                    }
                }
            }
        }

        UI.Label {
            Layout.fillWidth: true
            visible: !Setup.taskbarLayoutKnown
            text: qsTr("Wild OS couldn't reach the taskbar, so its layout can't be changed right now. You can change it later in Settings > Personalisation > Taskbar.")
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        UI.Label {
            text: qsTr("Appearance")
            size: 14
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Appearance")

            // Light and Automatic need the light colour scheme installed;
            // without it they are held back with the reason on hover, as
            // Settings > Personalisation shows them. Automatic switches at
            // sunrise and sunset (wildos-auto-theme).
            Repeater {
                model: [
                    // Each theme's page ground, as WildOS.UI draws it: a
                    // preview of the scheme itself.
                    { name: qsTr("Dark"), scheme: "WildOS", swatch: ["#16181c"] },
                    { name: qsTr("Light"), scheme: "WildOSLight", swatch: ["#eef1f7"] },
                    { name: qsTr("Automatic"), scheme: "", swatch: ["#eef1f7", "#16181c"] }
                ]

                Item {
                    id: mode

                    required property var modelData
                    readonly property bool automatic: modelData.scheme.length === 0
                    readonly property bool available: Setup.hasColourScheme(automatic ? "WildOSLight" : modelData.scheme)
                    readonly property string reason: qsTr("The light theme is not installed on this computer")

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: modeCard.implicitHeight

                    HoverHandler {
                        id: modeHover
                    }

                    UI.Tooltip {
                        text: mode.reason
                        visible: !mode.available && modeHover.hovered
                    }

                    UI.ChoiceCard {
                        id: modeCard
                        width: parent.width
                        padding: 14
                        radius: UI.Style.radiusMd
                        checkable: false
                        checked: mode.automatic ? Setup.automaticTheme : !Setup.automaticTheme && Setup.colourScheme === mode.modelData.scheme
                        enabled: mode.available
                        Accessible.name: mode.modelData.name
                        Accessible.description: mode.available ? "" : mode.reason
                        onClicked: mode.automatic ? Setup.setAutomaticTheme() : Setup.setColourScheme(mode.modelData.scheme)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Rectangle {
                                implicitWidth: 30
                                implicitHeight: 30
                                radius: UI.Style.radiusSm
                                clip: true
                                color: mode.modelData.swatch[0]
                                border.width: 1
                                border.color: UI.Style.borderControl

                                Rectangle {
                                    visible: mode.modelData.swatch.length > 1
                                    x: 15
                                    width: 15
                                    height: 30
                                    color: mode.modelData.swatch[1] || "transparent"
                                }
                            }

                            UI.Label {
                                Layout.fillWidth: true
                                text: mode.modelData.name
                                size: 14
                                font.weight: modeCard.checked ? UI.Style.weightSemiBold : UI.Style.weightMedium
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        UI.Label {
            text: qsTr("Accent colour")
            size: 14
            font.weight: UI.Style.weightSemiBold
            Accessible.role: Accessible.Heading
        }

        Row {
            spacing: 12
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Accent colour")

            Repeater {
                model: UI.Style.accents

                T.AbstractButton {
                    id: swatch

                    required property var modelData
                    readonly property bool chosen: Qt.colorEqual(Setup.accent, modelData.colour)

                    width: 36
                    height: 36
                    padding: 0
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: modelData.name
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: chosen
                    onClicked: Setup.setAccent(modelData.colour)

                    // box-shadow: 0 0 0 3px <sheet>, 0 0 0 5px <accent>.
                    // The swatches are the accents themselves, so their
                    // colours come from the accent list, not the scheme.
                    background: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 46
                            height: 46
                            radius: 23
                            visible: swatch.chosen
                            color: swatch.modelData.colour
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 42
                            height: 42
                            radius: 21
                            visible: swatch.chosen
                            color: UI.Style.windowOpaque
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 18
                            color: swatch.modelData.colour
                            scale: swatch.down ? 0.96 : swatch.hovered && !swatch.chosen ? 1.06 : 1
                            Behavior on scale {
                                NumberAnimation { duration: UI.Style.hoverDuration }
                            }
                        }

                        UI.FocusRing {
                            visible: swatch.visualFocus
                            anchors.margins: swatch.chosen ? -9 : -4
                            targetRadius: 18
                            radius: width / 2
                        }
                    }
                }
            }
        }
    }

    buttons: [
        UI.Button {
            text: qsTr("Back")
            visible: page.canGoBack
            onClicked: page.back()
        },
        UI.Button {
            id: continueButton
            text: qsTr("Continue")
            kind: "primary"
            onClicked: page.next()
        }
    ]
}
