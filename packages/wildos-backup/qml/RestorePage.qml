import QtCore as Core
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Backup
import WildOS.UI as UI

// Restore files: pick a backup by date, browse its folders or search it,
// tick files and folders, and put them back where they were or in a folder
// of your choosing. Nothing is replaced unless you say so.
Item {
    id: page

    property var app
    property string pendingDestination

    readonly property bool ready: Restore.state === "ready" && Restore.snapshots.length > 0
    readonly property string home: Core.StandardPaths.writableLocation(Core.StandardPaths.HomeLocation).toString().replace(/^file:\/\//, "")

    // The right-click menu of a row, also on the Menu key.
    function showRowMenu(item, x, y) {
        const entry = item.entry;
        const entries = [];
        if (entry.dir) {
            entries.push({ text: qsTr("Open folder"), icon: "open-folder", run: () => Restore.openFolder(entry.path) });
        }
        entries.push({ text: item.picked ? qsTr("Untick") : qsTr("Tick"), icon: "check", run: () => Restore.toggle(entry.path) });
        rowMenu.entries = entries;
        rowMenu.popup(item, x, y);
    }

    function display(path) {
        const h = decodeURIComponent(home);
        if (path === h) {
            return qsTr("Home");
        }
        if (path.startsWith(h + "/")) {
            return qsTr("Home") + path.slice(h.length).split("/").join(" › ");
        }
        return path;
    }
    function startRestore(destination) {
        const clashes = Restore.clashes(destination);
        if (clashes > 0) {
            pendingDestination = destination;
            clashDialog.count = clashes;
            clashDialog.open();
        } else {
            Restore.restore(destination, false);
        }
    }

    Component.onCompleted: Restore.open()

    Connections {
        target: Drives
        function onChanged() {
            if (Restore.state === "noDrive" && Drives.pluggedIn.length > 0) {
                Restore.open();
            }
        }
    }
    Connections {
        target: Restore
        function onRestored(count, where) {
            page.app.say(count === 1 ? qsTr("1 item restored to %1").arg(page.display(where)) : qsTr("%1 items restored to %2").arg(count).arg(page.display(where)));
        }
        function onRestoreFailed(reason, detail) {
            failDialog.text = reason;
            failDialog.detail = detail;
            failDialog.open();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 22
        anchors.bottomMargin: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            PageHeading {
                Layout.fillWidth: true
                title: qsTr("Restore files")
                subtitle: Restore.driveName.length > 0 && page.ready ? qsTr("From the backups on %1").arg(Restore.driveName) : ""
            }
            UI.ComboBox {
                id: dateBox
                Layout.alignment: Qt.AlignBottom
                Layout.preferredWidth: 220
                Layout.preferredHeight: 32
                visible: page.ready
                model: Restore.snapshots.map(s => ({ value: s.id, text: BackupStyle.when(s.time) }))
                currentIndex: Restore.snapshots.findIndex(s => s.id === Restore.snapshot)
                Accessible.name: qsTr("Backup to restore from")
                onActivated: index => Restore.chooseSnapshot(Restore.snapshots[index].id)
            }
            UI.SearchField {
                id: searchField
                Layout.alignment: Qt.AlignBottom
                Layout.preferredWidth: 240
                visible: page.ready
                placeholderText: qsTr("Search this backup")
                onAccepted: Restore.search(text)
                onTextChanged: {
                    if (text.length === 0) {
                        Restore.search("");
                    }
                }
            }
        }

        // Before the backups are open: no drive, opening, a password to
        // type, or what went wrong.
        Panel {
            Layout.fillWidth: true
            visible: !page.ready
            implicitHeight: notReady.implicitHeight + padding * 2

            RowLayout {
                id: notReady
                width: parent.width
                spacing: 18

                StatusCircle {
                    busy: Restore.state === "opening" || Restore.state === "idle"
                    iconName: Restore.state === "failed" ? "alert" : Restore.state === "needPassword" ? "key" : Restore.state === "ready" ? "history" : "drive"
                    tone: Restore.state === "failed" ? BackupStyle.bad : Restore.state === "noDrive" ? BackupStyle.late : UI.Style.accent
                    fill: Restore.state === "failed" ? BackupStyle.badSoft : Restore.state === "noDrive" ? BackupStyle.lateSoft : UI.Style.accentSoft
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    UI.Label {
                        Layout.fillWidth: true
                        size: 20
                        font.weight: UI.Style.weightBold
                        text: !Settings.configured ? qsTr("No backups yet")
                            : Restore.state === "noDrive" ? qsTr("Plug in your backup drive")
                            : Restore.state === "needPassword" ? qsTr("Type your backup password")
                            : Restore.state === "failed" ? qsTr("The backups couldn't be opened")
                            : Restore.state === "ready" ? qsTr("No backups on this drive yet")
                            : qsTr("Opening your backups")
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        size: 13
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                        text: !Settings.configured ? qsTr("Set a backup drive up on the Overview first.")
                            : Restore.state === "noDrive" ? qsTr("Your backups are on it. They open as soon as it's plugged in.")
                            : Restore.state === "needPassword" ? (Restore.error.length > 0 ? Restore.error : qsTr("Your keyring doesn't have the password for these backups. Type the one you wrote down."))
                            : Restore.state === "failed" ? Restore.error
                            : Restore.state === "ready" ? qsTr("The first backup shows up here once it's done.")
                            : qsTr("Reading the list of backups on %1.").arg(Restore.driveName)
                    }
                    ErrorDetails {
                        Layout.fillWidth: true
                        text: Settings.configured && Restore.state === "failed" ? Restore.errorDetail : ""
                    }
                    RowLayout {
                        Layout.topMargin: 6
                        visible: Restore.state === "needPassword"
                        spacing: 8

                        UI.TextField {
                            id: restorePassword
                            Layout.preferredWidth: 280
                            revealable: true
                            UI.NoPaste {}
                            placeholderText: qsTr("Backup password")
                            onAccepted: Restore.givePassword(text)
                        }
                        UI.Button {
                            kind: "primary"
                            text: qsTr("Open backups")
                            enabled: restorePassword.text.length > 0
                            onClicked: Restore.givePassword(restorePassword.text)
                        }
                    }
                }
                BoardButton {
                    visible: Settings.configured && (Restore.state === "failed" || Restore.state === "noDrive")
                    text: qsTr("Try again")
                    iconName: "refresh"
                    onClicked: {
                        Drives.refresh();
                        Restore.open();
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
            visible: !page.ready
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.ready
            padding: 0
            sidePadding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 10
                    Layout.rightMargin: 18
                    spacing: 6

                    UI.IconButton {
                        iconName: "up"
                        tip: qsTr("Up a folder")
                        enabled: Restore.searchText.length === 0 && Restore.folder !== Restore.root
                        onClicked: Restore.up()
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: Restore.searchText.length > 0 ? qsTr("Results for “%1”").arg(Restore.searchText) : page.display(Restore.folder)
                        size: 13.5
                        font.weight: UI.Style.weightMedium
                        elide: Text.ElideMiddle
                    }
                    UI.Spinner {
                        visible: Restore.listing
                        size: 16
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: BackupStyle.hairline
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 12

                    Item {
                        implicitWidth: 20
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Name")
                        size: 12
                        color: UI.Style.inkSubtle
                    }
                    UI.Label {
                        Layout.preferredWidth: 90
                        text: qsTr("Size")
                        size: 12
                        color: UI.Style.inkSubtle
                        horizontalAlignment: Text.AlignRight
                    }
                    UI.Label {
                        Layout.preferredWidth: 150
                        text: qsTr("Modified")
                        size: 12
                        color: UI.Style.inkSubtle
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Restore.entries
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: false
                    activeFocusOnTab: true
                    C.ScrollBar.vertical: UI.ScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Files and folders in this backup")

                    // The row the keyboard is on, by its path: the list is
                    // made again as a folder's entries come in, and would
                    // otherwise jump back to the top each time.
                    property string keptPath
                    function keep(row) {
                        currentIndex = row;
                        keptPath = row >= 0 && row < count ? model[row].path : "";
                    }
                    function step(by) {
                        if (count === 0) {
                            return;
                        }
                        keep(Math.max(0, Math.min(count - 1, currentIndex + by)));
                        positionViewAtIndex(currentIndex, ListView.Contain);
                    }
                    function activate() {
                        const entry = currentIndex >= 0 && currentIndex < count ? model[currentIndex] : null;
                        if (!entry) {
                            return;
                        }
                        if (entry.dir) {
                            Restore.openFolder(entry.path);
                        } else {
                            Restore.toggle(entry.path);
                        }
                    }
                    onModelChanged: {
                        const at = keptPath.length > 0 ? model.findIndex(e => e.path === keptPath) : -1;
                        if (at >= 0) {
                            currentIndex = at;
                        } else {
                            keptPath = "";
                        }
                    }
                    Keys.onUpPressed: step(-1)
                    Keys.onDownPressed: step(1)
                    Keys.onSpacePressed: {
                        if (currentIndex >= 0 && currentIndex < count) {
                            Restore.toggle(model[currentIndex].path);
                        }
                    }
                    Keys.onReturnPressed: activate()
                    Keys.onEnterPressed: activate()
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                            const item = itemAtIndex(currentIndex);
                            if (item) {
                                page.showRowMenu(item, 60, item.height - 4);
                            }
                            event.accepted = true;
                        }
                    }

                    delegate: EntryRow {
                        id: entryRow
                        required property var modelData
                        required property int index
                        width: list.width
                        entry: modelData
                        showPath: Restore.searchText.length > 0
                        pathText: page.display(modelData.path.slice(0, modelData.path.lastIndexOf("/")))
                        current: list.activeFocus && ListView.isCurrentItem
                        onPressed: {
                            list.keep(index);
                            list.forceActiveFocus();
                        }
                        onMenuRequested: (mx, my) => page.showRowMenu(entryRow, mx, my)
                    }

                    UI.Label {
                        anchors.centerIn: parent
                        visible: !Restore.listing && list.count === 0
                        text: Restore.searchText.length > 0 ? qsTr("Nothing in this backup matches “%1”.").arg(Restore.searchText) : qsTr("This folder was empty.")
                        size: 13
                        color: UI.Style.inkMuted
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: BackupStyle.hairline
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 12

                    UI.Label {
                        visible: !Restore.restoring
                        text: Restore.selected.length === 0 ? qsTr("Tick the files and folders to get back.")
                            : Restore.selected.length === 1 ? qsTr("1 picked") : qsTr("%1 picked").arg(Restore.selected.length)
                        size: 13
                        color: Restore.selected.length === 0 ? UI.Style.inkMuted : UI.Style.ink
                    }
                    BoardLink {
                        visible: !Restore.restoring && Restore.selected.length > 0
                        text: qsTr("Clear")
                        onClicked: Restore.clearSelection()
                    }
                    UI.Label {
                        visible: Restore.restoring
                        text: qsTr("Restoring")
                        size: 13
                    }
                    UI.ProgressBar {
                        Layout.fillWidth: true
                        visible: Restore.restoring
                        from: 0
                        to: 1
                        value: Math.max(0, Restore.restorePercent)
                        indeterminate: Restore.restorePercent < 0
                    }
                    Item {
                        Layout.fillWidth: true
                        visible: !Restore.restoring
                    }
                    BoardButton {
                        visible: Restore.restoring
                        text: qsTr("Cancel")
                        onClicked: Restore.cancelRestore()
                    }
                    BoardButton {
                        visible: !Restore.restoring
                        enabled: Restore.selected.length > 0
                        text: qsTr("Restore to…")
                        iconName: "open-folder"
                        onClicked: destinationDialog.open()
                    }
                    BoardButton {
                        visible: !Restore.restoring
                        enabled: Restore.selected.length > 0
                        kind: "primary"
                        text: qsTr("Restore")
                        iconName: "restore"
                        onClicked: page.startRestore("")
                    }
                }
            }
        }
    }

    FolderDialog {
        id: destinationDialog
        title: qsTr("Restore to")
        currentFolder: Core.StandardPaths.writableLocation(Core.StandardPaths.DocumentsLocation)
        onAccepted: page.startRestore(decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, "")))
    }

    UI.Dialog {
        id: clashDialog
        property int count
        title: count === 1 ? qsTr("1 of these is already there") : qsTr("%1 of these are already there").arg(count)
        text: qsTr("Keep both, and the restored copy gets “(restored %1)” in its name. Or replace what's there now with the copy from the backup.").arg(Qt.formatDate(Restore.snapshotTime, "d MMM"))
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: clashDialog.close()
            },
            UI.Button {
                text: qsTr("Replace")
                onClicked: {
                    clashDialog.close();
                    Restore.restore(page.pendingDestination, true);
                }
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Keep both")
                onClicked: {
                    clashDialog.close();
                    Restore.restore(page.pendingDestination, false);
                }
            }
        ]
    }

    UI.Menu {
        id: rowMenu
    }

    UI.Dialog {
        id: failDialog
        // The backup tool's own words, added under the sentence on Details.
        property string detail
        title: qsTr("Some files couldn't be restored")
        buttons: [
            UI.Button {
                visible: failDialog.detail.length > 0
                text: qsTr("Details")
                onClicked: {
                    failDialog.text = failDialog.text + "\n\n" + failDialog.detail;
                    failDialog.detail = "";
                }
            },
            UI.Button {
                kind: "primary"
                text: qsTr("OK")
                onClicked: failDialog.close()
            }
        ]
    }
}
