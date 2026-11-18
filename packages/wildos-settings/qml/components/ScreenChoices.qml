import QtQuick

// The screens a touchscreen or pen can be tied to, by the connector name
// KWin keys them on (QScreen.name, such as eDP-1), labelled with the
// screen's model and size, as KDE's touchscreen and tablet modules do.
QtObject {
    id: choices

    property string automaticText: qsTr("Automatic")
    property bool allScreens: false

    readonly property int screenCount: Qt.application.screens.length
    readonly property var model: {
        const list = [{ value: "", text: automaticText }];
        if (allScreens) {
            list.push({ value: "*", text: qsTr("All screens") });
        }
        const screens = Qt.application.screens;
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i];
            const label = s.model && s.model.length > 0 ? s.model : s.name;
            list.push({ value: s.name, text: qsTr("%1 (%2 × %3)").arg(label).arg(s.width).arg(s.height) });
        }
        return list;
    }

    function indexOf(value) {
        return Math.max(0, model.findIndex(o => o.value === value));
    }
}
