import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Window
import WildOS.AppBridge
import WildOS.UI as UI

// "Opening a Windows .exe", from WinApp.dc.html: a glass dialog that says
// what the program is and where it runs, as wild-run decided, and hands it
// to Windows mode. The window is the dialog plus a clear margin
// for its shadow; clicks in the margin pass through to the desktop. It is
// the large Wild OS dialog (UI.Dialog size "large") drawn as a window of
// its own, since nothing sits behind it to host a popup.
Window {
    id: root

    property string backdrop
    readonly property bool preview: backdrop.length > 0
    readonly property int margin: 100
    // A calling app with a Wild OS version gets the choice of WinModeCalls.dc
    // instead: 560 wide, the Wild OS version first.
    readonly property bool callsRoute: Bridge.callApp && Bridge.hasNativeVersion && Bridge.state === "ask"
    readonly property int dialogWidth: callsRoute ? 560 : 640
    property string routeError

    Connections {
        target: Bridge
        function onWindowsModeAnswered(message) {
            root.routeError = message;
        }
    }

    readonly property bool busy: Bridge.state === "installing"
    readonly property string state: Bridge.state
    // A file that is not a Windows program has no other way to run either.
    readonly property bool notWindows: root.state === "failed" && Bridge.notWindows

    width: preview ? 1440 : dialogWidth + margin * 2
    height: preview ? 900 : dialog.height + margin * 2 + 40
    visible: true
    color: preview ? UI.Style.lakeNavy : "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: Bridge.state === "done" ? qsTr("%1 is ready").arg(Bridge.appName) : qsTr("Install %1").arg(Bridge.appName)

    function updateRegion() {
        if (!preview) {
            Bridge.setDialogGeometry(root, Qt.rect(dialog.x, dialog.y, dialog.width, dialog.height), UI.Style.radiusDialog);
        }
    }

    Component.onCompleted: updateRegion()

    Shortcut {
        sequence: "Escape"
        enabled: !root.busy
        onActivated: Bridge.close()
    }

    Item {
        id: desktop
        anchors.fill: parent
        visible: root.preview

        Image {
            readonly property real fit: implicitWidth > 0 ? Math.max(parent.width / implicitWidth, parent.height / implicitHeight) : 1
            width: implicitWidth * fit
            height: implicitHeight * fit
            x: (parent.width - width) / 2
            y: (parent.height - height) * 0.45
            source: root.backdrop
        }

        Rectangle {
            anchors.fill: parent
            color: UI.Style.scrim
        }
    }

    Item {
        id: dialogMask
        anchors.fill: parent
        visible: false
        layer.enabled: true

        Rectangle {
            x: dialog.x
            y: dialog.y
            width: dialog.width
            height: dialog.height
            radius: UI.Style.radiusDialog
        }
    }

    MultiEffect {
        visible: root.preview
        anchors.fill: desktop
        source: desktop
        blurEnabled: true
        blur: 1.0
        blurMax: 64
        saturation: 0.4
        autoPaddingEnabled: false
        maskEnabled: true
        maskSource: dialogMask
    }

    Rectangle {
        id: shadowSource
        x: dialog.x
        y: dialog.y
        width: dialog.width
        height: dialog.height
        radius: UI.Style.radiusDialog
        color: UI.Style.shadowMenu
        visible: false
        layer.enabled: true
    }

    // Only the shadow is wanted, not the shape it is cast from, so the
    // dialog's own outline is cut back out of it and the translucent glass
    // is not darkened from below.
    Item {
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskInverted: true
            maskSource: dialogMask
        }

        MultiEffect {
            x: shadowSource.x
            y: shadowSource.y
            width: shadowSource.width
            height: shadowSource.height
            source: shadowSource
            shadowEnabled: true
            shadowColor: UI.Style.shadowMenu
            shadowBlur: 1.0
            blurMax: 64
            shadowVerticalOffset: 40
            autoPaddingEnabled: true
        }
    }

    UI.Glass {
        id: dialog

        level: "dialog"
        shadow: false
        x: root.preview ? 400 : root.margin
        y: root.preview ? 56 : root.margin - 20
        width: root.dialogWidth
        height: body.y + body.implicitHeight + 24 + footer.height + 2
        border.color: UI.Style.borderDialog
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        onHeightChanged: root.updateRegion()

        ColumnLayout {
            id: body
            x: 32
            y: 30
            width: parent.width - 64
            spacing: 22

            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                DragHandler {
                    target: null
                    onActiveChanged: if (active && !root.preview) root.startSystemMove()
                }

                Image {
                    Layout.preferredWidth: 60
                    Layout.preferredHeight: 60
                    source: Bridge.iconUrl.length > 0 ? Bridge.iconUrl : Qt.resolvedUrl("../images/windows-app.svg")
                    sourceSize: Qt.size(120, 120)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    UI.Label {
                        Layout.fillWidth: true
                        size: 22
                        font.weight: UI.Style.weightSemiBold
                        text: root.state === "done" ? qsTr("%1 is ready").arg(Bridge.appName)
                            : root.state === "installing" ? qsTr("Installing %1").arg(Bridge.appName)
                            : root.notWindows ? qsTr("This file can't be opened")
                            : root.state === "failed" ? qsTr("Couldn't install %1").arg(Bridge.appName)
                            : qsTr("Install %1?").arg(Bridge.appName)
                        Accessible.role: Accessible.Heading
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: root.callsRoute ? (Bridge.publisherState === "verified" ? qsTr("%1 · signed by %2").arg(Bridge.fileName).arg(Bridge.signer) : Bridge.fileName) : Bridge.fromText
                        size: root.callsRoute ? 12 : 13
                        color: root.callsRoute ? UI.Style.inkMuted : UI.Style.inkStatus
                    }
                }
            }

            UI.Label {
                Layout.fillWidth: true
                visible: root.state === "ask" && !root.callsRoute && !root.callsRoute
                text: Bridge.compat === "no" ? Bridge.compatReason
                    : Bridge.needsSetup ? qsTr("This is a Windows app. It runs in Windows mode, which needs setting up first.")
                    : qsTr("This is a Windows app. It installs in Windows mode, and then opens in its own window like any other app.")
                size: 14
                // CSS line-height 1.55 is of the font size; Qt's proportional
                // height would be of the taller line box.
                lineHeight: 14 * 1.55 * UI.Style.textScale
                lineHeightMode: Text.FixedHeight
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                color: UI.Style.inkStatus
            }

            GridLayout {
                Layout.fillWidth: true
                visible: root.state === "ask" && !root.callsRoute && !root.callsRoute
                columns: 2
                columnSpacing: 10
                rowSpacing: 10

                // How it runs comes from Wild OS's own list of Windows
                // programs (wild-run's), so the wording says that, rather
                // than claiming reports from other people.
                StatusCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    iconName: Bridge.compat === "well" ? "checkmark" : Bridge.compat === "unknown" ? "question" : Bridge.compat === "no" ? "cross" : "exclamation"
                    tone: Bridge.compat === "well" ? "good" : Bridge.compat === "unknown" ? "neutral" : Bridge.compat === "no" ? "danger" : "warning"
                    title: Bridge.compat === "well" ? qsTr("Should run well")
                         : Bridge.compat === "some" ? qsTr("May not run perfectly")
                         : Bridge.compat === "no" ? qsTr("Won't run on Wild OS")
                         : qsTr("Not tested yet")
                    description: Bridge.compat === "well" ? qsTr("Wild OS has tested this app")
                               : Bridge.compat === "some" ? qsTr("Some parts of it may not work")
                               : Bridge.compat === "no" ? qsTr("It can't run on this PC")
                               : qsTr("Wild OS hasn't tested this app yet")
                }

                StatusCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    readonly property bool verified: Bridge.publisherState === "verified"
                    iconName: verified ? "checkmark" : "exclamation"
                    tone: verified ? "good" : "warning"
                    title: verified ? qsTr("Verified publisher") : Bridge.publisherState === "checking" ? qsTr("Checking the publisher") : qsTr("Unknown publisher")
                    description: verified ? qsTr("Signed by %1").arg(Bridge.signer) : qsTr("Only install apps you trust")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.callsRoute
                spacing: 16

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 works best as a Wild OS app. Calls, screen sharing and notifications work natively, and it starts instantly without Windows mode.").arg(Bridge.appName)
                    size: 13
                    color: UI.Style.inkAlpha(0.82, 0.76)
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    lineHeight: 1.55
                }

                UI.ChoiceCard {
                    id: nativeChoice
                    Layout.fillWidth: true
                    showIndicator: true
                    checked: true
                    title: qsTr("Install %1 as a Wild OS app (recommended)").arg(Bridge.appName.replace("Microsoft ", ""))
                    description: qsTr("The full %1, with video calls, sharing your whole screen and your normal camera and microphone.").arg(Bridge.appName.replace("Microsoft ", ""))
                }

                UI.ChoiceCard {
                    id: windowsChoice
                    Layout.fillWidth: true
                    showIndicator: true
                    enabled: Bridge.windowsModeReady
                    title: qsTr("Install in Windows mode instead")
                    description: qsTr("Only if your organisation needs a %1 add-in that runs on Windows. Screen sharing can only show Windows apps.").arg(Bridge.appName.replace("Microsoft ", ""))
                        + (Bridge.windowsModeReady ? "" : " " + Bridge.windowsModeReason)
                }

                UI.Label {
                    Layout.fillWidth: true
                    visible: root.routeError.length > 0
                    text: root.routeError
                    size: 12
                    color: UI.Style.danger
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }

            // While the installer is handed to Windows mode, and what went
            // wrong if it could not be.
            UI.Card {
                Layout.fillWidth: true
                visible: root.state !== "ask"
                padding: 17

                contentItem: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            visible: !root.busy
                            implicitWidth: 30
                            implicitHeight: 30
                            radius: 15
                            color: root.state === "done" ? UI.Style.accentSelected : root.state === "disabled" ? UI.Style.tone(0.10) : Qt.alpha(UI.Style.danger, 0.18)

                            UI.Icon {
                                anchors.centerIn: parent
                                name: root.state === "done" ? "checkmark" : "exclamation"
                                size: 15
                                strokeWidth: 2.6
                                color: root.state === "done" ? UI.Style.accentText : root.state === "disabled" ? UI.Style.iconInk : UI.Style.danger
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            UI.Label {
                                Layout.fillWidth: true
                                font.weight: UI.Style.weightSemiBold
                                text: root.state === "done" ? qsTr("It's in Start")
                                    : root.state === "disabled" ? qsTr("Windows apps are turned off")
                                    : root.state === "failed" ? qsTr("That didn't work")
                                    : Bridge.progressText
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                                maximumLineCount: 3
                            }

                            UI.Label {
                                Layout.fillWidth: true
                                size: 12
                                color: UI.Style.inkMuted
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                                maximumLineCount: 3
                                text: root.state === "disabled" ? qsTr("Turn on Windows apps in Settings to install this.")
                                    : root.state === "failed" ? Bridge.message
                                    : qsTr("Its installer opens in a window of its own.")
                            }
                        }
                    }

                    UI.ProgressBar {
                        Layout.fillWidth: true
                        visible: root.busy
                        indeterminate: Bridge.progress < 0
                        value: Math.max(0, Bridge.progress) / 100
                    }

                    // An installer that added several programs to Start (an
                    // office suite, say): each can be opened from here, not
                    // only the first.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.state === "done" && Bridge.launchers.length > 1
                        spacing: 6

                        UI.Label {
                            text: qsTr("It added these to Start")
                            size: 12
                            color: UI.Style.inkMuted
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: Bridge.launchers

                                UI.Button {
                                    required property string modelData
                                    required property int index
                                    size: "small"
                                    text: modelData
                                    Accessible.name: qsTr("Open %1").arg(modelData)
                                    onClicked: Bridge.openApp(index)
                                }
                            }
                        }
                    }
                }
            }

            UI.Card {
                Layout.fillWidth: true
                visible: root.state === "ask" && !root.callsRoute && Bridge.nativeText.length > 0
                tinted: true
                leftPadding: 17
                rightPadding: 17
                topPadding: 15
                bottomPadding: 15

                contentItem: RowLayout {
                    spacing: 14

                    UI.ThemeIcon {
                        id: storeIcon
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        Layout.alignment: Qt.AlignVCenter
                        name: "org.wildos.store"
                        fallback: ""
                        visible: valid
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Prefer a native app? %1").arg(Bridge.nativeText)
                        lineHeight: 13 * 1.45 * UI.Style.textScale
                        lineHeightMode: Text.FixedHeight
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }

                    UI.LinkButton {
                        visible: Bridge.canOpenStore
                        text: qsTr("Show me")
                        onClicked: Bridge.openStore()
                    }
                }
            }
        }

        // The footer band of the large dialog, square at the top and rounded
        // with the dialog at the bottom.
        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            height: Math.max(76, Math.ceil(footerRow.implicitHeight) + 36)
            bottomLeftRadius: UI.Style.radiusDialog - 1
            bottomRightRadius: UI.Style.radiusDialog - 1
            color: UI.Style.surfaceBand

            Rectangle {
                width: parent.width
                height: 1
                color: UI.Style.borderSubtle
            }

            // Buttons at the bottom right, the main action on the right, as
            // WinApp.dc.html draws them.
            RowLayout {
                id: footerRow
                anchors.fill: parent
                anchors.leftMargin: 31
                anchors.rightMargin: 31
                spacing: 10

                Item {
                    Layout.fillWidth: true
                    implicitHeight: otherWays.implicitHeight

                    UI.LinkButton {
                        id: otherWays
                        x: -4
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !root.callsRoute && (root.state === "ask" || (root.state === "failed" && !root.notWindows))
                        text: qsTr("Other ways to run it")
                        onClicked: Bridge.openSettings()
                    }
                }

                UI.Button {
                    size: "large"
                    text: root.busy ? qsTr("Stop") : root.state === "done" ? qsTr("Done") : root.state === "failed" || (Bridge.compat === "no" && !root.callsRoute) ? qsTr("Close") : qsTr("Cancel")
                    onClicked: root.busy ? Bridge.stop() : Bridge.close()
                }

                UI.Button {
                    id: primary
                    visible: !root.busy && !(root.state === "ask" && Bridge.compat === "no" && !root.callsRoute) && !root.notWindows
                    size: "large"
                    kind: "primary"
                    text: root.state === "done" ? qsTr("Open %1").arg(Bridge.appName)
                        : root.state === "disabled" ? qsTr("Open Settings")
                        : root.state === "failed" ? qsTr("Try again")
                        : root.callsRoute ? qsTr("Install")
                        : Bridge.needsSetup ? qsTr("Set up Windows mode")
                        : qsTr("Install in Windows mode")
                    onClicked: {
                        if (root.callsRoute) {
                            if (nativeChoice.checked) {
                                Bridge.installNative();
                            } else {
                                root.routeError = Bridge.installInWindowsMode();
                            }
                        } else if (root.state === "done") {
                            Bridge.openApp(0);
                        } else if (root.state === "disabled") {
                            Bridge.openSettings();
                            Bridge.close();
                        } else {
                            Bridge.install();
                        }
                    }
                }
            }
        }
    }
}
