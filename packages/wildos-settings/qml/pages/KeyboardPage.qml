import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Keyboard, after Windows 11's keyboard settings (Language options >
// Keyboards, Typing, and the keyboard properties' repeat delay and rate):
// the layouts in the order they are used, the keys that switch between
// them, key repeat, and what Caps Lock and Num Lock do. KWin applies each
// change straight away.
PageScroll {
    id: page

    readonly property var layouts: Keyboard.layouts
    readonly property bool several: layouts.length > 1

    readonly property var switchChoices: [
        { value: "grp:win_space_toggle", text: "Windows key + Space" },
        { value: "grp:alt_shift_toggle", text: "Alt + Shift" },
        { value: "grp:ctrl_shift_toggle", text: "Ctrl + Shift" },
        { value: "grp:caps_toggle", text: "Caps Lock" },
        { value: "", text: "No keys" }
    ]

    readonly property var capsChoices: [
        { value: "", text: "Caps Lock" },
        { value: "ctrl:nocaps", text: "Another Ctrl key" },
        { value: "caps:escape", text: "Escape" },
        { value: "caps:none", text: "Nothing" }
    ]

    readonly property var numLockChoices: [
        { value: 0, text: "On" },
        { value: 1, text: "Off" },
        { value: 2, text: "As it was" }
    ]

    function openMenu(button, index) {
        const entries = [];
        if (several) {
            entries.push({ text: "Use now", icon: "check", enabled: index !== Keyboard.current, run: () => Keyboard.use(index) });
            entries.push({ separator: true });
        }
        entries.push({ text: "Move up", icon: "chevron-up", enabled: index > 0, run: () => Keyboard.move(index, index - 1) });
        entries.push({ text: "Move down", icon: "chevron-down", enabled: index < layouts.length - 1, run: () => Keyboard.move(index, index + 1) });
        entries.push({ separator: true });
        entries.push({ text: "Remove", icon: "delete", danger: true, enabled: several, run: () => Keyboard.remove(index) });
        layoutMenu.entries = entries;
        layoutMenu.popup(button, 0, button.height + 4);
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            SectionTitle {
                Layout.fillWidth: true
                text: "Keyboard layouts"
            }

            UI.Button {
                text: "Add a keyboard"
                iconName: "plus"
                onClicked: addDialog.open()
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.layouts

                    UI.SettingRow {
                        id: layoutRow
                        required property var modelData
                        required property int index
                        iconName: "keyboard"
                        title: modelData.name
                        description: index === 0 ? (page.several ? "Used first when you sign in" : "The only layout, used everywhere") : ""
                        status: page.several && index === Keyboard.current ? "In use" : ""
                        statusColor: UI.Style.accentText

                        UI.IconButton {
                            id: moreButton
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "more"
                            tip: "Options for " + layoutRow.modelData.name
                            onClicked: page.openMenu(moreButton, layoutRow.index)
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
                iconName: "swap"
                title: "Switch layouts with"
                description: page.several ? "Press these keys together to move to the next layout" : "Add a second keyboard to switch between them"
                enabled: page.several

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: "Switch layouts with"
                    model: page.switchChoices
                    readonly property string choice: Keyboard.switchKeys
                    currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                    onActivated: index => Keyboard.setSwitchKeys(model[index].value)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Typing"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: "Repeat a key while you hold it down"
                    status: Keyboard.repeat ? "On" : "Off"

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Keyboard.repeat
                        Accessible.name: "Repeat a key while you hold it down"
                        onToggled: Keyboard.setRepeat(checked)
                    }
                }

                UI.SettingRow {
                    title: "Repeat delay"
                    description: "How long to hold a key before it repeats"
                    enabled: Keyboard.repeat
                    status: Keyboard.repeatDelay + " ms"

                    UI.Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        accessibleName: "Repeat delay"
                        from: 150
                        to: 1000
                        stepSize: 50
                        value: Keyboard.repeatDelay
                        onMoved: Keyboard.setRepeatDelay(value)
                    }
                }

                UI.SettingRow {
                    title: "Repeat rate"
                    description: "How fast a held key repeats"
                    enabled: Keyboard.repeat
                    status: Keyboard.repeatRate + " a second"

                    UI.Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        accessibleName: "Repeat rate"
                        from: 5
                        to: 50
                        stepSize: 1
                        value: Keyboard.repeatRate
                        onMoved: Keyboard.setRepeatRate(value)
                    }
                }

                UI.SettingRow {
                    title: "Try it here"
                    enabled: Keyboard.repeat

                    UI.TextField {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        placeholderText: "Hold down a key in this box"
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: "Keys"
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: "Caps Lock key works as"

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: "Caps Lock key works as"
                        model: page.capsChoices
                        readonly property string choice: Keyboard.capsLock
                        currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                        onActivated: index => Keyboard.setCapsLock(model[index].value)
                    }
                }

                UI.SettingRow {
                    title: "Num Lock when you sign in"
                    description: "Takes effect the next time you sign in"

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: "Num Lock when you sign in"
                        model: page.numLockChoices
                        readonly property int choice: Keyboard.numLock
                        currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                        onActivated: index => Keyboard.setNumLock(model[index].value)
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: Pages.exists("keyboardadvanced")

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "cog"
                title: "More keyboard options"
                description: "Keyboard model, a compose key and other special keys"
                chevron: true
                onClicked: Navigator.open("keyboardadvanced")
            }
        }
    }

    UI.Menu {
        id: layoutMenu
    }

    UI.Dialog {
        id: addDialog

        property var all: []
        property int chosen: -1
        readonly property var shown: {
            const query = filter.text.trim().toLowerCase();
            const taken = page.layouts.map(l => l.layout + "|" + l.variant);
            return all.filter(l => !taken.includes(l.layout + "|" + l.variant)
                && (query.length === 0 || l.name.toLowerCase().includes(query) || (l.keywords || "").includes(query)));
        }

        title: "Add a keyboard"
        text: "Choose the layout printed on your keyboard, or one you type in."
        onAboutToShow: {
            if (all.length === 0) {
                all = Keyboard.available();
            }
            filter.text = "";
            chosen = -1;
            filter.forceActiveFocus();
        }

        UI.SearchField {
            id: filter
            Layout.fillWidth: true
            placeholderText: "Search by country, language or layout, for example Belgium"
            onTextChanged: addDialog.chosen = -1
            onDownPressed: {
                addDialog.chosen = Math.max(0, addDialog.chosen);
                list.forceActiveFocus();
            }
            onAccepted: {
                if (addDialog.shown.length === 1) {
                    addDialog.chosen = 0;
                }
                addButton.clicked();
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            radius: UI.Style.radiusSm
            color: UI.Style.surfaceSunken
            border.width: 1
            border.color: UI.Style.borderField

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: addDialog.shown
                currentIndex: addDialog.chosen
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                onCurrentIndexChanged: addDialog.chosen = currentIndex
                Keys.onReturnPressed: addButton.clicked()

                delegate: UI.ListRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    minHeight: 36
                    divider: false
                    radius: UI.Style.radiusXs
                    interactive: true
                    title: modelData.name
                    checked: index === addDialog.chosen
                    background: Rectangle {
                        radius: UI.Style.radiusXs
                        color: parent.checked ? UI.Style.accentSelected : parent.hovered ? UI.Style.surfaceHover : "transparent"
                    }
                    onClicked: addDialog.chosen = index
                    onDoubleClicked: {
                        addDialog.chosen = index;
                        addButton.clicked();
                    }
                }

                UI.Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "No layouts match"
                    color: UI.Style.inkMuted
                }
            }
        }

        buttons: [
            UI.Button {
                id: addButton
                text: "Add"
                kind: "primary"
                enabled: addDialog.chosen >= 0 && addDialog.chosen < addDialog.shown.length
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    const l = addDialog.shown[addDialog.chosen];
                    Keyboard.add(l.layout, l.variant);
                    addDialog.close();
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: addDialog.close()
            }
        ]
    }
}
