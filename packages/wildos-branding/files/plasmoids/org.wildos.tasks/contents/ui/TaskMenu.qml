/*
    SPDX-FileCopyrightText: 2012-2016 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    Wild OS fork of the Plasma 6.3.6 task manager's ContextMenu.qml. The
    jump list a right click opens on an app (MenusShell.dc "Taskbar app"):
    its pinned places or recent files under small headings, the app's own
    tasks, then the app itself in 600, pin or unpin, close and, when it is
    turned on in Settings > Personalisation > Taskbar, End task, nearest
    the taskbar as on Windows 11. A Windows app adds one quiet line
    saying it runs in Windows mode and a way to its settings. It is the
    shared Wild OS menu in a window of its own, as the taskbar is too short
    to hold it.
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.kde.plasma.plasma5support as P5Support
import org.kde.plasma.private.mpris as Mpris
import org.wildos.shell as WildOS
import WildOS.UI as UI

UI.Menu {
    id: menu

    required property Item task
    required property Item tasksRoot
    property bool keyboard: false

    readonly property var tasksModel: tasksRoot.tasksModel
    readonly property var atm: tasksRoot.atm
    readonly property var modelIndex: task.modelIndex()
    property bool showAllPlaces: false

    // Between the menu and the top of the taskbar.
    readonly property int gap: 12

    popupType: T.Popup.Window
    minimumWidth: Style.menuWidth
    maximumWidth: Style.menuWidth

    function get(role: int): var {
        return tasksModel.data(modelIndex, role);
    }

    function show(): void {
        compose();
        place();
        open();
        if (keyboard) {
            for (let i = entries.length - 1; i >= 0; --i) {
                const entry = entries[i];
                if (!entry.separator && entry.header === undefined && entry.enabled !== false) {
                    currentIndex = i;
                    break;
                }
            }
        }
    }

    // Centred over the button, with the menu's foot 12 px above the top of
    // the taskbar's window; the compositor keeps it on the screen.
    function place(): void {
        const scene = task.mapToItem(null, 0, 0);
        x = Math.round(task.width / 2 - implicitWidth / 2);
        y = Math.round(-scene.y - gap - implicitHeight);
    }

    onImplicitHeightChanged: {
        if (visible) {
            place();
        }
    }

    onClosed: {
        if (keyboard) {
            task.forceActiveFocus(Qt.TabFocusReason);
        }
        destroy();
    }

    readonly property P5Support.DataSource runner: P5Support.DataSource {
        engine: "executable"
        onNewData: source => disconnectSource(source)
    }

    // QActions from the task manager backend: jump list tasks, places and
    // recent files.
    function qActions(list: var): var {
        const result = [];
        for (const item of list) {
            if (typeof item === "string" || !item) {
                continue;
            }
            if (item.separator) {
                if (result.length > 0 && !result[result.length - 1].separator) {
                    result.push({ separator: true });
                }
                continue;
            }
            if (!item.visible) {
                continue;
            }
            const qaction = item;
            result.push({
                text: String(qaction.text).replace(/&(?!&)/g, ""),
                iconSource: qaction.icon,
                run: () => qaction.trigger(),
                enabled: qaction.enabled
            });
        }
        while (result.length > 0 && result[result.length - 1].separator) {
            result.pop();
        }
        return result;
    }

    function compose(): void {
        const list = [];
        const launcherUrl = get(atm.LauncherUrlWithoutIcon);
        const isLauncher = get(atm.IsLauncher) === true;
        const isStartup = get(atm.IsStartup) === true;
        const backend = tasksRoot.backend;

        if (launcherUrl && String(launcherUrl).length > 0) {
            const places = qActions(backend.placesActions(launcherUrl, showAllPlaces, menu));
            const recent = places.length > 0 ? [] : qActions(backend.recentDocumentActions(launcherUrl, menu));
            const jump = qActions(backend.jumpListActions(launcherUrl, menu));
            if (places.length > 0) {
                list.push({ header: "Pinned" }, ...places);
            }
            if (recent.length > 0) {
                list.push({ header: "Recent" }, ...recent);
            }
            if (jump.length > 0) {
                list.push({ header: "Tasks" }, ...jump);
            }
        }

        const player = tasksRoot.mpris2Source.playerForLauncherUrl(launcherUrl, get(atm.AppPid) ?? 0);
        const windows = get(atm.WinIdList);
        const windowCount = windows ? windows.length : 0;
        if (player && player.canControl && windowCount <= 1) {
            if (list.length > 0) {
                list.push({ separator: true });
            }
            const playing = player.playbackStatus === Mpris.PlaybackStatus.Playing;
            list.push({ text: "Previous track", icon: "media-previous", run: () => player.Previous(), enabled: player.canGoPrevious });
            if (playing && player.canPause) {
                list.push({ text: "Pause", icon: "media-pause", run: () => player.Pause() });
            } else {
                list.push({ text: "Play", icon: "play", run: () => player.Play(), enabled: player.canPlay });
            }
            list.push({ text: "Next track", icon: "media-next", run: () => player.Next(), enabled: player.canGoNext });
        }

        if (task.hasAudioStream) {
            const muted = task.muted;
            list.push({ text: muted ? "Unmute" : "Mute", icon: muted ? "sound" : "sound-off", run: () => task.toggleMuted() });
        }

        if (list.length > 0) {
            list.push({ separator: true });
        }

        // The app itself, as Windows lists it: opens another window, or for an
        // app that only runs once (and a pinned app) brings it up.
        if (!isStartup) {
            const canOpenAnother = !isLauncher && get(atm.CanLaunchNewInstance) !== false;
            list.push({
                text: task.model.AppName || task.model.display || "Open",
                iconSource: task.model.decoration,
                bold: true,
                run: () => canOpenAnother ? task.newInstance()
                    : isLauncher ? task.activate(0)
                    : tasksModel.requestActivate(menu.modelIndex)
            });
        }

        const canPin = Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable
            && launcherUrl && String(launcherUrl).length > 0 && !isStartup;
        if (canPin) {
            if (tasksRoot.hasLauncher(launcherUrl)) {
                list.push({ text: "Unpin from taskbar", icon: "menu-unpin", run: () => tasksModel.requestRemoveLauncher(launcherUrl) });
            } else {
                list.push({ text: "Pin to taskbar", icon: "menu-pin", run: () => tasksModel.requestAddLauncher(get(atm.LauncherUrl)) });
            }
        }

        // Windows apps get their launchers from wildos-appbridge, which names
        // every one wildos-winapp-<app>.desktop.
        const windowsApp = /wildos-winapp-[^\/]*\.desktop$/.test(String(launcherUrl || ""));

        if (!isLauncher && !isStartup) {
            const group = get(atm.IsGroupParent) === true;
            list.push({
                text: group ? "Close all windows" : "Close window",
                icon: "close",
                shortcut: group && windowCount > 1 ? String(windowCount) : "",
                run: () => tasksModel.requestClose(menu.modelIndex),
                enabled: get(atm.IsClosable) !== false
            });

            // Off by default, as on Windows 11: Close window lets the app
            // ask about unsaved work, End task does not. Every Windows app's
            // window comes from the one Windows mode viewer, so ending it
            // would close all of them at once.
            const pids = WildOS.TaskbarStyle.endTask === true && !windowsApp ? appPids(group) : [];
            if (pids.length > 0) {
                list.push({ text: "End task", icon: "menu-task-manager", run: () => menu.endTask(pids) });
            }
        }

        if (windowsApp) {
            list.push({ separator: true },
                { text: "Runs in Windows mode", icon: "menu-windows-mode", enabled: false },
                { text: "Windows mode settings", icon: "menu-settings", run: () => menu.runner.connectSource(
                    "kstart --desktopfile org.wildos.settings -- wildos-settings --page windowsapps") });
        }

        entries = list;
    }

    // The processes behind the app's windows; a group's windows can come
    // from more than one.
    function appPids(group: bool): var {
        const indexes = [];
        if (group) {
            for (let i = 0; i < tasksModel.rowCount(modelIndex); ++i) {
                indexes.push(tasksModel.makeModelIndex(task.index, i));
            }
        } else {
            indexes.push(modelIndex);
        }
        const pids = [];
        for (const index of indexes) {
            const pid = Math.floor(Number(tasksModel.data(index, atm.AppPid) ?? 0));
            if (pid > 1 && !pids.includes(pid)) {
                pids.push(pid);
            }
        }
        return pids;
    }

    // End task, as on Windows 11, for an app that no longer answers: the app
    // and everything it started are asked to quit, and whatever is still
    // running three seconds later is killed. The desktop's own processes are
    // left alone: their dialogs are tasks too, and ending them would end the
    // session. setsid lets the wait outlive this menu, which is gone the
    // moment the entry is chosen.
    function endTask(pids: var): void {
        const script = "all=; walk() { for p; do case $(cat /proc/$p/comm 2>/dev/null) in plasmashell|kwin_wayland|ksmserver|Xwayland|kded6|systemd) continue;; esac; all=\"$all $p\"; walk $(pgrep -P \"$p\"); done; }; "
            + "walk " + pids.join(" ") + "; kill -TERM $all 2>/dev/null; "
            + "for i in 1 2 3 4 5 6; do sleep 0.5; left=; "
            + "for p in $all; do kill -0 $p 2>/dev/null && left=\"$left $p\"; done; "
            + "[ -z \"$left\" ] && exit 0; done; kill -KILL $left 2>/dev/null";
        runner.connectSource("setsid -f sh -c '" + script + "'");
    }

    // "Show all" in the places list asks for the menu again with every place.
    property Connections placesWatcher: Connections {
        target: menu.tasksRoot.backend
        function onShowAllPlaces(): void {
            menu.showAllPlaces = true;
            menu.compose();
        }
    }
}
