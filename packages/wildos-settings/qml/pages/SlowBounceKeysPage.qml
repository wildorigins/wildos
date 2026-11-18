import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Accessibility > Slow and bounce keys: kaccessrc [Keyboard] SlowKeys and
// BounceKeys with their delays, as kcm_access keeps them. KWin applies
// bounce keys itself; slow keys come from the Wild OS KWin plugin
// (wildos-shell), as KWin 6.3 has none of its own.
PageScroll {
    id: page

    function seconds(ms) {
        const s = Math.round(ms / 100) / 10;
        return s === 1 ? qsTr("1 second") : qsTr("%1 seconds").arg(s);
    }

    Component.onCompleted: Accessibility.refresh()

    component DelaySlider: UI.Slider {
        id: slider

        property int delay
        signal settled(int ms)

        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        implicitWidth: 180
        from: 100
        to: 2000
        stepSize: 100
        value: delay
        // Written once the slider settles rather than at every step.
        onMoved: settle.restart()

        Timer {
            id: settle
            interval: 300
            onTriggered: {
                if (slider.pressed) {
                    restart();
                    return;
                }
                slider.settled(Math.round(slider.value));
                slider.value = Qt.binding(() => slider.delay);
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Slow keys")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Slow keys")
                    description: qsTr("Only count a key that's held down for a moment, so brushing one does nothing")
                    status: Accessibility.slowKeys ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Accessibility.slowKeys
                        Accessible.name: qsTr("Slow keys")
                        onToggled: Accessibility.setSlowKeys(checked)
                    }
                }

                UI.SettingRow {
                    title: qsTr("How long to hold a key")
                    description: page.seconds(slowDelay.value)
                    enabled: Accessibility.slowKeys

                    DelaySlider {
                        id: slowDelay
                        delay: Accessibility.slowKeysDelay
                        accessibleName: qsTr("How long to hold a key")
                        onSettled: ms => Accessibility.setSlowKeysDelay(ms)
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Bounce keys")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Bounce keys")
                    description: qsTr("Ignore a key pressed again straight away, so a shaky press types one letter")
                    status: Accessibility.bounceKeys ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: Accessibility.bounceKeys
                        Accessible.name: qsTr("Bounce keys")
                        onToggled: Accessibility.setBounceKeys(checked)
                    }
                }

                UI.SettingRow {
                    title: qsTr("Ignore repeats within")
                    description: page.seconds(bounceDelay.value)
                    enabled: Accessibility.bounceKeys

                    DelaySlider {
                        id: bounceDelay
                        delay: Accessibility.bounceKeysDelay
                        accessibleName: qsTr("Ignore repeats within")
                        onSettled: ms => Accessibility.setBounceKeysDelay(ms)
                    }
                }
            }
        }
    }
}
