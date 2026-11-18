import QtQuick
import QtQuick.Layouts
import WildOS.Desktop
import WildOS.UI as UI

// New > Shortcut: where the shortcut goes and what to call it, as in the
// Windows Create shortcut wizard, in one step.
DeskDialog {
    id: dialog

    property url folder
    property bool nameTouched: false
    readonly property var check: DeskOps.checkShortcut(locationBox.text)
    property bool tried: false

    title: qsTr("Create a shortcut")
    defaultAction: () => dialog.create()
    text: qsTr("A shortcut opens a file, a folder or a web page from here.")

    function show(target) {
        folder = target;
        locationInput.text = "";
        nameInput.text = "";
        nameTouched = false;
        tried = false;
        present();
        locationInput.forceActiveFocus();
    }

    function create() {
        tried = true;
        if (!check.ok) {
            locationInput.forceActiveFocus();
            return;
        }
        close();
        DeskOps.newShortcut(folder, locationInput.text, nameInput.text);
    }

    onCheckChanged: if (!nameTouched) nameInput.text = check.ok ? check.name : ""

    component Field: UI.TextField {
        property string label
        Layout.fillWidth: true
        Accessible.name: label
        Keys.onReturnPressed: dialog.create()
        Keys.onEnterPressed: dialog.create()
    }

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Location")
        font.weight: UI.Style.weightMedium
    }

    Field {
        id: locationBox
        label: qsTr("Location")
        placeholderText: qsTr("~/Documents/Budget 2026.ods or www.example.com")
        error: dialog.tried && !dialog.check.ok
    }

    UI.Label {
        Layout.fillWidth: true
        visible: dialog.tried && !dialog.check.ok
        text: dialog.check.error ?? ""
        font.pixelSize: UI.Style.px(12)
        color: UI.Style.danger
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: qsTr("Name")
        font.weight: UI.Style.weightMedium
    }

    Field {
        id: nameBox
        label: qsTr("Name")
        placeholderText: qsTr("What to call the shortcut")
    }

    property alias locationInput: locationBox
    property alias nameInput: nameBox

    Connections {
        target: nameBox
        function onTextEdited() {
            dialog.nameTouched = nameBox.text.length > 0;
        }
    }

    buttons: [
        UI.Button {
            kind: "primary"
            text: qsTr("Create")
            enabled: dialog.locationInput.text.trim().length > 0
            onClicked: dialog.create()
        },
        UI.Button {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
    ]
}
