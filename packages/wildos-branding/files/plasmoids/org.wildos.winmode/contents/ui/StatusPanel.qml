pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import WildOS.UI as UI

// The panel from boards 4.14 and WinModeStatusPanel. While Windows runs: the
// edition, a Running pill, processor use over the last minute, memory against
// the limit set for Windows, what is open, and Pause, Save to disk, Restart
// and Shut down along a darker foot. Paused, saved or off it is narrower: how
// long, the memory held or what coming back costs, and Resume or Start
// first. It only offers what the person can act on, so Save to disk waits
// until the service can save. Plasma's theme draws the glass; sizes are the
// board's outer edge with the theme's frame margins taken off, as in the
// security flyout.
FocusScope {
    id: panel

    required property var winmode

    KSvg.FrameSvgItem {
        id: frame
        imagePath: "dialogs/background"
        visible: false
    }

    readonly property real edgeLeft: frame.margins.left
    readonly property real edgeRight: frame.margins.right
    readonly property real edgeTop: frame.margins.top
    readonly property real edgeBottom: frame.margins.bottom

    readonly property string level: winmode.baseLevel
    readonly property bool running: level === "running"
    readonly property bool vmRunning: winmode.system !== null && winmode.system.vm === "running"

    readonly property int glassBorder: 1
    readonly property int pad: 18
    readonly property int frameRadius: 22
    readonly property int outerWidth: running ? 360 : 330
    readonly property real innerWidth: outerWidth - 2 * (glassBorder + pad)

    readonly property real footHeight: foot.implicitHeight + 26
    readonly property real glassHeight: 2 * glassBorder + pad + body.implicitHeight + 14 + footHeight

    readonly property real contentWidth: outerWidth - edgeLeft - edgeRight
    readonly property real contentHeight: glassHeight - edgeTop - edgeBottom

    Layout.minimumWidth: contentWidth
    Layout.preferredWidth: contentWidth
    Layout.maximumWidth: contentWidth
    Layout.minimumHeight: contentHeight
    Layout.preferredHeight: contentHeight
    Layout.maximumHeight: contentHeight

    focus: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            panel.winmode.expanded = false;
            event.accepted = true;
        }
    }

    Connections {
        target: panel.winmode
        function onExpandedChanged() {
            if (panel.winmode.expanded) {
                panel.forceActiveFocus();
            }
        }
    }

    component Caption: UI.Label {
        size: 12
        color: UI.Style.inkMuted
    }

    component Figure: UI.Label {
        size: 13
        font.weight: UI.Style.weightSemiBold
        font.features: ({ "tnum": 1 })
    }

    component Note: UI.Label {
        Layout.fillWidth: true
        size: 11
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    component Action: UI.Button {
        Layout.fillWidth: true
        Layout.preferredHeight: 34
    }

    // "Windows mode settings" and a chevron across the panel's width. Inline
    // components can't reach the panel's ids, so each use says what a click
    // does.
    component SettingsLink: T.AbstractButton {
        id: link
        Layout.fillWidth: true
        implicitHeight: 20
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        text: qsTr("Windows mode settings")
        Accessible.role: Accessible.Link
        Accessible.name: text

        background: Item {
            UI.FocusRing {
                visible: link.visualFocus
                targetRadius: UI.Style.radiusXs
            }
        }

        contentItem: RowLayout {
            spacing: 8

            UI.Label {
                Layout.fillWidth: true
                text: link.text
                size: 12
                color: UI.Style.accentText
                font.underline: link.hovered
                opacity: link.down ? 0.75 : 1
            }

            UI.Glyph {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                path: "M9 6l6 6-6 6"
                color: UI.Style.accentText
                strokeWidth: 2
            }
        }
    }

    ColumnLayout {
        id: body
        x: panel.glassBorder + panel.pad - panel.edgeLeft
        y: panel.glassBorder + panel.pad - panel.edgeTop
        width: panel.innerWidth
        spacing: panel.running ? 16 : 14

        // The mark in an accent tile, "Windows mode" and what it is doing,
        // and the state in a pill.
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                radius: 10
                color: UI.Style.accentChip

                UI.Icon {
                    anchors.centerIn: parent
                    name: "windowsapps"
                    size: 20
                    strokeWidth: 1.8
                    color: UI.Style.accentText
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Windows mode")
                    size: 15
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }

                Caption {
                    Layout.fillWidth: true
                    text: panel.winmode.subtitle()
                }
            }

            Rectangle {
                Layout.preferredHeight: 22
                Layout.preferredWidth: pill.implicitWidth + 18
                radius: 11
                color: Tones.pillFill(panel.level)

                RowLayout {
                    id: pill
                    anchors.centerIn: parent
                    spacing: 6

                    StatusDot {
                        level: panel.level
                    }

                    UI.Label {
                        text: panel.winmode.pillFor(panel.level)
                        size: 11
                        font.weight: UI.Style.weightSemiBold
                        color: Tones.pillText(panel.level)
                    }
                }
            }
        }

        ColumnLayout {
            visible: panel.running && panel.winmode.qemu !== null
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true

                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Processor, last minute")
                }

                Figure {
                    text: qsTr("%1%").arg(panel.winmode.processorPercent)
                }
            }

            Sparkline {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                points: panel.winmode.processorTrace
            }
        }

        ColumnLayout {
            visible: (panel.running || panel.level === "paused") && panel.winmode.qemu !== null
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true

                Caption {
                    Layout.fillWidth: true
                    text: panel.running ? qsTr("Memory") : qsTr("Memory held")
                }

                Figure {
                    text: panel.running && panel.winmode.memoryLimit > 0
                        ? qsTr("%1 of %2").arg(panel.winmode.bytesText(panel.winmode.memoryHeld)).arg(panel.winmode.bytesText(panel.winmode.memoryLimit))
                        : panel.winmode.bytesText(panel.winmode.memoryHeld)
                }
            }

            Meter {
                Layout.fillWidth: true
                value: panel.winmode.memoryLimit > 0 ? panel.winmode.memoryHeld / panel.winmode.memoryLimit : 0
                held: !panel.running
                Accessible.name: panel.running ? qsTr("Memory") : qsTr("Memory held")
            }

            Note {
                text: panel.running ? qsTr("Unused memory goes back to Wild OS") : qsTr("No processor use. Ready in about a second.")
            }
        }

        // Saved or off: nothing held, so no meter, only what it costs to
        // come back.
        UI.Label {
            visible: panel.level === "saved" || panel.level === "off"
            Layout.fillWidth: true
            size: 12
            color: UI.Style.inkMuted
            lineHeight: 1.5
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            text: panel.winmode.restingText()
        }

        UI.Label {
            visible: panel.level === "starting" || panel.level === "updating" || panel.level === "attention"
            Layout.fillWidth: true
            size: 13
            lineHeight: 1.5
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            color: panel.level === "attention" ? UI.Style.ink : UI.Style.inkMuted
            text: panel.level === "starting" ? qsTr("Getting Windows ready. Your app opens as soon as it is.")
                : panel.level === "updating" ? qsTr("Installing Windows and Office updates that Wild OS has tested. Your apps open as normal once it's done.")
                : panel.winmode.attentionText()
        }

        ColumnLayout {
            visible: panel.running && panel.winmode.openApps.length > 0
            Layout.fillWidth: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                implicitHeight: 1
                color: UI.Style.tone(0.08)
            }

            Caption {
                Layout.bottomMargin: 2
                text: qsTr("Open now")
            }

            Repeater {
                model: panel.winmode.openApps

                RowLayout {
                    id: app
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        radius: 8
                        color: UI.Style.tone(0.08)

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            source: app.modelData.icon || "wildos-windows-app"
                            fallback: "wildos-windows-app"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        UI.Label {
                            Layout.fillWidth: true
                            text: app.modelData.name
                            size: 13
                        }

                        UI.Label {
                            visible: text.length > 0
                            Layout.fillWidth: true
                            text: app.modelData.title || ""
                            size: 11
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }
        }

        UI.Label {
            readonly property string idle: panel.running ? panel.winmode.idleText() : ""
            visible: idle.length > 0
            Layout.fillWidth: true
            size: 12
            color: UI.Style.inkMuted
            lineHeight: 1.5
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            text: idle
        }
    }

    // The darker foot, edge to edge inside the glass border, as on quick
    // settings: what can be done with Windows now, and the way to Windows
    // mode's settings. Running: Pause, Save to disk, Restart, Shut down.
    // Paused: Resume first, filled. Saved: Resume across the top, as the
    // apps it was saved with come back with it. Off: Start Windows mode.
    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: panel.glassBorder - panel.edgeLeft
        anchors.right: parent.right
        anchors.rightMargin: panel.glassBorder - panel.edgeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: panel.glassBorder - panel.edgeBottom
        height: panel.footHeight
        color: UI.Style.surfaceBand
        bottomLeftRadius: panel.frameRadius - panel.glassBorder
        bottomRightRadius: panel.frameRadius - panel.glassBorder

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: UI.Style.tone(0.07)
        }

        ColumnLayout {
            id: foot
            x: panel.pad
            y: 12
            width: panel.innerWidth
            spacing: 10

            GridLayout {
                id: controls
                readonly property bool live: panel.vmRunning && (panel.running || panel.level === "attention")
                readonly property bool paused: panel.level === "paused"
                readonly property bool saved: panel.level === "saved"
                visible: live || paused || saved
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 8
                columnSpacing: 8
                enabled: !panel.winmode.busyAction

                Action {
                    visible: controls.live
                    text: qsTr("Pause")
                    onClicked: panel.winmode.power("pause")
                }

                Action {
                    visible: controls.paused || controls.saved
                    Layout.columnSpan: controls.saved ? 2 : 1
                    kind: "primary"
                    text: qsTr("Resume")
                    onClicked: panel.winmode.power("resume")
                }

                Action {
                    visible: (controls.live || controls.paused) && panel.winmode.canSave
                    text: qsTr("Save to disk")
                    onClicked: panel.winmode.power("save")
                }

                Action {
                    text: qsTr("Restart")
                    onClicked: panel.winmode.power("restart")
                }

                Action {
                    text: qsTr("Shut down")
                    onClicked: panel.winmode.power("shutdown")
                }
            }

            Action {
                visible: panel.level === "off"
                enabled: !panel.winmode.busyAction
                kind: "primary"
                text: qsTr("Start Windows mode")
                onClicked: panel.winmode.power("start")
            }

            SettingsLink {
                onClicked: panel.winmode.openSettings()
            }
        }
    }
}
