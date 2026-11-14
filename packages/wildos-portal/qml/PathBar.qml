pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The path bar of DialogSave.dc: a sunken 34px field with the folder icon
// and the way to here as clickable steps ("Home > Documents"). Clicking
// past the steps, or Ctrl+L, turns it into a box to type a path in.
Rectangle {
    id: bar

    required property var folder
    property bool editing: false

    signal done()

    function edit() {
        editing = true;
        field.text = folder.pathText;
        field.forceActiveFocus();
        field.selectAll();
    }

    implicitHeight: 34
    radius: UI.Style.radiusSm
    color: UI.Style.surfaceSunken
    border.width: 1
    border.color: field.activeFocus ? UI.Style.accentBorderStrong : UI.Style.borderGlass
    Accessible.role: Accessible.Grouping
    Accessible.name: "Location"

    MouseArea {
        anchors.fill: parent
        visible: !bar.editing
        cursorShape: Qt.IBeamCursor
        onClicked: bar.edit()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 8
        visible: !bar.editing

        UI.ThemeIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            name: bar.folder.isComputer ? "computer" : "folder"
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: crumbRow.width
            clip: true
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            // The end of a long path is what matters, as in Files.
            onContentWidthChanged: contentX = Math.max(0, contentWidth - width)
            onWidthChanged: contentX = Math.max(0, contentWidth - width)

            Row {
                id: crumbRow
                height: flick.height

                Repeater {
                    model: bar.folder.crumbs

                    Row {
                        id: crumb
                        required property var modelData
                        required property int index
                        height: crumbRow.height

                        UI.Label {
                            visible: crumb.index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 6
                            rightPadding: 6
                            text: "›"
                            opacity: 0.5
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: crumbLabel.implicitWidth + 8
                            height: 24
                            radius: UI.Style.radiusXs
                            color: crumbMouse.pressed ? UI.Style.surfacePressed : crumbMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                            Accessible.role: Accessible.Button
                            Accessible.name: crumb.modelData.name
                            Accessible.onPressAction: bar.folder.openUrl(crumb.modelData.url)

                            UI.Label {
                                id: crumbLabel
                                anchors.centerIn: parent
                                text: crumb.modelData.name
                            }

                            MouseArea {
                                id: crumbMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: bar.folder.openUrl(crumb.modelData.url)
                            }
                        }
                    }
                }
            }
        }
    }

    TextInput {
        id: field
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        visible: bar.editing
        verticalAlignment: TextInput.AlignVCenter
        color: UI.Style.inkField
        selectionColor: UI.Style.textSelection
        selectedTextColor: UI.Style.ink
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(UI.Style.px(13))
        font.pixelSize: UI.Style.px(13)
        clip: true
        Accessible.role: Accessible.EditableText
        Accessible.name: "Location"
        onAccepted: {
            if (bar.folder.openText(text)) {
                bar.editing = false;
                bar.done();
            } else {
                bar.folder.failed("Wild OS cannot find \"" + text + "\". Check the spelling and try again.");
            }
        }
        // The text menu holds focus while it is open; the location is still
        // being typed.
        onActiveFocusChanged: if (!activeFocus && !locationMenu.opened) bar.editing = false
        Keys.onEscapePressed: {
            bar.editing = false;
            bar.done();
        }
        Keys.onPressed: event => {
            if (locationMenu.isMenuKey(event)) {
                locationMenu.openAtCaret();
                event.accepted = true;
            }
        }

        UI.TextMenu {
            id: locationMenu
            onFocusLeft: bar.editing = false
        }
    }
}
