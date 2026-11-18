/*
    SPDX-FileCopyrightText: 2026 Wild OS <dev@wildos.io>

    SPDX-License-Identifier: GPL-2.0-or-later

    The menu a right click on a window's preview opens, as on Windows 11:
    the window by name, then Restore, Minimise, Maximise and Close for that
    one window. It opens from the app's taskbar button, in the taskbar's
    window like the app's own menu (TaskMenu.qml), because the preview is a
    window that never takes the keyboard and a menu has to.
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

import WildOS.UI as UI

UI.Menu {
    id: menu

    required property Item task
    required property Item tasksRoot
    required property string winId
    required property string windowTitle
    property var windowIcon
    property bool keyboard: false

    readonly property var tasksModel: tasksRoot.tasksModel
    readonly property var atm: tasksRoot.atm

    // Between the menu and the top of the taskbar.
    readonly property int gap: 12

    popupType: T.Popup.Window
    minimumWidth: Style.menuWidth
    maximumWidth: Style.menuWidth

    // Windows open and close while the menu is up, which moves the others
    // about in the model, so the window is found again by its id each time
    // it is needed. Null once it has gone.
    function windowIndex(): var {
        const at = task.index;
        const holds = index => {
            const ids = tasksModel.data(index, atm.WinIdList);
            return ids && ids.length > 0 && String(ids[0]) === winId;
        };
        const button = tasksModel.makeModelIndex(at);
        if (task.model.IsGroupParent !== true) {
            return holds(button) ? button : null;
        }
        for (let i = 0; i < tasksModel.rowCount(button); ++i) {
            const child = tasksModel.makeModelIndex(at, i);
            if (holds(child)) {
                return child;
            }
        }
        return null;
    }

    function act(what: string): void {
        const index = windowIndex();
        if (index === null) {
            return;
        }
        if (what === "close") {
            tasksModel.requestClose(index);
        } else if (what === "maximise") {
            tasksModel.requestToggleMaximized(index);
        } else if (what === "minimise") {
            tasksModel.requestToggleMinimized(index);
        } else if (tasksModel.data(index, atm.IsMinimized) === true) {
            tasksModel.requestToggleMinimized(index);
            tasksModel.requestActivate(index);
        } else {
            tasksModel.requestToggleMaximized(index);
        }
    }

    // False when the window went away before the menu could open.
    function show(): bool {
        const index = windowIndex();
        if (index === null) {
            return false;
        }
        const minimised = tasksModel.data(index, atm.IsMinimized) === true;
        const maximised = tasksModel.data(index, atm.IsMaximized) === true;
        entries = [
            { text: windowTitle, iconSource: windowIcon, enabled: false },
            { separator: true },
            { text: qsTr("Restore"), icon: "win-restore", enabled: minimised || maximised, run: () => menu.act("restore") },
            { text: qsTr("Minimise"), icon: "win-minimise",
                enabled: !minimised && tasksModel.data(index, atm.IsMinimizable) !== false, run: () => menu.act("minimise") },
            { text: qsTr("Maximise"), icon: "win-maximise",
                enabled: !minimised && !maximised && tasksModel.data(index, atm.IsMaximizable) !== false, run: () => menu.act("maximise") },
            { separator: true },
            { text: qsTr("Close window"), icon: "close", shortcut: "Alt+F4",
                enabled: tasksModel.data(index, atm.IsClosable) !== false, run: () => menu.act("close") }
        ];
        place();
        open();
        if (keyboard) {
            for (let i = entries.length - 1; i >= 0; --i) {
                const entry = entries[i];
                if (!entry.separator && entry.enabled !== false) {
                    currentIndex = i;
                    break;
                }
            }
        }
        return true;
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
}
