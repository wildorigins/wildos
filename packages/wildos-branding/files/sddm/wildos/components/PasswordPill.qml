import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// The 300x46 glass pill with the round accent arrow (Lock.dc). Used for the
// password and, when the user list is hidden, for the username too. It is a
// sign-in control of its own rather than a UI.TextField because it sits on
// the photograph; its colours are the shared tokens. While `busy` the arrow
// turns into a spinner and typing waits.
FocusScope {
    id: pill

    property Item backdrop: null
    property alias field: field
    property alias text: field.text
    property string placeholder: "Password"
    property bool secret: true
    property bool showSubmit: true
    property bool busy: false
    property string submitLabel: "Sign in"

    signal submitted()

    implicitWidth: 300
    implicitHeight: 46
    opacity: enabled || busy ? 1 : 0.5

    function shake() {
        shakeAnimation.restart();
    }

    transform: Translate { id: nudge }

    SequentialAnimation {
        id: shakeAnimation
        NumberAnimation { target: nudge; property: "x"; to: -7; duration: 55; easing.type: Easing.OutQuad }
        NumberAnimation { target: nudge; property: "x"; to: 6; duration: 80; easing.type: Easing.InOutQuad }
        NumberAnimation { target: nudge; property: "x"; to: -4; duration: 70; easing.type: Easing.InOutQuad }
        NumberAnimation { target: nudge; property: "x"; to: 2; duration: 60; easing.type: Easing.InOutQuad }
        NumberAnimation { target: nudge; property: "x"; to: 0; duration: 50; easing.type: Easing.OutQuad }
    }

    Glass {
        anchors.fill: parent
        backdrop: pill.backdrop
        tint: UI.Style.glassOnWallpaperField
        borderColor: field.activeFocus && !pill.busy ? UI.Style.accentBorderStrong : UI.Style.borderOnWallpaperField
        radius: height / 2
        blurRadius: 30
    }

    T.TextField {
        id: field

        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: submit.visible ? submit.left : parent.right
        anchors.rightMargin: submit.visible ? 8 : 18
        anchors.verticalCenter: parent.verticalCenter
        height: 40
        focus: true
        readOnly: pill.busy
        padding: 0
        background: null
        color: UI.Style.inkOnWallpaper
        selectionColor: UI.Style.accentBorderStrong
        selectedTextColor: UI.Style.inkOnWallpaper
        font.family: UI.Style.fontFamily
        font.pixelSize: UI.Style.px(14)
        font.letterSpacing: pill.secret && text.length > 0 ? 1.5 : 0
        echoMode: pill.secret ? TextInput.Password : TextInput.Normal
        passwordCharacter: "•"
        UI.NoPaste { enabled: pill.secret }
        placeholderText: pill.placeholder
        placeholderTextColor: UI.Style.inkOnWallpaperMuted
        inputMethodHints: pill.secret ? (Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText)
                                      : (Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText)
        verticalAlignment: TextInput.AlignVCenter
        Accessible.name: pill.placeholder

        // T.TextField draws no placeholder of its own.
        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: field.text.length === 0 && field.preeditText.length === 0
            text: field.placeholderText
            color: field.placeholderTextColor
            font.family: UI.Style.fontFamily
            font.letterSpacing: UI.Style.tracking(field.font.pixelSize)
            font.pixelSize: field.font.pixelSize
            elide: Text.ElideRight
        }

        onAccepted: if (!pill.busy) pill.submitted()
    }

    T.AbstractButton {
        id: submit

        visible: pill.showSubmit
        anchors.right: parent.right
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 36
        height: 36
        enabled: !pill.busy
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: pill.busy ? "Signing in" : pill.submitLabel
        Accessible.role: Accessible.Button

        background: Rectangle {
            radius: height / 2
            color: submit.down ? UI.Style.accentPressed : submit.hovered ? UI.Style.accentHover : UI.Style.accent
            Behavior on color { ColorAnimation { duration: UI.Style.quickDuration } }

            UI.FocusRing {
                visible: submit.visualFocus
                targetRadius: parent.radius
            }
        }

        contentItem: Item {
            UI.Icon {
                anchors.centerIn: parent
                visible: !pill.busy
                name: "arrow"
                size: 16
                strokeWidth: 2.4
                color: UI.Style.textOnAccent
                rotation: Qt.application.layoutDirection === Qt.RightToLeft ? 180 : 0
            }

            UI.Spinner {
                anchors.centerIn: parent
                visible: pill.busy
                size: 16
                color: UI.Style.textOnAccent
            }
        }

        onClicked: pill.submitted()
    }
}
