pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI
import WildOS.UI as UI

// Pinned apps and recent documents, the page Start opens on, and the Office
// apps Windows mode is getting ready while it sets itself up (board 9.13).
FocusScope {
    id: home

    required property var launcher

    property alias allAppsButton: pinnedHeader.button
    property alias pinnedGrid: pinnedGrid
    property alias moreButton: recentHeader.button
    property alias recentGrid: recentGrid
    readonly property bool hasRecents: launcher.recentModel.count > 0
    // "More" swaps the pinned apps for the whole recent list, as Windows'
    // Recommended page does; it is only offered when there is more to see.
    property bool showAllRecents: false
    readonly property bool moreRecents: launcher.recentModel.count > 6
    // The page's height with the pinned apps showing. "More" swaps them for
    // a list of another length, and Start keeps the height it had.
    property real restHeight: 0
    onImplicitHeightChanged: {
        if (!showAllRecents) {
            restHeight = implicitHeight;
        }
    }
    Component.onCompleted: restHeight = implicitHeight

    signal allAppsRequested()
    signal moreRequested()
    signal leaveUp()
    signal leaveDown()
    signal menuRequested(var model, int index, Item item, real x, real y, bool pinned)

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: Style.sectionGap

        Column {
            width: parent.width
            spacing: 8
            visible: !home.showAllRecents

            SectionHeader {
                id: pinnedHeader
                width: parent.width
                title: qsTr("Pinned")
                buttonText: qsTr("All apps")
                onActivated: home.allAppsRequested()
            }

            Item {
                width: parent.width
                height: pinnedGrid.implicitHeight

                NavGrid {
                    id: pinnedGrid
                    anchors.fill: parent
                    anchors.rightMargin: -gap
                    columns: 6
                    // The design's 92 px holds a 42 px icon and one line of
                    // 12 px text; larger text makes the line taller.
                    rowHeight: 92 + UI.Style.px(12) - 12
                    model: home.launcher.rootModel.favoritesModel
                    delegate: AppTile {
                        grid: pinnedGrid
                    }
                    reorder: true
                    onMoved: (from, to) => model.moveRow(from, to)
                    onActivated: index => {
                        if (model.trigger(index, "", null)) {
                            home.launcher.close();
                        }
                    }
                    onMenuRequested: (index, item, x, y) => home.menuRequested(model, index, item, x, y, true)
                    onExitUp: home.leaveUp()
                    onExitDown: home.hasRecents ? recentGrid.enter(false) : home.leaveDown()
                }

                UI.Label {
                    anchors.centerIn: parent
                    visible: pinnedGrid.count === 0
                    size: 12
                    color: UI.Style.inkMuted
                    text: qsTr("Pin apps from All apps to keep them here")
                }
            }
        }

        WindowsSetupSection {
            width: parent.width
            launcher: home.launcher
        }

        Column {
            width: parent.width
            spacing: 8
            visible: home.hasRecents

            SectionHeader {
                id: recentHeader
                width: parent.width
                title: home.showAllRecents ? qsTr("Recommended") : qsTr("Pick up where you left off")
                buttonText: home.showAllRecents ? qsTr("Back") : home.moreRecents ? qsTr("More") : ""
                backward: home.showAllRecents
                onActivated: home.moreRequested()
            }

            NavGrid {
                id: recentGrid
                width: parent.width + gap
                height: implicitHeight
                columns: 2
                limit: home.showAllRecents ? 0 : 6
                maxRows: home.showAllRecents ? 6 : 3
                rowHeight: 58 + UI.Style.px(13) + UI.Style.px(12) - 25
                model: home.launcher.recentModel
                delegate: RecentRow {
                    grid: recentGrid
                }
                onActivated: index => {
                    if (model.trigger(index, "", null)) {
                        home.launcher.close();
                    }
                }
                onMenuRequested: (index, item, x, y) => home.menuRequested(model, index, item, x, y, false)
                onExitUp: home.showAllRecents || pinnedGrid.count === 0 ? home.leaveUp() : pinnedGrid.enter(true)
                onExitDown: home.leaveDown()
            }
        }
    }
}
