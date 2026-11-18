import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI
import "Formats.js" as Formats

// Day, work week and week (CalendarMicrosoft.dc): a column per day under
// its date, events that fill whole days in the band above the hours, and
// the hours themselves, 40px each, with a line at the time now. Events can
// be dragged to another time or day; a double-click on an empty slot
// starts a new event there, and a right-click on an event or a slot opens
// its menu.
Item {
    id: view

    property date startDate: new Date()
    property int days: 7
    property real hourHeight: 40
    property int scrollToHour: 8
    // The event the card is open for, drawn raised.
    property string openKey

    signal eventClicked(string key, Item block)
    signal newAt(date start, bool allDay)
    signal dayClicked(date day)
    // `from` is the event when its menu was asked for with the keyboard.
    signal eventMenu(string key, var from)
    signal slotMenu(date start, bool allDay)

    readonly property real gutter: 58
    readonly property real columnWidth: Math.max(40, (width - gutter - 1) / days)
    readonly property date endDate: dayAfter(startDate, days)

    function dayAfter(d, n) {
        return new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
    }
    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function scrollToStart() {
        hours.contentY = Math.max(0, Math.min(hours.contentHeight - hours.height, scrollToHour * hourHeight - 8));
    }
    onStartDateChanged: Qt.callLater(scrollToStart)
    Component.onCompleted: Qt.callLater(scrollToStart)

    // Ticks once a minute for the line at the time now.
    property date now: new Date()
    Timer {
        interval: 30000
        running: true
        repeat: true
        onTriggered: view.now = new Date()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The dates.
        Row {
            Layout.fillWidth: true
            Layout.leftMargin: view.gutter
            height: 58

            Repeater {
                model: view.days
                delegate: Item {
                    id: head
                    required property int index
                    readonly property date day: view.dayAfter(view.startDate, index)
                    readonly property bool today: view.sameDay(day, view.now)
                    width: view.columnWidth
                    height: 58

                    Rectangle {
                        visible: head.today
                        x: 0
                        y: 0
                        width: parent.width - 1
                        height: 2
                        color: UI.Style.accent
                    }
                    Column {
                        x: 12
                        y: 8
                        spacing: 0
                        UI.Label {
                            text: head.day.getDate()
                            size: 20
                            font.weight: UI.Style.weightSemiBold
                            color: UI.Style.ink
                        }
                        UI.Label {
                            text: Formats.names().toString(head.day, "dddd")
                            size: 12
                            color: head.today ? UI.Style.ink : UI.Style.inkMuted
                            elide: Text.ElideRight
                            width: view.columnWidth - 16
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: UI.Style.borderSubtle
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: view.dayClicked(head.day)
                    }
                }
            }
        }

        // Events that fill whole days.
        Item {
            id: band
            Layout.fillWidth: true
            readonly property var items: CalendarEngine.revision >= 0 ? CalendarEngine.allDayBetween(view.startDate, view.endDate) : []
            readonly property int rows: items.reduce((n, e) => Math.max(n, e.row + 1), 0)
            implicitHeight: Math.max(26, rows * 22 + 6)

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: UI.Style.borderSubtle
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: UI.Style.borderSubtle
            }
            UI.Label {
                x: 0
                width: view.gutter - 10
                y: 6
                horizontalAlignment: Text.AlignRight
                text: qsTr("all-day")
                size: 11
                color: UI.Style.inkSubtle
            }
            Repeater {
                model: view.days
                delegate: Item {
                    required property int index
                    x: view.gutter + index * view.columnWidth
                    width: view.columnWidth
                    height: band.height
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: UI.Style.borderSubtle
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (mouse) {
                            if (mouse.button === Qt.RightButton) {
                                view.slotMenu(view.dayAfter(view.startDate, index), true);
                            }
                        }
                        onDoubleClicked: function (mouse) {
                            if (mouse.button === Qt.LeftButton) {
                                view.newAt(view.dayAfter(view.startDate, index), true);
                            }
                        }
                    }
                }
            }
            Repeater {
                model: band.items
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    x: view.gutter + modelData.firstColumn * view.columnWidth + 3
                    y: 4 + modelData.row * 22
                    width: (modelData.lastColumn - modelData.firstColumn + 1) * view.columnWidth - 7
                    height: 19
                    radius: 3
                    color: Qt.rgba(Qt.color(modelData.color).r, Qt.color(modelData.color).g, Qt.color(modelData.color).b, view.openKey === modelData.key ? 0.55 : 0.38)
                    border.width: view.openKey === modelData.key ? 1 : 0
                    border.color: modelData.color
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("%1, all day").arg(modelData.title)
                    Keys.onPressed: function (event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                            view.eventClicked(modelData.key, chip);
                        } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                            view.eventMenu(modelData.key, chip);
                        } else {
                            return;
                        }
                        event.accepted = true;
                    }

                    Rectangle {
                        width: 3
                        height: parent.height
                        radius: 1
                        color: chip.modelData.color
                    }
                    UI.FocusRing {
                        visible: chip.activeFocus
                        anchors.margins: 0
                        targetRadius: chip.radius - 4
                    }
                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 9
                        width: parent.width - 14
                        text: chip.modelData.title
                        size: 12
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (mouse) {
                            if (mouse.button === Qt.RightButton) {
                                view.eventMenu(chip.modelData.key, null);
                            } else {
                                view.eventClicked(chip.modelData.key, chip);
                            }
                        }
                    }
                }
            }
        }

        // The hours.
        Flickable {
            id: hours
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: 24 * view.hourHeight + 1
            boundsBehavior: Flickable.StopAtBounds
            QQC.ScrollBar.vertical: UI.ScrollBar {}

            Item {
                width: view.width
                height: hours.contentHeight

                // Hour lines and times.
                Repeater {
                    model: 24
                    delegate: Item {
                        required property int index
                        y: index * view.hourHeight
                        width: view.width
                        height: view.hourHeight
                        UI.Label {
                            visible: index > 0
                            width: view.gutter - 10
                            y: -7
                            horizontalAlignment: Text.AlignRight
                            text: Formats.hour(index)
                            size: 11
                            color: UI.Style.inkSubtle
                        }
                        Rectangle {
                            x: view.gutter
                            width: parent.width - view.gutter
                            height: 1
                            color: UI.Style.borderSubtle
                        }
                        Rectangle {
                            x: view.gutter
                            y: view.hourHeight / 2
                            width: parent.width - view.gutter
                            height: 1
                            color: UI.Style.borderSubtle
                            opacity: 0.4
                        }
                    }
                }

                // A column per day, with its events.
                Repeater {
                    model: view.days
                    delegate: Item {
                        id: column
                        required property int index
                        readonly property date day: view.dayAfter(view.startDate, index)
                        readonly property var events: CalendarEngine.revision >= 0 ? CalendarEngine.timedOn(day) : []
                        x: view.gutter + index * view.columnWidth
                        width: view.columnWidth
                        height: parent.height

                        Rectangle {
                            anchors.right: parent.right
                            width: 1
                            height: parent.height
                            color: UI.Style.borderSubtle
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            // The half hour under the pointer.
                            function slotAt(y) {
                                const minutes = Math.floor(y / view.hourHeight * 2) * 30;
                                const d = column.day;
                                return new Date(d.getFullYear(), d.getMonth(), d.getDate(), Math.floor(minutes / 60), minutes % 60);
                            }
                            onClicked: function (mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    view.slotMenu(slotAt(mouse.y), false);
                                }
                            }
                            onDoubleClicked: function (mouse) {
                                if (mouse.button === Qt.LeftButton) {
                                    view.newAt(slotAt(mouse.y), false);
                                }
                            }
                        }

                        Repeater {
                            model: column.events
                            delegate: Rectangle {
                                id: block
                                required property var modelData
                                readonly property real slot: (column.width - 6) / modelData.columns
                                readonly property bool open: view.openKey === modelData.key
                                property real dragDx: 0
                                property real dragDy: 0
                                x: 3 + modelData.column * slot + dragDx
                                y: modelData.startMinute / 60 * view.hourHeight + 1 + dragDy
                                z: drag.active || open ? 5 : 1
                                width: slot - 3
                                height: Math.max(18, (modelData.endMinute - modelData.startMinute) / 60 * view.hourHeight - 3)
                                radius: 4
                                color: Qt.rgba(Qt.color(modelData.color).r, Qt.color(modelData.color).g, Qt.color(modelData.color).b, open ? 0.55 : 0.36)
                                border.width: open ? 1.5 : 0
                                border.color: Qt.lighter(modelData.color, 1.2)
                                opacity: modelData.invited && modelData.myStatus === "notGoing" ? 0.5 : 1
                                clip: true
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("%1, %2").arg(modelData.title).arg(modelData.rangeText)
                                Keys.onPressed: function (event) {
                                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                        view.eventClicked(modelData.key, block);
                                    } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                        view.eventMenu(modelData.key, block);
                                    } else {
                                        return;
                                    }
                                    event.accepted = true;
                                }
                                // An event reached with Tab may be hours away
                                // from the ones on screen.
                                onActiveFocusChanged: {
                                    if (!activeFocus) {
                                        return;
                                    }
                                    if (y < hours.contentY) {
                                        hours.contentY = Math.max(0, y - 8);
                                    } else if (y + height > hours.contentY + hours.height) {
                                        hours.contentY = Math.max(0, Math.min(hours.contentHeight - hours.height, y + height - hours.height + 8));
                                    }
                                }

                                Rectangle {
                                    width: 3
                                    height: parent.height
                                    color: block.modelData.color
                                }
                                // Inside the block, which clips what it draws.
                                UI.FocusRing {
                                    visible: block.activeFocus
                                    anchors.margins: 0
                                    targetRadius: block.radius - 4
                                }
                                Column {
                                    x: 9
                                    y: 3
                                    width: parent.width - 13
                                    spacing: 0
                                    UI.Label {
                                        width: parent.width
                                        text: block.modelData.title
                                        size: 12
                                        font.weight: UI.Style.weightSemiBold
                                        elide: Text.ElideRight
                                    }
                                    UI.Label {
                                        visible: block.height > 34
                                        width: parent.width
                                        text: block.modelData.rangeText
                                        size: 11
                                        color: UI.Style.inkMuted
                                        elide: Text.ElideRight
                                    }
                                    UI.Label {
                                        visible: block.height > 52 && block.modelData.location.length > 0
                                        width: parent.width
                                        text: block.modelData.location
                                        size: 11
                                        color: UI.Style.inkMuted
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: drag
                                    property real startX
                                    property real startY
                                    property bool active: false
                                    anchors.fill: parent
                                    cursorShape: active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    preventStealing: true
                                    onPressed: function (mouse) {
                                        startX = mouse.x;
                                        startY = mouse.y;
                                    }
                                    onPositionChanged: function (mouse) {
                                        if (!(pressedButtons & Qt.LeftButton)) {
                                            return;
                                        }
                                        const dx = mouse.x - startX;
                                        const dy = mouse.y - startY;
                                        if (!active && Math.abs(dx) + Math.abs(dy) > 8 && !block.modelData.readOnly) {
                                            active = true;
                                        }
                                        if (active) {
                                            // Snaps to quarter hours and whole days.
                                            const quarter = view.hourHeight / 4;
                                            block.dragDy = Math.round(dy / quarter) * quarter;
                                            const dayShift = Math.round(dx / view.columnWidth);
                                            const target = Math.max(0, Math.min(view.days - 1, column.index + dayShift));
                                            block.dragDx = (target - column.index) * view.columnWidth;
                                        }
                                    }
                                    onReleased: {
                                        if (!active) {
                                            return;
                                        }
                                        active = false;
                                        const dayShift = Math.round(block.dragDx / view.columnWidth);
                                        const minutes = block.modelData.startMinute + Math.round(block.dragDy / (view.hourHeight / 4)) * 15;
                                        block.dragDx = 0;
                                        block.dragDy = 0;
                                        if (dayShift === 0 && minutes === block.modelData.startMinute) {
                                            return;
                                        }
                                        const s = block.modelData.start;
                                        const d = view.dayAfter(column.day, dayShift);
                                        CalendarEngine.moveEvent(block.modelData.key, new Date(d.getFullYear(), d.getMonth(), d.getDate(), 0, minutes));
                                    }
                                    onClicked: function (mouse) {
                                        if (active) {
                                            return;
                                        }
                                        if (mouse.button === Qt.RightButton) {
                                            view.eventMenu(block.modelData.key, null);
                                        } else {
                                            view.eventClicked(block.modelData.key, block);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // The time now.
                Item {
                    readonly property int todayIndex: Math.floor((new Date(view.now.getFullYear(), view.now.getMonth(), view.now.getDate()) - view.startDate) / 86400000 + 0.5)
                    visible: todayIndex >= 0 && todayIndex < view.days
                    y: (view.now.getHours() * 60 + view.now.getMinutes()) / 60 * view.hourHeight
                    x: view.gutter
                    width: view.width - view.gutter
                    z: 10
                    Rectangle {
                        width: parent.width
                        height: 1.5
                        color: UI.Style.accent
                        opacity: 0.85
                    }
                    Rectangle {
                        x: parent.todayIndex * view.columnWidth - 4
                        y: -3.25
                        width: 8
                        height: 8
                        radius: 4
                        color: UI.Style.accent
                    }
                }
            }
        }
    }
}
