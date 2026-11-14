/* Licence (InstallerLicence.dc), a notesqml step and the first page. It says
 * in plain words that Wild OS is GPL software with no warranty and that its
 * makers are not liable, and the person ticks the box before going on.
 *
 * The tick lives in Installer.licenceAccepted, so it starts clear on every
 * run and is still ticked after Back. Continue in the footer waits for it,
 * and the wildoslicence job, first in the install, refuses to touch the
 * disk unless wildosLicenceAccepted is in global storage.
 */
import io.calamares.core 1.0
import io.calamares.ui 1.0
import WildOS.Installer
import WildOS.UI as UI

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

WPage {
    id: page

    readonly property color amber: "#f5c27a"

    title: qsTr("Free software, with no warranty")
    subtitle: qsTr("Wild OS is free and open source. Please read what that means before you install it.")
    gap: 18

    function store() {
        Global.insert("wildosLicenceAccepted", Installer.licenceAccepted);
    }
    function onActivate() { store(); }
    function onLeave() { store(); }

    Connections {
        target: Installer
        function onLicenceAcceptedChanged() { page.store(); }
    }

    readonly property var rows: [
        { title: qsTr("Yours to use, copy, change and share"),
          body: qsTr("Wild OS is released under the GNU General Public License, version 3 or later. There is no fee and no account."),
          icon: "M7 3h7l4 4v14H7z M14 3v4h4 M10 12h5 M10 16h5", warn: false, link: true },
        { title: qsTr("It comes as it is, with no warranty"),
          body: qsTr("Nobody promises that Wild OS will work on this PC, suit a particular job or be free of faults."),
          icon: "M12 4L2.8 20h18.4L12 4z M12 10v4.5 M12 17.1v0.4", warn: true, link: false },
        { title: qsTr("The people who make it are not liable"),
          body: qsTr("If something goes wrong, including lost files, lost work or a PC that will not start, the Wild OS project and its contributors are not responsible, as far as the law allows."),
          icon: "M12 4L2.8 20h18.4L12 4z M12 10v4.5 M12 17.1v0.4", warn: true, link: false },
        { title: qsTr("Back up before you carry on"),
          body: qsTr("Installing changes your disk. Copy anything you cannot afford to lose somewhere safe first."),
          icon: "M6 5h12l2 9v4a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1v-4l2-9z M4 14h16 M8 16.8v0.4", warn: false, link: false }
    ]

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: panel.implicitHeight + 2
        radius: UI.Style.radiusMd
        color: UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.borderSubtle

        ColumnLayout {
            id: panel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 1
            spacing: 0

            Repeater {
                model: page.rows

                ColumnLayout {
                    id: entry
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        visible: entry.index > 0
                        height: 1
                        color: UI.Style.divider
                    }

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(62, words.implicitHeight + 22)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            spacing: 16

                            WIcon {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                stroke: entry.modelData.warn ? page.amber : UI.Style.accentText
                                path: entry.modelData.icon
                            }
                            ColumnLayout {
                                id: words
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: entry.modelData.title
                                    color: UI.Style.ink
                                    font.family: UI.Style.fontFamily
                                    font.letterSpacing: UI.Style.tracking(14)
                                    font.pixelSize: 14
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: entry.modelData.body
                                    color: UI.Style.inkMuted
                                    font.family: UI.Style.fontFamily
                                    font.letterSpacing: UI.Style.tracking(12)
                                    font.pixelSize: 12
                                    lineHeight: 1.5
                                    wrapMode: Text.WordWrap
                                }
                            }
                            T.AbstractButton {
                                id: readLink
                                visible: entry.modelData.link
                                text: qsTr("Read the full licence")
                                implicitWidth: linkText.implicitWidth
                                implicitHeight: linkText.implicitHeight + 6
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                Accessible.role: Accessible.Link
                                Accessible.name: text
                                onClicked: licenceWindow.openAtWarranty()

                                contentItem: Text {
                                    id: linkText
                                    text: readLink.text
                                    color: readLink.hovered ? "#ffffff" : "#8fb4ff"
                                    font.family: UI.Style.fontFamily
                                    font.letterSpacing: UI.Style.tracking(13)
                                    font.pixelSize: 13
                                    font.underline: readLink.visualFocus
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Other software keeps its own terms: Brave, hardware drivers and anything you add from the Store. Windows mode needs your own Windows licence and Microsoft's terms.")
        color: UI.Style.inkMuted
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(12)
        font.pixelSize: 12
        lineHeight: 1.5
        wrapMode: Text.WordWrap
    }

    // The whole panel ticks the box, as a label does; the box itself takes
    // the keyboard focus, and Space ticks it.
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: acceptRow.implicitHeight + 28
        radius: UI.Style.radiusMd
        color: UI.Style.accentTint
        border.width: 1
        border.color: Qt.rgba(143 / 255, 180 / 255, 1, 0.4)

        // Under the row, so a click on the box itself reaches only the box.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Installer.licenceAccepted = !Installer.licenceAccepted
        }

        RowLayout {
            id: acceptRow
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 14

            UI.CheckBox {
                id: tick
                Layout.alignment: Qt.AlignVCenter
                checked: Installer.licenceAccepted
                Accessible.name: acceptText.text
                onToggled: {
                    Installer.licenceAccepted = checked;
                    checked = Qt.binding(() => Installer.licenceAccepted);
                }
            }
            Text {
                id: acceptText
                Layout.fillWidth: true
                text: qsTr("I have read this. I accept that Wild OS comes with no warranty and that I install it at my own risk.")
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(14)
                font.pixelSize: 14
                lineHeight: 1.5
                wrapMode: Text.WordWrap
            }
        }
    }

    Item { Layout.fillHeight: true }

    LicenceWindow { id: licenceWindow }
}
