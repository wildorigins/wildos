import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// System > Storage > Free up space (SettingsFreeSpace.dc): what Wild OS can
// clear, each with its real size from settings-free-space. The four items
// that cost nothing are ticked to begin with; the three with a consequence
// are not, and large downloads are only ever cleared from the list in
// Review. The ticks are kept, because Tidy up automatically clears them.
PageScroll {
    id: page

    readonly property var sys: FreeSpace.system
    readonly property var home: FreeSpace.home
    readonly property bool measured: !FreeSpace.measuring && Object.keys(page.home).length > 0

    readonly property var defaultTicks: ["bin", "restorePoints", "updates", "caches"]
    property var ticked: page.ticksFrom(FreeSpace.hasSavedItems ? FreeSpace.savedItems : page.defaultTicks)
    // Paths of the large downloads ticked in Review.
    property var chosenDownloads: []

    function part(map, key) {
        return map && map[key] ? map[key] : { bytes: 0, count: 0, targets: [], kept: [] };
    }

    readonly property var bin: part(home, "bin")
    readonly property var points: part(sys, "restorePoints")
    readonly property var updates: part(sys, "updates")
    readonly property var caches: part(home, "caches")
    readonly property var leftoversSystem: part(sys, "leftoversSystem")
    readonly property var leftoversHome: part(home, "leftoversHome")
    readonly property var winmode: part(sys, "winmode")
    readonly property var downloads: part(home, "downloads")

    readonly property int leftoverApps: leftoversSystem.count + (leftoversHome.apps ? leftoversHome.apps.length : 0)
    readonly property real chosenDownloadBytes: downloads.targets.filter(t => chosenDownloads.indexOf(t.path) >= 0)
        .reduce((sum, t) => sum + t.bytes, 0)

    readonly property var sizes: ({
        bin: bin.bytes,
        restorePoints: points.bytes,
        updates: updates.bytes,
        caches: caches.bytes,
        leftovers: leftoversSystem.bytes + leftoversHome.bytes,
        winmode: winmode.known ? winmode.bytes : 0,
        downloads: chosenDownloadBytes
    })
    readonly property real total: Object.keys(sizes).reduce((sum, k) => sum + (ticked[k] ? sizes[k] : 0), 0)

    readonly property var rows: [
        { key: "bin", icon: "trash-round", title: "Recycle Bin", shown: true, bytes: sizes.bin,
          description: bin.files > 0 ? Format.plural(bin.files, "file", "files") + " you have already deleted." : "The Recycle Bin is empty." },
        // The system half can fail (no polkit agent, say): then its rows
        // say nothing they could not check.
        { key: "restorePoints", icon: "history", title: "Old restore points", shown: points.available !== false, bytes: sizes.restorePoints,
          unknown: FreeSpace.systemFailed,
          description: FreeSpace.systemFailed ? "Keeps the newest 3 and removes the older ones."
              : points.count > 0 ? "Keeps the newest 3 and removes " + points.count + " older " + (points.count === 1 ? "one." : "ones.")
              : "Keeps the newest 3. There are no older ones." },
        { key: "updates", icon: "download-round", title: "Update files already installed", shown: true, bytes: sizes.updates,
          unknown: FreeSpace.systemFailed,
          description: "Downloads that are no longer needed." },
        { key: "caches", icon: "layers", title: "App caches", shown: true, bytes: sizes.caches,
          description: "Temporary files that apps rebuild when they need them." },
        { key: "leftovers", icon: "package", title: "Leftovers from removed apps", shown: true, bytes: sizes.leftovers,
          description: leftoverApps > 0 ? "Settings kept for " + Format.plural(leftoverApps, "app", "apps") + " you removed. Clearing them means a reinstall starts fresh."
              : "Nothing is left from apps you removed." },
        // Only where Windows mode is set up.
        { key: "winmode", icon: "app-window", title: "Unused space inside Windows mode", shown: winmode.available === true,
          bytes: sizes.winmode, unknown: winmode.known !== true,
          // Windows trims its own free space, so it has to be running; a
          // tick here is kept for Tidy up automatically to do it then.
          description: winmode.known === true ? "Shrinks the Windows disk file. Windows mode needs to stay on while it runs."
              : "Shrinks the Windows disk file. This can only be done while Windows mode is on." },
        { key: "downloads", icon: "folder-round", title: "Large files in Downloads", shown: true,
          bytes: ticked.downloads ? chosenDownloadBytes : downloads.bytes,
          description: downloads.count > 0 ? Format.plural(downloads.count, "file", "files") + " over 100 MB that you have not opened for 90 days."
              : "No files over 100 MB that you have left unopened for 90 days." }
    ]

    function ticksFrom(list) {
        const t = { downloads: false };
        for (const k of ["bin", "restorePoints", "updates", "caches", "leftovers", "winmode"]) {
            t[k] = list.indexOf(k) >= 0;
        }
        return t;
    }

    // Downloads are never kept: Review chooses them each time.
    function keptTicks(t) {
        return Object.keys(t).filter(k => k !== "downloads" && t[k]);
    }

    function tick(key, on) {
        const t = Object.assign({}, ticked);
        t[key] = on;
        ticked = t;
        if (key !== "downloads") {
            FreeSpace.saveItems(keptTicks(t));
        }
    }

    readonly property var tickedItems: rows.filter(r => r.shown && ticked[r.key] && (r.key !== "downloads" || chosenDownloads.length > 0))
    readonly property string clearList: {
        const names = { bin: "the Recycle Bin", restorePoints: "old restore points", updates: "update files",
            caches: "app caches", leftovers: "leftovers from removed apps", winmode: "unused space inside Windows mode",
            downloads: Format.plural(chosenDownloads.length, "file", "files") + " from Downloads" };
        const list = tickedItems.map(r => names[r.key]);
        return list.length <= 1 ? (list[0] || "") : list.slice(0, -1).join(", ") + " and " + list[list.length - 1];
    }

    Component.onCompleted: FreeSpace.measure()

    Connections {
        target: FreeSpace
        function onCleaned(result) {
            if (result.cancelled) {
                return;
            }
            page.chosenDownloads = [];
            page.tick("downloads", false);
            const items = result.items || {};
            const freed = Object.keys(items).reduce((sum, k) => sum + (items[k].freed || 0), 0);
            const skipped = Object.keys(items).reduce((all, k) => all.concat(items[k].skipped || []), []);
            const notes = [];
            if (skipped.some(s => s.why === "an update is running")) {
                notes.push("Update files were left because an update is running.");
            }
            if (skipped.some(s => s.why === "an open app is using it")) {
                notes.push("Files that open apps are using were left.");
            }
            if (skipped.some(s => s.why === "Windows mode isn't on")) {
                notes.push("Windows mode was off, so its disk was not shrunk.");
            }
            if (freed > 0) {
                notice.show(System.formatBytes(freed) + " was freed up", notes.join(" "), false);
            } else {
                notice.show("Nothing was freed up", notes.length > 0 ? notes.join(" ") : "There was nothing left to clear.", notes.length > 0);
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        Notice {
            id: notice
            Layout.fillWidth: true
        }

        // The hero: the total of the ticked items, and the one button.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: heroRow.implicitHeight + 28
            radius: 12
            color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.035)
            border.width: 1
            border.color: UI.Style.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)

            RowLayout {
                id: heroRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18

                Rectangle {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    radius: 23
                    color: Qt.rgba(95 / 255, 211 / 255, 141 / 255, 0.16)

                    PathIcon {
                        anchors.centerIn: parent
                        name: "hard-drive"
                        size: 24
                        strokeWidth: 2.1
                        color: HistoryWords.green
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    UI.Label {
                        Layout.fillWidth: true
                        text: !page.measured ? "Measuring what can be freed"
                            : page.total > 0 ? System.formatBytes(page.total) + " can be freed safely"
                            : page.tickedItems.length > 0 ? "There is nothing to free up" : "Tick what you want cleared"
                        size: 16.5
                        font.weight: UI.Style.weightSemiBold
                        wrapMode: Text.Wrap
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        text: "This disk is " + System.formatBytes(FreeSpace.diskTotal) + " with " + System.formatBytes(FreeSpace.diskFree)
                            + " free. Nothing here touches your documents, pictures or settings."
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                UI.Button {
                    kind: "primary"
                    text: FreeSpace.cleaning ? "Freeing up space"
                        : page.measured && page.total > 0 ? "Free up " + System.formatBytes(page.total) : "Free up space"
                    busy: FreeSpace.measuring || FreeSpace.cleaning
                    enabled: page.measured && !FreeSpace.cleaning && page.total > 0
                    onClicked: confirmDialog.open()
                }
            }
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                Repeater {
                    model: page.rows

                    UI.SettingRow {
                        id: row
                        required property var modelData
                        readonly property bool on: page.ticked[modelData.key] === true
                        readonly property bool isDownloads: modelData.key === "downloads"
                        width: parent.width
                        visible: modelData.shown
                        title: modelData.title
                        description: modelData.description

                        leading: [
                            UI.CheckBox {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: row.on
                                enabled: page.measured
                                Accessible.name: row.title
                                onToggled: {
                                    const want = checked;
                                    // The page's ticks decide, so a Review
                                    // closed without choosing leaves the box
                                    // clear.
                                    checked = Qt.binding(() => row.on);
                                    if (!row.isDownloads) {
                                        page.tick(row.modelData.key, want);
                                    } else if (want) {
                                        // Never ticked without Review: only
                                        // what is ticked there counts.
                                        reviewDialog.open();
                                    } else {
                                        page.chosenDownloads = [];
                                        page.tick("downloads", false);
                                    }
                                }
                            },
                            PathIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: row.modelData.icon
                                size: 19
                            }
                        ]

                        UI.LinkButton {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.isDownloads && page.downloads.count > 0
                            text: "Review"
                            onClicked: reviewDialog.open()
                        }

                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 70
                            horizontalAlignment: Text.AlignRight
                            text: !page.measured ? "" : row.modelData.unknown ? "Unknown" : System.formatBytes(row.modelData.bytes)
                            size: 13.5
                            font.features: ({ "tnum": 1 })
                            color: row.on ? UI.Style.ink : UI.Style.inkMuted
                        }
                    }
                }
            }
        }

        UI.Card {
            Layout.fillWidth: true

            UI.SettingRow {
                width: parent.width
                title: "Tidy up automatically"
                description: "When less than 10% of the disk is free, clear the ticked items. You are told what was removed."
                leading: PathIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "refresh-cw"
                    size: 20
                }

                UI.Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: FreeSpace.autoOn
                    onToggled: {
                        const want = checked;
                        // Follows FreeSpace, so a password that was not
                        // given leaves it as it was.
                        checked = Qt.binding(() => FreeSpace.autoOn);
                        FreeSpace.setAuto(want, page.keptTicks(page.ticked));
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            PathIcon {
                name: "info-round"
                size: 14
                color: UI.Style.inkSubtle
            }

            UI.Label {
                Layout.fillWidth: true
                text: "There is no registry to clean and nothing to defragment on Wild OS. This page is only about getting space back."
                size: 12.5
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
        }
    }

    UI.Dialog {
        id: confirmDialog
        title: "Free up " + System.formatBytes(page.total) + "?"
        text: "This removes " + page.clearList + " for good. Your documents, pictures and settings are not touched"
            + (page.ticked.leftovers ? ", apart from the settings of apps you removed." : ".")
        buttons: [
            UI.Button {
                text: "Free up " + System.formatBytes(page.total)
                kind: "primary"
                onClicked: {
                    confirmDialog.close();
                    FreeSpace.clean(page.tickedItems.map(r => r.key), page.ticked.downloads ? page.chosenDownloads : []);
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: confirmDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: reviewDialog

        property var picks: []

        size: "large"
        title: "Large files in Downloads"
        text: "Files over 100 MB in Downloads that you have not opened for 90 days. Tick the ones to remove. Nothing else in Downloads is touched."
        onOpened: picks = page.chosenDownloads.slice()

        Column {
            Layout.fillWidth: true

            Repeater {
                model: page.downloads.targets

                UI.SettingRow {
                    id: fileRow
                    required property var modelData
                    width: parent.width
                    title: modelData.name
                    description: "Not opened since " + Format.day(new Date(modelData.last * 1000))
                    leading: UI.CheckBox {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: reviewDialog.picks.indexOf(fileRow.modelData.path) >= 0
                        onToggled: {
                            const p = reviewDialog.picks.filter(x => x !== fileRow.modelData.path);
                            if (checked) {
                                p.push(fileRow.modelData.path);
                            }
                            reviewDialog.picks = p;
                        }
                    }

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: System.formatBytes(fileRow.modelData.bytes)
                        color: UI.Style.inkMuted
                    }
                }
            }
        }

        buttons: [
            UI.Button {
                text: "Done"
                kind: "primary"
                onClicked: {
                    page.chosenDownloads = reviewDialog.picks;
                    page.tick("downloads", reviewDialog.picks.length > 0);
                    reviewDialog.close();
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: reviewDialog.close()
            }
        ]
    }
}
