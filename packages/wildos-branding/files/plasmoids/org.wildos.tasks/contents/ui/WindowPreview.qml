/*
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    The window previews over a running app, as on Windows 11: a live picture
    of each of its windows with the title and a close button. Pointing at
    one brings that window forward full size and fades the rest (KWin's
    highlight effect, through Plasma's task manager backend); clicking it
    switches to it and a middle click closes it. The glass is Plasma's
    popup frame, the one the clock and quick settings flyouts are drawn
    with, so it follows the theme's glass and text settings with them.
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import org.kde.ksvg as KSvg
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
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
    onWindowCountChanged: if (windowCount > 0) sizedCount = windowCount

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
    // The design's 16:9 picture, narrowed when many windows would not fit
    // across the screen.
    readonly property int pictureWidth: {
        const screen = tasksModel.screenGeometry;
        const room = (screen.width > 0 ? screen.width : 1920) - 32 - 2 * (glassBorder + pad);
        const n = sizedCount;
        const tile = Math.floor((room + tileGap) / n - tileGap);
        return Math.max(120, Math.min(200, tile - 2 * tilePad));
    }
    readonly property int pictureHeight: Math.round(pictureWidth * 9 / 16)
    readonly property int tileWidth: pictureWidth + 2 * tilePad
    readonly property int tileHeight: tilePad + headerHeight + 4 + pictureHeight + tilePad

    readonly property real glassWidth: 2 * (glassBorder + pad) + sizedCount * tileWidth + (sizedCount - 1) * tileGap
    readonly property real glassHeight: 2 * (glassBorder + pad) + tileHeight

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
        backend.cancelHighlightWindows();
        if (visible) {
            place();
        }
    }
    onWidthChanged: if (visible) place()
    onHeightChanged: if (visible) place()

    // Centred over the button, its foot 12 px above the taskbar's window and
    // kept 8 px inside the screen.
    function place(): void {
        if (!task) {
            return;
        }
        const rect = backend.globalRect(task);
        const barTop = rect.y - task.mapToItem(null, 0, 0).y;
        const screen = tasksModel.screenGeometry;
        let left = Math.round(rect.x + rect.width / 2 - width / 2);
        if (screen.width > 0) {
            left = Math.max(screen.x + 8, Math.min(left, screen.x + screen.width - 8 - width));
        }
        x = left;
        y = Math.round(barTop - gap - height);
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
            x: preview.glassBorder + preview.pad - frame.margins.left
            y: preview.glassBorder + preview.pad - frame.margins.top
            spacing: preview.tileGap

            Repeater {
                model: preview.windowCount

                delegate: Item {
                    id: tile

                    required property int index

                    width: preview.tileWidth
                    height: preview.tileHeight

                    function modelIndex(): var {
                        const at = preview.task.index;
                        return preview.group ? preview.tasksModel.makeModelIndex(at, index) : preview.tasksModel.makeModelIndex(at);
                    }

                    function get(role: int): var {
                        return preview.task ? preview.tasksModel.data(modelIndex(), role) : undefined;
                    }

                    readonly property string winId: {
                        preview.revision;
                        const ids = get(preview.atm.WinIdList);
                        return ids && ids.length > 0 ? String(ids[0]) : "";
                    }
                    readonly property string title: {
                        preview.revision;
                        return String(get(Qt.DisplayRole) ?? "");
                    }
                    readonly property bool minimized: {
                        preview.revision;
                        return get(preview.atm.IsMinimized) === true;
                    }
                    readonly property var icon: {
                        preview.revision;
                        return get(Qt.DecorationRole);
                    }

                    function activate(): void {
                        preview.backend.cancelHighlightWindows();
                        preview.tasksModel.requestActivate(modelIndex());
                        preview.tasksRoot.hidePreview();
                    }

                    function close(): void {
                        preview.backend.cancelHighlightWindows();
                        preview.tasksModel.requestClose(modelIndex());
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: title
                    Accessible.onPressAction: activate()

                    HoverHandler {
                        id: tileHover
                        onHoveredChanged: preview.highlight(tile.winId, hovered)
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
                        color: tileTap.pressed ? UI.Style.surfacePressed
                             : tileHover.hovered ? UI.Style.surfaceHover
                             : "transparent"
                        Behavior on color {
                            ColorAnimation { duration: UI.Style.hoverDuration }
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
                        tip: "Close window"
                        focusPolicy: Qt.NoFocus
                        opacity: tileHover.hovered ? 1 : 0
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
    }
}
