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
        { value: "grp:win_space_toggle", text: qsTr("Windows key + Space") },
        { value: "grp:alt_shift_toggle", text: qsTr("Alt + Shift") },
        { value: "grp:ctrl_shift_toggle", text: qsTr("Ctrl + Shift") },
        { value: "grp:caps_toggle", text: qsTr("Caps Lock") },
        { value: "", text: qsTr("No keys") }
    ]

    readonly property var capsChoices: [
        { value: "", text: qsTr("Caps Lock") },
        { value: "ctrl:nocaps", text: qsTr("Another Ctrl key") },
        { value: "caps:escape", text: qsTr("Escape") },
        { value: "caps:none", text: qsTr("Nothing") }
    ]

    readonly property var numLockChoices: [
        { value: 0, text: qsTr("On") },
        { value: 1, text: qsTr("Off") },
        { value: 2, text: qsTr("As it was") }
    ]

    function openMenu(button, index) {
        const entries = [];
        if (several) {
            entries.push({ text: qsTr("Use now"), icon: "check", enabled: index !== Keyboard.current, run: () => Keyboard.use(index) });
            entries.push({ separator: true });
        }
        entries.push({ text: qsTr("Move up"), icon: "chevron-up", enabled: index > 0, run: () => Keyboard.move(index, index - 1) });
        entries.push({ text: qsTr("Move down"), icon: "chevron-down", enabled: index < layouts.length - 1, run: () => Keyboard.move(index, index + 1) });
        entries.push({ separator: true });
        entries.push({ text: qsTr("Remove"), icon: "delete", danger: true, enabled: several, run: () => Keyboard.remove(index) });
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
                text: qsTr("Keyboard layouts")
            }

            UI.Button {
                text: qsTr("Add a keyboard")
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
                        description: index === 0 ? (page.several ? qsTr("Used first when you sign in") : qsTr("The only layout, used everywhere")) : ""
                        status: page.several && index === Keyboard.current ? qsTr("In use") : ""
                        statusColor: UI.Style.accentText

                        UI.IconButton {
                            id: moreButton
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "more"
                            tip: qsTr("Options for %1").arg(layoutRow.modelData.name)
                            onClicked: page.openMenu(moreButton, layoutRow.index)
                        }
                    }
                }
            }
        }
    }

    // A typing method such as Vietnamese (Telex) is fetched the first time
    // it is added; until then its entry types plain US English.
    StatusCard {
        visible: Keyboard.methodMissing || Keyboard.downloading
        iconName: "keyboard"
        warning: !Keyboard.downloading
        spinning: Keyboard.downloading
        title: Keyboard.downloading ? Keyboard.downloadStep : qsTr("Vietnamese typing needs a download")
        description: Keyboard.downloading ? qsTr("This takes a minute.")
            : Keyboard.downloadError.length > 0 ? Keyboard.downloadError
            : qsTr("Wild OS downloads the typing method once, then Windows key + Space switches to it.")

        UI.Button {
            visible: !Keyboard.downloading
            kind: "primary"
            text: qsTr("Download")
            onClicked: Keyboard.download()
        }
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "swap"
                title: qsTr("Switch layouts with")
                description: page.several ? qsTr("Press these keys together to move to the next layout") : qsTr("Add a second keyboard to switch between them")
                enabled: page.several

                UI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: qsTr("Switch layouts with")
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
            text: qsTr("Typing")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Repeat a key while you hold it down")
                    status: Keyboard.repeat ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Keyboard.repeat
                        Accessible.name: qsTr("Repeat a key while you hold it down")
                        onToggled: Keyboard.setRepeat(checked)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Repeat delay")
                    description: qsTr("How long to hold a key before it repeats")
                    enabled: Keyboard.repeat
                    status: qsTr("%1 ms").arg(Keyboard.repeatDelay)

                    UI.Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        accessibleName: qsTr("Repeat delay")
                        from: 150
                        to: 1000
                        stepSize: 50
                        value: Keyboard.repeatDelay
                        onMoved: Keyboard.setRepeatDelay(value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Repeat rate")
                    description: qsTr("How fast a held key repeats")
                    enabled: Keyboard.repeat
                    status: qsTr("%1 a second").arg(Keyboard.repeatRate)

                    UI.Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        accessibleName: qsTr("Repeat rate")
                        from: 5
                        to: 50
                        stepSize: 1
                        value: Keyboard.repeatRate
                        onMoved: Keyboard.setRepeatRate(value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Try it here")
                    enabled: Keyboard.repeat

                    UI.TextField {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        placeholderText: qsTr("Hold down a key in this box")
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Keys")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Caps Lock key works as")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Caps Lock key works as")
                        model: page.capsChoices
                        readonly property string choice: Keyboard.capsLock
                        currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                        onActivated: index => Keyboard.setCapsLock(model[index].value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Num Lock when you sign in")
                    description: qsTr("Takes effect the next time you sign in")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Num Lock when you sign in")
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
                iconName: "keyboard"
                title: qsTr("Keyboard shortcuts")
                description: qsTr("See and change shortcuts, and add your own")
                chevron: true
                onClicked: Navigator.open("shortcuts")
            }

            UI.SettingRow {
                iconName: "cog"
                title: qsTr("More keyboard options")
                description: qsTr("Keyboard model, a compose key and other special keys")
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
            const key = l => l.layout + "|" + l.variant + "|" + (l.method || "");
            const taken = page.layouts.map(key);
            return all.filter(l => !taken.includes(key(l))
                && (query.length === 0 || l.name.toLowerCase().includes(query) || (l.keywords || "").includes(query)));
        }

        title: qsTr("Add a keyboard")
        text: qsTr("Choose the layout printed on your keyboard, or one you type in.")
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
            placeholderText: qsTr("Search by country, language or layout, for example Belgium")
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
                    text: qsTr("No layouts match")
                    color: UI.Style.inkMuted
                }
            }
        }

        buttons: [
            UI.Button {
                id: addButton
                text: qsTr("Add")
                kind: "primary"
                enabled: addDialog.chosen >= 0 && addDialog.chosen < addDialog.shown.length
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    const l = addDialog.shown[addDialog.chosen];
                    Keyboard.add(l.layout, l.variant, l.method || "");
                    addDialog.close();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: addDialog.close()
            }
        ]
    }
}
