pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ksvg as KSvg
import WildOS.UI as UI

// The updates flyout, built as the security flyout is: Plasma's theme draws
// the glass, and sizes come from the design's outer edge with the theme's
// frame margins taken off. The status banner is the Updates page's status
// card scaled to a flyout; the rows are Settings' grouped list.
FocusScope {
    id: flyout

    required property var updates

    KSvg.FrameSvgItem {
        id: frame
        imagePath: "dialogs/background"
        visible: false
    }

    readonly property real edgeLeft: frame.margins.left
    readonly property real edgeRight: frame.margins.right
    readonly property real edgeTop: frame.margins.top
    readonly property real edgeBottom: frame.margins.bottom

    readonly property real innerWidth: Metrics.flyoutWidth - 2 * (Metrics.glassBorder + Metrics.pad)
    readonly property real bodyHeight: summary.implicitHeight
    readonly property real glassHeight: 2 * Metrics.glassBorder + Metrics.pad + bodyHeight + Metrics.sectionGap + Metrics.footerHeight

    readonly property real contentWidth: Metrics.flyoutWidth - edgeLeft - edgeRight
    readonly property real contentHeight: glassHeight - edgeTop - edgeBottom

    Layout.minimumWidth: contentWidth
    Layout.preferredWidth: contentWidth
    Layout.maximumWidth: contentWidth
    Layout.minimumHeight: contentHeight
    Layout.preferredHeight: contentHeight
    Layout.maximumHeight: contentHeight

    focus: true

    // Sand for what needs doing: a failed check, or security fixes waiting.
    readonly property bool attention: updates.failed || (updates.ready && updates.securityCount > 0)

    Connections {
        target: flyout.updates
        function onExpandedChanged() {
            if (flyout.updates.expanded) {
                flyout.forceActiveFocus();
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            updates.expanded = false;
            event.accepted = true;
        }
    }

    Item {
        id: body
        x: Metrics.glassBorder + Metrics.pad - flyout.edgeLeft
        y: Metrics.glassBorder + Metrics.pad - flyout.edgeTop
        width: flyout.innerWidth
        height: flyout.bodyHeight

        ColumnLayout {
            id: summary
            width: parent.width
            spacing: Metrics.sectionGap

            // The status banner: the icon in a filled circle, the answer in
            // the design's 18 px title and one line of why underneath.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Math.max(44, bannerText.implicitHeight) + 32
                radius: UI.Style.radiusLg
                color: flyout.attention ? UI.Style.warningSoft : UI.Style.accentSoft
                border.width: 1
                border.color: flyout.attention ? Qt.rgba(UI.Style.warning.r, UI.Style.warning.g, UI.Style.warning.b, 0.28) : UI.Style.accentBorder

                Rectangle {
                    id: badge
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44
                    height: 44
                    radius: 22
                    color: flyout.attention ? UI.Style.warning : UI.Style.accent

                    readonly property bool done: flyout.updates.phase === "current" && !flyout.updates.restartNeeded

                    UI.Icon {
                        visible: flyout.updates.failed || badge.done
                        anchors.centerIn: parent
                        name: flyout.updates.failed ? "warning" : "check"
                        size: 24
                        strokeWidth: badge.done ? 2.6 : 2.2
                        color: UI.Style.textOnAccent
                    }

                    // The taskbar's two-arrow icon.
                    UI.Glyph {
                        id: badgeIcon
                        visible: !flyout.updates.failed && !badge.done
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        path: "M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8M21 3v5h-5M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16M8 16H3v5"
                        strokeWidth: 2.2
                        color: UI.Style.textOnAccent

                        RotationAnimator {
                            target: badgeIcon
                            running: flyout.updates.busy && (flyout.updates.phase !== "installing" || flyout.updates.working)
                            from: 0
                            to: 360
                            duration: 1200
                            loops: Animation.Infinite
                            onRunningChanged: if (!running) badgeIcon.rotation = 0
                        }
                    }
                }

                ColumnLayout {
                    id: bannerText
                    anchors.left: badge.right
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    UI.Label {
                        Layout.fillWidth: true
                        text: flyout.updates.title
                        size: 18
                        font.weight: UI.Style.weightSemiBold
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Accessible.role: Accessible.Heading
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        // The last check on its own line, as on the security
                        // flyout, so the time never wraps alone. Not while
                        // checking, when it would read as the check now running,
                        // nor after a failed one, which it would seem to contradict.
                        text: {
                            const u = flyout.updates;
                            const when = u.loaded && ["na", "checking", "error"].indexOf(u.phase) < 0 && u.number(u.status.checked) > 0
                                ? u.checkedText + "." : "";
                            return [u.detail, when].filter(t => t.length > 0).join("\n");
                        }
                        color: UI.Style.inkStatus
                        lineHeight: 1.3
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                }
            }

            // Settings' grouped list: what is waiting, with Check now and
            // Install, then whether security fixes install by themselves. A
            // click anywhere else on a row opens Settings > Updates.
            UI.Card {
                visible: flyout.updates.loaded && flyout.updates.phase !== "na"
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.ListRow {
                        iconName: "refresh"
                        title: flyout.updates.ready ? flyout.updates.plural(flyout.updates.count, "update", "updates") + " ready"
                            : flyout.updates.phase === "checking" ? "Checking now"
                            : flyout.updates.phase === "installing" ? "Installing now"
                            : "System updates"
                        interactive: true
                        onClicked: flyout.updates.openUpdates()

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            UI.Button {
                                visible: flyout.updates.phase !== "installing"
                                size: "small"
                                kind: flyout.updates.ready ? "text" : "secondary"
                                text: flyout.updates.phase === "checking" ? "Checking" : flyout.updates.failed ? "Try again" : "Check now"
                                busy: flyout.updates.phase === "checking"
                                enabled: flyout.updates.canCheck
                                focusPolicy: Qt.NoFocus
                                onClicked: flyout.updates.checkNow()
                            }

                            UI.Button {
                                visible: flyout.updates.ready
                                size: "small"
                                text: "Install"
                                focusPolicy: Qt.NoFocus
                                onClicked: flyout.updates.install()
                            }
                        }
                    }

                    UI.ListRow {
                        visible: flyout.updates.status.auto !== undefined
                        iconName: "shield"
                        title: "Automatic security updates"
                        description: flyout.updates.status.auto === "on"
                            ? "Security fixes install by themselves" : "Off, security fixes wait for you"
                        chevron: true
                        onClicked: flyout.updates.openUpdates()
                    }
                }
            }
        }
    }

    // The darker band along the foot, edge to edge inside the glass border,
    // as on quick settings.
    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: Metrics.glassBorder - flyout.edgeLeft
        anchors.right: parent.right
        anchors.rightMargin: Metrics.glassBorder - flyout.edgeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.glassBorder - flyout.edgeBottom
        height: Metrics.footerHeight
        color: UI.Style.surfaceBand
        bottomLeftRadius: Metrics.frameRadius - Metrics.glassBorder
        bottomRightRadius: Metrics.frameRadius - Metrics.glassBorder

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: UI.Style.tone(0.08)
        }

        UI.LinkButton {
            // The link's text lines up with the content edge above it.
            x: Metrics.pad - 4
            anchors.verticalCenter: parent.verticalCenter
            text: "Open Updates settings"
            onClicked: flyout.updates.openUpdates()
        }
    }
}
