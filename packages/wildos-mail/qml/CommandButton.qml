import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A button in Mail's command bar (MailMicrosoft.dc): 34px, icon and label
// in 13px ink, no fill until hovered. The icon is ink too: the accent is
// for the main button only. Set `menu` to open it on click.
T.AbstractButton {
    id: control

    property string iconName
    property bool iconOnly: false
    property string tip: text
    property UI.Menu menu: null
    property color iconColor: UI.Style.iconInk
    // Turns the icon while a sync runs, as the Updates icon does.
    property bool spinning: false

    implicitHeight: 34
    implicitWidth: iconOnly ? 34 : row.implicitWidth + 20
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : 0.45
    Accessible.name: tip

    onClicked: if (menu) menu.popup(control, 0, control.height + 4)

    background: Rectangle {
        radius: control.iconOnly ? UI.Style.radiusSm : UI.Style.radiusXs
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.hovered
            ColorAnimation { duration: UI.Style.hoverDuration }
        }
        color: control.down ? UI.Style.surfacePressed : control.hovered ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
        UI.FocusRing {
            visible: control.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7

            MailIcon {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: 17
                color: control.iconColor
                visible: control.iconName.length > 0

                // A plain number animation, not an animator, so the angle is known
                // when the sync ends and the turn can finish instead of snapping
                // back to upright. Easing out over twice the time the spin would
                // take leaves at the spin's own speed and slows to rest.
                NumberAnimation {
                    id: spin
                    target: icon
                    property: "rotation"
                    from: 0
                    to: 360
                    duration: 1200
                    loops: Animation.Infinite
                }
                NumberAnimation {
                    id: settle
                    target: icon
                    property: "rotation"
                    to: 360
                    easing.type: Easing.OutQuad
                    onFinished: icon.rotation = 0
                }
                Connections {
                    target: control
                    function onSpinningChanged() {
                        if (control.spinning) {
                            settle.stop();
                            if (!spin.running) {
                                spin.start();
                            }
                        } else if (spin.running) {
                            spin.stop();
                            settle.duration = Math.max(200, 2 * (360 - icon.rotation) / 360 * 1200);
                            settle.start();
                        }
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !control.iconOnly
                text: control.text
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
                font.pixelSize: UI.Style.px(13)
            }
        }
    }

    UI.Tooltip {
        visible: control.iconOnly && control.hovered && control.tip.length > 0
        text: control.tip
    }
}
