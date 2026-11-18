pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.ksvg as KSvg
import WildOS.UI as UI

// The security flyout, built like quick settings: Plasma's theme draws the
// glass, and sizes come from the design's outer edge with the theme's frame
// margins taken off. The status banner is the Privacy and security page's,
// scaled to a flyout; the rows are Settings' grouped list.
FocusScope {
    id: flyout

    required property var security

    // "" is the summary; "usb" lists the blocked devices.
    property string page: ""

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
    readonly property real bodyHeight: page === "usb" ? usbPage.implicitHeight : summary.implicitHeight
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

    readonly property bool attention: security.level === "attention"

    function open(what: string): void {
        if (what === "page:usb") {
            page = "usb";
            if (Nav.keyboard) {
                Qt.callLater(() => usbPage.focusFirst());
            }
        } else {
            security.act(what);
        }
    }

    function showSummary(): void {
        page = "";
    }

    Connections {
        target: flyout.security
        function onExpandedChanged() {
            if (flyout.security.expanded) {
                flyout.page = "";
                Nav.keyboard = false;
                flyout.forceActiveFocus();
            }
        }
        // Nothing left to review: back to the summary rather than an empty page.
        function onPendingDevicesChanged() {
            if (flyout.page === "usb" && flyout.security.pendingDevices.length === 0) {
                flyout.page = "";
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            if (page !== "") {
                showSummary();
            } else {
                security.expanded = false;
            }
            event.accepted = true;
            return;
        }
        if ([Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0) {
            Nav.keyboard = true;
        }
        event.accepted = false;
    }

    HoverHandler {
        onPointChanged: Nav.keyboard = false
    }

    Item {
        id: body
        x: Metrics.glassBorder + Metrics.pad - flyout.edgeLeft
        y: Metrics.glassBorder + Metrics.pad - flyout.edgeTop
        width: flyout.innerWidth
        height: flyout.bodyHeight

        ColumnLayout {
            id: summary
            visible: flyout.page === ""
            width: parent.width
            spacing: Metrics.sectionGap

            // The status banner: a filled shield in a circle, the answer in
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

                    ShieldGlyph {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        mark: flyout.security.level === "protected" ? "tick"
                            : flyout.attention ? "bang" : ""
                        color: UI.Style.textOnAccent
                        strokeWidth: 2.2
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
                        text: flyout.security.level === "attention" ? qsTr("Action needed")
                            : flyout.security.level === "checking" ? qsTr("Checking your protection")
                            : flyout.security.level === "unchecked" ? flyout.security.uncheckedTitle
                            : qsTr("Your computer is protected")
                        size: 18
                        font.weight: UI.Style.weightSemiBold
                        Accessible.role: Accessible.Heading
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: {
                            const s = flyout.security;
                            if (s.level === "checking") {
                                return qsTr("This only takes a moment.");
                            }
                            // "Checked just now" would read as an update check
                            // that never happened.
                            if (s.level === "unchecked") {
                                return qsTr("Check now to see whether any security updates are waiting.");
                            }
                            const count = s.warnings.length;
                            // 32-bit apps on is a choice, not a problem, but
                            // the summary should not read as if it weren't there.
                            const lead = count === 0 && s.status.compat32 === "on" ? qsTr("32-bit apps are on, by your choice.")
                                : count === 0 ? qsTr("Nothing needs your attention.")
                                : count === 1 ? qsTr("1 thing needs your attention.")
                                : qsTr("%1 things need your attention.").arg(count);
                            // On its own line, so "just now" never wraps alone.
                            return qsTr("%1\n%2.").arg(lead).arg(s.checkedText());
                        }
                        color: UI.Style.inkStatus
                        lineHeight: 1.3
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                }
            }

            // Settings' grouped list: 62 px rows in a 12 px card, the status
            // under each title in sand when it needs doing, and either the one
            // thing to do or a chevron. The whole row does what its button
            // does, so the button labels the click rather than being a small
            // target of its own.
            UI.Card {
                visible: flyout.security.checked
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    Repeater {
                        model: flyout.security.areas

                        UI.ListRow {
                            id: area
                            required property var modelData
                            iconName: modelData.icon
                            title: modelData.title
                            description: modelData.status
                            descriptionColor: modelData.tone === "warn" ? UI.Style.warning
                                : modelData.tone === "note" ? UI.Style.accentText : UI.Style.inkMuted
                            interactive: true
                            chevron: modelData.action.length === 0
                            onClicked: flyout.open(modelData.run)

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                UI.Button {
                                    visible: area.modelData.ignorable === true
                                    size: "small"
                                    kind: "text"
                                    text: qsTr("Ignore")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: flyout.open("ignore:" + area.modelData.id)
                                }

                                UI.Button {
                                    visible: area.modelData.action.length > 0
                                    size: "small"
                                    text: area.modelData.action
                                    focusPolicy: Qt.NoFocus
                                    onClicked: flyout.open(area.modelData.run)
                                }
                            }
                        }
                    }
                }
            }
        }

        UsbPage {
            id: usbPage
            visible: flyout.page === "usb"
            width: parent.width
            security: flyout.security
            onBack: flyout.showSummary()
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
            text: qsTr("Open Privacy and security")
            onClicked: flyout.security.openPrivacy()
        }
    }
}
