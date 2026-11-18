import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Month (CalMsMonth.dc): six weeks under the day names, each day with as
// many of its events as fit, all-day ones as filled bars and the rest as a
// dot, a time and a title. Today is the circled date and its cell is lit.
Item {
    id: view

    // Any day in the month shown.
    property date month: new Date()
    property int firstDay: 1
    property string openKey

    signal eventClicked(string key, Item block)
    signal dayClicked(date day)
    signal newAt(date start, bool allDay)
    // `from` is the event when its menu was asked for with the keyboard.
    signal eventMenu(string key, var from)
    signal slotMenu(date start, bool allDay)

    readonly property date firstShown: {
        const first = new Date(month.getFullYear(), month.getMonth(), 1);
        const offset = (first.getDay() - firstDay + 7) % 7;
        return new Date(month.getFullYear(), month.getMonth(), 1 - offset);
    }
    readonly property date endShown: new Date(firstShown.getFullYear(), firstShown.getMonth(), firstShown.getDate() + 42)
    readonly property real cellWidth: width / 7
    readonly property real cellHeight: (height - 30) / 6

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }

    Row {
        id: names
        width: parent.width
        height: 30
        Repeater {
            model: 7
            delegate: Item {
                required property int index
                width: view.cellWidth
                height: 30
                UI.Label {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: Formats.names().dayName((index + view.firstDay) % 7, Locale.LongFormat)
                    size: 12
                    color: UI.Style.inkMuted
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: UI.Style.borderSubtle
                }
            }
        }
    }

    Grid {
        y: 30
        columns: 7
        Repeater {
            model: 42
            delegate: Rectangle {
                id: cell
                required property int index
                readonly property date day: new Date(view.firstShown.getFullYear(), view.firstShown.getMonth(), view.firstShown.getDate() + index)
                readonly property bool inMonth: day.getMonth() === view.month.getMonth()
                readonly property bool today: view.sameDay(day, new Date())
                readonly property var events: CalendarEngine.revision >= 0 ? CalendarEngine.eventsOn(day) : []
                readonly property int room: Math.max(0, Math.floor((height - 34) / 18))
                width: view.cellWidth
                height: view.cellHeight
                color: today ? UI.Style.accentSoft : cellMouse.containsMouse ? UI.Style.surfaceHover : "transparent"

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: UI.Style.borderSubtle
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: UI.Style.borderSubtle
                }
                MouseArea {
                    id: cellMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton) {
                            view.slotMenu(cell.day, true);
                        }
                    }
                    onDoubleClicked: function (mouse) {
                        if (mouse.button === Qt.LeftButton) {
                            view.newAt(cell.day, true);
                        }
                    }
                }

                Rectangle {
                    visible: cell.today
                    x: 6
                    y: 5
                    width: 24
                    height: 24
                    radius: 12
                    color: UI.Style.accent
                }
                UI.Label {
                    x: cell.today ? 6 + (24 - implicitWidth) / 2 : 10
                    y: 9
                    // The first of a month carries its name.
                    text: cell.day.getDate() === 1 ? Formats.shortDayAndMonth(cell.day) : cell.day.getDate()
                    size: 13
                    font.weight: cell.today ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: cell.today ? UI.Style.textOnAccent : cell.inMonth ? UI.Style.ink : UI.Style.inkSubtle
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: view.dayClicked(cell.day)
                    }
                }

                Column {
                    x: 6
                    y: 32
                    width: parent.width - 12
                    spacing: 0
                    Repeater {
                        model: cell.events.length > cell.room ? cell.events.slice(0, Math.max(0, cell.room - 1)) : cell.events
                        delegate: Rectangle {
                            id: line
                            required property var modelData
                            readonly property bool bar: modelData.allDay || modelData.timeText === ""
                            width: parent.width
                            height: 18
                            radius: 3
                            color: bar ? Qt.rgba(Qt.color(modelData.color).r, Qt.color(modelData.color).g, Qt.color(modelData.color).b, 0.38)
                                 : lineMouse.containsMouse || view.openKey === modelData.key ? UI.Style.surfaceHover : "transparent"
                            opacity: cell.inMonth ? 1 : 0.6
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: bar ? qsTr("%1, all day").arg(modelData.title) : qsTr("%1, %2").arg(modelData.title).arg(modelData.timeText)
                            Keys.onPressed: function (event) {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                    view.eventClicked(modelData.key, line);
                                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                    view.eventMenu(modelData.key, line);
                                } else {
                                    return;
                                }
                                event.accepted = true;
                            }

                            UI.FocusRing {
                                visible: line.activeFocus
                                anchors.margins: 0
                                targetRadius: line.radius - 4
                            }

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: line.bar ? 6 : 4
                                spacing: 6
                                width: parent.width - 8
                                Rectangle {
                                    visible: !line.bar
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    color: line.modelData.color
                                }
                                UI.Label {
                                    visible: !line.bar
                                    text: line.modelData.timeText
                                    size: 12
                                    color: UI.Style.inkMuted
                                }
                                UI.Label {
                                    width: parent.width - (line.bar ? 0 : 60)
                                    text: line.modelData.title
                                    size: 12
                                    font.weight: line.bar ? UI.Style.weightSemiBold : UI.Style.weightMedium
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                id: lineMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function (mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        view.eventMenu(line.modelData.key, null);
                                    } else {
                                        view.eventClicked(line.modelData.key, line);
                                    }
                                }
                            }
                        }
                    }
                    UI.Label {
                        visible: cell.events.length > cell.room
                        leftPadding: 4
                        text: qsTr("%1 more").arg(cell.events.length - Math.max(0, cell.room - 1))
                        size: 12
                        color: UI.Style.accentText
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: view.dayClicked(cell.day)
                        }
                    }
                }
            }
        }
    }
}
