import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// More keyboard options: the keyboard model, and where the compose and Alt
// Gr keys are, the XKB options people actually reach for. Caps Lock and
// the keys that switch layouts are on the Keyboard page itself. KWin
// rebuilds the keymap as soon as a choice is made.
PageScroll {
    id: page

    readonly property var composeChoices: [
        { value: "", text: qsTr("None") },
        { value: "compose:ralt", text: qsTr("Right Alt") },
        { value: "compose:rwin", text: qsTr("Right Windows key") },
        { value: "compose:menu", text: qsTr("Menu key") },
        { value: "compose:rctrl", text: qsTr("Right Ctrl") },
        { value: "compose:caps", text: qsTr("Caps Lock") },
        { value: "compose:prsc", text: qsTr("Print Screen") },
        { value: "compose:sclk", text: qsTr("Scroll Lock") },
        { value: "compose:paus", text: qsTr("Pause") },
        { value: "compose:ins", text: qsTr("Insert") }
    ]

    readonly property var altGrChoices: [
        { value: "", text: qsTr("As the layout has it") },
        { value: "lv3:ralt_switch", text: qsTr("Right Alt") },
        { value: "lv3:lalt_switch", text: qsTr("Left Alt") },
        { value: "lv3:alt_switch", text: qsTr("Either Alt") },
        { value: "lv3:rwin_switch", text: qsTr("Right Windows key") },
        { value: "lv3:menu_switch", text: qsTr("Menu key") },
        { value: "lv3:switch", text: qsTr("Right Ctrl") },
        { value: "lv3:caps_switch", text: qsTr("Caps Lock") },
        { value: "lv3:ralt_alt", text: qsTr("None, Right Alt is a normal Alt") }
    ]

    // A choice made somewhere else, such as by hand in kxkbrc, still shows
    // as itself instead of passing for the first entry.
    function withCurrent(choices, current) {
        if (choices.some(o => o.value === current)) {
            return choices;
        }
        return choices.concat([{ value: current, text: qsTr("Another key") }]);
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "keyboard"
                title: qsTr("Keyboard model")
                description: Keyboard.modelName

                UI.Button {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Change")
                    onClicked: modelDialog.open()
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Special keys")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Compose key")
                    description: qsTr("Press it, then two keys in turn, to type characters such as é, ñ or €")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Compose key")
                        readonly property string choice: Keyboard.compose
                        model: page.withCurrent(page.composeChoices, choice)
                        currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                        onActivated: index => Keyboard.setCompose(model[index].value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Alt Gr key")
                    description: qsTr("Hold it to type the extra characters shown on some keys")

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("Alt Gr key")
                        readonly property string choice: Keyboard.altGr
                        model: page.withCurrent(page.altGrChoices, choice)
                        currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                        onActivated: index => Keyboard.setAltGr(model[index].value)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Try it here")

                    UI.TextField {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        placeholderText: qsTr("Type in this box")
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
                iconName: "caps-lock"
                title: qsTr("Caps Lock and switching layouts")
                description: qsTr("Set on the Keyboard page")
                chevron: true
                onClicked: Navigator.open("keyboard")
            }
        }
    }

    UI.Dialog {
        id: modelDialog

        property var all: []
        property int chosen: -1
        readonly property var shown: {
            const query = filter.text.trim().toLowerCase();
            return all.filter(m => query.length === 0 || m.name.toLowerCase().includes(query) || m.model.toLowerCase().includes(query));
        }

        title: qsTr("Keyboard model")
        text: qsTr("Most keyboards work as a Generic 105-key PC. Choose your own model if some of its keys do nothing.")
        onAboutToShow: {
            if (all.length === 0) {
                all = Keyboard.models();
            }
            filter.text = "";
            chosen = shown.findIndex(m => m.model === Keyboard.model);
            filter.forceActiveFocus();
        }

        UI.SearchField {
            id: filter
            Layout.fillWidth: true
            placeholderText: qsTr("Search models, for example Logitech")
            onTextChanged: modelDialog.chosen = -1
            onDownPressed: {
                modelDialog.chosen = Math.max(0, modelDialog.chosen);
                list.forceActiveFocus();
            }
            onAccepted: {
                if (modelDialog.shown.length === 1) {
                    modelDialog.chosen = 0;
                }
                useButton.clicked();
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
                model: modelDialog.shown
                currentIndex: modelDialog.chosen
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                onCurrentIndexChanged: modelDialog.chosen = currentIndex
                Keys.onReturnPressed: useButton.clicked()

                delegate: UI.ListRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    minHeight: 36
                    divider: false
                    radius: UI.Style.radiusXs
                    interactive: true
                    title: modelData.name
                    checked: index === modelDialog.chosen
                    background: Rectangle {
                        radius: UI.Style.radiusXs
                        color: parent.checked ? UI.Style.accentSelected : parent.hovered ? UI.Style.surfaceHover : "transparent"
                    }
                    onClicked: modelDialog.chosen = index
                    onDoubleClicked: {
                        modelDialog.chosen = index;
                        useButton.clicked();
                    }
                }

                UI.Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No models match")
                    color: UI.Style.inkMuted
                }
            }
        }

        buttons: [
            UI.Button {
                id: useButton
                text: qsTr("Use")
                kind: "primary"
                enabled: modelDialog.chosen >= 0 && modelDialog.chosen < modelDialog.shown.length
                onClicked: {
                    if (!enabled) {
                        return;
                    }
                    Keyboard.setModel(modelDialog.shown[modelDialog.chosen].model);
                    modelDialog.close();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: modelDialog.close()
            }
        ]
    }
}
