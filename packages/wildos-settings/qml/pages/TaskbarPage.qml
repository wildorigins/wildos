import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI
import org.kde.plasma.workspace.dbus as DBus

// Taskbar behaviour and items. The panel is changed through Plasma's own
// scripting interface, so the setting takes effect at once and survives
// restarts.
PageScroll {
    id: page

    // Coming back from the widget picker: show what was added.
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) {
                System.readAddedTaskbarWidgets();
            }
        }
    }

    Component.onCompleted: {
        System.readTaskbarWinModeStatus();
        System.readTaskbarWidgets();
        System.readAddedTaskbarWidgets();
        readTrayIcons();
    }

    readonly property var choices: [
        { mode: 0, title: qsTr("Always show the taskbar"), description: qsTr("Windows stop above it") },
        { mode: 1, title: qsTr("Hide the taskbar automatically"), description: qsTr("Move the pointer to the bottom of the screen to show it") },
        { mode: 2, title: qsTr("Hide it when a window covers it"), description: qsTr("It stays visible until a window needs the space") }
    ]

    // Clock, battery and End task choices live in wildosrc [Taskbar]; the
    // taskbar watches the file and follows straight away.
    property int revision: 0
    function taskbarOption(key) {
        revision;
        return System.config("wildosrc", "Taskbar", key, false);
    }
    function setTaskbarOption(key, on) {
        System.setConfig("wildosrc", "Taskbar", key, on);
        revision++;
    }

    readonly property var clockOptions: [
        { key: "ClockSeconds", title: qsTr("Show seconds in the clock") },
        { key: "ClockLongDate", title: qsTr("Show the full date"), description: qsTr("The day of the week and the month written out") },
        { key: "BatteryPercent", title: qsTr("Show battery percentage"), description: qsTr("Beside the battery icon, on computers with a battery") }
    ]

    // The icons apps put in the taskbar corner. The taskbar's app icons
    // widget keeps the list of them and where each one goes in its own
    // settings, which are read and written through Plasma's scripting
    // interface like the rest of the panel.
    property var trayPlaces: ({})
    ListModel {
        id: trayIcons
    }
    // Held until they answer, or they are collected before they do.
    property var plasmaReplies: []

    readonly property string trayScript:
        "var t = []; panels().forEach(function (p) { t = t.concat(p.widgets('org.wildos.tray')); });"

    function plasmaScript(script, done) {
        const reply = DBus.SessionBus.asyncCall({
            service: "org.kde.plasmashell",
            path: "/PlasmaShell",
            iface: "org.kde.PlasmaShell",
            member: "evaluateScript",
            arguments: [new DBus.string(script)]
        });
        plasmaReplies.push(reply);
        reply.finished.connect(() => {
            const at = plasmaReplies.indexOf(reply);
            if (at >= 0) {
                plasmaReplies.splice(at, 1);
            }
            if (!reply.isError && done) {
                done(String(reply.value).trim());
            }
        });
    }

    function readTrayIcons() {
        plasmaScript(trayScript + "if (t.length) { t[0].currentConfigGroup = ['General'];"
            + " print(JSON.stringify([String(t[0].readConfig('seenIcons', '')), String(t[0].readConfig('iconPlaces', ''))])); }", answer => {
            if (answer.length === 0) {
                return;
            }
            let icons, places;
            try {
                const parts = JSON.parse(answer);
                icons = parts[0].length > 0 ? JSON.parse(parts[0]) : [];
                places = parts[1].length > 0 ? JSON.parse(parts[1]) : {};
            } catch (e) {
                return;
            }
            if (!Array.isArray(icons) || places === null || typeof places !== "object") {
                return;
            }
            trayPlaces = places;
            showTrayIcons(icons.filter(icon => icon && typeof icon.key === "string" && String(icon.title || "").length > 0));
        });
    }

    function trayIconRow(key) {
        for (let i = 0; i < trayIcons.count; i++) {
            if (trayIcons.get(i).iconKey === key) {
                return i;
            }
        }
        return -1;
    }

    // Rows are changed where they stand, not rebuilt, so a switch with the
    // keyboard on it keeps it. What was chosen here wins over where the
    // taskbar last said the icon was, which can be a moment behind.
    function showTrayIcons(icons) {
        for (let i = trayIcons.count - 1; i >= 0; i--) {
            const key = trayIcons.get(i).iconKey;
            if (!icons.some(icon => icon.key === key)) {
                trayIcons.remove(i);
            }
        }
        icons.forEach(icon => {
            const place = trayPlaces[icon.key];
            const row = {
                iconKey: icon.key,
                iconTitle: String(icon.title),
                iconTheme: String(icon.icon || ""),
                kept: place === "bar" || (place === undefined && icon.bar === true)
            };
            const at = trayIconRow(icon.key);
            if (at < 0) {
                trayIcons.append(row);
            } else {
                trayIcons.set(at, row);
            }
        });
    }

    function setTrayIconOnBar(key, on) {
        const places = Object.assign({}, trayPlaces);
        places[key] = on ? "bar" : "overflow";
        trayPlaces = places;
        const at = trayIconRow(key);
        if (at >= 0) {
            trayIcons.setProperty(at, "kept", on);
        }
        // Quoted twice: once as the text that is saved, once to stand in
        // the script as a string.
        plasmaScript(trayScript + "t.forEach(function (w) { w.currentConfigGroup = ['General'];"
            + " w.writeConfig('iconPlaces', " + JSON.stringify(JSON.stringify(places)) + "); w.reloadConfig(); });",
            () => readTrayIcons());
    }

    // Apps come and go while the page is open.
    Timer {
        interval: 5000
        running: page.visible
        repeat: true
        onTriggered: page.readTrayIcons()
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        SectionTitle {
            text: qsTr("Taskbar behaviours")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.choices

                    UI.SettingRow {
                        id: choice
                        required property var modelData
                        readonly property bool selected: System.taskbarHiding === modelData.mode
                        title: modelData.title
                        description: modelData.description
                        interactive: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: selected
                        onClicked: System.setTaskbarHiding(modelData.mode)

                        leading: UI.RadioButton {
                            checked: choice.selected
                            focusPolicy: Qt.NoFocus
                            Accessible.ignored: true
                            onClicked: System.setTaskbarHiding(choice.modelData.mode)
                        }
                    }
                }
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("Show the desktop from the taskbar's far corner")
                    description: qsTr("Click the right end of the taskbar to show the desktop, and again to bring your windows back")
                    status: (System.taskbarShowDesktop) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarShowDesktop
                        Accessible.name: qsTr("Show the desktop from the taskbar's far corner")
                        onToggled: System.taskbarShowDesktop = checked
                    }
                }

                UI.SettingRow {
                    title: qsTr("Peek at the desktop")
                    description: qsTr("Rest the pointer on that corner to see the desktop until you move away")
                    status: (System.taskbarPeek) ? qsTr("On") : qsTr("Off")
                    enabled: System.taskbarShowDesktop

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarPeek
                        Accessible.name: qsTr("Peek at the desktop")
                        onToggled: System.taskbarPeek = checked
                    }
                }
            }
        }

        // Off by default, as on Windows 11: Close window lets an app ask
        // about unsaved work, End task does not.
        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    title: qsTr("End task")
                    description: qsTr("Adds End task to an app's right-click menu on the taskbar, to close an app that isn't responding. Anything unsaved in it is lost.")
                    status: page.taskbarOption("EndTask") ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: page.taskbarOption("EndTask")
                        Accessible.name: qsTr("End task")
                        onToggled: page.setTaskbarOption("EndTask", checked)
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("Taskbar items")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "windowsapps"
                    title: qsTr("Show Windows mode status on the taskbar")
                    description: qsTr("Once Windows mode is set up: whether it's running and the memory it holds")
                    status: (System.taskbarWinModeStatus) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarWinModeStatus
                        Accessible.name: qsTr("Show Windows mode status on the taskbar")
                        onToggled: System.setTaskbarWinModeStatus(checked)
                    }
                }

                UI.SettingRow {
                    iconName: "grid"
                    title: qsTr("Widgets on the taskbar")
                    description: qsTr("Lets you add widgets, like network speed or the weather. The taskbar is locked while this is off, so it can't be changed by accident.")
                    status: (System.taskbarWidgets) ? qsTr("On") : qsTr("Off")

                    UI.Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: System.taskbarWidgets
                        Accessible.name: qsTr("Widgets on the taskbar")
                        onToggled: System.setTaskbarWidgets(checked)
                    }
                }

                UI.SettingRow {
                    visible: System.taskbarWidgets
                    title: qsTr("Add a widget")
                    description: qsTr("Drag a widget from the list onto the taskbar. The ones you add are listed here, to remove them.")

                    UI.Button {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Add widgets")
                        onClicked: System.openWidgetExplorer()
                    }
                }

                Repeater {
                    model: System.taskbarWidgets ? System.addedTaskbarWidgets : []

                    UI.SettingRow {
                        id: widgetRow
                        required property var modelData
                        themeIcon: modelData.icon
                        title: modelData.name

                        UI.Button {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Remove")
                            Accessible.name: qsTr("Remove %1 from the taskbar").arg(widgetRow.modelData.name)
                            onClicked: System.removeTaskbarWidget(widgetRow.modelData.id)
                        }
                    }
                }

                Repeater {
                    model: page.clockOptions

                    UI.SettingRow {
                        id: optionRow
                        required property var modelData
                        title: modelData.title
                        description: modelData.description ?? ""
                        status: page.taskbarOption(modelData.key) ? qsTr("On") : qsTr("Off")

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: page.taskbarOption(optionRow.modelData.key)
                            Accessible.name: optionRow.modelData.title
                            onToggled: page.setTaskbarOption(optionRow.modelData.key, checked)
                        }
                    }
                }
            }
        }

        SectionTitle {
            visible: trayIcons.count > 0
            text: qsTr("Other taskbar corner icons")
        }

        UI.Label {
            visible: trayIcons.count > 0
            Layout.fillWidth: true
            text: qsTr("Choose which app icons stay on the taskbar. The rest wait behind the arrow.")
            color: UI.Style.inkMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }

        UI.Card {
            visible: trayIcons.count > 0
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: trayIcons

                    UI.SettingRow {
                        id: iconRow
                        required property string iconKey
                        required property string iconTitle
                        required property string iconTheme
                        required property bool kept
                        themeIcon: iconTheme.length > 0 ? iconTheme : "application-x-executable"
                        title: iconTitle
                        status: kept ? qsTr("On") : qsTr("Off")

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: iconRow.kept
                            Accessible.name: iconRow.iconTitle
                            onToggled: page.setTrayIconOnBar(iconRow.iconKey, checked)
                        }
                    }
                }
            }
        }
    }
}
