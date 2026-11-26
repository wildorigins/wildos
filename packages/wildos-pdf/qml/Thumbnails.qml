import QtQuick
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.PDF
import WildOS.UI as UI

// The pages down the left (AppPdf.dc): 170px wide, each page 104px across
// with its number under it, the page in view outlined in the accent. A PDF
// with a contents list of its own gets Pages and Contents above them, and
// Contents shows that list instead, wider, with the part in view marked.
Rectangle {
    id: strip

    property int current: 0
    readonly property var sizes: PdfDocument.pageSizes
    readonly property var outline: PdfDocument.outline
    // Contents was the last one picked; it shows when this PDF has one.
    property bool wantContents: false
    readonly property bool contents: wantContents && outline.length > 0
    // The last line of the contents at or before the page in view.
    readonly property int currentLine: {
        let line = -1;
        for (let i = 0; i < outline.length; ++i) {
            if (outline[i].page >= 0 && outline[i].page <= current) {
                line = i;
            } else if (outline[i].page > current) {
                break;
            }
        }
        return line;
    }

    signal pageChosen(int index)
    signal spotChosen(int index, real top)
    signal contentsChosen(bool shown)

    // Room for Pages and Contents side by side, at any text size.
    readonly property real tabsWidth: tabs.visible ? tabs.x * 2 + tabs.width : 0

    onCurrentLineChanged: {
        if (contents && currentLine >= 0) {
            lines.positionViewAtIndex(currentLine, ListView.Contain);
        }
    }

    color: UI.Style.shade(0.25, 0.05)

    onCurrentChanged: list.positionViewAtIndex(current, ListView.Contain)

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: UI.Style.divider
    }

    Row {
        id: tabs
        visible: strip.outline.length > 0
        x: 10
        y: 8
        spacing: 4

        UI.SegmentButton {
            checkable: false
            text: qsTr("Pages")
            checked: !strip.contents
            onClicked: strip.contentsChosen(false)
        }
        UI.SegmentButton {
            checkable: false
            text: qsTr("Contents")
            checked: strip.contents
            onClicked: strip.contentsChosen(true)
        }
    }

    ListView {
        id: lines
        anchors.fill: parent
        anchors.rightMargin: 1
        anchors.topMargin: tabs.y + tabs.height + 4
        visible: strip.contents
        topMargin: 4
        bottomMargin: 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: strip.contents ? strip.outline : []
        currentIndex: -1
        keyNavigationEnabled: false
        activeFocusOnTab: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Contents")

        function go(line) {
            const item = strip.outline[line];
            if (item && item.page >= 0) {
                strip.spotChosen(item.page, item.top);
            }
        }
        function step(by) {
            if (count === 0) {
                return;
            }
            const from = currentIndex >= 0 ? currentIndex : strip.currentLine;
            currentIndex = Math.max(0, Math.min(count - 1, from + by));
            positionViewAtIndex(currentIndex, ListView.Contain);
        }
        Keys.onUpPressed: step(-1)
        Keys.onDownPressed: step(1)
        Keys.onReturnPressed: go(currentIndex)
        Keys.onEnterPressed: go(currentIndex)
        Keys.onSpacePressed: go(currentIndex)

        delegate: Item {
            id: line
            required property var modelData
            required property int index
            readonly property bool isCurrent: index === strip.currentLine
            readonly property bool leads: modelData.page >= 0

            width: lines.width
            height: 30
            Accessible.role: Accessible.ListItem
            Accessible.name: leads ? qsTr("%1, page %2").arg(modelData.title).arg(modelData.page + 1) : modelData.title
            Accessible.selected: isCurrent

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                radius: UI.Style.radiusXs
                // Fades in on hover and clears at once, so moving along a list
                // never lights two items. It comes before the colour so it sees
                // the change in hover first.
                Behavior on color {
                    enabled: lineMouse.containsMouse
                    ColorAnimation { duration: UI.Style.hoverDuration }
                }
                color: line.isCurrent ? UI.Style.surfaceSelected : lineMouse.containsMouse && line.leads ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear

                Rectangle {
                    anchors.fill: parent
                    visible: lines.activeFocus && lines.currentIndex === line.index
                    radius: parent.radius
                    color: "transparent"
                    border.width: 2
                    border.color: UI.Style.accent
                }
            }
            UI.Label {
                anchors.left: parent.left
                anchors.leftMargin: 16 + Math.min(line.modelData.level, 4) * 12
                anchors.right: pageNumber.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: line.modelData.title
                size: 12.5
                font.weight: line.modelData.level === 0 ? UI.Style.weightMedium : UI.Style.weightRegular
                color: line.leads ? UI.Style.ink : UI.Style.inkSubtle
            }
            UI.Label {
                id: pageNumber
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: line.leads ? line.modelData.page + 1 : ""
                size: 12
                color: UI.Style.inkSubtle
            }
            MouseArea {
                id: lineMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: line.leads ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    lines.currentIndex = line.index;
                    lines.go(line.index);
                }
            }
        }

        T.ScrollBar.vertical: UI.ScrollBar {}
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.rightMargin: 1
        anchors.topMargin: tabs.visible ? tabs.y + tabs.height + 4 : 0
        visible: !strip.contents
        topMargin: 18
        bottomMargin: 18
        spacing: 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: PdfDocument.loaded ? PdfDocument.pageCount : 0
        reuseItems: true

        delegate: Item {
            id: thumb
            required property int index
            readonly property var size: strip.sizes[index]
            readonly property bool isCurrent: index === strip.current
            readonly property real frameHeight: size && size.width > 0 ? 104 * size.height / size.width : 147

            width: list.width
            height: frameHeight + 6 + number.height

            Rectangle {
                id: frame
                anchors.horizontalCenter: parent.horizontalCenter
                width: 104
                height: thumb.frameHeight
                color: "#ffffff"
                radius: 2

                Image {
                    anchors.fill: parent
                    source: "image://page/" + PdfDocument.generation + "/" + thumb.index + "/" + Math.round(104 * Screen.devicePixelRatio) + "/" + (PdfDocument.revisions[thumb.index] ?? 0)
                    asynchronous: true
                    smooth: true
                    retainWhileLoading: true
                }

                // The board's outline, 2px out from the page.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: thumb.isCurrent ? -4 : -3
                    color: "transparent"
                    radius: 4
                    border.width: thumb.isCurrent ? 2 : 1
                    border.color: thumb.isCurrent ? UI.Style.accent : hover.containsMouse ? UI.Style.borderControl : UI.Style.borderSubtle
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Page %1").arg(thumb.index + 1)
                    onClicked: strip.pageChosen(thumb.index)
                }
            }

            UI.Label {
                id: number
                anchors.top: frame.bottom
                anchors.topMargin: 6
                anchors.horizontalCenter: parent.horizontalCenter
                text: thumb.index + 1
                size: 12
                color: thumb.isCurrent ? UI.Style.ink : UI.Style.inkMuted
            }
        }

        T.ScrollBar.vertical: UI.ScrollBar {}
    }
}
