import QtQuick
import QtQuick.Templates as T

// The Wild OS button. Based on wildos-settings WButton (icon, busy) and the
// Welcome footer's large size, measured against the artboards:
//   primary    accent fill, on-accent text, weight 600 (BringFiles,
//              WelcomePrivacy, WinApp, Store "Install")
//   secondary  surface-control 0.06 with a 0.16 border, weight 500
//              (BringFiles, Flyouts, Store, Welcome, WinApp; the "Check
//              now" button in Settings.dc at 0.08 and 0.18 is the outlier)
//   text       no fill until hovered, ink label, weight 400: the Files
//              command bar's New, Sort and View (FilesDetails.dc)
// Sizes: "small" 32px with 12px text (Flyouts, Store cards), "medium" 36px
// with 13px (Settings, BringFiles), "large" 40px with 14px and radius 10
// (Welcome and WinApp footers). Pressing darkens the button's own fill
// instead of swapping it for grey.
//
// `danger: true` marks a secondary or text button that removes something:
// its label and icon take the danger red and the fill stays neutral. A
// primary button keeps the accent, as the confirm button of a Windows
// "Delete" dialog does, so `danger` changes nothing there.
T.Button {
    id: control

    property string kind: "secondary"
    property string size: "medium"
    property string iconName
    property bool danger: false
    property bool busy: false
    property bool dropDown: false
    // The Files command bar draws New's plus in the accent at 18px.
    property color iconColor: primary ? Style.textOnAccent : danger ? Style.danger : Style.iconInk
    property real iconSize: textKind ? 17 : size === "large" ? 16 : 15
    property real iconStroke: textKind ? 1.8 : 2
    // Words on a photograph (the Store's banner) stay white in either scheme.
    property color textColor: primary ? Style.textOnAccent : danger ? Style.danger : onWallpaper ? Style.inkOnWallpaper : Style.ink
    // A secondary button straight on a dimmed photograph (Ctrl+Alt+Del):
    // the dark-mode fill, edge and light text in either scheme, since the
    // backdrop stays dark.
    property bool onWallpaper: false
    // Lets the gallery and design reviews show hover and focus without a
    // pointer or keyboard.
    property bool previewHover: false
    property bool previewFocus: false

    readonly property bool primary: kind === "primary"
    readonly property bool textKind: kind === "text"
    readonly property bool lit: (hovered || previewHover) && enabled && !busy
    readonly property int fontSize: size === "small" ? 12 : size === "large" ? 14 : 13

    // Heights are the design's; larger system text grows them to fit.
    implicitHeight: Math.max(size === "small" ? Style.controlHeightSmall : size === "large" ? Style.controlHeightLarge : Style.controlHeight,
                             implicitContentHeight + 14)
    implicitWidth: Math.max(textKind ? 0 : 64, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: textKind ? 10 : size === "small" ? 14 : size === "large" ? 24 : 16
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : Style.disabledOpacity

    Accessible.name: text
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: control.size === "large" ? 10 : Style.radiusSm
        color: control.primary
            ? (control.down ? Style.accentPressed : control.lit ? Style.accentHover : Style.accent)
            : control.onWallpaper ? (control.down ? Style.controlOnWallpaperPressed : control.lit ? Style.controlOnWallpaperHover : Style.controlOnWallpaper)
            : control.down ? Style.surfacePressed
            : control.lit || (control.textKind && control.checked) ? Style.surfaceHover
            : control.textKind ? Style.surfaceHoverClear : Style.surfaceControl
        border.width: control.primary || control.textKind ? 0 : 1
        border.color: control.onWallpaper ? Style.borderControlOnWallpaper : Style.borderControl
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }

        FocusRing {
            visible: control.visualFocus || control.previewFocus
            targetRadius: parent.radius
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            opacity: control.busy ? 0 : 1

            Icon {
                visible: control.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: control.iconSize
                strokeWidth: control.iconStroke
                color: control.iconColor
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                size: control.fontSize
                font.weight: control.primary ? Style.weightSemiBold : control.textKind ? Style.weightRegular : Style.weightMedium
                color: control.textColor
            }

            Icon {
                visible: control.dropDown
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: 11
                strokeWidth: 2.4
                color: control.primary ? Style.textOnAccent : Style.iconInk
            }
        }

        Spinner {
            visible: control.busy
            anchors.centerIn: parent
            size: 16
            color: control.primary ? Style.textOnAccent : Style.ink
        }
    }
}
