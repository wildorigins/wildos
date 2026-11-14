import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.Settings
import WildOS.UI as UI

// Set up fingerprint (FingerprintSetup.dc and FingerprintSteps.dc): one
// 460px dialog over the dimmed window that moves through four steps, with
// the dots at the bottom showing where you are.
//
//   1  Confirm it's you: the password, checked, then handed to fprintd's
//      polkit question so nothing else asks for it.
//   2  Touch the sensor, then lift: the first two thirds of the reader's
//      touches (fprintd's num-enroll-stages).
//   3  Now the edges: the rest.
//   4  You're all set: the ring turns green, with a tick in the middle.
//
// The ring and the print fill with fprintd's enroll-stage-passed count. A
// touch that doesn't count leaves both where they are and changes only the
// line under the heading.
T.Popup {
    id: dialog

    property int step: 1
    property string error
    // The finger wasn't saved (fprintd ended the enrolment); Try again
    // starts it over.
    property string failure
    readonly property bool checking: Fingerprint.busy === "auth"
    readonly property real progress: step === 4 ? 1 : Fingerprint.stages > 0 ? Fingerprint.passed / Fingerprint.stages : 0
    readonly property int edgesFrom: Math.ceil(Fingerprint.stages * 2 / 3)
    readonly property bool moreFingers: Fingerprint.fingers.length < 10

    // The board's colours; the green has no token of its own.
    readonly property color surface: UI.Style.dark ? "#23252d" : UI.Style.glassDialog
    readonly property color edge: UI.Style.tone(0.12)
    readonly property color hairline: UI.Style.tone(0.07)
    readonly property color ok: UI.Style.dark ? "#5fd38d" : "#1d7a4c"
    readonly property color shieldInk: Qt.rgba(UI.Style.accent.r + (1 - UI.Style.accent.r) * 0.55, UI.Style.accent.g + (1 - UI.Style.accent.g) * 0.55,
                                                UI.Style.accent.b + (1 - UI.Style.accent.b) * 0.55, 1)

    function scanStep() {
        return Fingerprint.passed >= edgesFrom ? 3 : 2;
    }

    function submit() {
        if (passwordInput.text.length === 0 || checking) {
            return;
        }
        error = "";
        Fingerprint.begin(passwordInput.text);
    }

    function anotherFinger() {
        failure = "";
        // The ring empties at once for the next finger, then fills as
        // before once the reader is listening.
        ring.animated = false;
        step = 2;
        Fingerprint.again();
    }

    Connections {
        target: Fingerprint
        enabled: dialog.visible

        function onStarted() {
            Qt.callLater(() => ring.animated = true);
            passwordInput.text = "";
            dialog.failure = "";
            dialog.step = dialog.scanStep();
        }
        function onEnrollChanged() {
            if (dialog.step === 2 || dialog.step === 3) {
                dialog.step = dialog.scanStep();
            }
        }
        function onEnrolled(finger) {
            dialog.step = 4;
        }
        function onFailed(what, reason) {
            ring.animated = true;
            if (what === "password" || what === "setup") {
                if (dialog.step === 1) {
                    dialog.error = reason;
                    passwordInput.selectAll();
                    passwordInput.forceActiveFocus();
                } else {
                    dialog.failure = reason;
                }
            } else if (what === "enroll") {
                dialog.failure = reason;
            }
        }
    }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 48 : 460)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    modal: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape

    onAboutToShow: {
        step = 1;
        error = "";
        failure = "";
        passwordInput.text = "";
    }
    onOpened: passwordInput.forceActiveFocus()
    onClosed: Fingerprint.finish()

    T.Overlay.modal: Rectangle {
        radius: Window.window && Window.window.visibility !== Window.Maximized && Window.window.visibility !== Window.FullScreen ? UI.Style.windowRadius : 0
        color: UI.Style.dark ? Qt.rgba(4 / 255, 6 / 255, 10 / 255, 0.55) : UI.Style.scrim
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionFast }
        }
    }

    background: Rectangle {
        radius: 12
        color: dialog.surface
        border.width: 1
        border.color: dialog.edge
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, UI.Style.dark ? 0.5 : 0.2)
            shadowVerticalOffset: 30
            shadowBlur: 1.0
            blurMax: 64
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        UI.Label {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 20
            Layout.bottomMargin: 4
            text: "Set up fingerprint"
            size: 15
            font.weight: 600
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 10
            Layout.bottomMargin: 20
            spacing: 14

            Item {
                visible: dialog.step === 1
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 44
                implicitHeight: 56

                PathIcon {
                    anchors.centerIn: parent
                    name: "shield-round-check"
                    size: 44
                    strokeWidth: 1.8
                    color: dialog.shieldInk
                }
            }

            // The ring, and the print inside it or the tick once done.
            Item {
                id: ring

                property bool animated: true
                property real shown: dialog.progress
                Behavior on shown {
                    enabled: ring.animated
                    NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
                }

                visible: dialog.step > 1
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 196
                implicitHeight: 196

                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: UI.Style.tone(0.08)
                        strokeWidth: 4
                        fillColor: "transparent"
                        PathAngleArc {
                            centerX: 98
                            centerY: 98
                            radiusX: 93
                            radiusY: 93
                            startAngle: 0
                            sweepAngle: 360
                        }
                    }

                    ShapePath {
                        strokeColor: dialog.step === 4 ? dialog.ok : UI.Style.accent
                        strokeWidth: ring.shown > 0 ? 4 : 0
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathAngleArc {
                            centerX: 98
                            centerY: 98
                            radiusX: 93
                            radiusY: 93
                            startAngle: -90
                            sweepAngle: 360 * Math.min(ring.shown, 1)
                        }
                    }
                }

                // Fluent's fingerprint_48_regular at 120px: a faint copy, and
                // the accent copy clipped by a rectangle that rises from the
                // bottom as touches count (y = 43 - 38 * progress on the 48
                // grid).
                Item {
                    id: print

                    readonly property real unit: 120 / 48
                    readonly property real clipTop: (43 - 38 * Math.min(ring.shown, 1)) * unit

                    visible: dialog.step !== 4
                    anchors.centerIn: parent
                    width: 120
                    height: 120

                    Shape {
                        width: 48
                        height: 48
                        preferredRendererType: Shape.CurveRenderer
                        transform: Scale {
                            xScale: print.unit
                            yScale: print.unit
                        }

                        ShapePath {
                            fillColor: UI.Style.tone(0.22)
                            fillRule: ShapePath.WindingFill
                            strokeWidth: -1
                            PathSvg {
                                path: Fingerprint.print
                            }
                        }
                    }

                    Item {
                        y: print.clipTop
                        width: 120
                        height: 120 - print.clipTop
                        clip: true

                        Shape {
                            y: -print.clipTop
                            width: 48
                            height: 48
                            preferredRendererType: Shape.CurveRenderer
                            transform: Scale {
                                xScale: print.unit
                                yScale: print.unit
                            }

                            ShapePath {
                                fillColor: UI.Style.accent
                                fillRule: ShapePath.WindingFill
                                strokeWidth: -1
                                PathSvg {
                                    path: Fingerprint.print
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: dialog.step === 4
                    anchors.centerIn: parent
                    width: 64
                    height: 64
                    radius: 32
                    color: Qt.rgba(dialog.ok.r, dialog.ok.g, dialog.ok.b, 0.16)

                    PathIcon {
                        anchors.centerIn: parent
                        name: "check"
                        size: 34
                        strokeWidth: 3
                        color: dialog.ok
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                UI.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: dialog.step === 1 ? "First, confirm it's you"
                        : dialog.step === 2 ? (Fingerprint.scanType === "swipe" ? "Swipe your finger" : "Touch the sensor, then lift")
                        : dialog.step === 3 ? "Now the edges"
                        : "You're all set"
                    size: 18
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(360, parent.width)
                    horizontalAlignment: Text.AlignHCenter
                    text: dialog.step === 1
                        ? "Enter the password for " + (Users.me.realName || Users.me.userName || "this account")
                            + ". You'll still need it to unlock the disk at start-up and if the reader stops working."
                        : dialog.step === 4
                            ? "Use your fingerprint to sign in, unlock and approve admin prompts. Add another finger in case this one's wet or cut."
                        : dialog.failure.length > 0 ? dialog.failure
                        : Fingerprint.retry.length > 0 ? Fingerprint.retry
                        : dialog.step === 2
                            ? (Fingerprint.scanType === "swipe" ? "Swipe the same finger across the reader, and repeat. Move it slightly each time."
                                : "Rest the same finger on the reader, lift it, and repeat. Move it slightly each time.")
                        : "Tilt your finger and touch with the sides and tip, so it works however you hold it."
                    // Two lines' room whatever it says, so a one-line retry
                    // message doesn't make the dialog jump.
                    Layout.minimumHeight: Math.ceil(font.pixelSize * 1.5) * 2
                    verticalAlignment: Text.AlignTop
                    size: 13
                    lineHeight: 1.5
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            ColumnLayout {
                visible: dialog.step === 1
                Layout.fillWidth: true
                spacing: 6

                UI.Label {
                    text: "Password"
                    size: 12.5
                    color: UI.Style.inkMuted
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: 6
                    color: UI.Style.dark ? Qt.rgba(9 / 255, 11 / 255, 15 / 255, 0.35) : UI.Style.tone(0.04)
                    border.width: 1
                    border.color: dialog.error.length > 0 ? UI.Style.danger : UI.Style.tone(0.14)

                    // The 2px accent line along the bottom while it has the
                    // keyboard, inside the rounded corners.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        bottomLeftRadius: 6
                        bottomRightRadius: 6
                        visible: passwordInput.activeFocus
                        color: UI.Style.accent
                    }

                    TextInput {
                        id: passwordInput
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        echoMode: TextInput.Password
                        UI.NoPaste {}
                        passwordCharacter: "•"
                        color: UI.Style.ink
                        selectionColor: UI.Style.textSelection
                        selectedTextColor: UI.Style.ink
                        font.family: UI.Style.fontFamily
                        font.pixelSize: UI.Style.px(15)
                        font.letterSpacing: UI.Style.px(15) * 0.25
                        clip: true
                        readOnly: dialog.checking
                        Accessible.name: "Password"
                        Accessible.passwordEdit: true
                        onTextChanged: dialog.error = ""
                        onAccepted: dialog.submit()
                        Keys.onPressed: event => {
                            if (passwordMenu.isMenuKey(event)) {
                                passwordMenu.openAtCaret();
                                event.accepted = true;
                            }
                        }

                        UI.TextMenu {
                            id: passwordMenu
                        }
                    }
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: dialog.error
                    size: 12
                    color: UI.Style.danger
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Accessible.role: Accessible.StaticText
                Accessible.name: "Step " + dialog.step + " of 4"

                Repeater {
                    model: 4

                    Rectangle {
                        required property int index
                        width: index + 1 === dialog.step ? 18 : 6
                        height: 6
                        radius: 3
                        color: index + 1 <= dialog.step ? UI.Style.accent : UI.Style.tone(0.2)
                        Behavior on width {
                            NumberAnimation { duration: UI.Style.motionNormal; easing.type: Easing.OutCubic }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            bottomLeftRadius: 12
            bottomRightRadius: 12
            color: UI.Style.tone(0.03)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: dialog.hairline
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                DialogButton {
                    text: "Cancel"
                    visible: dialog.step < 4
                    onClicked: dialog.close()
                }
                DialogButton {
                    text: "Continue"
                    primary: true
                    visible: dialog.step === 1
                    enabled: passwordInput.text.length > 0 && !dialog.checking
                    onClicked: dialog.submit()
                }
                DialogButton {
                    text: "Try again"
                    primary: true
                    visible: (dialog.step === 2 || dialog.step === 3) && dialog.failure.length > 0
                    enabled: Fingerprint.busy === ""
                    onClicked: dialog.anotherFinger()
                }
                DialogButton {
                    text: "Add another finger"
                    visible: dialog.step === 4 && dialog.moreFingers
                    enabled: Fingerprint.busy === ""
                    onClicked: dialog.anotherFinger()
                }
                DialogButton {
                    text: "Done"
                    primary: true
                    visible: dialog.step === 4
                    onClicked: dialog.close()
                }
            }
        }
    }

    // The board's buttons: 32px, 16px either side, 6px corners; the main
    // one filled with the accent.
    component DialogButton: T.Button {
        id: button

        property bool primary: false

        implicitWidth: label.implicitWidth + 32
        implicitHeight: 32
        focusPolicy: Qt.StrongFocus
        Accessible.name: text
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()

        background: Rectangle {
            radius: 6
            opacity: button.enabled ? 1 : 0.5
            color: button.primary ? (button.down ? UI.Style.accentPressed : button.hovered ? UI.Style.accentHover : UI.Style.accent)
                : button.down ? UI.Style.tone(0.12) : button.hovered ? UI.Style.tone(0.09) : UI.Style.tone(0.06)
            border.width: button.primary ? 0 : 1
            border.color: UI.Style.tone(0.10)

            UI.FocusRing {
                visible: button.visualFocus
                targetRadius: 6
            }
        }

        contentItem: UI.Label {
            id: label
            text: button.text
            size: 13
            font.weight: button.primary ? UI.Style.weightSemiBold : UI.Style.weightRegular
            color: button.primary ? UI.Style.textOnAccent : UI.Style.ink
            horizontalAlignment: Text.AlignHCenter
            opacity: button.enabled ? 1 : 0.5
        }
    }
}
