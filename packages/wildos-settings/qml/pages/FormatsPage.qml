import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// More format options, after Windows 11's "Customise format": numbers,
// money, dates and times, measurements and paper each follow the regional
// format or a format of their own, with how it looks beside each choice.
// Linux keeps the date and the time together in one part (LC_TIME), so
// they are chosen together here.
PageScroll {
    id: page

    readonly property var formats: Language.formats()
    readonly property var choices: [{ value: "", text: qsTr("Same as regional format") }].concat(formats)
    readonly property string regional: formats.find(o => o.value === Language.format)?.text ?? Language.format

    readonly property var parts: [
        { key: "LC_NUMERIC", icon: "list", title: qsTr("Numbers"), what: qsTr("Decimal point and thousands separator") },
        { key: "LC_MONETARY", icon: "store", title: qsTr("Currency"), what: qsTr("Currency symbol and where it goes") },
        { key: "LC_TIME", icon: "clock", title: qsTr("Dates and times"), what: qsTr("Date order, 12 or 24 hour clock, first day of the week") },
        { key: "LC_MEASUREMENT", icon: "resolution", title: qsTr("Measurement units"), what: qsTr("Metric or imperial") },
        { key: "LC_PAPER", icon: "printer", title: qsTr("Paper size"), what: qsTr("The paper documents are laid out on") }
    ]

    readonly property var fileSizeChoices: [
        { value: 0, text: qsTr("KiB, MiB (1,024 bytes)") },
        { value: 1, text: qsTr("KB, MB (1,024 bytes)") },
        { value: 2, text: qsTr("kB, MB (1,000 bytes)") }
    ]

    // Typing to pick in a closed list chooses at every letter; only the
    // choice it settles on is applied, so setting up a format the computer
    // lacks asks for a password once.
    Timer {
        id: applyTimer
        property string key
        property string choice
        interval: 700
        onTriggered: Language.setCategory(key, choice)
    }

    function choose(key, value) {
        if (applyTimer.running && applyTimer.key !== key) {
            applyTimer.stop();
            Language.setCategory(applyTimer.key, applyTimer.choice);
        }
        applyTimer.key = key;
        applyTimer.choice = value;
        applyTimer.restart();
    }

    Connections {
        target: Language
        function onFailed(reason) {
            notice.show(qsTr("The format was not changed"), reason, true);
        }
    }

    Notice {
        id: notice
    }

    StatusCard {
        visible: Language.needsSignOut
        iconName: "sign-out"
        title: qsTr("Sign out to finish")
        description: qsTr("Apps use the new formats when you sign out and back in. Save your work first.")

        UI.Button {
            text: qsTr("Sign out now")
            onClicked: Language.signOut()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Formats")
        }

        UI.Label {
            Layout.fillWidth: true
            text: qsTr("Your regional format is %1. Choose another format for any of these and the rest stay as they are.").arg(page.regional)
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.parts

                    UI.SettingRow {
                        id: part
                        required property var modelData
                        readonly property string chosen: Language.overrides[modelData.key] || ""
                        readonly property string effective: chosen.length > 0 ? chosen : Language.format
                        iconName: modelData.icon
                        title: modelData.title
                        description: Language.example(modelData.key, effective)
                        enabled: !Language.busy

                        UI.ComboBox {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: part.modelData.title
                            Accessible.description: part.modelData.what
                            model: page.choices
                            readonly property string choice: part.chosen
                            currentIndex: model.findIndex(o => o.value === choice)
                            displayText: currentIndex >= 0 ? currentText : choice
                            onActivated: index => page.choose(part.modelData.key, model[index].value)
                        }
                    }
                }

                UI.SettingRow {
                    iconName: "storage"
                    title: qsTr("File sizes")
                    description: qsTr("A file of 1,536,000 bytes shows as %1").arg(Language.fileSizeExample(Language.fileSizeUnits))

                    UI.ComboBox {
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: qsTr("File sizes")
                        model: page.fileSizeChoices
                        readonly property int choice: Language.fileSizeUnits
                        currentIndex: Math.max(0, model.findIndex(o => o.value === choice))
                        onActivated: index => Language.setFileSizeUnits(model[index].value)
                    }
                }
            }
        }

        UI.Label {
            Layout.fillWidth: true
            text: Language.busy ? qsTr("Getting the format ready on this computer") : qsTr("File sizes change in each app the next time it opens.")
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }
}
