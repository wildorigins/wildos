pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ksvg as KSvg
import WildOS.UI as UI

// The panel while Windows mode sets itself up (board 9.12): the same glass
// as the other Windows mode panels, 380 wide. The overall percentage and
// time left, the steps of the setup window (9.5) with the one under way
// filling a ring, and Pause and Open full window. Everything comes from the
// system service's setup record; closing the panel changes nothing.
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

    readonly property var record: winmode.setupRecord || ({})
    readonly property bool paused: record.paused === true
    readonly property var problem: record.problem || null

    readonly property int glassBorder: 1
    readonly property int pad: 18
    readonly property int outerWidth: 380
    readonly property real innerWidth: outerWidth - 2 * (glassBorder + pad)

    readonly property real footHeight: foot.implicitHeight + 12 + 16
    readonly property real glassHeight: 2 * glassBorder + pad + body.implicitHeight + 6 + 10 + footHeight

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

    // The panel is narrower than the setup window, so its steps use board
    // 9.12's shorter words for the service's usual ones. Its other fixed
    // lines are translated in wildos-winmode's list of them (ServiceText.js);
    // a line with numbers in it (how much has downloaded) stays as it is.
    readonly property var shorter: ({
        "Download Windows 11 from Microsoft": qsTr("Download Windows 11"),
        "About 6 GB, checked against Microsoft's signature": qsTr("From Microsoft, checked against its signature"),
        "Automatic, no questions to answer": qsTr("Automatic, nothing to answer"),
        "Removes adverts, tracking and apps you won't use": qsTr("Removes adverts, tracking and extras"),
        "Install Microsoft 365": qsTr("Install Microsoft Office"),
        "Word, Excel, PowerPoint, Outlook, OneNote": qsTr("Word, Excel, PowerPoint and Outlook")
    })

    function words(text: string): string {
        return shorter[text] || qsTranslate("WinmodeService", text);
    }

    ColumnLayout {
        id: body
        x: panel.glassBorder + panel.pad - panel.edgeLeft
        y: panel.glassBorder + panel.pad - panel.edgeTop
        width: panel.innerWidth
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                radius: 10
                color: UI.Style.tone(0.061)

                UI.Icon {
                    anchors.centerIn: parent
                    name: "windowsapps"
                    size: 20
                    strokeWidth: 1.8
                    color: UI.Style.ink
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("Setting up Windows mode")
                    size: 15
                    font.weight: UI.Style.weightSemiBold
                    Accessible.role: Accessible.Heading
                }

                UI.Label {
                    Layout.fillWidth: true
                    size: 12
                    color: panel.problem ? Tones.dot("attention") : UI.Style.inkMuted
                    text: panel.problem ? qsTr("Needs attention") : panel.paused ? qsTr("Paused") : panel.words(panel.record.left || "")
                }
            }

            UI.Label {
                text: qsTr("%1%").arg(panel.winmode.setupPercent)
                size: 20
                font.weight: UI.Style.weightSemiBold
                font.features: ({ "tnum": 1 })
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 6
            radius: 3
            color: UI.Style.tone(0.12)
            Accessible.role: Accessible.ProgressBar
            Accessible.name: qsTr("Windows mode setup")

            Rectangle {
                width: Math.round(parent.width * panel.winmode.setupPercent / 100)
                height: parent.height
                radius: 3
                color: UI.Style.accent
                visible: width > 0
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                implicitHeight: 1
                color: UI.Style.tone(0.08)
            }

            Repeater {
                model: Array.isArray(panel.record.stages) ? panel.record.stages : []

                RowLayout {
                    id: step

                    required property var modelData
                    readonly property string state: modelData.state || "pending"

                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    spacing: 12
                    Accessible.role: Accessible.ListItem
                    Accessible.name: state === "done" ? qsTr("%1, done").arg(panel.words(modelData.title)) : state === "active" ? qsTr("%1, %2%").arg(panel.words(modelData.title)).arg(modelData.percent) : qsTr("%1, not started").arg(panel.words(modelData.title))

                    Item {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20

                        Rectangle {
                            anchors.fill: parent
                            visible: step.state === "done" || step.state === "failed"
                            radius: 10
                            color: step.state === "done" ? Qt.rgba(UI.Style.success.r, UI.Style.success.g, UI.Style.success.b, 0.18) : UI.Style.warningSoft

                            UI.Icon {
                                anchors.centerIn: parent
                                name: step.state === "done" ? "checkmark" : "exclamation"
                                size: 12
                                strokeWidth: 3
                                color: step.state === "done" ? UI.Style.success : UI.Style.warning
                            }
                        }

                        ProgressRing {
                            anchors.centerIn: parent
                            visible: step.state === "active"
                            size: 20
                            thickness: 2.4
                            value: (Number(step.modelData.percent) || 0) / 100
                        }

                        Rectangle {
                            anchors.fill: parent
                            visible: step.state === "pending"
                            radius: 10
                            color: "transparent"
                            border.width: 1.5
                            border.color: UI.Style.tone(0.35)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        UI.Label {
                            Layout.fillWidth: true
                            text: panel.words(step.modelData.title)
                            size: 13
                            font.weight: step.state === "active" ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }

                        UI.Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: panel.words(step.modelData.detail || "")
                            size: 11
                            color: UI.Style.inkMuted
                        }
                    }

                    UI.Label {
                        text: step.state === "done" ? qsTr("Done") : step.state === "active" ? qsTr("%1%").arg(Number(step.modelData.percent) || 0) : ""
                        size: 12
                        color: UI.Style.inkMuted
                        font.features: ({ "tnum": 1 })
                    }
                }
            }
        }

        UI.Label {
            Layout.fillWidth: true
            size: 12
            lineHeightMode: Text.FixedHeight
            lineHeight: Math.round(UI.Style.px(12) * 1.5)
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            color: panel.problem ? UI.Style.ink : UI.Style.inkMuted
            text: panel.problem ? panel.winmode.attentionText()
                : qsTr("Uses spare capacity, so your PC stays quick. If it restarts or sleeps, setup carries on where it left off.")
        }
    }

    // The foot, edge to edge inside the glass border, with a hairline above:
    // Pause (or Resume) at the left, Open full window at the right.
    Item {
        anchors.left: parent.left
        anchors.leftMargin: panel.glassBorder - panel.edgeLeft
        anchors.right: parent.right
        anchors.rightMargin: panel.glassBorder - panel.edgeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: panel.glassBorder - panel.edgeBottom
        height: panel.footHeight

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: UI.Style.tone(0.08)
        }

        RowLayout {
            id: foot
            x: panel.pad
            y: 12
            width: panel.innerWidth
            spacing: 8

            UI.Button {
                Layout.preferredHeight: 32
                visible: !panel.problem
                text: panel.paused ? qsTr("Resume") : qsTr("Pause")
                onClicked: panel.winmode.setupControl(panel.paused ? "resume" : "pause")
            }

            Item {
                Layout.fillWidth: true
            }

            UI.Button {
                Layout.preferredHeight: 32
                kind: panel.problem ? "primary" : "secondary"
                text: qsTr("Open full window")
                onClicked: panel.winmode.setupControl("window")
            }
        }
    }
}
