import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// One choice of a set drawn as a card: theme and taskbar layouts in
// Welcome and Personalisation, protection levels in Settings, disks in the
// installer, starter apps. The artboards agree on the look (Welcome.dc,
// Personalisation.dc, Installer.dc, StarterApps.dc): rest 0.04 fill with a
// 1px 0.12 edge, chosen 2px accent edge on accent at 0.10; radius 12 for
// rows and 14 for tall cards. From wildos-welcome OptionCard, with
// Settings RadioCard's radio dot and note chip as options.
//
// `exclusive` (the default) makes it a radio; set it false for a set of
// ticks (StarterApps). Put any preview inside: it is the default property.
T.AbstractButton {
    id: control

    property string title
    property string description
    property string note
    property bool exclusive: true
    property bool showIndicator: false
    property real radius: implicitHeight > 72 ? Style.radiusLg : Style.radiusMd
    property bool previewHover: false
    property bool previewFocus: false
    default property alias extra: extraColumn.data

    readonly property bool lit: (hovered || previewHover) && enabled

    checkable: true
    autoExclusive: exclusive
    implicitWidth: Math.max(160, body.implicitWidth + leftPadding + rightPadding)
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    padding: 16
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.55
    Accessible.role: exclusive ? Accessible.RadioButton : Accessible.CheckBox
    Accessible.name: title
    Accessible.description: description

    background: Rectangle {
        radius: control.radius
        // Fades in on hover and clears at once, so moving along a list
        // never lights two items. It comes before the colour so it sees
        // the change in hover first.
        Behavior on color {
            enabled: control.lit
            ColorAnimation { duration: Style.hoverDuration }
        }
        color: control.checked ? Style.accentTint : control.down ? Style.surfacePressed : control.lit ? Style.surfaceHover : Style.surfaceRest
        border.width: control.checked ? 2 : 1
        border.color: control.checked ? Style.accent : Style.borderGlass

        FocusRing {
            visible: control.visualFocus || control.previewFocus
            targetRadius: control.radius
        }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: 6

        ColumnLayout {
            id: extraColumn
            Layout.fillWidth: true
            visible: children.length > 0
            spacing: 10
        }

        RowLayout {
            Layout.fillWidth: true
            visible: control.title.length > 0
            spacing: 8

            Rectangle {
                visible: control.showIndicator && control.exclusive
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                radius: 8
                color: control.checked ? Style.accentDot : "transparent"
                border.width: control.checked ? 5 : 1.5
                border.color: control.checked ? Style.accent : Style.radioRing
            }

            Rectangle {
                visible: control.showIndicator && !control.exclusive
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 6
                color: control.checked ? Style.accent : "transparent"
                border.width: control.checked ? 0 : 1.5
                border.color: Style.toggleOffBorder

                Icon {
                    visible: control.checked
                    anchors.centerIn: parent
                    name: "checkmark"
                    size: 13
                    strokeWidth: 3
                    color: Style.textOnAccent
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: Math.min(implicitWidth, 120)
                text: control.title
                size: 14
                font.weight: Style.weightSemiBold
            }

            Chip {
                visible: control.note.length > 0
                text: control.note
                granted: false
            }
        }

        Label {
            Layout.fillWidth: true
            visible: control.description.length > 0
            text: control.description
            size: 12
            color: Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            lineHeight: 1.4
        }
    }
}
