import QtQuick
import QtQuick.Effects
import WildOS.UI as UI

import "components"

// The avatar, name, password pill and sign-in options. Keeps the contract
// LockScreenUi and Breeze's VirtualKeyboardLoader rely on: mainPasswordBox,
// visibleBoundary, passwordResult and playHighlightAnimation.
FocusScope {
    id: block

    property Item backdrop: null
    property real screenHeight: height
    property real topEdge: 0
    property bool lockScreenUiVisible: false

    property string userName: ""
    property string userImage: ""
    property string notificationMessage: ""
    // Set after a wrong password; shown with the message while it lasts.
    property string passwordHint: ""
    property bool capsLockOn: false
    // A finger is enrolled and the reader is waiting for it.
    property bool fingerprintReady: false
    property bool canSwitchUser: false
    property Item layoutSwitcher: null
    property bool showMediaControls: false
    property bool optionsOpen: false

    readonly property alias mainPasswordBox: pill.field
    readonly property var keyboardLayout: layoutSwitcher !== null ? layoutSwitcher.keyboardLayout : null
    readonly property bool hasLayouts: layoutSwitcher !== null && layoutSwitcher.hasMultipleKeyboardLayouts
    readonly property bool hasOptions: canSwitchUser || hasLayouts

    // How far down the screen the virtual keyboard must leave visible.
    readonly property real visibleBoundary: column.y + pill.y + pill.height + 12
    // Where the avatar starts, so what sits above can keep clear of it.
    readonly property real contentTop: column.y

    signal passwordResult(string password)
    signal switchUserRequested()

    function startLogin() {
        const password = pill.text;
        // Moving focus off the field first works round a Qt crash when the
        // greeter quits while a TextField has focus (QTBUG-55460).
        pill.forceActiveFocus();
        passwordResult(password);
    }

    function playHighlightAnimation() {
        messageNotice.pulse();
    }

    function shake() {
        pill.shake();
    }

    Column {
        id: column

        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(block.topEdge, Math.round(block.screenHeight * 500 / 900))
        width: 400
        spacing: 14

        UI.Avatar {
            anchors.horizontalCenter: parent.horizontalCenter
            size: 88
            ring: true
            shadow: true
            source: block.userImage
        }

        UI.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: block.userName
            size: 18
            font.weight: UI.Style.weightSemiBold
            color: UI.Style.inkOnWallpaper
            bottomPadding: 6
            layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: UI.Style.textShadowOnWallpaper
                shadowBlur: 0.5
                blurMax: 24
                shadowVerticalOffset: 1
                shadowHorizontalOffset: 0
            }
        }

        PasswordPill {
            id: pill
            anchors.horizontalCenter: parent.horizontalCenter
            backdrop: block.backdrop
            focus: true
            placeholder: "Password"
            submitLabel: "Unlock"
            enabled: !authenticator.graceLocked
            // The field is focused even while the prompt is faded out, so it
            // can wake the screen; only treat Enter as an unlock when visible.
            onSubmitted: if (block.lockScreenUiVisible) block.startLogin()

            // Every screen has its own prompt; sharing the text through this
            // singleton keeps what was typed when the pointer changes screen.
            text: PasswordSync.password

            Binding {
                target: PasswordSync
                property: "password"
                value: pill.text
            }
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            spacing: 6
            visible: messageNotice.shown || hintNotice.shown || capsNotice.shown || fingerprintNotice.shown

            Notice {
                id: messageNotice
                shown: block.notificationMessage.length > 0
                text: block.notificationMessage
            }
            Notice {
                id: hintNotice
                shown: block.notificationMessage.length > 0 && block.passwordHint.length > 0
                text: "Hint: " + block.passwordHint
                muted: true
            }
            Notice {
                id: fingerprintNotice
                shown: block.fingerprintReady && !messageNotice.shown && pill.text.length === 0
                text: "Or touch the fingerprint reader"
                muted: true
            }
            Notice {
                id: capsNotice
                shown: block.capsLockOn
                text: "Caps Lock is on"
                iconName: "caps-lock"
                muted: true
            }
        }

        UI.Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: block.hasOptions
            kind: "text"
            textColor: UI.Style.inkOnWallpaper
            text: block.optionsOpen ? "Hide sign-in options" : "Sign-in options"
            onClicked: block.optionsOpen = !block.optionsOpen
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            visible: opacity > 0
            opacity: block.optionsOpen && block.hasOptions ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: UI.Style.motionFast; easing.type: Easing.OutCubic } }

            ChoiceRow {
                visible: block.canSwitchUser
                backdrop: block.backdrop
                iconName: "account"
                label: "Switch user"
                onClicked: block.switchUserRequested()
            }

            ChoiceRow {
                visible: block.hasLayouts
                backdrop: block.backdrop
                iconName: "keyboard"
                label: "Keyboard"
                value: block.hasLayouts ? block.layoutSwitcher.layoutNames.longName : ""
                choices: {
                    const layout = block.keyboardLayout;
                    if (!block.hasLayouts || !layout || !layout.layoutsList) {
                        return [];
                    }
                    return layout.layoutsList.map((names, index) => ({
                        text: names.longName,
                        checked: index === layout.layout,
                        run: () => {
                            layout.layout = index;
                            pill.field.forceActiveFocus();
                        }
                    }));
                }
            }
        }

        Loader {
            anchors.horizontalCenter: parent.horizontalCenter
            active: block.showMediaControls
            visible: item !== null && item.visible
            source: "MediaControls.qml"
            onLoaded: item.backdrop = Qt.binding(() => block.backdrop)
        }
    }
}
