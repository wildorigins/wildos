pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import WildOS.Files
import WildOS.UI as UI

// The 42px title bar with the tabs in it, as in Windows 11 File Explorer:
// a tab per folder, + for another, the window buttons on the right, and
// the empty part for dragging the window.
Item {
    id: bar

    required property var view
    property var frame: null
    readonly property bool maximised: titleBar.maximised

    height: Metrics.titleHeight

    readonly property real activeX: tabs.x + titleBar.contentX + bar.view.currentIndex * (tabs.tabWidth + tabs.spacing)

    // One fill for the bar with the current tab cut out of it. The tab's own
    // sheet is the navigation row's fill, so tab and row read as one piece
    // and no part of the bar is filled twice.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: Metrics.titleBar
            strokeColor: "transparent"
            strokeWidth: 0
            fillRule: ShapePath.OddEvenFill

            PathSvg {
                path: {
                    const w = bar.width;
                    const h = bar.height;
                    const r = bar.maximised ? 0 : UI.Style.windowRadius - 1;
                    let d = "M0 " + h + "V" + r;
                    if (r > 0) {
                        d += "A" + r + " " + r + " 0 0 1 " + r + " 0";
                    }
                    d += "H" + (w - r);
                    if (r > 0) {
                        d += "A" + r + " " + r + " 0 0 1 " + w + " " + r;
                    }
                    d += "V" + h + "Z";
                    if (bar.view.tabs.length > 0) {
                        const x = bar.activeX;
                        const t = h - tabs.height;
                        const k = 10;
                        d += "M" + x + " " + h + "V" + (t + k)
                            + "A" + k + " " + k + " 0 0 1 " + (x + k) + " " + t
                            + "H" + (x + tabs.tabWidth - k)
                            + "A" + k + " " + k + " 0 0 1 " + (x + tabs.tabWidth) + " " + (t + k)
                            + "V" + h + "Z";
                    }
                    return d;
                }
            }
        }
    }

    UI.TitleBar {
        id: titleBar

        // Where the middle of the bar starts, for the cut-out above.
        readonly property real contentX: 16

        anchors.fill: parent
        frame: bar.frame
        onMiddleClicked: bar.view.newTab()

        UI.TabBar {
            id: tabs

            // File Explorer's first tab sits 8px from the window's edge,
            // closer than a title would.
            x: -8
            anchors.bottom: parent.bottom
            width: parent.width + 8
            room: bar.width - 190
            currentIndex: bar.view.currentIndex
            onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex !== bar.view.currentIndex) bar.view.currentIndex = currentIndex
            onNewTabRequested: bar.view.newTab()
            reorderable: true
            onMoved: (from, to) => bar.view.moveTab(from, to)
            onContextMenuRequested: (index, x, y) => bar.view.openTabMenu(index, tabs, x, y)

            Repeater {
                model: bar.view.tabs

                delegate: UI.TabButton {
                    required property var modelData
                    required property int index

                    text: modelData.title
                    themeIcon: modelData.iconName
                    // The container only checks a tab that exists when its
                    // index is set; tabs come and go with the folders.
                    checked: index === bar.view.currentIndex
                    onPressed: bar.view.currentIndex = index
                    onCloseRequested: bar.view.closeTab(index)
                }
            }
        }

        // Between two flat tabs, a short rule, as on Windows 11. It goes
        // away next to the current or a hovered tab.
        Repeater {
            model: Math.max(0, bar.view.tabs.length - 1)

            delegate: Rectangle {
                required property int index
                readonly property var leftTab: tabs.itemAt(index)
                readonly property var rightTab: tabs.itemAt(index + 1)
                visible: leftTab && rightTab && !leftTab.checked && !leftTab.hovered && !rightTab.checked && !rightTab.hovered
                x: tabs.x + (index + 1) * (tabs.tabWidth + tabs.spacing) - tabs.spacing / 2 - 0.5
                y: tabs.y + (tabs.height - 5 - height) / 2 + 3
                width: 1
                height: 16
                color: Metrics.separator
            }
        }
    }
}
