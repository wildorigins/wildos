/* Ready to install (InstallerSummary.dc): the disk as it will be, then what
 * happens to it, each with a way back to the step that decides it. The
 * partition module stores the plan in global storage (wildosPlan, from
 * wildos-disk-page.patch); the encryption page stores wildosEncrypt.
 * Nothing touches the disk until Install in the footer.
 */
import io.calamares.core 1.0
import io.calamares.ui 1.0
import WildOS.Installer
import WildOS.UI as UI

import QtQuick
import QtQuick.Layouts

WPage {
    id: page

    property var plan: ({})
    property bool encrypted: true
    property string keyboardName: ""
    readonly property string mode: plan.mode || ""
    readonly property string other: plan.other || ""
    readonly property bool windows: other === "Windows"
    // The model name, or "this disk" when the system only knows a device name.
    readonly property string diskName: (plan.disk || "") === "" || plan.disk === qsTr("This disk") ? qsTr("this disk") : plan.disk
    readonly property string diskText: qsTr("%1, %2").arg(diskName).arg(gb(plan.diskSize || 0))

    // The step indexes in settings.conf, for the Change links.
    readonly property int keyboardStep: 3
    readonly property int diskStep: 4

    title: qsTr("Ready to install")
    subtitle: qsTr("Check everything below. This is the last step before anything on your disk changes.")
    gap: 18

    function gb(bytes) {
        return qsTr("%1 GB").arg(Math.round(bytes / 1e9));
    }

    function onActivate() {
        plan = Global.value("wildosPlan") || {};
        encrypted = Global.value("wildosEncrypt") !== false;
        var kb = Installer.keyboard;
        if (kb) {
            var l = kb.keyboardLayoutsModel, v = kb.keyboardVariantsModel;
            var name = l.data(l.index(l.currentIndex, 0), Qt.DisplayRole) || "";
            var variant = v.currentIndex >= 0 ? v.data(v.index(v.currentIndex, 0), 256) : "";
            keyboardName = variant ? v.data(v.index(v.currentIndex, 0), Qt.DisplayRole) : name;
        }
    }

    // Calamares' summary step does not call onActivate() on its page, so the
    // page refreshes itself when it becomes the current step.
    Connections {
        target: ViewManager
        function onCurrentStepChanged() {
            if (ViewManager.currentStepIndex === page.diskStep + 2)
                page.onActivate();
        }
    }

    function goBackTo(step) {
        for (var i = 0; i < 12 && ViewManager.currentStepIndex > step; ++i)
            ViewManager.back();
    }

    readonly property var rows: {
        var wild = gb(plan.wildosSize || 0) + (encrypted ? qsTr(", encrypted with your disk password") : qsTr(", not encrypted"));
        var out = [];
        if (mode === "alongside") {
            out.push({ label: qsTr("Where"), value: windows ? qsTr("Alongside Windows on %1").arg(diskText)
                                                            : qsTr("Alongside the other system on %1").arg(diskText), step: diskStep });
            out.push({ label: windows ? qsTr("Windows") : qsTr("Other system"),
                       value: qsTr("Shrinks to %1. Its files and programs stay exactly as they are.").arg(gb(plan.otherSize || 0)), step: diskStep });
            out.push({ label: qsTr("Wild OS"), value: wild, step: diskStep });
            out.push({ label: qsTr("Will erase"), value: "<b>" + qsTr("Nothing") + "</b>", step: diskStep });
        } else if (mode === "erase") {
            out.push({ label: qsTr("Where"), value: windows ? qsTr("In place of Windows on %1").arg(diskText)
                                                            : qsTr("The whole of %1").arg(diskText), step: diskStep });
            if (other !== "")
                out.push({ label: windows ? qsTr("Windows") : qsTr("Other system"),
                           value: qsTr("Erased, with all its files and programs."), step: diskStep });
            out.push({ label: qsTr("Wild OS"), value: wild, step: diskStep });
            out.push({ label: qsTr("Will erase"), value: "<b>" + qsTr("Everything on this disk") + "</b>", step: diskStep });
        } else {
            out.push({ label: qsTr("Where"), value: qsTr("Partitions you set up on %1").arg(diskText), step: diskStep });
            out.push({ label: qsTr("Wild OS"), value: wild, step: diskStep });
            var erase = plan.erase || [];
            out.push({ label: qsTr("Will erase"), value: "<b>" + (erase.length ? erase.join(", ") : qsTr("Nothing")) + "</b>", step: diskStep });
        }
        out.push({ label: qsTr("Keyboard"), value: keyboardName, step: keyboardStep });
        return out;
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        radius: UI.Style.radiusSm
        color: "transparent"
        clip: true

        Row {
            anchors.fill: parent
            spacing: 2
            Rectangle {
                visible: page.mode === "alongside"
                width: visible ? (parent.width - 2) * (page.plan.otherSize || 0) / Math.max(1, (page.plan.otherSize || 0) + (page.plan.wildosSize || 0)) : 0
                height: parent.height
                color: Qt.rgba(1, 1, 1, 0.18)
                topLeftRadius: UI.Style.radiusSm
                bottomLeftRadius: UI.Style.radiusSm
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: (page.windows ? qsTr("Windows") : qsTr("Other system")) + " " + page.gb(page.plan.otherSize || 0)
                    color: UI.Style.ink
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                    font.weight: UI.Style.weightSemiBold
                }
            }
            Rectangle {
                width: page.mode === "alongside" ? parent.width - 2 - parent.children[0].width : parent.width
                height: parent.height
                color: UI.Style.accent
                topRightRadius: UI.Style.radiusSm
                bottomRightRadius: UI.Style.radiusSm
                topLeftRadius: page.mode === "alongside" ? 0 : UI.Style.radiusSm
                bottomLeftRadius: page.mode === "alongside" ? 0 : UI.Style.radiusSm
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Wild OS %1").arg(page.gb(page.plan.wildosSize || 0)) + (page.encrypted ? qsTr(", encrypted") : "")
                    color: UI.Style.textOnAccent
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                    font.weight: UI.Style.weightSemiBold
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: list.implicitHeight + 2
        radius: UI.Style.radiusMd
        color: UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.borderSubtle

        ColumnLayout {
            id: list
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 1
            spacing: 0

            Repeater {
                model: page.rows
                delegate: Item {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: Math.max(50, value.implicitHeight + 20)

                    Rectangle {
                        visible: index > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: UI.Style.divider
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 16
                        Text {
                            Layout.preferredWidth: 150
                            Layout.alignment: Qt.AlignBaseline
                            text: modelData.label
                            color: UI.Style.inkMuted
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(13)
                            font.pixelSize: 13
                        }
                        Text {
                            id: value
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignBaseline
                            text: modelData.value
                            textFormat: Text.StyledText
                            color: UI.Style.ink
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(14)
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                        UI.LinkButton {
                            Layout.alignment: Qt.AlignBaseline
                            text: qsTr("Change")
                            onClicked: page.goBackTo(modelData.step)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: info.implicitHeight + 28
        radius: UI.Style.radiusMd
        color: UI.Style.surfaceRest
        border.width: 1
        border.color: UI.Style.borderSubtle
        RowLayout {
            id: info
            anchors.fill: parent
            anchors.margins: 14
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 14
            WIcon {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                stroke: UI.Style.accentText
                path: "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0 M12 7v5l3 2"
            }
            Text {
                Layout.fillWidth: true
                // Started straight into the installer, or by Remove everything,
                // there is no trial behind it.
                text: Installer.startMode === "install" || Installer.startMode === "recovery"
                      ? qsTr("Takes about 10 minutes. Keep the computer plugged in.")
                      : qsTr("Takes about 10 minutes. Keep the computer plugged in; you can keep using this trial while it installs.")
                color: Qt.rgba(232 / 255, 236 / 255, 248 / 255, 0.85)
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(13)
                font.pixelSize: 13
                lineHeight: 1.5
                wrapMode: Text.WordWrap
            }
        }
    }

    Item { Layout.fillHeight: true }
}
