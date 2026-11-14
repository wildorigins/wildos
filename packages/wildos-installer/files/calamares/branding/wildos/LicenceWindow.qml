/* The full licence (InstallerLicenceFull.dc), opened by "Read the full
 * licence". The text is the system's own copy of the GPL, read at run time
 * and shown word for word in English whatever the installer's language,
 * because the licence only allows verbatim copies. It opens on section 15
 * with 15 and 16, warranty and liability, outlined.
 *
 * A window over the whole screen, so it can dim the installer sheet behind
 * it the way the board does.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Templates as T
import WildOS.Installer
import WildOS.UI as UI

Window {
    id: win

    readonly property var sections: Installer.licenceSections()
    readonly property var numbered: sections.filter(function(s) { return s.number >= 0; })
    // The section whose text is at the top of the view.
    property int current: 15
    property bool jumping: false
    property string saveProblem: ""
    readonly property color amber: "#f5c27a"

    function openAtWarranty() {
        x = screen.virtualX;
        y = screen.virtualY;
        width = screen.width;
        height = screen.height;
        saveProblem = "";
        show();
        raise();
        requestActivate();
        // After the first layout, or the list has no height to jump in.
        Qt.callLater(jumpTo, 15);
    }

    function jumpTo(number) {
        for (var i = 0; i < sections.length; ++i) {
            if (sections[i].number === number) {
                jumping = true;
                current = number;
                text.positionViewAtIndex(i, ListView.Beginning);
                // Keep the list's top margin above the section, as at the start.
                scrollTo(text.contentY - text.topMargin);
                jumping = false;
                return;
            }
        }
    }

    function scrollTo(y) {
        var top = text.originY - text.topMargin;
        var bottom = text.originY + text.contentHeight + text.bottomMargin - text.height;
        text.contentY = Math.max(top, Math.min(y, Math.max(top, bottom)));
    }

    // Window-wide, so the keys scroll the text whatever has the focus.
    Shortcut {
        sequences: [StandardKey.MoveToNextPage]
        onActivated: win.scrollTo(text.contentY + text.height - 60)
    }
    Shortcut {
        sequences: [StandardKey.MoveToPreviousPage]
        onActivated: win.scrollTo(text.contentY - text.height + 60)
    }
    Shortcut {
        sequences: [StandardKey.MoveToStartOfDocument, "Home"]
        onActivated: win.scrollTo(text.originY - text.topMargin)
    }
    Shortcut {
        sequences: [StandardKey.MoveToEndOfDocument, "End"]
        onActivated: win.scrollTo(text.originY + text.contentHeight)
    }

    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.ApplicationModal
    color: "transparent"
    title: qsTr("GNU General Public License")

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: win.close()
    }

    // The installer sheet, dimmed. Clicks on it go nowhere.
    Rectangle {
        anchors.centerIn: parent
        width: 1040
        height: 720
        radius: UI.Style.radiusXl
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea { anchors.fill: parent }
    }

    Rectangle {
        id: dialog
        anchors.centerIn: parent
        width: 860
        height: 600
        radius: UI.Style.radiusLg
        color: "#222428"
        border.width: 1
        border.color: UI.Style.borderDialog
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("GNU General Public License, version 3")

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 55
                Layout.leftMargin: 21
                Layout.rightMargin: 21
                spacing: 12

                Text {
                    text: qsTr("GNU General Public License")
                    color: UI.Style.ink
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(15)
                    font.pixelSize: 15
                    font.weight: UI.Style.weightSemiBold
                }
                Text {
                    text: qsTr("Version 3, 29 June 2007")
                    color: UI.Style.inkMuted
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(13)
                    font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("The full, unchanged text. In English only.")
                    color: UI.Style.inkMuted
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Contents: the numbered sections, each a jump.
                Rectangle {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    color: "#1a1c20"

                    ListView {
                        id: contents
                        anchors.fill: parent
                        anchors.topMargin: 14
                        anchors.bottomMargin: 14
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: win.numbered
                        currentIndex: win.numbered.findIndex(function(s) { return s.number === win.current; })
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        T.ScrollBar.vertical: UI.ScrollBar {}

                        delegate: T.AbstractButton {
                            id: entry
                            required property var modelData
                            readonly property bool isCurrent: modelData.number === win.current

                            width: ListView.view.width
                            height: 30
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.role: Accessible.Link
                            Accessible.name: modelData.number + ". " + modelData.title
                            onClicked: win.jumpTo(modelData.number)

                            background: Rectangle {
                                radius: 6
                                color: entry.isCurrent ? Qt.rgba(1, 1, 1, 0.09)
                                       : entry.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                                border.width: entry.visualFocus ? 2 : 0
                                border.color: "#8fb4ff"
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Text {
                                    Layout.leftMargin: 10
                                    Layout.preferredWidth: 18
                                    horizontalAlignment: Text.AlignRight
                                    text: entry.modelData.number
                                    color: UI.Style.inkSubtle
                                    font.family: UI.Style.fontFamily
                                    font.pixelSize: 12
                                    font.features: { "tnum": 1 }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.rightMargin: 10
                                    text: entry.modelData.title
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    color: entry.isCurrent ? UI.Style.ink : UI.Style.inkMuted
                                    font.family: UI.Style.fontFamily
                                    font.letterSpacing: UI.Style.tracking(12)
                                    font.pixelSize: 12
                                    font.weight: entry.isCurrent ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        width: 1
                        color: Qt.rgba(1, 1, 1, 0.07)
                    }
                }

                ListView {
                    id: text
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    spacing: 14
                    topMargin: 18
                    bottomMargin: 18
                    // Every section is built up front, so jumps land exactly.
                    cacheBuffer: 1000000
                    boundsBehavior: Flickable.StopAtBounds
                    model: win.sections
                    T.ScrollBar.vertical: UI.ScrollBar {}

                    onContentYChanged: {
                        if (win.jumping)
                            return;
                        var i = indexAt(40, contentY + topMargin + 4);
                        // The title page and preamble light no entry.
                        if (i >= 0)
                            win.current = win.sections[i].number;
                    }


                    delegate: Item {
                        id: section
                        required property var modelData
                        readonly property bool outlined: modelData.number === 15 || modelData.number === 16

                        width: ListView.view.width
                        height: box.height

                        Rectangle {
                            id: box
                            x: 22
                            width: parent.width - 50
                            height: paragraphs.implicitHeight + (section.outlined ? 28 : 0)
                            radius: 10
                            color: section.outlined ? Qt.rgba(245 / 255, 194 / 255, 122 / 255, 0.07) : "transparent"
                            border.width: section.outlined ? 1 : 0
                            border.color: Qt.rgba(245 / 255, 194 / 255, 122 / 255, 0.28)

                            ColumnLayout {
                                id: paragraphs
                                x: section.outlined ? 16 : 0
                                y: section.outlined ? 14 : 0
                                width: box.width - 2 * x
                                spacing: 10

                                Repeater {
                                    model: section.modelData.paragraphs
                                    Text {
                                        required property string modelData
                                        required property int index
                                        readonly property bool heading: index === 0 && section.modelData.number >= 0

                                        Layout.fillWidth: true
                                        text: modelData
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                        color: heading || section.outlined ? UI.Style.ink : UI.Style.inkMuted
                                        font.family: UI.Style.fontFamily
                                        font.letterSpacing: UI.Style.tracking(heading ? 14 : 13)
                                        font.pixelSize: heading ? 14 : 13
                                        font.weight: heading ? UI.Style.weightSemiBold : UI.Style.weightRegular
                                        lineHeight: heading ? 1 : 1.62
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 80
                        visible: win.sections.length === 0
                        text: qsTr("The licence file is missing from this system. The GNU General Public License is at gnu.org/licenses/gpl-3.0.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: UI.Style.inkMuted
                        font.family: UI.Style.fontFamily
                        font.pixelSize: 13
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: Qt.rgba(1, 1, 1, 0.03)
                bottomLeftRadius: UI.Style.radiusLg - 1
                bottomRightRadius: UI.Style.radiusLg - 1

                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 21
                    anchors.rightMargin: 21
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: win.saveProblem !== "" ? win.saveProblem
                              : qsTr("Sections 15 and 16 cover warranty and liability. A copy is installed with Wild OS.")
                        elide: Text.ElideRight
                        color: win.saveProblem !== "" ? win.amber : UI.Style.inkMuted
                        font.family: UI.Style.fontFamily
                        font.letterSpacing: UI.Style.tracking(12)
                        font.pixelSize: 12
                    }
                    UI.Button {
                        text: qsTr("Save a copy")
                        enabled: win.sections.length > 0
                        onClicked: win.saveProblem = Installer.saveLicence(win)
                    }
                    UI.Button {
                        kind: "primary"
                        text: qsTr("Close")
                        onClicked: win.close()
                    }
                }
            }
        }
    }
}
