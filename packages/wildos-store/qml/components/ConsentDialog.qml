import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.Store
import WildOS.UI as UI

// "Before OBS Studio installs", StoreConsent.dc.html: what a Flathub app
// asks to reach, each with a switch, before anything downloads. Install
// sets the choices and starts the install; Cancel leaves the app as it was.
T.Popup {
    id: dialog

    property var app: null
    // The items as they were when the dialog opened, and the switches'
    // positions by token.
    property var items: []
    property var choices: ({})

    function ask(target) {
        app = target;
        items = target.consentItems;
        const c = {};
        for (const item of items) {
            c[item.token] = item.required || item.allowed;
        }
        choices = c;
        open();
    }

    function install() {
        const target = app;
        const picked = choices;
        close();
        target.installWith(picked);
    }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(448, parent ? parent.width - 48 : 448)
    height: Math.min(implicitHeight, parent ? parent.height - 48 : implicitHeight)
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    modal: true
    focus: true
    topPadding: 28
    leftPadding: 28
    rightPadding: 28
    bottomPadding: 22
    closePolicy: T.Popup.CloseOnEscape

    T.Overlay.modal: Rectangle {
        radius: Window.window && Window.window.visibility !== Window.Maximized && Window.window.visibility !== Window.FullScreen ? UI.Style.windowRadius : 0
        color: UI.Style.dark ? Qt.rgba(7 / 255, 9 / 255, 13 / 255, 0.55) : UI.Style.scrim
        Behavior on opacity {
            NumberAnimation { duration: UI.Style.motionFast }
        }
    }

    background: Rectangle {
        radius: UI.Style.radiusDialog
        color: Metrics.dialog
        border.width: 1
        border.color: UI.Style.borderDialog
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, UI.Style.dark ? 0.6 : 0.2)
            shadowVerticalOffset: 40
            shadowBlur: 1.0
            blurMax: 64
        }
    }

    contentItem: Flickable {
        Accessible.role: Accessible.Dialog
        Accessible.name: dialog.app ? qsTr("Before %1 installs").arg(dialog.app.name) : ""
        contentHeight: content.implicitHeight
        clip: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height

        ColumnLayout {
            id: content
            width: dialog.availableWidth
            spacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12

                AppIcon {
                    Layout.alignment: Qt.AlignHCenter
                    size: 72
                    source: dialog.app ? dialog.app.iconSource : ""
                }

                UI.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: dialog.app ? qsTr("Before %1 installs").arg(dialog.app.name) : ""
                    size: 20
                    font.weight: UI.Style.weightBold
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    Accessible.role: Accessible.Heading
                }

                UI.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Choose what it can reach. Anything you leave off, it simply can't see. You can change this later in Settings.")
                    size: 13.5
                    lineHeight: 1.5
                    color: Metrics.inkSecondary
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            Column {
                Layout.fillWidth: true
                Layout.topMargin: 12

                Repeater {
                    model: dialog.items

                    Item {
                        id: choice
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: Math.max(34, texts.implicitHeight) + 24

                        Rectangle {
                            visible: choice.index > 0
                            width: parent.width
                            height: 1
                            color: Metrics.divider
                        }

                        Rectangle {
                            id: well
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 34
                            radius: 9
                            color: Metrics.iconWell

                            StoreIcon {
                                anchors.centerIn: parent
                                name: Metrics.reachIcons[choice.modelData.icon] || "shield"
                                size: 18
                                color: UI.Style.iconInk
                            }
                        }

                        Column {
                            id: texts
                            anchors.left: well.right
                            anchors.leftMargin: 13
                            anchors.right: trailing.left
                            anchors.rightMargin: 13
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            UI.Label {
                                width: parent.width
                                text: choice.modelData.label
                                size: 14
                                font.weight: UI.Style.weightSemiBold
                            }

                            UI.Label {
                                width: parent.width
                                visible: text.length > 0
                                text: choice.modelData.detail || ""
                                size: 12.5
                                color: Metrics.inkSecondary
                                wrapMode: Text.Wrap
                                elide: Text.ElideNone
                            }
                        }

                        Item {
                            id: trailing
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: choice.modelData.required ? requiredLabel.implicitWidth : toggle.implicitWidth
                            height: 24

                            UI.Label {
                                id: requiredLabel
                                visible: choice.modelData.required
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Required")
                                size: 12
                                color: Metrics.inkTertiary
                            }

                            UI.Toggle {
                                id: toggle
                                visible: !choice.modelData.required
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: dialog.choices[choice.modelData.token] === true
                                Accessible.name: choice.modelData.label
                                onToggled: {
                                    const c = Object.assign({}, dialog.choices);
                                    c[choice.modelData.token] = checked;
                                    dialog.choices = c;
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                spacing: 10

                component DialogButton: T.Button {
                    id: button
                    property bool primary: false
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: Math.max(40, Math.ceil(buttonLabel.implicitHeight) + 16)
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: text

                    background: Rectangle {
                        radius: 10
                        color: button.primary ? (button.down ? UI.Style.accentPressed : button.hovered ? UI.Style.accentHover : UI.Style.accent)
                            : button.down ? Metrics.buttonPressed : button.hovered ? Metrics.buttonHover : UI.Style.tone(0.09)
                        Behavior on color {
                            ColorAnimation { duration: UI.Style.hoverDuration }
                        }

                        UI.FocusRing {
                            visible: button.visualFocus
                            targetRadius: 10
                        }
                    }

                    contentItem: UI.Label {
                        id: buttonLabel
                        horizontalAlignment: Text.AlignHCenter
                        text: button.text
                        size: 14
                        font.weight: button.primary ? UI.Style.weightBold : UI.Style.weightSemiBold
                        color: button.primary ? UI.Style.textOnAccent : UI.Style.ink
                    }
                }

                DialogButton {
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }

                DialogButton {
                    primary: true
                    text: qsTr("Install")
                    focus: true
                    onClicked: dialog.install()
                }
            }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: UI.Style.motionFast; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: UI.Style.scaleEnter; to: 1; duration: UI.Style.motionNormal; easing.type: UI.Style.easeEnter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: UI.Style.motionFast; easing.type: UI.Style.easeExit }
    }
}
