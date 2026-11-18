import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Pen and tablet, after Windows 11's Pen and Windows Ink: which hand you
// write with, and which screen a drawing tablet's surface stands for.
// KWin applies each change straight away and keeps it for the pen.
PageScroll {
    id: page

    readonly property var pens: InputDevices.pens

    ScreenChoices {
        id: screenChoices
        automaticText: qsTr("The screen the pointer is on")
        allScreens: true
    }

    // KWin keeps "all screens" as its own switch beside the screen name, so
    // the one choice here sets both, as KDE's tablet module does.
    function setScreen(pen, value) {
        InputDevices.setDevice(pen.sysName, "mapToWorkspace", value === "*");
        InputDevices.setDevice(pen.sysName, "outputName", value === "*" ? "" : value);
    }

    StatusCard {
        visible: page.pens.length === 0
        iconName: "pencil"
        warning: true
        title: qsTr("No pen or tablet connected")
        description: qsTr("Plug in a drawing tablet, or use the pen of a touchscreen PC, and its settings appear here.")
    }

    Repeater {
        model: page.pens

        ColumnLayout {
            id: pen
            required property var modelData
            Layout.fillWidth: true
            spacing: 10

            SectionTitle {
                text: pen.modelData.name
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        visible: pen.modelData.supportsLeftHanded === true
                        iconName: "pencil"
                        title: qsTr("Which hand do you write with?")
                        description: qsTr("A tablet held for the left hand is turned round")

                        UI.ComboBox {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Which hand do you write with?")
                            model: [{ value: false, text: qsTr("Right hand") }, { value: true, text: qsTr("Left hand") }]
                            readonly property bool choice: pen.modelData.leftHanded === true
                            currentIndex: model.findIndex(o => o.value === choice)
                            onActivated: index => InputDevices.setDevice(pen.modelData.sysName, "leftHanded", model[index].value)
                        }
                    }

                    UI.SettingRow {
                        title: qsTr("Screen")
                        description: qsTr("Where the pen points when it touches the tablet")

                        UI.ComboBox {
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Screen for %1").arg(pen.modelData.name)
                            model: screenChoices.model
                            currentIndex: screenChoices.indexOf(pen.modelData.mapToWorkspace === true ? "*" : (pen.modelData.outputName || ""))
                            onActivated: index => page.setScreen(pen.modelData, model[index].value)
                        }
                    }
                }
            }
        }
    }
}
