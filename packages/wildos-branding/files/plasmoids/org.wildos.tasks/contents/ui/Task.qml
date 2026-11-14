/*
    SPDX-FileCopyrightText: 2012-2013 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2024 Nate Graham <nate@kde.org>
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    Wild OS fork of the Plasma 6.3.6 task manager's Task.qml. One app button
    from the Taskbar artboard (48 px square, 12 px corners, 34 px icon) with
    the four states of the LaunchStates artboard: the icon dips the moment it
    is pressed; a starting app gets a pale line that stretches and a faint
    glow; a running app a short white line; the app in front an accent line
    and a soft highlight. Nothing bounces and nothing spins.
*/

pragma ComponentBehavior: Bound

import QtQuick
import WildOS.UI as UI

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami

Item {
    id: task

    required property var model
    required property int index
    required property Item tasksRoot

    readonly property var tasksModel: tasksRoot.tasksModel
    readonly property var backend: tasksRoot.backend

    // A pin whose app was uninstalled: Plasma keeps it, as a blank page
    // that only plays the error sound. It stays in the list, out of sight,
    // so putting the app back from the Store brings its pin back where it
    // was.
    readonly property bool orphaned: model.IsLauncher === true && String(model.AppId || "") === ""

    visible: !orphaned
    width: Style.button
    height: Style.button

    activeFocusOnTab: !orphaned

    readonly property string launcherKey: String(model.LauncherUrlWithoutIcon || "")
    readonly property string appKey: String(model.AppId || "").replace(/\.desktop$/, "")
    readonly property var launchKeys: [launcherKey, appKey]
    // The desktop id in lower case, as wildos-launchd names its launches.
    readonly property string desktopKey: (appKey || launcherKey.replace(/^.*[\/:]/, "").replace(/\.desktop$/, "")).toLowerCase()

    readonly property bool isWindow: model.IsWindow === true
    readonly property bool isStartup: model.IsStartup === true
    readonly property bool isLauncher: model.IsLauncher === true
    readonly property bool isActive: model.IsActive === true
    readonly property bool demandsAttention: model.IsDemandingAttention === true
        || (smartLauncher !== null && smartLauncher.urgent)

    readonly property bool menuOpen: tasksRoot.menuTask === task
    readonly property bool hovered: hover.hovered
    readonly property bool pressed: leftTap.pressed || middleTap.pressed

    // KDE's animation speed set to instant (AnimationDurationFactor=0) is
    // reduced motion here: the dip, the stretch and the glow stop, while
    // the lines stay.
    readonly property bool reducedMotion: Kirigami.Units.longDuration <= 1

    // Set the moment this button starts its app, until a window turns up,
    // so the starting line shows straight away rather than once Plasma hears
    // of the launch. Also covers apps that never report that they started.
    property bool awaitingWindow: false
    readonly property int childCount: model.ChildCount ?? 0
    property int childCountAtLaunch: 0

    // Still opening as far as wildos-launchd knows, which outlasts KWin's
    // feedback when another window is clicked meanwhile.
    readonly property bool daemonStarting: !isWindow && tasksRoot.daemonStarting[desktopKey] === true

    readonly property bool starting: isStartup || awaitingWindow || daemonStarting

    function startWaiting(): void {
        childCountAtLaunch = childCount;
        awaitingWindow = true;
        awaitingTimer.restart();
    }

    property int lastChildCount: childCount
    onChildCountChanged: {
        if (awaitingWindow && isWindow && childCount > childCountAtLaunch) {
            awaitingWindow = false;
        }
        if (isWindow && childCount > lastChildCount) {
            tasksRoot.windowOpened([launcherKey, appKey, desktopKey]);
        }
        lastChildCount = childCount;
    }

    Timer {
        id: awaitingTimer
        interval: 10000
        onTriggered: task.awaitingWindow = false
    }

    Connections {
        target: task.tasksRoot
        function onLaunchFinished(key: string): void {
            if (key === task.desktopKey) {
                task.awaitingWindow = false;
            }
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: model.display || ""
    Accessible.description: starting ? "Starting"
        : isLauncher ? "Open " + (model.display || "")
        : model.IsGroupParent ? model.ChildCount + " windows"
        : isActive ? "In front" : "Running"
    Accessible.onPressAction: activate(0)

    function modelIndex(): var {
        return tasksModel.makeModelIndex(index);
    }

    // Left click, as on Windows: open a pinned app, bring a window forward,
    // or put the one in front away. Several windows take turns.
    function activate(modifiers: int): void {
        const index = modelIndex();
        if (modifiers & Qt.ShiftModifier) {
            newInstance();
            return;
        }
        if (isLauncher) {
            tasksRoot.noteLaunch(launchKeys);
            startWaiting();
            tasksModel.requestActivate(index);
            return;
        }
        if (model.IsGroupParent) {
            const children = [];
            let lastUsed = null;
            let highestStacking = -1;
            for (let i = 0; i < tasksModel.rowCount(index); ++i) {
                const child = tasksModel.makeModelIndex(task.index, i);
                children.push(child);
                const stacking = tasksModel.data(child, tasksRoot.atm.StackingOrder);
                if (stacking > highestStacking) {
                    highestStacking = stacking;
                    lastUsed = child;
                }
            }
            const activeAt = children.findIndex(child => tasksModel.data(child, tasksRoot.atm.IsActive));
            if (activeAt < 0) {
                tasksModel.requestActivate(lastUsed);
            } else {
                tasksModel.requestActivate(children[(activeAt + 1) % children.length]);
            }
            return;
        }
        if (model.IsMinimized) {
            tasksModel.requestToggleMinimized(index);
            tasksModel.requestActivate(index);
        } else if (isActive && Plasmoid.configuration.minimizeActiveTaskOnClick) {
            tasksModel.requestToggleMinimized(index);
        } else {
            tasksModel.requestActivate(index);
        }
    }

    function newInstance(): void {
        if (isLauncher) {
            activate(0);
            return;
        }
        if (model.CanLaunchNewInstance === false) {
            tasksModel.requestActivate(modelIndex());
            return;
        }
        tasksRoot.noteLaunch(launchKeys);
        startWaiting();
        tasksModel.requestNewInstance(modelIndex());
    }

    // Unread counts and progress that apps publish for their launcher.
    property QtObject smartLauncher: null

    Component.onCompleted: {
        if (!isStartup) {
            const component = Qt.createComponent("org.kde.plasma.private.taskmanager", "SmartLauncherItem");
            if (component.status === Component.Ready) {
                smartLauncher = component.createObject(task);
                smartLauncher.launcherUrl = Qt.binding(() => task.model.LauncherUrlWithoutIcon);
            }
            component.destroy();
        }

        // Plasma can rebuild a pinned button while its app starts; the new
        // one keeps showing that the app is on its way.
        if (tasksRoot.loaded && isLauncher && tasksRoot.launchedRecently(launchKeys)) {
            startWaiting();
        }

        if (isWindow) {
            tasksRoot.windowOpened([launcherKey, appKey, desktopKey]);
            publishGeometry.start();
            updateAudioStreams();
        }
    }

    onIsWindowChanged: {
        if (isWindow) {
            awaitingWindow = false;
            tasksRoot.windowOpened([launcherKey, appKey, desktopKey]);
            publishGeometry.start();
            updateAudioStreams();
        }
    }

    onXChanged: publishGeometry.restart()

    Timer {
        id: publishGeometry
        interval: 250
        onTriggered: {
            if (task.isWindow) {
                task.tasksModel.requestPublishDelegateGeometry(task.modelIndex(), task.backend.globalRect(task), task);
            }
        }
    }

    // The press dip from the LaunchStates keyframes, run on the press itself
    // so it never waits on the app or the model: down to 0.86, a spring past
    // full size to 1.04, and back, 100 ms in all.
    onPressedChanged: {
        if (pressed) {
            tasksRoot.mutePreview(task);
            if (!reducedMotion) {
                dip.restart();
            }
        }
    }

    onHoveredChanged: tasksRoot.taskHovered(task, hovered)

    SequentialAnimation {
        id: dip
        NumberAnimation { target: icon; property: "scale"; to: Style.dipScale; duration: 35; easing.type: Easing.OutCubic }
        NumberAnimation { target: icon; property: "scale"; to: Style.dipOvershoot; duration: 35; easing.type: Easing.OutCubic }
        NumberAnimation { target: icon; property: "scale"; to: 1; duration: 30; easing.type: Easing.InOutQuad }
    }

    Rectangle {
        id: fill
        anchors.fill: parent
        anchors.margins: Style.highlightInset
        radius: Style.highlightRadius
        color: task.pressed ? Style.pressed
             : task.hovered || task.menuOpen || task.dropTarget ? (task.isActive ? Style.focusedHover : Style.hover)
             : task.demandsAttention ? Style.attention
             : task.isActive ? Style.focused
             : "transparent"
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    // The starting glow: a faint fill behind the icon that comes and goes
    // in step with the line's stretch.
    Rectangle {
        id: glow
        anchors.fill: parent
        anchors.margins: Style.highlightInset
        radius: Style.highlightRadius
        color: Style.startingGlow
        opacity: 0
        visible: task.starting && !task.reducedMotion

        SequentialAnimation on opacity {
            running: glow.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 1; duration: Style.startingCycle / 2; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1; to: 0; duration: Style.startingCycle / 2; easing.type: Easing.InOutSine }
        }
    }

    readonly property bool dropTarget: tasksRoot.dropHovered === task

    // Sized by the artwork's visible shape, so every app sits at the same
    // height whatever padding its icon file has.
    UI.ThemeIcon {
        id: icon
        trim: true
        anchors.centerIn: parent
        width: Style.icon
        height: Style.icon
        source: task.model.decoration
    }

    // The line under the icon. Starting: pale blue, stretching from 6 to
    // 22 px and back. Running: 6 px of white at 70%. In front: 18 px in the
    // accent. Progress that an app reports (a download, a copy) fills a
    // wider track in its place.
    readonly property real restingMarkWidth: starting ? Style.markWidth
        : isActive ? Style.markFocusedWidth : Style.markWidth

    Rectangle {
        id: mark
        visible: (task.isWindow || task.starting) && !progress.visible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Style.markBottom
        height: Style.markHeight
        radius: 2
        width: task.restingMarkWidth
        color: task.starting ? Style.startingMark
             : task.demandsAttention ? Style.warning
             : task.isActive ? Style.accent : Style.mark
        Behavior on width {
            enabled: !stretch.running && !task.reducedMotion
            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
        }
        Behavior on color {
            enabled: !task.reducedMotion
            ColorAnimation { duration: Style.hoverDuration }
        }
    }

    // The fill keyframes: widest and brightest 60% of the way through.
    SequentialAnimation {
        id: stretch
        running: task.starting && task.visible && !task.reducedMotion
        loops: Animation.Infinite
        onRunningChanged: {
            if (!running) {
                mark.opacity = 1;
                mark.width = Qt.binding(() => task.restingMarkWidth);
            }
        }
        ParallelAnimation {
            NumberAnimation { target: mark; property: "width"; from: Style.markWidth; to: Style.startingWidth; duration: Style.startingCycle * 0.6; easing.type: Easing.InOutSine }
            NumberAnimation { target: mark; property: "opacity"; from: 0.5; to: 1; duration: Style.startingCycle * 0.6; easing.type: Easing.InOutSine }
        }
        ParallelAnimation {
            NumberAnimation { target: mark; property: "width"; from: Style.startingWidth; to: Style.markWidth; duration: Style.startingCycle * 0.4; easing.type: Easing.InOutSine }
            NumberAnimation { target: mark; property: "opacity"; from: 1; to: 0.5; duration: Style.startingCycle * 0.4; easing.type: Easing.InOutSine }
        }
    }


    Item {
        id: progress
        visible: task.isWindow && task.smartLauncher !== null && task.smartLauncher.progressVisible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Style.markBottom
        width: 24
        height: Style.markHeight

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: Style.progressTrack
        }
        Rectangle {
            width: parent.width * Math.max(0, Math.min(100, task.smartLauncher ? task.smartLauncher.progress : 0)) / 100
            height: parent.height
            radius: 2
            color: Style.accent
        }
    }

    // An app's unread count, in a small pill at the top right corner.
    Rectangle {
        id: countBadge
        visible: task.smartLauncher !== null && task.smartLauncher.countVisible && task.smartLauncher.count > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 1
        anchors.rightMargin: 1
        height: Math.max(16, countText.implicitHeight + 2)
        width: Math.max(height, countText.implicitWidth + 8)
        radius: height / 2
        color: Style.accent

        Text {
            id: countText
            anchors.centerIn: parent
            text: !task.smartLauncher ? "" : task.smartLauncher.count > 99 ? "99+" : String(task.smartLauncher.count)
            color: Style.textOnAccent
            font.family: Style.fontFamily
            font.letterSpacing: Style.tracking(Style.px(10))
            font.pixelSize: Style.px(10)
            font.weight: UI.Style.weightSemiBold
        }
    }

    // Sound: a speaker at the bottom right corner of an app playing sound,
    // crossed out when it is muted. Clicking it mutes or unmutes the app.
    property var audioStreams: []
    readonly property bool hasAudioStream: audioStreams.length > 0
    readonly property bool playingAudio: hasAudioStream && audioStreams.some(stream => !stream.corked)
    readonly property bool muted: hasAudioStream && audioStreams.every(stream => stream.muted)

    function updateAudioStreams(): void {
        const pa = tasksRoot.pulseAudio;
        if (!pa || !isWindow) {
            audioStreams = [];
            return;
        }
        // Apps behind a portal are matched by app id first, then by process,
        // then by name for apps that never matched a process.
        let streams = pa.streamsForAppId(appKey);
        if (!streams.length) {
            streams = pa.streamsForPid(model.AppPid);
            if (streams.length) {
                pa.registerPidMatch(model.AppName);
            } else if (!pa.hasPidMatch(model.AppName)) {
                streams = pa.streamsForAppName(model.AppName);
            }
        }
        audioStreams = streams;
    }

    function toggleMuted(): void {
        if (muted) {
            audioStreams.forEach(stream => stream.unmute());
        } else {
            audioStreams.forEach(stream => stream.mute());
        }
    }

    Connections {
        target: task.tasksRoot.pulseAudio
        ignoreUnknownSignals: true
        function onStreamsChanged(): void {
            task.updateAudioStreams();
        }
    }

    readonly property int pid: model.AppPid ?? 0
    onPidChanged: updateAudioStreams()

    Rectangle {
        id: audioBadge
        visible: task.playingAudio || task.muted
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 1
        anchors.bottomMargin: 1
        width: 18
        height: 18
        radius: 9
        color: audioMouse.containsMouse ? Style.badgeHover : Style.badge
        border.width: 1
        border.color: Style.badgeBorder
        z: 2

        Accessible.role: Accessible.Button
        Accessible.name: task.muted ? "Unmute " + (task.model.display || "") : "Mute " + (task.model.display || "")
        Accessible.onPressAction: task.toggleMuted()

        Glyph {
            anchors.centerIn: parent
            width: 12
            height: 12
            strokeWidth: 2.2
            path: task.muted ? "M4 9.5h4l5-4v13l-5-4H4zM16 9.5l5 5M21 9.5l-5 5"
                             : "M4 9.5h4l5-4v13l-5-4H4zM16.5 9a4.5 4.5 0 0 1 0 6"
        }

        MouseArea {
            id: audioMouse
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            onClicked: task.toggleMuted()
        }
    }

    FocusRing {
        visible: task.activeFocus && Nav.keyboard
    }

    HoverHandler {
        id: hover
    }

    TapHandler {
        id: leftTap
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: (eventPoint, button) => task.activate(point.modifiers)
        // Holding a finger on an app opens its menu, as on a Windows tablet.
        onLongPressed: task.tasksRoot.showMenu(task, false)
    }

    TapHandler {
        id: middleTap
        acceptedButtons: Qt.MiddleButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: task.newInstance()
    }

    // Windows opens the jump list when the right button comes up.
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: task.tasksRoot.showMenu(task, false)
    }

    DragHandler {
        id: dragHandler
        target: null
        grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType

        // Stops the panel taking the drag as a widget being dropped on it.
        function setInhibitDnd(value: bool): void {
            let item = task;
            while (item.parent) {
                item = item.parent;
                if (item.appletRequestsInhibitDnD !== undefined) {
                    item.appletRequestsInhibitDnD = value;
                }
            }
        }

        onActiveChanged: {
            const helper = task.tasksRoot.dragHelper;
            if (active) {
                icon.grabToImage(result => {
                    // grabToImage is asynchronous; the drag may be over already.
                    if (!dragHandler.active) {
                        return;
                    }
                    setInhibitDnd(true);
                    task.tasksRoot.dragSource = task;
                    helper.Drag.imageSource = result.url;
                    const mime = {
                        "text/x-orgkdeplasmataskmanager_taskurl": task.backend.tryDecodeApplicationsUrl(task.model.LauncherUrlWithoutIcon).toString()
                    };
                    // Pinned apps that are not running carry no window data.
                    if (task.model.MimeType && task.model.MimeData) {
                        mime[task.model.MimeType] = task.model.MimeData;
                        mime["application/x-orgkdeplasmataskmanager_taskbuttonitem"] = task.model.MimeData;
                    }
                    helper.Drag.mimeData = mime;
                    helper.Drag.active = dragHandler.active;
                });
            } else {
                setInhibitDnd(false);
                helper.Drag.active = false;
                helper.Drag.imageSource = "";
            }
        }
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            Nav.keyboard = true;
            activate(event.modifiers);
            event.accepted = true;
            break;
        case Qt.Key_Menu:
            Nav.keyboard = true;
            tasksRoot.showMenu(task, true);
            event.accepted = true;
            break;
        case Qt.Key_Left:
        case Qt.Key_Right: {
            Nav.keyboard = true;
            const step = event.key === Qt.Key_Left ? -1 : 1;
            if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
                tasksModel.move(index, index + step);
                tasksModel.syncLaunchers();
            } else {
                nextItemInFocusChain(step > 0)?.forceActiveFocus(Qt.TabFocusReason);
            }
            event.accepted = true;
            break;
        }
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
            Nav.keyboard = true;
            event.accepted = false;
            break;
        default:
            event.accepted = false;
        }
    }
}
