import QtQuick
import WildOS.Settings
import WildOS.UI as UI

// The text size choice, shown in Personalisation and in Accessibility; both
// change the one setting through System.setTextScale. A size set elsewhere
// (KDE's fonts module) is listed too, so the box never shows nothing.
UI.ComboBox {
    readonly property var sizes: [100, 110, 125, 150]
    readonly property var choice: System.textScale

    Accessible.name: qsTr("Text size")
    model: sizes.concat(sizes.includes(System.textScale) ? [] : [System.textScale]).map(p => ({ value: p, text: p + "%" }))
    currentIndex: model ? model.findIndex(o => o.value === choice) : -1
    onActivated: index => System.setTextScale(model[index].value)
}
