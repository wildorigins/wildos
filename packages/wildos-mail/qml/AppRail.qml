import QtQuick
import WildOS.Mail
import WildOS.UI as UI

// The rail down the left of Mail, Calendar, People and Tasks
// (MailMicrosoft.dc, CalendarMicrosoft.dc, MailPeople.dc, MailTasks.dc):
// each is a tab of the same window, and right-clicking Calendar opens it in
// a window of its own. The one open has the neutral selected fill and an
// accent bar, as WinUI's navigation does.
Rectangle {
    id: rail

    property string current: "mail"
    property bool squareCorner: false

    signal picked(string app)

    implicitWidth: 60
    color: UI.Style.shade(0.30, 0.04)
    bottomLeftRadius: squareCorner ? 0 : UI.Style.windowRadius - 1

    Column {
        x: 4
        y: 8
        spacing: 4

        Repeater {
            model: [
                { id: "mail", name: qsTr("Mail"), icon: "mail" },
                { id: "calendar", name: qsTr("Calendar"), icon: "calendar" },
                { id: "people", name: qsTr("People"), icon: "people" },
                { id: "tasks", name: qsTr("Tasks"), icon: "tasks" }
            ]
            delegate: Rectangle {
                id: item
                required property var modelData
                readonly property bool active: rail.current === modelData.id
                width: 52
                height: 52
                radius: UI.Style.radiusSm
                // Fades in on hover and clears at once, so moving along a list
                // never lights two items. It comes before the colour so it sees
                // the change in hover first.
                Behavior on color {
                    enabled: mouse.containsMouse
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
                color: active ? UI.Style.surfaceSelected : mouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.name
                Accessible.selected: active
                activeFocusOnTab: true
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        if (!item.active) {
                            rail.picked(modelData.id);
                        }
                    } else if (modelData.id === "calendar" && (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))) {
                        railMenu.app = modelData.id;
                        railMenu.popup(item, item.width + 4, 0);
                    } else {
                        return;
                    }
                    event.accepted = true;
                }

                UI.FocusRing {
                    visible: item.activeFocus
                    anchors.margins: 0
                    targetRadius: item.radius - 4
                }

                Rectangle {
                    visible: item.active
                    x: 0
                    y: 10
                    width: 3
                    height: 32
                    radius: 2
                    color: UI.Style.accent
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 3
                    MailIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: item.modelData.icon
                        size: 19
                        color: item.active ? UI.Style.iconInk : UI.Style.iconMuted
                    }
                    UI.Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: item.modelData.name
                        size: 10
                        font.weight: item.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        color: item.active ? UI.Style.ink : UI.Style.inkMuted
                    }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: item.active ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: function (event) {
                        if (event.button === Qt.RightButton && item.modelData.id === "calendar") {
                            railMenu.app = item.modelData.id;
                            railMenu.popup();
                        } else if (!item.active) {
                            rail.picked(item.modelData.id);
                        }
                    }
                }
            }
        }
    }

    UI.Menu {
        id: railMenu
        property string app
        entries: [
            { text: qsTr("Open in new window"), icon: "external", run: () => Apps.openWindow(railMenu.app) }
        ]
    }
}
