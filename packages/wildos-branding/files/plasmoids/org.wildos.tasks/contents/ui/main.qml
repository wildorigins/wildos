/*
    SPDX-FileCopyrightText: 2012-2016 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    Wild OS fork of the Plasma 6.3.6 task manager (org.kde.plasma.taskmanager).
    The task models and backend are Plasma's own C++ (org.kde.taskmanager and
    org.kde.plasma.private.taskmanager); the buttons, launch feedback, the
    right-click menu and the window previews are redrawn to the Wild OS
    Taskbar design, and the options Wild OS never shows (text labels,
    stripes, tooltips, activities) are gone.
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.mpris as Mpris

import org.kde.taskmanager as TaskManager
import org.kde.plasma.private.taskmanager as TaskManagerApplet

PlasmoidItem {
    id: tasks

    readonly property bool shouldShrinkToZero: tasksModel.count === 0 && orphanLaunches.length === 0

    // The task being dragged to a new place, if any.
    property Item dragSource: null
    property Item menuTask: null
    readonly property Item dropHovered: dropArea.hoveredItem
    readonly property var atm: TaskManager.AbstractTasksModel
    property alias taskRow: taskRow

    // Apps that were just started, by launcher URL and app id, with the time.
    // Plasma can rebuild a pinned button while its app starts, and the new
    // button uses this to keep showing the starting line.
    property var launches: ({})
    // Buttons made while the plasmoid first loads are not launches.
    property bool loaded: false

    preferredRepresentation: fullRepresentation

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.constraintHints: Plasmoid.CanFillArea

    Layout.fillWidth: false
    Layout.fillHeight: true
    Layout.minimumWidth: shouldShrinkToZero ? 1 : taskRow.implicitWidth
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: Style.button

    function noteLaunch(keys: var): void {
        const now = Date.now();
        const next = Object.assign({}, launches);
        keys.forEach(key => {
            if (key) {
                next[key] = now;
            }
        });
        launches = next;
        launchExpiry.restart();
    }

    function launchedRecently(keys: var): bool {
        const now = Date.now();
        return keys.some(key => key && launches[key] !== undefined && now - launches[key] < 15000);
    }

    // A window of the app turned up, so its start is over. Without this, a
    // pin that Plasma brings back when that window closes would take the
    // recent launch for one still on its way and pulse.
    function windowOpened(keys: var): void {
        const ids = keys.filter(key => key);
        if (ids.some(key => launches[key] !== undefined)) {
            const next = Object.assign({}, launches);
            ids.forEach(key => delete next[key]);
            launches = next;
        }
        if (ids.some(key => daemonIds[key] === true && settled[key] !== true)) {
            const next = Object.assign({}, settled);
            ids.forEach(key => {
                if (daemonIds[key] === true) {
                    next[key] = true;
                }
            });
            settled = next;
        }
    }

    // What wildos-launchd is still opening, by lower-case desktop id. KWin's
    // launch feedback (Plasma's IsStartup) stops the moment another window
    // is activated, while the daemon keeps waiting for the app's window. The watcher lives in
    // wildos-shell's plugin; without it the taskbar runs on KWin alone.
    readonly property QtObject launchWatcher: {
        const component = Qt.createComponent("org.wildos.shell", "LaunchWatcher");
        if (component.status !== Component.Ready) {
            return null;
        }
        const watcher = component.createObject(tasks);
        component.destroy();
        return watcher;
    }
    readonly property var allDaemonLaunches: launchWatcher ? launchWatcher.launches : []
    readonly property var daemonIds: {
        const ids = {};
        allDaemonLaunches.forEach(launch => ids[launch.desktopId] = true);
        return ids;
    }
    // Launches the daemon still lists although the app has shown a window
    // since, under an id it did not recognise. They never count as starting.
    property var settled: ({})

    // The daemon ended a launch it was following: the window is there, under
    // whatever button it went to, or the app gave up. The button's own wait
    // ends with it.
    signal launchFinished(key: string)
    property var followed: ({})
    onAllDaemonLaunchesChanged: {
        const listed = {};
        const now = {};
        allDaemonLaunches.forEach(launch => {
            listed[launch.desktopId] = true;
            if (launch.confirmed) {
                now[launch.desktopId] = true;
            }
        });
        const ended = Object.keys(followed).filter(key => listed[key] !== true);
        followed = now;
        if (ended.length > 0) {
            const next = Object.assign({}, launches);
            Object.keys(next).forEach(key => {
                if (ended.indexOf(desktopIdOf(key)) >= 0) {
                    delete next[key];
                }
            });
            launches = next;
            ended.forEach(key => launchFinished(key));
        }
    }

    // A launcher URL or app id as a lower-case desktop id, as the daemon
    // names its launches.
    function desktopIdOf(key: string): string {
        return String(key).replace(/^.*[\/:]/, "").replace(/\.desktop$/, "").toLowerCase();
    }

    onDaemonIdsChanged: {
        const next = {};
        let dropped = false;
        Object.keys(settled).forEach(key => {
            if (daemonIds[key] === true) {
                next[key] = true;
            } else {
                dropped = true;
            }
        });
        if (dropped) {
            settled = next;
        }
    }
    readonly property var daemonLaunches: allDaemonLaunches.filter(launch => settled[launch.desktopId] !== true)
    readonly property var daemonStarting: {
        const ids = {};
        daemonLaunches.forEach(launch => ids[launch.desktopId] = true);
        return ids;
    }

    // Launches no button stands for any more; they get a starting button of
    // their own at the end of the row.
    readonly property var orphanLaunches: {
        const shown = {};
        for (let i = 0; i < taskRepeater.count; ++i) {
            const task = taskRepeater.itemAt(i) as Task;
            if (task) {
                shown[task.desktopKey] = true;
            }
        }
        return daemonLaunches.filter(launch => !shown[launch.desktopId]);
    }

    Timer {
        id: launchExpiry
        interval: 15500
        onTriggered: tasks.launches = ({})
    }

    onDragSourceChanged: {
        if (dragSource === null) {
            tasksModel.syncLaunchers();
        } else {
            hidePreview();
        }
    }

    // Window previews: they open after a short rest on a running app, follow
    // the pointer from app to app while up, and close a moment after it has
    // left both the buttons and the preview, so crossing the gap between
    // them does not close it.
    WindowPreview {
        id: windowPreview
        tasksRoot: tasks

        onHoveredChanged: {
            if (hovered) {
                previewCloseTimer.stop();
            } else {
                previewCloseTimer.restart();
            }
        }
    }

    property Item previewPending: null
    // A button just clicked keeps its preview away until the pointer leaves
    // it, as on Windows.
    property Item previewMuted: null

    function taskHovered(task: Item, hovered: bool): void {
        if (!hovered) {
            if (previewMuted === task) {
                previewMuted = null;
            }
            if (previewPending === task) {
                previewPending = null;
                previewOpenTimer.stop();
            }
            previewCloseTimer.restart();
            return;
        }
        const wanted = (task as Task).isWindow && previewMuted !== task && menuTask === null && dragSource === null;
        if (!wanted) {
            previewOpenTimer.stop();
            previewCloseTimer.restart();
            return;
        }
        previewCloseTimer.stop();
        if (windowPreview.visible) {
            windowPreview.task = task;
            return;
        }
        previewPending = task;
        previewOpenTimer.restart();
    }

    function hidePreview(): void {
        previewOpenTimer.stop();
        previewCloseTimer.stop();
        previewPending = null;
        windowPreview.shown = false;
    }

    function mutePreview(task: Item): void {
        previewMuted = task;
        hidePreview();
    }

    Timer {
        id: previewOpenTimer
        interval: 400
        onTriggered: {
            const task = tasks.previewPending as Task;
            tasks.previewPending = null;
            if (task && task.hovered && task.isWindow) {
                windowPreview.task = task;
                windowPreview.shown = true;
            }
        }
    }

    Timer {
        id: previewCloseTimer
        interval: 300
        onTriggered: {
            const task = windowPreview.task as Task;
            if (!windowPreview.hovered && !(task && task.hovered)) {
                tasks.hidePreview();
            }
        }
    }

    readonly property TaskManager.TasksModel tasksModel: TaskManager.TasksModel {
        id: tasksModel

        virtualDesktop: virtualDesktopInfo.currentDesktop
        screenGeometry: Plasmoid.containment.screenGeometry
        activity: activityInfo.currentActivity

        filterByVirtualDesktop: Plasmoid.configuration.showOnlyCurrentDesktop
        filterByScreen: false
        filterByActivity: true

        // As the stock icons-only task manager: a pinned app's button turns
        // into its window's button in the same place, and windows of one app
        // share a button.
        hideActivatedLaunchers: true
        sortMode: TaskManager.TasksModel.SortManual
        launchInPlace: true
        separateLaunchers: true
        groupMode: TaskManager.TasksModel.GroupApplications
        groupInline: false
        groupingWindowTasksThreshold: -1

        onLauncherListChanged: {
            Plasmoid.configuration.launchers = launcherList;
        }
        onGroupingAppIdBlacklistChanged: {
            Plasmoid.configuration.groupingAppIdBlacklist = groupingAppIdBlacklist;
        }
        onGroupingLauncherUrlBlacklistChanged: {
            Plasmoid.configuration.groupingLauncherUrlBlacklist = groupingLauncherUrlBlacklist;
        }

        Component.onCompleted: {
            launcherList = tasks.mendLaunchers(Plasmoid.configuration.launchers);
            groupingAppIdBlacklist = Plasmoid.configuration.groupingAppIdBlacklist;
            groupingLauncherUrlBlacklist = Plasmoid.configuration.groupingLauncherUrlBlacklist;

            // Only hook up the view once the churn above is done.
            taskRepeater.model = tasksModel;
            Qt.callLater(() => tasks.loaded = true);
        }
    }

    readonly property TaskManagerApplet.Backend backend: TaskManagerApplet.Backend {
        id: backend
        // Only the window previews ask for it: pointing at one shows that
        // window full size and fades the others.
        highlightWindows: true

        onAddLauncher: url => tasks.addLauncher(url)
    }

    TaskManager.VirtualDesktopInfo {
        id: virtualDesktopInfo
    }

    TaskManager.ActivityInfo {
        id: activityInfo
    }

    readonly property Mpris.Mpris2Model mpris2Source: Mpris.Mpris2Model {}

    // Per-app sound streams, for the speaker on apps playing sound. Loaded
    // on its own so a system without the sound plugin still gets a taskbar.
    readonly property QtObject pulseAudio: pulseAudioLoader.item
    Loader {
        id: pulseAudioLoader
        active: Plasmoid.configuration.indicateAudioStreams
        Component.onCompleted: setSource("PulseAudio.qml", { backend: tasks.backend })
    }

    Connections {
        target: Plasmoid.configuration

        function onLaunchersChanged(): void {
            tasksModel.launcherList = tasks.mendLaunchers(Plasmoid.configuration.launchers);
        }
        function onGroupingAppIdBlacklistChanged(): void {
            tasksModel.groupingAppIdBlacklist = Plasmoid.configuration.groupingAppIdBlacklist;
        }
        function onGroupingLauncherUrlBlacklistChanged(): void {
            tasksModel.groupingLauncherUrlBlacklist = Plasmoid.configuration.groupingLauncherUrlBlacklist;
        }
    }

    // Window geometry for KWin's minimise animation, which flies a window
    // down into its taskbar button.
    function publishIconGeometries(): void {
        for (let i = 0; i < taskRepeater.count; ++i) {
            const task = taskRepeater.itemAt(i) as Task;
            if (task && !task.model.IsLauncher && !task.model.IsStartup) {
                tasksModel.requestPublishDelegateGeometry(tasksModel.makeModelIndex(task.index),
                    backend.globalRect(task), task);
            }
        }
    }

    Timer {
        id: iconGeometryTimer
        interval: 500
        onTriggered: tasks.publishIconGeometries()
    }

    Connections {
        target: Plasmoid
        function onLocationChanged(): void {
            iconGeometryTimer.restart();
        }
    }

    Connections {
        target: Plasmoid.containment
        function onScreenGeometryChanged(): void {
            iconGeometryTimer.restart();
        }
    }

    // Carries the drag image and data while a button is dragged.
    Item {
        id: dragHelper

        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
        Drag.onDragFinished: dropAction => {
            tasks.dragSource = null;
        }
    }
    property alias dragHelper: dragHelper

    DropArea {
        id: dropArea
        anchors.fill: parent

        property Item hoveredItem: null

        onEntered: event => {
            if (event.formats.indexOf("text/x-plasmoidservicename") >= 0) {
                event.accepted = false;
            }
        }

        onPositionChanged: event => {
            const point = mapToItem(taskRow, event.x, event.y);
            const above = taskRow.childAt(point.x, Math.min(Math.max(point.y, 1), taskRow.height - 1)) as Task;
            if (!above) {
                hoveredItem = null;
                activationTimer.stop();
                return;
            }
            if (tasks.dragSource) {
                if (tasks.dragSource !== above && tasks.dragSource.index !== above.index) {
                    tasksModel.move(tasks.dragSource.index, above.index);
                }
            } else if (hoveredItem !== above) {
                // Dragging a file over an app brings its window forward, so the
                // file can be dropped into it, as on Windows.
                hoveredItem = above;
                activationTimer.restart();
            }
        }

        onExited: {
            hoveredItem = null;
            activationTimer.stop();
        }

        onDropped: event => {
            if (event.formats.indexOf("application/x-orgkdeplasmataskmanager_taskbuttonitem") >= 0
                    || event.formats.indexOf("text/x-plasmoidservicename") >= 0) {
                event.accepted = false;
                return;
            }
            if (!event.hasUrls) {
                return;
            }
            const urls = event.urls;
            // Apps dropped on the taskbar are pinned; files dropped on an app
            // open in it.
            if (urls.every(url => backend.isApplication(url))) {
                urls.forEach(url => tasks.addLauncher(url));
                return;
            }
            if (hoveredItem) {
                tasksModel.requestOpenUrls((hoveredItem as Task).modelIndex(), urls);
            }
        }

        Timer {
            id: activationTimer
            interval: 250
            onTriggered: {
                const task = dropArea.hoveredItem as Task;
                if (task && !task.model.IsLauncher && !task.model.IsStartup) {
                    tasksModel.requestActivate(task.modelIndex());
                }
            }
        }
    }

    HoverHandler {
        onPointChanged: Nav.keyboard = false
    }

    Row {
        id: taskRow
        anchors.verticalCenter: parent.verticalCenter
        height: Style.button
        spacing: Style.gap

        move: Transition {
            NumberAnimation {
                properties: "x"
                duration: 200
                easing.type: Easing.OutCubic
            }
        }

        onWidthChanged: iconGeometryTimer.restart()

        Repeater {
            id: taskRepeater

            delegate: Task {
                tasksRoot: tasks
            }
        }

        Repeater {
            model: tasks.orphanLaunches

            delegate: StartingTask {}
        }
    }

    readonly property bool supportsLaunchers: true

    // Start's Pin to taskbar once wrote "applications:applications:<app>"
    // for apps picked from search or the pinned grid, which Plasma cannot
    // resolve: a blank button that only plays the error sound. Such pins are
    // mended where they are read, and the mended list is saved back.
    function mendLaunchers(launchers: var): var {
        const mended = [];
        Array.from(launchers ?? []).forEach(launcher => {
            const url = String(launcher).replace(/^(applications:)+/, "applications:");
            if (!mended.includes(url)) {
                mended.push(url);
            }
        });
        return mended;
    }

    function hasLauncher(url: url): bool {
        return tasksModel.launcherPosition(url) !== -1;
    }

    function addLauncher(url: url): void {
        if (Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable) {
            tasksModel.requestAddLauncher(url);
        }
    }

    function removeLauncher(url: url): void {
        if (Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable) {
            tasksModel.requestRemoveLauncher(url);
        }
    }

    // Called by the shell for Meta+1 to Meta+9.
    function activateTaskAtIndex(index: var): void {
        if (typeof index !== "number") {
            return;
        }
        const task = taskRepeater.itemAt(index) as Task;
        if (task) {
            task.activate(0);
        }
    }

    readonly property Component menuComponent: Qt.createComponent("TaskMenu.qml")

    function showMenu(task: Item, fromKeyboard: bool): void {
        if (menuComponent.status !== Component.Ready) {
            console.warn("Wild OS taskbar menu:", menuComponent.errorString());
            return;
        }
        const menu = menuComponent.createObject(task, {
            task: task,
            tasksRoot: tasks,
            keyboard: fromKeyboard
        });
        hidePreview();
        menuTask = task;
        menu.closed.connect(() => {
            if (tasks.menuTask === task) {
                tasks.menuTask = null;
            }
        });
        menu.show();
    }
}
