/* Wild OS installer step list, the left side of the sheet.
 *
 * Calamares has one entry per module, more than people need to see, so
 * several modules share a step here. The ranges follow the sequence in
 * settings.conf and must change with it: notesqml@licence, welcomeq and
 * localeq (no page), keyboardq, partition, notesqml@encryption, then
 * summaryq, the install itself and finishedq.
 */
import io.calamares.ui 1.0
import io.calamares.core 1.0

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Templates as T
import WildOS.UI as UI
import WildOS.Installer

Rectangle {
    id: sideBar

    // Calamares takes the panel's width from here (see setDimension()).
    implicitWidth: 260
    color: Qt.rgba(9 / 255, 11 / 255, 15 / 255, 0.28)
    topLeftRadius: 20
    bottomLeftRadius: 20

    readonly property int current: ViewManager.currentStepIndex
    readonly property int diskStep: 4
    readonly property int encryptionStep: 5
    readonly property int installStep: 7
    readonly property int finishedStep: 8
    readonly property var steps: [
        { label: qsTr("Licence"), first: 0, last: 0 },
        { label: qsTr("Before we start"), first: 1, last: 2 },
        { label: qsTr("Keyboard"), first: 3, last: 3 },
        { label: qsTr("Where to install"), first: 4, last: 4 },
        { label: qsTr("Encryption"), first: 5, last: 5 },
        { label: qsTr("Install"), first: 6, last: 8 }
    ]

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: Qt.rgba(1, 1, 1, 0.07)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.topMargin: 32
        anchors.bottomMargin: 26
        spacing: 28

        RowLayout {
            spacing: 12

            Image {
                source: "mark.png"
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                sourceSize: Qt.size(60, 60)
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }
            Text {
                text: qsTr("Install Wild OS")
                color: "#eef1f8"
                font.family: "Albert Sans"
                font.pixelSize: 17
                font.weight: UI.Style.weightSemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            Repeater {
                model: sideBar.steps

                Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: sideBar.current >= modelData.first && sideBar.current <= modelData.last
                    // The finished page is the end of the last step: it is done then.
                    readonly property bool isDone: sideBar.current > modelData.last
                                                   || (sideBar.current === sideBar.finishedStep && modelData.last === sideBar.finishedStep
                                                       && Installer.finished !== null && !Installer.finished.failed)

                    Layout.fillWidth: true
                    height: 40
                    radius: 8
                    color: isCurrent ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.label

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            radius: 11
                            color: row.isCurrent ? "#8fb4ff" : (row.isDone ? Qt.rgba(143 / 255, 180 / 255, 1, 0.25) : "transparent")
                            border.width: (row.isCurrent || row.isDone) ? 0 : 1.5
                            border.color: Qt.rgba(232 / 255, 236 / 255, 248 / 255, 0.45)

                            Text {
                                anchors.centerIn: parent
                                visible: !row.isDone
                                text: row.index + 1
                                color: row.isCurrent ? "#0f1530" : "#eef1f8"
                                font.family: "Albert Sans"
                                font.pixelSize: 11
                                font.weight: row.isCurrent ? UI.Style.weightBold : UI.Style.weightRegular
                            }
                            WIcon {
                                anchors.centerIn: parent
                                width: 12
                                height: 12
                                visible: row.isDone
                                strokeWidth: 3
                                stroke: row.isCurrent ? "#0f1530" : "#b9cfff"
                                path: "M5 12l5 5 9-10"
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.label
                            elide: Text.ElideRight
                            color: row.isCurrent ? "#eef1f8" : Qt.rgba(232 / 255, 236 / 255, 248 / 255, row.isDone ? 0.8 : 0.75)
                            font.family: "Albert Sans"
                            font.pixelSize: 13
                            font.weight: row.isCurrent ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 14

            SideLink {
                text: sideBar.current === sideBar.diskStep ? qsTr("Help choosing") : qsTr("Get help")
                iconPath: "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0 M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.6.3-1 .8-1 1.5v.7 M12 16.6v0.4"
                onClicked: help.openOn(sideBar.current === sideBar.diskStep ? "choosing"
                                       : sideBar.current === sideBar.encryptionStep ? "encryption" : "general")
            }

            // Only distribution developers run Calamares with -d, and they need a way into the debug window.
            SideLink {
                visible: typeof debug !== "undefined" && debug.enabled
                text: qsTr("Debug information")
                iconPath: "M4 6h16 M4 12h16 M4 18h10"
                onClicked: debug.toggle()
            }

            SideLink {
                visible: sideBar.current < sideBar.installStep
                text: qsTr("Close the installer")
                iconPath: "M6 6l12 12 M18 6L6 18"
                onClicked: ViewManager.quit()
            }
        }
    }

    // A link with an icon.
    component SideLink: T.AbstractButton {
        id: link
        property string iconPath

        Layout.fillWidth: true
        implicitHeight: 20
        // Qt hands the first link focus when the sheet is clicked, which
        // would show a ring nobody asked for; the pages hold the focus.
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.role: Accessible.Link

        contentItem: RowLayout {
            spacing: 8
            WIcon {
                Layout.preferredWidth: 15
                Layout.preferredHeight: 15
                strokeWidth: 2
                stroke: link.hovered ? "#ffffff" : UI.Style.accentText
                path: link.iconPath
            }
            Text {
                Layout.fillWidth: true
                text: link.text
                color: link.hovered ? "#ffffff" : UI.Style.accentText
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(13)
                font.pixelSize: 13
            }
        }
    }

    HelpWindow { id: help }
}
