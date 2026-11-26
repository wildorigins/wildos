/*
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    The window previews over a running app, as on Windows 11: a live picture
    of each of its windows with the title and a close button. Pointing at
    one brings that window forward full size and fades the rest (KWin's
    highlight effect, through Plasma's task manager backend); clicking it
    switches to it, a middle click closes it and a right click opens the
    window's menu. An app with more windows than fit across the screen as
    pictures gets a list of their titles instead, as on Windows. The glass
    is Plasma's popup frame, the one the clock and quick settings flyouts
    are drawn with, so it follows the theme's glass and text settings with
    them.

    The preview never takes the keyboard: the window it shows full size
    would lose it. A button with the keyboard on it opens its preview with
    the up arrow and drives it from there (keyPressed), and currentIndex
    marks the window the keys are on.
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import org.kde.ksvg as KSvg
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import WildOS.UI as UI

PlasmaCore.Dialog {
    id: preview

    required property Item tasksRoot
    // The button whose windows are shown.
    property Item task: null
    property bool shown: false

    readonly property var tasksModel: tasksRoot.tasksModel
    readonly property var backend: tasksRoot.backend
    readonly property var atm: tasksRoot.atm
    readonly property bool hovered: hover.hovered

    readonly property bool group: task !== null && task.model.IsGroupParent === true
    readonly property int windowCount: task === null || !task.isWindow ? 0 : group ? task.childCount : 1
    // The glass is sized for the last count that had windows. When the
    // button goes away under the preview (its app hides a window for a
    // moment, or the row is rebuilt), the count drops to 0 a moment before
    // the preview hides; sized from that, Plasma shrinks the window to its
    // bare frame, and the next hover maps that sliver before growing it,
    // where it can stay.
    property int sizedCount: 1
    onWindowCountChanged: {
        if (windowCount > 0) {
            sizedCount = windowCount;
        }
        if (currentIndex >= windowCount) {
            currentIndex = windowCount - 1;
        }
    }

    // The window the keyboard is on, or -1 while the pointer drives.
    property int currentIndex: -1
    onCurrentIndexChanged: {
        backend.cancelHighlightWindows();
        if (currentIndex >= 0 && visible) {
            highlight(winIdAt(currentIndex), true);
        }
    }

    // Titles and minimised states change while the preview is up; every
    // tile reads them again when the model says something changed.
    property int revision: 0

    // Between the preview and the top of the taskbar, as for the menu.
    readonly property int gap: 12
    readonly property int glassBorder: 1
    readonly property int pad: 6
    readonly property int tilePad: 6
    readonly property int tileGap: 4
    readonly property int headerHeight: 28
    readonly property int minimumPicture: 120
    readonly property int room: {
        const screen = tasksModel.screenGeometry;
        return (screen.width > 0 ? screen.width : 1920) - 32 - 2 * (glassBorder + pad);
    }
    // Past the number of pictures that fit across the screen at their
    // smallest, the windows are listed by title.
    readonly property bool listMode: sizedCount > Math.max(1, Math.floor((room + tileGap) / (minimumPicture + 2 * tilePad + tileGap)))
    readonly property int listWidth: 320
    readonly property int rowHeight: 36
    readonly property int rowGap: 2
    // As many rows as the screen has room for above the taskbar; the rest
    // scroll.
    readonly property int listRows: {
        const screen = tasksModel.screenGeometry;
        const tall = (screen.height > 0 ? screen.height : 1080) - 160 - 2 * (glassBorder + pad);
        return Math.max(1, Math.min(sizedCount, Math.floor((tall + rowGap) / (rowHeight + rowGap))));
    }

    // The design's 16:9 picture, narrowed when many windows would not fit
    // across the screen.
    readonly property int pictureWidth: {
        const n = sizedCount;
        const tile = Math.floor((room + tileGap) / n - tileGap);
        return Math.max(minimumPicture, Math.min(200, tile - 2 * tilePad));
    }
    readonly property int pictureHeight: Math.round(pictureWidth * 9 / 16)
    readonly property int tileWidth: pictureWidth + 2 * tilePad
    readonly property int tileHeight: tilePad + headerHeight + 4 + pictureHeight + tilePad

    readonly property real glassWidth: 2 * (glassBorder + pad)
        + (listMode ? listWidth : sizedCount * tileWidth + (sizedCount - 1) * tileGap)
    readonly property real glassHeight: 2 * (glassBorder + pad)
        + (listMode ? listRows * rowHeight + (listRows - 1) * rowGap : tileHeight)

    // A notification-type window is placed exactly where it is told, never
    // takes keyboard focus, and KWin's highlight effect leaves it unfaded.
    type: PlasmaCore.Dialog.Notification
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    location: PlasmaCore.Types.Floating
    hideOnWindowDeactivate: false
    visible: shown && windowCount > 0

    onVisibleChanged: {
        if (visible) {
            place();
            settle.restart();
        } else {
            backend.cancelHighlightWindows();
            // Closing the last window hides it too; the next hover waits
            // again rather than opening at once. Later, as visible is bound
            // to shown.
            Qt.callLater(() => {
                if (!preview.visible) {
                    preview.shown = false;
                }
            });
        }
    }
    onShownChanged: if (shown) place()
    onTaskChanged: {
        currentIndex = -1;
        backend.cancelHighlightWindows();
        if (visible) {
            place();
        }
    }
    onWidthChanged: if (visible) place()
    onHeightChanged: if (visible) place()

    // Centred over the button, its foot 12 px above the taskbar's window and
    // kept 8 px inside the screen; with the taskbar at the top, 12 px below.
    function place(): void {
        if (!task) {
            return;
        }
        const rect = backend.globalRect(task);
        const barTop = rect.y - task.mapToItem(null, 0, 0).y;
        const barHeight = task.Window.window ? task.Window.window.height : 0;
        const screen = tasksModel.screenGeometry;
        let left = Math.round(rect.x + rect.width / 2 - width / 2);
        if (screen.width > 0) {
            left = Math.max(screen.x + 8, Math.min(left, screen.x + screen.width - 8 - width));
        }
        x = left;
        y = Plasmoid.location === PlasmaCore.Types.TopEdge ? Math.round(barTop + barHeight + gap) : Math.round(barTop - gap - height);
    }

    // Plasma can still be sizing the window as it appears. It only resizes
    // when the content's size changes, so a window mapped at a stale size
    // would keep it on every hover of a button with the same count.
    property Timer settle: Timer {
        interval: 120
        onTriggered: {
            if (Math.round(preview.width) !== Math.round(preview.glassWidth) || Math.round(preview.height) !== Math.round(preview.glassHeight)) {
                preview.width = preview.glassWidth;
                preview.height = preview.glassHeight;
            }
            preview.place();
        }
    }

    function highlight(winId: string, on: bool): void {
        if (winId.length > 0) {
            backend.windowsHovered([winId], on);
        }
    }

    function windowIndex(at: int): var {
        return group ? tasksModel.makeModelIndex(task.index, at) : tasksModel.makeModelIndex(task.index);
    }

    function windowData(at: int, role: int): var {
        return task ? tasksModel.data(windowIndex(at), role) : undefined;
    }

    function winIdAt(at: int): string {
        const ids = windowData(at, atm.WinIdList);
        return ids && ids.length > 0 ? String(ids[0]) : "";
    }

    function activateWindow(at: int): void {
        backend.cancelHighlightWindows();
        tasksModel.requestActivate(windowIndex(at));
        tasksRoot.hidePreview();
    }

    function closeWindow(at: int): void {
        backend.cancelHighlightWindows();
        tasksModel.requestClose(windowIndex(at));
    }

    // The menu opens from the taskbar button, in the taskbar's own window,
    // and the preview makes way for it.
    function showWindowMenu(at: int, fromKeyboard: bool): void {
        const button = task;
        const winId = winIdAt(at);
        const title = String(windowData(at, Qt.DisplayRole) ?? "");
        const icon = windowData(at, Qt.DecorationRole);
        if (button && winId.length > 0) {
            tasksRoot.showWindowMenu(button, winId, title, icon, fromKeyboard);
        }
    }

    // Keys from the taskbar button the preview belongs to, while the
    // keyboard drives it. Pictures run left to right; the list runs down
    // to the taskbar, so going down past its last row leaves it.
    function keyPressed(key: int): bool {
        const back = listMode ? Qt.Key_Up : Qt.Key_Left;
        const on = listMode ? Qt.Key_Down : Qt.Key_Right;
        if (key === back) {
            currentIndex = Math.max(0, currentIndex - 1);
        } else if (key === on) {
            if (currentIndex < windowCount - 1) {
                currentIndex++;
            } else if (listMode) {
                tasksRoot.hidePreview();
            }
        } else if (key === Qt.Key_Home) {
            currentIndex = 0;
        } else if (key === Qt.Key_End) {
            currentIndex = windowCount - 1;
        } else if (key === Qt.Key_Return || key === Qt.Key_Enter || key === Qt.Key_Space) {
            activateWindow(currentIndex);
        } else if (key === Qt.Key_Delete) {
            closeWindow(currentIndex);
        } else if (key === Qt.Key_Menu) {
            showWindowMenu(currentIndex, true);
        } else if (key === Qt.Key_Escape || key === Qt.Key_Down) {
            tasksRoot.hidePreview();
        } else if (key === Qt.Key_Up) {
            return true;
        } else {
            // Moving on along the taskbar leaves the preview behind.
            if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Tab, Qt.Key_Backtab].indexOf(key) >= 0) {
                tasksRoot.hidePreview();
            }
            return false;
        }
        return true;
    }

    mainItem: Item {
        Connections {
            target: preview.tasksModel
            enabled: preview.visible
            function onDataChanged(): void {
                preview.revision++;
            }
        }

        KSvg.FrameSvgItem {
            id: frame
            imagePath: "dialogs/background"
            visible: false
        }

        width: preview.glassWidth - frame.margins.left - frame.margins.right
        height: preview.glassHeight - frame.margins.top - frame.margins.bottom

        HoverHandler {
            id: hover
        }

        Row {
            visible: !preview.listMode
            x: preview.glassBorder + preview.pad - frame.margins.left
            y: preview.glassBorder + preview.pad - frame.margins.top
            spacing: preview.tileGap

            Repeater {
                model: preview.listMode ? 0 : preview.windowCount

                delegate: Item {
                    id: tile

                    required property int index

                    width: preview.tileWidth
                    height: preview.tileHeight

                    readonly property string winId: {
                        preview.revision;
                        return preview.winIdAt(index);
                    }
                    readonly property string title: {
                        preview.revision;
                        return String(preview.windowData(index, Qt.DisplayRole) ?? "");
                    }
                    readonly property bool minimized: {
                        preview.revision;
                        return preview.windowData(index, preview.atm.IsMinimized) === true;
                    }
                    readonly property var icon: {
                        preview.revision;
                        return preview.windowData(index, Qt.DecorationRole);
                    }
                    readonly property bool current: preview.currentIndex === index

                    function activate(): void {
                        preview.activateWindow(index);
                    }

                    function close(): void {
                        preview.closeWindow(index);
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: title
                    Accessible.onPressAction: activate()

                    // The pointer takes over from the keyboard.
                    HoverHandler {
                        id: tileHover
                        onHoveredChanged: {
                            if (hovered) {
                                preview.currentIndex = -1;
                            }
                            preview.highlight(tile.winId, hovered);
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: preview.showWindowMenu(tile.index, false)
                    }

                    TapHandler {
                        id: tileTap
                        acceptedButtons: Qt.LeftButton
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: tile.activate()
                    }

                    TapHandler {
                        acceptedButtons: Qt.MiddleButton
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: tile.close()
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: UI.Style.radiusSm
                        // Fades in on hover and clears at once, so moving along a list
                        // never lights two items. It comes before the colour so it sees
                        // the change in hover first.
                        Behavior on color {
                            enabled: tileHover.hovered
                            ColorAnimation { duration: UI.Style.hoverDuration }
                        }
                        color: tileTap.pressed ? UI.Style.surfacePressed
                             : tileHover.hovered || tile.current ? UI.Style.surfaceHover
                             : UI.Style.surfaceHoverClear

                        // Inside the tile's edge: 4 px is all there is
                        // between two tiles.
                        UI.FocusRing {
                            visible: tile.current
                            anchors.margins: 0
                            targetRadius: UI.Style.radiusSm - 4
                        }
                    }

                    Kirigami.Icon {
                        id: smallIcon
                        x: preview.tilePad + 2
                        y: preview.tilePad + (preview.headerHeight - height) / 2
                        width: 16
                        height: 16
                        source: tile.icon ?? ""
                        animated: false
                    }

                    UI.Label {
                        anchors.left: smallIcon.right
                        anchors.leftMargin: 8
                        anchors.right: closeButton.left
                        anchors.rightMargin: 4
                        y: preview.tilePad
                        height: preview.headerHeight
                        size: 12
                        text: tile.title
                    }

                    // Shown on the tile under the pointer, as Windows does,
                    // so a row of previews is not a row of close buttons.
                    UI.IconButton {
                        id: closeButton
                        x: tile.width - preview.tilePad - width
                        y: preview.tilePad + (preview.headerHeight - height) / 2
                        width: 24
                        height: 24
                        iconName: "close"
                        iconSize: 12
                        tip: qsTr("Close window")
                        focusPolicy: Qt.NoFocus
                        opacity: tileHover.hovered || tile.current ? 1 : 0
                        enabled: tileHover.hovered
                        onClicked: tile.close()
                    }

                    Item {
                        id: picture
                        x: preview.tilePad
                        y: preview.tilePad + preview.headerHeight + 4
                        width: preview.pictureWidth
                        height: preview.pictureHeight

                        readonly property bool live: stream.item !== null && stream.item.hasThumbnail
                        readonly property size streamSize: stream.item ? stream.item.streamSize : Qt.size(0, 0)
                        readonly property real fit: streamSize.width > 0 && streamSize.height > 0
                            ? Math.min(width / streamSize.width, height / streamSize.height) : 1

                        // Fitted to the window's shape, with the design's
                        // corners.
                        Item {
                            id: shot
                            anchors.centerIn: parent
                            width: picture.streamSize.width > 0 ? Math.round(picture.streamSize.width * picture.fit) : picture.width
                            height: picture.streamSize.height > 0 ? Math.round(picture.streamSize.height * picture.fit) : picture.height
                            opacity: picture.live ? 1 : 0
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: corners
                            }

                            // A minimised window has nothing to stream; its
                            // icon stands in, as for an app KWin cannot capture.
                            Loader {
                                id: stream
                                anchors.fill: parent
                                active: !tile.minimized && tile.winId.length > 0
                                source: "PipeWireThumbnail.qml"
                                onLoaded: item.winId = Qt.binding(() => tile.winId)
                            }
                        }

                        Rectangle {
                            id: corners
                            width: shot.width
                            height: shot.height
                            radius: UI.Style.radiusXs
                            visible: false
                            layer.enabled: true
                        }

                        // Waits a moment for the stream before showing the
                        // icon, so a live picture does not flash it first.
                        Timer {
                            id: patience
                            interval: 300
                            running: true
                        }

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 48
                            height: 48
                            source: tile.icon ?? ""
                            visible: !picture.live && (tile.minimized || !stream.active || !patience.running)
                        }
                    }
                }
            }
        }

        ListView {
            id: titles
            visible: preview.listMode
            x: preview.glassBorder + preview.pad - frame.margins.left
            y: preview.glassBorder + preview.pad - frame.margins.top
            width: preview.listWidth
            height: preview.listRows * preview.rowHeight + (preview.listRows - 1) * preview.rowGap
            spacing: preview.rowGap
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: preview.listMode ? preview.windowCount : 0
            T.ScrollBar.vertical: UI.ScrollBar {}

            Connections {
                target: preview
                function onCurrentIndexChanged(): void {
                    if (preview.listMode && preview.currentIndex >= 0) {
                        titles.positionViewAtIndex(preview.currentIndex, ListView.Contain);
                    }
                }
            }

            delegate: Item {
                id: line

                required property int index

                width: titles.width
                height: preview.rowHeight

                readonly property string winId: {
                    preview.revision;
                    return preview.winIdAt(index);
                }
                readonly property string title: {
                    preview.revision;
                    return String(preview.windowData(index, Qt.DisplayRole) ?? "");
                }
                readonly property var icon: {
                    preview.revision;
                    return preview.windowData(index, Qt.DecorationRole);
                }
                readonly property bool current: preview.currentIndex === index

                Accessible.role: Accessible.Button
                Accessible.name: title
                Accessible.onPressAction: preview.activateWindow(index)

                // The pointer takes over from the keyboard.
                HoverHandler {
                    id: lineHover
                    onHoveredChanged: {
                        if (hovered) {
                            preview.currentIndex = -1;
                        }
                        preview.highlight(line.winId, hovered);
                    }
                }

                TapHandler {
                    id: lineTap
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: preview.activateWindow(line.index)
                }

                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: preview.closeWindow(line.index)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: preview.showWindowMenu(line.index, false)
                }

                Rectangle {
                    anchors.fill: parent
                    radius: UI.Style.radiusSm
                    // Fades in on hover and clears at once, so moving along a list
                    // never lights two items. It comes before the colour so it sees
                    // the change in hover first.
                    Behavior on color {
                        enabled: lineHover.hovered
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                    color: lineTap.pressed ? UI.Style.surfacePressed
                         : lineHover.hovered || line.current ? UI.Style.surfaceHover
                         : UI.Style.surfaceHoverClear

                    // Inside the row's edge, where the list does not clip it.
                    UI.FocusRing {
                        visible: line.current
                        anchors.margins: 0
                        targetRadius: UI.Style.radiusSm - 4
                    }
                }

                Kirigami.Icon {
                    id: lineIcon
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    source: line.icon ?? ""
                    animated: false
                }

                UI.Label {
                    anchors.left: lineIcon.right
                    anchors.leftMargin: 10
                    anchors.right: lineClose.left
                    anchors.rightMargin: 4
                    height: parent.height
                    text: line.title
                }

                UI.IconButton {
                    id: lineClose
                    x: line.width - 6 - width
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    iconName: "close"
                    iconSize: 12
                    tip: qsTr("Close window")
                    focusPolicy: Qt.NoFocus
                    opacity: lineHover.hovered || line.current ? 1 : 0
                    enabled: lineHover.hovered
                    onClicked: preview.closeWindow(line.index)
                }
            }
        }
    }
}
