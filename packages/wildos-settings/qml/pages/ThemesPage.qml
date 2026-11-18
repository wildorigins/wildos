pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// Themes, laid out as SettingsThemes.dc: the six themes three to a row,
// then the wallpaper and, for a theme that brings it (Overclock) or once the
// person has turned it on, the taskbar underglow and keyboard and mouse
// lighting (UnderglowOptions.dc). A theme sets the wallpaper, light or dark,
// the accent and the glass at once; each can still be changed on its own
// page afterwards.
PageScroll {
    id: page

    readonly property var theme: Themes.currentTheme
    readonly property bool underglowShown: (theme.underglow || false) || Themes.underglow
    readonly property bool custom: Themes.underglowColour === "custom"

    Component.onCompleted: Themes.refreshRgbDevices()

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 18
        rowSpacing: 16
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("Theme")

        Repeater {
            model: Themes.themes

            T.AbstractButton {
                id: card

                required property var modelData
                readonly property bool selected: Themes.current === modelData.id

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: frame.height + 8 + nameRow.implicitHeight
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.name
                Accessible.description: modelData.description
                Accessible.checked: selected
                onClicked: Themes.apply(modelData.id)

                UI.Tooltip {
                    text: card.modelData.description
                    visible: card.hovered && card.modelData.description.length > 0
                }

                background: Item {
                    Rectangle {
                        id: frame
                        width: parent.width
                        height: Math.round((width - 8) * 124 / 250) + 8
                        radius: UI.Style.radiusMd
                        color: "transparent"
                        border.width: card.selected ? 2 : 1
                        border.color: card.selected ? UI.Style.accent : card.hovered ? UI.Style.borderControl : UI.Style.borderGlass
                        Behavior on border.color {
                            ColorAnimation { duration: UI.Style.hoverDuration }
                        }

                        ThemePreview {
                            anchors.fill: parent
                            anchors.margins: 4
                            theme: card.modelData
                        }

                        UI.FocusRing {
                            visible: card.visualFocus
                            anchors.margins: -3
                            targetRadius: UI.Style.radiusMd + 3
                        }
                    }

                    RowLayout {
                        id: nameRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: frame.bottom
                        anchors.topMargin: 8
                        spacing: 8

                        UI.Label {
                            Layout.fillWidth: true
                            text: card.modelData.name
                            size: 13
                            font.weight: card.selected ? UI.Style.weightSemiBold : UI.Style.weightMedium
                        }

                        Rectangle {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            radius: 6
                            color: card.modelData.accent
                            border.width: 1
                            border.color: UI.Style.tone(0.20)
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                title: qsTr("Wallpaper")
                description: {
                    const more = page.theme.moreWallpaperNames || [];
                    return more.length === 0 ? System.wallpaperName + "."
                        : more.length === 1 ? qsTr("%1. %2 is also included.").arg(System.wallpaperName).arg(more[0])
                        : qsTr("%1. %2 and %3 are also included.").arg(System.wallpaperName).arg(more.slice(0, -1).join(", ")).arg(more[more.length - 1]);
                }

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    size: "small"
                    text: qsTr("Change")
                    Accessible.name: qsTr("Change wallpaper")
                    onClicked: Navigator.open("background")
                }
            }

            UI.SettingRow {
                visible: page.underglowShown && Themes.rgbDevices > 0
                title: qsTr("Lighting on RGB keyboards and mice")
                description: qsTr("On most gaming keyboards, mice and laptops. Follows the underglow unless Cooler sets an effect")
                status: Themes.lightingRgb ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Themes.lightingRgb
                    Accessible.name: qsTr("Lighting on RGB keyboards and mice")
                    onToggled: Themes.setLightingRgb(checked)
                }
            }

            UI.SettingRow {
                visible: page.underglowShown
                title: qsTr("Taskbar underglow")
                description: qsTr("A thin line of light along the top of the taskbar")
                status: Themes.underglow ? qsTr("On") : qsTr("Off")

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Themes.underglow
                    Accessible.name: qsTr("Taskbar underglow")
                    onToggled: Themes.setUnderglow(checked)
                }
            }

            UI.SettingRow {
                visible: page.underglowShown && Themes.underglow
                title: qsTr("Underglow colour and effect")
                description: Themes.rgbDevices > 0 && Themes.lightingRgb
                    ? qsTr("Keyboard and mouse lighting copies these unless Cooler sets an effect")
                    : qsTr("Your accent, a colour of your own, or every colour in turn")

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Underglow colour")
                    model: [
                        { value: "accent", text: qsTr("Accent") },
                        { value: "rgb", text: "RGB" },
                        { value: "custom", text: qsTr("Custom") }
                    ]
                    readonly property string choice: Themes.underglowColour
                    currentIndex: model.findIndex(o => o.value === choice)
                    onActivated: index => Themes.setUnderglowColour(model[index].value)
                }

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Underglow effect")
                    // RGB flows along the bar; a single colour is steady or
                    // breathes.
                    model: Themes.underglowColour === "rgb"
                        ? [{ value: "flowing", text: qsTr("Flowing") }]
                        : [{ value: "steady", text: qsTr("Steady") }, { value: "breathing", text: qsTr("Breathing") }]
                    readonly property string choice: Themes.underglowEffect
                    currentIndex: model.findIndex(o => o.value === choice)
                    onActivated: index => Themes.setUnderglowEffect(model[index].value)
                }
            }

            UI.SettingRow {
                id: customRow
                visible: page.underglowShown && Themes.underglow && page.custom
                title: qsTr("Custom colour")

                readonly property var swatches: [
                    { colour: "#3dff7a", name: qsTr("Green") },
                    { colour: "#ff2d95", name: qsTr("Pink") },
                    { colour: "#3dd5ff", name: qsTr("Cyan") },
                    { colour: "#ffb03d", name: qsTr("Amber") },
                    { colour: "#c85cff", name: qsTr("Violet") },
                    { colour: "#ffffff", name: qsTr("White") }
                ]

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Custom colour")

                    Repeater {
                        model: customRow.swatches

                        UI.Swatch {
                            required property var modelData
                            anchors.verticalCenter: parent.verticalCenter
                            size: 28
                            colour: modelData.colour
                            name: modelData.name
                            selected: Qt.colorEqual(Themes.underglowCustom, modelData.colour)
                            onClicked: Themes.setUnderglowCustom(modelData.colour)
                        }
                    }
                }

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 6
                    text: qsTr("Or any colour")
                    size: 12
                    color: UI.Style.inkMuted
                }

                UI.TextField {
                    id: hexField
                    anchors.verticalCenter: parent.verticalCenter
                    width: 124
                    leftPadding: 36
                    placeholderText: "#RRGGBB"
                    maximumLength: 7
                    Accessible.name: qsTr("Underglow colour, as a hex code")
                    validator: RegularExpressionValidator { regularExpression: /#?[0-9A-Fa-f]{0,6}/ }
                    readonly property string normalised: (text.startsWith("#") ? text : "#" + text).toLowerCase()
                    readonly property bool complete: /^#[0-9a-f]{6}$/.test(normalised)
                    error: activeFocus && text.length > 0 && !complete
                    onAccepted: save()
                    // Saving puts the stored colour back in the box, which would
                    // undo a paste from the text menu; the menu holds focus while
                    // it is up.
                    onActiveFocusChanged: {
                        if (!activeFocus && !textMenuOpen) {
                            save();
                        }
                    }
                    onTextMenuFocusLeft: save()
                    function save(): void {
                        if (complete) {
                            Themes.setUnderglowCustom(normalised);
                        }
                        text = Themes.underglowCustom.toString().toUpperCase();
                    }
                    Component.onCompleted: text = Themes.underglowCustom.toString().toUpperCase()

                    Connections {
                        target: Themes
                        function onUnderglowChanged() {
                            if (!hexField.activeFocus) {
                                hexField.text = Themes.underglowCustom.toString().toUpperCase();
                            }
                        }
                    }

                    Rectangle {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        height: 16
                        radius: 4
                        color: hexField.complete ? hexField.normalised : Themes.underglowCustom
                        border.width: 1
                        border.color: UI.Style.tone(0.20)
                    }
                }
            }
        }
    }
}
