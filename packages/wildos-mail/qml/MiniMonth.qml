import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// The small month (CalendarMicrosoft.dc, left pane): the days on screen
// share one band, today is the filled circle. Choosing a day goes there.
ColumnLayout {
    id: mini

    // The month shown, as its first day.
    property date month: new Date()
    // The days on screen, highlighted together.
    property date rangeFrom: new Date()
    property date rangeTo: new Date()
    property int firstDay: 1
    property bool showBusy: true

    signal picked(date day)

    spacing: 6

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function dayAt(index) {
        const first = new Date(month.getFullYear(), month.getMonth(), 1);
        const offset = (first.getDay() - firstDay + 7) % 7;
        return new Date(month.getFullYear(), month.getMonth(), 1 - offset + index);
    }
    function shift(months) {
        month = new Date(month.getFullYear(), month.getMonth() + months, 1);
    }

    RowLayout {
        Layout.fillWidth: true
        UI.Label {
            Layout.fillWidth: true
            text: Formats.monthAndYear(mini.month)
            size: 13
            font.weight: UI.Style.weightSemiBold
        }
        UI.IconButton {
            iconName: "chevron-left"
            implicitWidth: 26
            implicitHeight: 26
            tip: qsTr("Previous month")
            onClicked: mini.shift(-1)
        }
        UI.IconButton {
            iconName: "chevron-right"
            implicitWidth: 26
            implicitHeight: 26
            tip: qsTr("Next month")
            onClicked: mini.shift(1)
        }
    }

    Grid {
        id: grid
        columns: 7
        Layout.fillWidth: true
        readonly property real cell: Math.floor(width / 7)

        Repeater {
            model: 7
            delegate: UI.Label {
                required property int index
                width: grid.cell
                height: 22
                horizontalAlignment: Text.AlignHCenter
                text: Formats.names().dayName((index + mini.firstDay) % 7, Locale.ShortFormat).charAt(0).toUpperCase()
                size: 11
                font.weight: UI.Style.weightSemiBold
                color: UI.Style.inkSubtle
            }
        }
        Repeater {
            model: 42
            delegate: Item {
                id: cell
                required property int index
                readonly property date day: mini.dayAt(index)
                readonly property bool inMonth: day.getMonth() === mini.month.getMonth()
                readonly property bool today: mini.sameDay(day, new Date())
                readonly property bool inRange: day >= mini.rangeFrom && day < mini.rangeTo
                readonly property bool rangeStart: inRange && (index % 7 === 0 || mini.sameDay(day, mini.rangeFrom))
                readonly property bool rangeEnd: inRange && (index % 7 === 6 || mini.sameDay(new Date(day.getFullYear(), day.getMonth(), day.getDate() + 1), mini.rangeTo))
                width: grid.cell
                height: 33
                // Six rows only when the month needs them.
                visible: index < 35 || mini.dayAt(35).getMonth() === mini.month.getMonth()
                Accessible.role: Accessible.Button
                Accessible.name: Formats.longDate(day)

                Rectangle {
                    visible: cell.inRange
                    anchors.verticalCenter: parent.verticalCenter
                    x: cell.rangeStart ? 2 : 0
                    width: parent.width - (cell.rangeStart ? 2 : 0) - (cell.rangeEnd ? 2 : 0)
                    height: 28
                    radius: cell.rangeStart || cell.rangeEnd ? 14 : 0
                    color: UI.Style.surfaceSelected
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    radius: 14
                    color: cell.today ? UI.Style.accent : mouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                }
                UI.Label {
                    anchors.centerIn: parent
                    text: cell.day.getDate()
                    size: 12
                    font.weight: cell.today || cell.inRange ? UI.Style.weightSemiBold : UI.Style.weightRegular
                    color: cell.today ? UI.Style.textOnAccent : cell.inMonth ? UI.Style.ink : UI.Style.inkSubtle
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mini.picked(cell.day)
                }
            }
        }
    }
}
