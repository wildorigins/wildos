pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.plasma.workspace.dbus as DBus
import WildOS.UI as UI

// Board 9.13: while Windows mode sets itself up with Office, Start shows the
// Office apps it is bringing, dimmed and "Getting ready", under a button
// with setup's progress that opens the Windows mode panel on the taskbar.
// Asked of Windows mode's agent over D-Bus when Start opens, and only when
// the agent is already running, so a PC without Windows mode never starts
// it from here.
Column {
    id: section

    required property var launcher

    // The agent's Status, or null.
    property var status: null
    readonly property bool settingUp: status !== null && status.state === "setup"
    readonly property int percent: settingUp ? Number(status.percent) || 0 : 0

    visible: settingUp && status.options && status.options.office === true
    spacing: 8

    readonly property var apps: [
        { name: "Word", icon: "application-vnd.openxmlformats-officedocument.wordprocessingml.document" },
        { name: "Excel", icon: "application-vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
        { name: "PowerPoint", icon: "application-vnd.openxmlformats-officedocument.presentationml.presentation" },
        { name: "Outlook", icon: "internet-mail" }
    ]

    DBus.DBusServiceWatcher {
        id: agent
        busType: DBus.BusType.Session
        watchedService: "org.wildos.WindowsMode1"
    }

    function call(member: string): var {
        return DBus.SessionBus.asyncCall({
            service: "org.wildos.WindowsMode1",
            path: "/org/wildos/WindowsMode1",
            iface: "org.wildos.WindowsMode1",
            member: member
        });
    }

    function read(): void {
        if (!agent.registered) {
            status = null;
            return;
        }
        const reply = call("Status");
        reply.finished.connect(() => {
            try {
                section.status = reply.isError ? null : JSON.parse(String(reply.value));
            } catch (e) {
                section.status = null;
            }
        });
    }

    Connections {
        target: section.launcher
        function onExpandedChanged() {
            if (section.launcher.expanded) {
                section.read();
            }
        }
    }

    Timer {
        running: section.launcher.expanded && section.settingUp
        repeat: true
        interval: 5000
        onTriggered: section.read()
    }

    Item {
        width: parent.width
        implicitHeight: 28

        UI.Label {
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            size: 14
            font.weight: UI.Style.weightSemiBold
            text: "Microsoft Office"
            Accessible.role: Accessible.Heading
        }

        WButton {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: progress.implicitWidth + 20
            height: 28
            radius: 7
            baseColor: UI.Style.tone(0.07)
            accessibleName: qsTr("Windows mode: %1").arg(progressText.text)
            onClicked: {
                section.call("ShowSetupProgress");
                section.launcher.close();
            }

            Row {
                id: progress
                anchors.centerIn: parent
                spacing: 8

                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    height: 12

                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: UI.Style.tone(0.22)
                            strokeWidth: 2
                            PathAngleArc { centerX: 6; centerY: 6; radiusX: 5; radiusY: 5; startAngle: 0; sweepAngle: 360 }
                        }

                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: UI.Style.accent
                            strokeWidth: 2
                            capStyle: ShapePath.RoundCap
                            PathAngleArc { centerX: 6; centerY: 6; radiusX: 5; radiusY: 5; startAngle: -90; sweepAngle: 3.6 * section.percent }
                        }
                    }
                }

                UI.Label {
                    id: progressText
                    anchors.verticalCenter: parent.verticalCenter
                    size: 12
                    text: (section.status && section.status.paused ? qsTr("Setup paused · %1%") : qsTr("Setting up · %1%")).arg(section.percent)
                }
            }
        }
    }

    GridLayout {
        width: parent.width
        columns: 2
        rowSpacing: 4
        columnSpacing: 12

        Repeater {
            model: section.apps

            RowLayout {
                id: app
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: 56
                spacing: 14
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("%1, getting ready").arg(modelData.name)

                Kirigami.Icon {
                    Layout.leftMargin: 12
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    source: app.modelData.icon
                    fallback: "wildos-windows-app"
                    opacity: 0.45
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    UI.Label {
                        size: 13
                        text: app.modelData.name
                        color: UI.Style.inkAlpha(0.62, 0.62)
                    }

                    UI.Label {
                        size: 12
                        text: qsTr("Getting ready")
                        color: UI.Style.inkAlpha(0.5, 0.5)
                    }
                }
            }
        }
    }

    // The board's 18px under the apps, less the column's own 8.
    Item {
        width: 1
        height: 10
    }
}
