import QtCore
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// Settings, Import and export (MailMsSetImport.dc): mail brought in from
// another mail app on this PC or from files, contacts and calendars from
// vCard and iCalendar files, and all of it taken out again, one kind at a
// time or as one backup file. A long import or export shows its progress
// on the page and can be paused or cancelled; Mail carries on meanwhile.
ColumnLayout {
    id: page

    property string account: MailEngine.accounts.length > 0 ? MailEngine.accounts[0].id : ""
    // Thunderbird and Evolution, when they hold mail: [{id, name}].
    property var apps: []
    property string note: ""
    property bool noteOk: true
    readonly property bool busy: MailEngine.transferStatus.length > 0
    readonly property string accountEmail: (MailEngine.accounts.find(a => a.id === account) || { email: "" }).email

    spacing: 6

    onVisibleChanged: if (visible) apps = MailEngine.mailApps()
    Component.onCompleted: if (visible) apps = MailEngine.mailApps()

    // Each action starts a fresh note; contacts and calendars answer one
    // after the other, so their notices are put together.
    function begin() {
        note = "";
        noteOk = true;
    }
    function add(text, ok) {
        note = note.length > 0 ? note + " " + text : text;
        noteOk = noteOk && ok;
    }
    function count(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0);
    }

    readonly property string appsText: {
        const names = apps.map(a => a.name);
        if (names.length === 0) {
            return qsTr("No other mail app with mail in it was found on this PC");
        }
        if (names.length === 1) {
            return qsTr("%1 found. Brings its mail folders").arg(names[0]);
        }
        return qsTr("%1 and %2 found. Brings their mail folders").arg(names[0]).arg(names[1]);
    }

    readonly property string progressText: {
        const done = MailEngine.transferDone;
        const total = MailEngine.transferTotal;
        const into = MailEngine.transferAccount;
        const kind = MailEngine.transferKind;
        const carriesOn = qsTr("Mail carries on working while this runs.");
        if (MailEngine.transferPaused) {
            return total > 0 ? qsTr("Paused at %1 of %2 messages.").arg(count(Math.min(done, total))).arg(count(total)) : qsTr("Paused.");
        }
        if (total === 0) {
            return kind === "backup" ? carriesOn : qsTr("Counting the messages. %1").arg(carriesOn);
        }
        const shown = count(Math.min(done, total));
        if ((kind === "import" || kind === "restore") && into.length > 0) {
            return qsTr("%1 of %2 messages, into %3. %4").arg(shown).arg(count(total)).arg(into).arg(carriesOn);
        }
        if (kind === "import" || kind === "restore") {
            return qsTr("%1 of %2 messages. %3").arg(shown).arg(count(total)).arg(carriesOn);
        }
        return qsTr("%1 of %2 messages saved. %3").arg(shown).arg(count(total)).arg(carriesOn);
    }

    Connections {
        target: MailEngine
        function onTransferFinished(ok, message) {
            page.begin();
            page.add(message, ok);
        }
    }
    Connections {
        target: PeopleEngine
        enabled: page.visible
        function onNotice(text) {
            page.add(text, true);
        }
    }
    Connections {
        target: CalendarEngine
        enabled: page.visible
        function onNotice(text) {
            page.add(text, true);
        }
    }

    UI.Label {
        visible: MailEngine.accounts.length === 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        size: 13
        color: UI.Style.inkMuted
        text: qsTr("Add an account first. Imported mail goes into one of your accounts, so it is on your other devices too.")
    }

    UI.Label {
        text: qsTr("Bring mail in")
        size: 12
        font.weight: UI.Style.weightSemiBold
        color: UI.Style.inkMuted
        topPadding: 6
    }
    UI.Card {
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 0
            UI.SettingRow {
                Layout.fillWidth: true
                visible: MailEngine.accounts.length > 1
                title: qsTr("Bring mail into")
                description: qsTr("A folder called Imported in this account, so it reaches your email provider too")
                UI.ComboBox {
                    Layout.preferredWidth: 260
                    model: MailEngine.accounts.map(a => ({ value: a.id, text: a.email }))
                    currentIndex: MailEngine.accounts.findIndex(a => a.id === page.account)
                    onActivated: function (i) {
                        page.account = MailEngine.accounts[i].id;
                    }
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("From another mail app on this PC")
                description: page.appsText
                UI.Button {
                    id: appButton
                    text: qsTr("Choose app")
                    iconName: "apps"
                    dropDown: true
                    enabled: !page.busy && page.apps.length > 0 && page.account.length > 0
                    onClicked: appMenu.popup(appButton, 0, appButton.height + 4)
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("From a file")
                description: qsTr("mbox, eml and pst files, or a whole folder of them")
                UI.Button {
                    text: qsTr("Choose folder")
                    iconName: "folder"
                    enabled: !page.busy && page.account.length > 0
                    onClicked: {
                        page.begin();
                        mailFolderPicker.open();
                    }
                }
                UI.Button {
                    text: qsTr("Choose file")
                    iconName: "document"
                    enabled: !page.busy && page.account.length > 0
                    onClicked: {
                        page.begin();
                        mailFilePicker.open();
                    }
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Contacts and calendars")
                description: qsTr("vCard (.vcf) and iCalendar (.ics) files")
                UI.Button {
                    text: qsTr("Choose file")
                    iconName: "document"
                    onClicked: {
                        page.begin();
                        cardsPicker.open();
                    }
                }
            }
        }
    }

    UI.Card {
        visible: page.busy
        Layout.fillWidth: true
        Layout.topMargin: 8
        tinted: true
        padding: 14
        ColumnLayout {
            width: parent.width
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                MailIcon {
                    name: MailEngine.transferKind === "import" || MailEngine.transferKind === "restore" ? "download" : "upload"
                    size: 18
                    color: UI.Style.accent
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    UI.Label {
                        Layout.fillWidth: true
                        text: MailEngine.transferStatus
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                        elide: Text.ElideMiddle
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: page.progressText
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                }
                UI.Button {
                    text: MailEngine.transferPaused ? qsTr("Resume") : qsTr("Pause")
                    iconName: MailEngine.transferPaused ? "play" : "media-pause"
                    onClicked: MailEngine.pauseTransfer(!MailEngine.transferPaused)
                }
                UI.Button {
                    text: qsTr("Cancel")
                    onClicked: MailEngine.cancelTransfer()
                }
            }
            UI.ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, MailEngine.transferTotal)
                value: Math.min(MailEngine.transferDone, MailEngine.transferTotal)
                indeterminate: MailEngine.transferTotal === 0 && !MailEngine.transferPaused
            }
        }
    }

    UI.Label {
        visible: page.note.length > 0 && !page.busy
        Layout.fillWidth: true
        Layout.topMargin: 4
        text: page.note
        size: 12
        color: page.noteOk ? UI.Style.inkMuted : UI.Style.warning
        wrapMode: Text.WordWrap
    }

    UI.Label {
        text: qsTr("Take mail out")
        size: 12
        font.weight: UI.Style.weightSemiBold
        color: UI.Style.inkMuted
        topPadding: 12
    }
    UI.Card {
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 0
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Export mail")
                description: qsTr("Choose accounts and folders. Saved as mbox, which every mail app can open")
                UI.Button {
                    text: qsTr("Export")
                    iconName: "share"
                    enabled: !page.busy && MailEngine.accounts.length > 0
                    onClicked: {
                        page.begin();
                        exportDialog.ask();
                    }
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Export contacts and calendars")
                description: qsTr("vCard and iCalendar files")
                UI.Button {
                    text: qsTr("Export")
                    iconName: "share"
                    onClicked: {
                        page.begin();
                        cardsFolderPicker.open();
                    }
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: qsTr("Back up everything")
                description: qsTr("One file with mail, contacts, calendars, rules, signatures and settings. Passwords are never included")
                UI.Button {
                    text: qsTr("Restore")
                    enabled: !page.busy
                    onClicked: {
                        page.begin();
                        restorePicker.open();
                    }
                }
                UI.Button {
                    text: qsTr("Back up")
                    iconName: "zip"
                    enabled: !page.busy
                    onClicked: {
                        page.begin();
                        backupPicker.selectedFile = backupPicker.currentFolder + "/" + qsTr("Mail backup %1").arg(Qt.formatDate(new Date(), "yyyy-MM-dd")) + ".tar.gz";
                        backupPicker.open();
                    }
                }
            }
        }
    }

    UI.Menu {
        id: appMenu
        entries: page.apps.map(a => ({ text: a.name, run: () => MailEngine.importApp(page.account, a.id) }))
    }

    FileDialog {
        id: mailFilePicker
        title: qsTr("Import mail")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Mail files (*.mbox *.mbx *.eml *.pst *.ost)"), qsTr("All files (*)")]
        onAccepted: MailEngine.importMail(page.account, selectedFiles)
    }
    FolderDialog {
        id: mailFolderPicker
        title: qsTr("Import a folder of mail")
        onAccepted: MailEngine.importMail(page.account, [selectedFolder])
    }
    FileDialog {
        id: cardsPicker
        title: qsTr("Import contacts and calendars")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Contacts and calendars (*.vcf *.vcard *.ics)"), qsTr("All files (*)")]
        onAccepted: {
            for (const f of selectedFiles) {
                if (f.toString().toLowerCase().endsWith(".ics")) {
                    CalendarEngine.importFrom(f, "");
                } else {
                    PeopleEngine.importFrom(f, "");
                }
            }
        }
    }
    FolderDialog {
        id: cardsFolderPicker
        title: qsTr("Export contacts and calendars")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            PeopleEngine.exportTo(PeopleEngine.list("all", "", "").map(p => p.key), selectedFolder + "/" + qsTr("Contacts") + ".vcf");
            CalendarEngine.exportTo(selectedFolder);
        }
    }
    FolderDialog {
        id: mailExportPicker
        title: qsTr("Export mail")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: MailEngine.exportMail(exportDialog.choice(), selectedFolder)
    }
    FileDialog {
        id: backupPicker
        title: qsTr("Back up everything")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "tar.gz"
        nameFilters: [qsTr("Mail backups (*.tar.gz)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: MailEngine.backUp(selectedFile)
    }
    FileDialog {
        id: restorePicker
        title: qsTr("Restore a backup")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Mail backups (*.tar.gz *.tgz)"), qsTr("All files (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: MailEngine.restore(selectedFile)
    }

    // Which accounts, and with one account which of its folders, before
    // choosing where the mbox files go.
    UI.Dialog {
        id: exportDialog
        // Empty for every account.
        property string account: ""
        property var folders: []
        property var unticked: ({})

        function ask() {
            account = MailEngine.accounts.length === 1 ? MailEngine.accounts[0].id : "";
            load();
            open();
        }
        function load() {
            folders = account.length > 0 ? MailEngine.moveTargets(account).filter(f => f.role !== "outbox") : [];
            unticked = {};
        }
        function choice() {
            const out = {};
            if (account.length === 0) {
                for (const a of MailEngine.accounts) {
                    out[a.id] = [];
                }
            } else {
                out[account] = folders.filter(f => !unticked[f.fullName]).map(f => f.fullName);
            }
            return out;
        }
        onAccountChanged: load()

        title: qsTr("Export mail")
        text: qsTr("Each folder is saved as its own mbox file, in a folder named after its account.")
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            UI.ComboBox {
                visible: MailEngine.accounts.length > 1
                Layout.fillWidth: true
                model: [{ value: "", text: qsTr("Every account") }].concat(MailEngine.accounts.map(a => ({ value: a.id, text: a.email })))
                currentIndex: exportDialog.account.length === 0 ? 0 : MailEngine.accounts.findIndex(a => a.id === exportDialog.account) + 1
                onActivated: function (i) {
                    exportDialog.account = i === 0 ? "" : MailEngine.accounts[i - 1].id;
                }
            }
            UI.Label {
                visible: exportDialog.account.length === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                size: 13
                color: UI.Style.inkMuted
                text: qsTr("Every folder of every account")
            }
            QQC.ScrollView {
                visible: exportDialog.account.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, folderColumn.implicitHeight)
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: folderColumn
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: exportDialog.folders
                        delegate: UI.CheckBox {
                            required property var modelData
                            Layout.leftMargin: modelData.depth * 18
                            text: modelData.name
                            checked: !exportDialog.unticked[modelData.fullName]
                            onToggled: {
                                const next = Object.assign({}, exportDialog.unticked);
                                next[modelData.fullName] = !checked;
                                exportDialog.unticked = next;
                            }
                        }
                    }
                }
            }
        }
        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: exportDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Choose where to save")
                enabled: exportDialog.account.length === 0 || exportDialog.folders.some(f => !exportDialog.unticked[f.fullName])
                onClicked: {
                    exportDialog.close();
                    mailExportPicker.open();
                }
            }
        ]
    }
}
