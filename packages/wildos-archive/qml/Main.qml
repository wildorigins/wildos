import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Archive
import WildOS.UI as UI

// Wild OS Archive (AppArchive.dc): the title bar with the archive's name,
// the toolbar, the path inside the archive, the read only note, the table
// of what is in it, the extract pane at the right and the status bar.
// With no archive open it shows the start view instead.
UI.AppWindow {
    id: window

    // From main.cpp or ArchiveApp: an archive to open, or files for a new
    // archive (Files' Compress...).
    property string startPath
    property var compressUrls: []
    // Still on the start view, so ArchiveApp may open an archive here.
    readonly property bool idle: archive.state === "empty" && !newDialog.visible

    readonly property bool hasArchive: archive.state !== "empty"
    readonly property bool ready: archive.state === "ready"

    objectName: "main"
    width: 1384
    height: 802
    minimumWidth: 940
    minimumHeight: 560
    visible: true
    title: archive.fileName.length > 0 ? archive.fileName : qsTr("Archive")

    // One archive to a window: another one opens in a window of its own.
    function openPath(path) {
        const p = String(path);
        if (archive.state === "empty" || archive.state === "failed") {
            archive.open(p);
        } else if (p !== archive.path) {
            ArchiveApp.openWindow(p);
        }
    }
    function openUrl(url) {
        const s = url.toString();
        openPath(decodeURIComponent(s.replace(/^file:\/\//, "")));
    }
    function chooseArchive() {
        openDialog.currentFolder = archive.path.length > 0 ? archive.folderUrl() : StandardPaths.writableLocation(StandardPaths.DownloadLocation);
        openDialog.open();
    }
    function chooseDestination() {
        destinationDialog.currentFolder = archive.destinationUrl();
        destinationDialog.open();
    }
    function say(message) {
        if (message.length > 0) {
            toast.show(message);
        }
    }
    function focusList() {
        list.forceActiveFocus();
    }

    Component.onCompleted: {
        if (startPath.length > 0) {
            archive.open(startPath);
        }
        if (compressUrls.length > 0) {
            Qt.callLater(() => newDialog.startWith(compressUrls));
        }
    }
    onClosing: {
        archive.cancel();
        ArchiveApp.windowClosed(window);
    }

    Archive {
        id: archive
        onPasswordNeeded: wrong => passwordDialog.ask(wrong)
        onFinished: (job, ok, heading, message, details) => {
            if (heading.length === 0) {
                window.say(message);
            } else {
                messageDialog.show(heading, message, details);
            }
        }
        onLoaded: {
            if (state === "ready") {
                window.focusList();
            }
        }
    }

    Shortcut { sequence: StandardKey.Open; onActivated: window.chooseArchive() }
    Shortcut { sequence: StandardKey.New; onActivated: newDialog.startWith([]) }
    Shortcut { sequence: StandardKey.Find; enabled: window.ready; onActivated: searchInput.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+E"; enabled: window.ready && !archive.busy && !archive.lockedFormat; onActivated: archive.extract(true) }
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.close() }
    Shortcut { sequences: [StandardKey.Back]; enabled: archive.canGoBack; onActivated: archive.back() }
    Shortcut { sequence: "Alt+Up"; enabled: archive.canGoUp; onActivated: archive.up() }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length > 0) {
                if (window.ready) {
                    // Dropped on an open archive: a new archive with both.
                    newDialog.startFrom(archive, drop.urls);
                } else {
                    window.openUrl(drop.urls[0]);
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            frame: window.frame
            showTitle: false
            buttons.buttonHeight: 44

            // The title bar puts its content 16px in; the board's icon is 14.
            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: ArchiveStyle.icon
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: archive.fileName.length > 0
                    text: archive.fileName
                    font.weight: UI.Style.weightMedium
                    color: window.active ? UI.Style.ink : UI.Style.inkMuted
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Archive")
                    color: archive.fileName.length > 0 ? UI.Style.inkSubtle : window.active ? UI.Style.ink : UI.Style.inkMuted
                    font.weight: archive.fileName.length > 0 ? UI.Style.weightRegular : UI.Style.weightMedium
                }
            }
        }

        // The toolbar: 50px on a slightly deeper shade, a hairline under it.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            visible: window.hasArchive
            color: ArchiveStyle.toolbarShade

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: ArchiveStyle.hairline
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                ToolButton {
                    accent: true
                    iconName: "extract-all"
                    text: qsTr("Extract all")
                    enabled: window.ready && !archive.busy && !archive.lockedFormat
                    onClicked: archive.extract(true)
                }
                Item { width: 6; height: 1 }
                ToolButton {
                    iconName: "extract-selected"
                    text: qsTr("Extract selected")
                    enabled: window.ready && !archive.busy && !archive.lockedFormat && archive.selectedCount > 0
                    onClicked: archive.extract(false)
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13
                    height: 22
                    color: "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: 1
                        height: 22
                        color: ArchiveStyle.hairline
                    }
                }
                ToolButton {
                    iconName: "add-files"
                    text: qsTr("Add files")
                    tip: qsTr("Makes a new archive with this one's contents and your files")
                    enabled: window.ready && !archive.busy && !archive.lockedFormat
                    onClicked: {
                        addFilesDialog.currentFolder = archive.folderUrl();
                        addFilesDialog.open();
                    }
                }
                ToolButton {
                    iconName: "new-archive"
                    text: qsTr("New archive")
                    onClicked: newDialog.startWith([])
                }
                ToolButton {
                    iconName: "test"
                    text: qsTr("Test")
                    tip: qsTr("Check that every file in the archive is intact")
                    enabled: window.ready && !archive.busy && !archive.lockedFormat
                    onClicked: archive.test()
                }
            }

            // The board's search box: 220 by 30, radius 6, magnifier first.
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 220
                height: 30
                radius: 6
                color: ArchiveStyle.fieldFill
                border.width: 1
                border.color: searchInput.activeFocus ? UI.Style.accent : ArchiveStyle.fieldEdge
                opacity: window.ready ? 1 : 0.45

                ArchiveIcon {
                    id: searchIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 14
                    color: UI.Style.inkMuted
                }
                TextInput {
                    id: searchInput
                    anchors.left: searchIcon.right
                    anchors.leftMargin: 8
                    anchors.right: clearSearch.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: window.ready
                    clip: true
                    font.family: UI.Style.fontFamily
                    font.pixelSize: UI.Style.px(12.5)
                    color: UI.Style.ink
                    selectionColor: UI.Style.textSelection
                    selectedTextColor: UI.Style.ink
                    selectByMouse: true
                    Accessible.name: qsTr("Search this archive")
                    onTextEdited: searchTimer.restart()
                    Keys.onEscapePressed: {
                        text = "";
                        archive.search = "";
                        window.focusList();
                    }
                    Keys.onDownPressed: window.focusList()
                    Keys.onPressed: event => {
                        if (searchMenu.isMenuKey(event)) {
                            searchMenu.openAtCaret();
                            event.accepted = true;
                        }
                    }

                    UI.TextMenu {
                        id: searchMenu
                    }

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: searchInput.text.length === 0
                        text: qsTr("Search this archive")
                        size: 12.5
                        color: UI.Style.inkSubtle
                    }
                }
                ToolButton {
                    id: clearSearch
                    anchors.right: parent.right
                    anchors.rightMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    side: 24
                    iconSize: 12
                    iconName: "x"
                    tip: qsTr("Clear")
                    visible: searchInput.text.length > 0
                    onClicked: {
                        searchInput.text = "";
                        archive.search = "";
                    }
                }
                Timer {
                    id: searchTimer
                    interval: 200
                    onTriggered: archive.search = searchInput.text
                }
                Connections {
                    target: archive
                    function onViewChanged() {
                        if (archive.search.length === 0 && searchInput.text.length > 0 && !searchTimer.running) {
                            searchInput.text = "";
                        }
                    }
                }
            }
        }

        StartView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !window.hasArchive
            onOpenArchive: window.chooseArchive()
            onNewArchive: newDialog.startWith([])
            onOpenRecent: path => archive.open(path)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: window.hasArchive
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Back, up and where in the archive this is.
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        ToolButton {
                            anchors.verticalCenter: parent.verticalCenter
                            side: 28
                            iconSize: 15
                            iconName: "back"
                            tip: qsTr("Back")
                            enabled: archive.canGoBack
                            onClicked: archive.back()
                        }
                        ToolButton {
                            anchors.verticalCenter: parent.verticalCenter
                            side: 28
                            iconSize: 15
                            iconName: "up"
                            tip: qsTr("Up")
                            enabled: archive.canGoUp
                            onClicked: archive.up()
                        }
                        Item { width: 6; height: 1 }
                        Crumb {
                            anchors.verticalCenter: parent.verticalCenter
                            showIcon: true
                            text: archive.fileName
                            current: archive.folder.length === 0 && archive.search.length === 0
                            onClicked: archive.goTo("")
                        }
                        Repeater {
                            model: archive.search.length > 0 ? [{ name: qsTr("Search results for “%1”").arg(archive.search), path: "" }] : archive.crumbs

                            Row {
                                id: crumbRow
                                required property var modelData
                                required property int index
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                ArchiveIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "chevron"
                                    size: 13
                                    color: UI.Style.inkSubtle
                                }
                                Crumb {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: crumbRow.modelData.name
                                    current: archive.search.length > 0 || crumbRow.index === archive.crumbs.length - 1
                                    onClicked: {
                                        if (archive.search.length === 0) {
                                            archive.goTo(crumbRow.modelData.path);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Banner {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 8
                    visible: window.ready
                    iconName: "info"
                    text: qsTr("This archive is read only. Extract files to change them.")
                }
                Banner {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 8
                    visible: window.ready && archive.lockedFormat
                    warning: true
                    iconName: "lock"
                    text: qsTr("This RAR is protected with the older RAR 4 encryption, which Archive can't unlock. You can see what's in it, but not extract it.")
                }
                Banner {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 8
                    visible: window.ready && archive.namesHidden
                    iconName: "lock"
                    text: qsTr("This archive is protected with a password, and even the names of its files are hidden until you enter it.")
                    action: qsTr("Enter password")
                    onActivated: archive.askPassword()
                }
                Banner {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 8
                    visible: window.ready && archive.unsafeCount > 0
                    warning: true
                    iconName: "warning"
                    text: archive.unsafeCount === 1
                        ? qsTr("1 item in this archive points outside it. Archive won't extract it.")
                        : qsTr("%1 items in this archive point outside it. Archive won't extract them.").arg(archive.unsafeCount)
                }
                Banner {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 8
                    visible: window.ready && archive.errorText.length > 0
                    warning: true
                    iconName: "warning"
                    text: qsTr("Part of this archive couldn't be read: %1").arg(archive.errorText)
                }

                // Column headings; a click sorts by that column.
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    visible: window.ready

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        height: 1
                        color: ArchiveStyle.hairline
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 28
                        anchors.rightMargin: 28
                        spacing: 12

                        Item { width: 18; height: 1 }
                        Heading {
                            width: parent.width - 18 - 80 - 80 - 140 - 100 - 5 * 12
                            text: qsTr("Name")
                            key: "name"
                            sortKey: archive.sortKey
                            ascending: archive.sortAscending
                            onClicked: archive.sortBy("name")
                        }
                        Heading {
                            width: 80
                            alignRight: true
                            text: qsTr("Size")
                            key: "size"
                            sortKey: archive.sortKey
                            ascending: archive.sortAscending
                            onClicked: archive.sortBy("size")
                        }
                        Heading {
                            width: 80
                            alignRight: true
                            text: qsTr("Packed")
                            key: "packed"
                            sortKey: archive.sortKey
                            ascending: archive.sortAscending
                            onClicked: archive.sortBy("packed")
                        }
                        Heading {
                            width: 140
                            inset: 24
                            text: qsTr("Modified")
                            key: "modified"
                            sortKey: archive.sortKey
                            ascending: archive.sortAscending
                            onClicked: archive.sortBy("modified")
                        }
                        Heading {
                            width: 100
                            text: qsTr("Type")
                            key: "type"
                            sortKey: archive.sortKey
                            ascending: archive.sortAscending
                            onClicked: archive.sortBy("type")
                        }
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: window.ready
                    clip: true
                    topMargin: 4
                    bottomMargin: 8
                    model: archive.model
                    currentIndex: -1
                    keyNavigationEnabled: false
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: true
                    C.ScrollBar.vertical: UI.ScrollBar {}
                    Accessible.role: Accessible.Table
                    Accessible.name: qsTr("Contents of %1").arg(archive.fileName)

                    function step(by, modifiers) {
                        if (count === 0) {
                            return;
                        }
                        currentIndex = Math.max(0, Math.min(count - 1, currentIndex + by));
                        positionViewAtIndex(currentIndex, ListView.Contain);
                        if (!(modifiers & Qt.ControlModifier)) {
                            archive.select(currentIndex, modifiers & Qt.ShiftModifier);
                        }
                    }
                    Keys.onUpPressed: event => step(-1, event.modifiers)
                    Keys.onDownPressed: event => step(1, event.modifiers)
                    Keys.onSpacePressed: {
                        if (currentIndex >= 0) {
                            archive.toggle(currentIndex);
                        }
                    }
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0) {
                            archive.activate(currentIndex);
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0) {
                            archive.activate(currentIndex);
                        }
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Backspace) {
                            archive.up();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                            archive.selectAll();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Escape && archive.selectedCount > 0) {
                            archive.clearSelection();
                            event.accepted = true;
                        }
                    }
                    Connections {
                        target: archive
                        function onViewChanged() {
                            list.currentIndex = -1;
                        }
                    }

                    delegate: EntryRow {
                        id: entryRow
                        width: ListView.view.width
                        onPressed: modifiers => {
                            list.currentIndex = index;
                            list.forceActiveFocus();
                            archive.select(index, modifiers);
                        }
                        onOpened: archive.activate(index)
                        onToggled: {
                            list.currentIndex = index;
                            archive.toggle(index);
                        }
                        onMenuRequested: (mx, my) => {
                            const isFolder = entryRow.isDir;
                            const row = entryRow.index;
                            const openable = entryRow.kind !== "unsafe" && entryRow.kind !== "link";
                            rowMenu.entries = [
                                { text: isFolder ? qsTr("Open folder") : qsTr("Open"), icon: "open", enabled: openable, run: () => archive.activate(row) },
                                { text: qsTr("Extract"), icon: "download", enabled: !archive.busy, run: () => archive.extract(false) },
                                { separator: true },
                                { text: qsTr("Select all"), icon: "check", shortcut: "Ctrl+A", run: () => archive.selectAll() }
                            ];
                            rowMenu.popup(entryRow, mx, my);
                        }
                    }

                    UI.Label {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        text: archive.search.length > 0 ? qsTr("Nothing in this archive matches “%1”").arg(archive.search) : qsTr("This folder is empty")
                        size: 13
                        color: UI.Style.inkSubtle
                    }
                }

                // Reading a big archive.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: archive.state === "loading"

                    Column {
                        anchors.centerIn: parent
                        spacing: 12
                        UI.Spinner {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: 28
                        }
                        UI.Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: archive.jobProgress > 0.02 ? qsTr("Reading the archive, %1%").arg(Math.round(archive.jobProgress * 100)) : qsTr("Reading the archive")
                            size: 13
                            color: UI.Style.inkMuted
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: archive.state === "failed"

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(460, parent.width - 48)
                        spacing: 8
                        ArchiveIcon {
                            Layout.alignment: Qt.AlignHCenter
                            name: "warning"
                            size: 28
                            color: UI.Style.warning
                        }
                        UI.Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Archive couldn't open this file")
                            size: 15
                            font.weight: UI.Style.weightSemiBold
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: archive.errorText
                            size: 13
                            color: UI.Style.inkMuted
                            wrapMode: Text.Wrap
                        }
                        UI.Button {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 8
                            size: "small"
                            text: qsTr("Open another archive")
                            onClicked: window.chooseArchive()
                        }
                    }
                }
            }

            ExtractPane {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                visible: window.hasArchive
                archive: archive
                enabled: window.ready && !archive.lockedFormat
                onChooseFolder: window.chooseDestination()
            }
        }

        // The status bar: what is here and the archive's figures, and what
        // Archive opens.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            visible: window.hasArchive
            color: ArchiveStyle.statusShade
            bottomLeftRadius: window.maximised ? 0 : UI.Style.windowRadius - 1
            bottomRightRadius: bottomLeftRadius

            Rectangle {
                width: parent.width
                height: 1
                color: ArchiveStyle.hairline
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18

                UI.Label {
                    visible: window.ready
                    text: {
                        const items = archive.itemCount === 1 ? qsTr("1 item") : qsTr("%1 items").arg(archive.itemCount);
                        return archive.selectedCount > 0 ? qsTr("%1, %2 selected").arg(items).arg(archive.selectedCount) : items;
                    }
                    size: 12
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    text: archive.summary
                    size: 12
                    color: UI.Style.inkMuted
                }
                UI.Label {
                    visible: archive.job === "extract" || archive.job === "test" || archive.job === "open"
                    text: archive.jobText
                    size: 12
                    color: UI.Style.inkMuted
                }
            }
            UI.Label {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: ArchiveApp.formatsText
                size: 12
                color: UI.Style.inkMuted
            }
        }
    }

    UI.Menu {
        id: rowMenu
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open an archive")
        nameFilters: [qsTr("Archives (*.zip *.7z *.rar *.tar *.tar.gz *.tgz *.tar.xz *.txz *.tar.bz2 *.tbz2 *.tar.zst *.gz *.xz *.bz2 *.zst *.cab *.cpio *.lzh)"), qsTr("All files (*)")]
        onAccepted: window.openUrl(selectedFile)
    }
    FileDialog {
        id: addFilesDialog
        title: qsTr("Add files to a new archive")
        fileMode: FileDialog.OpenFiles
        onAccepted: newDialog.startFrom(archive, selectedFiles)
    }
    FolderDialog {
        id: destinationDialog
        title: qsTr("Extract to")
        onAccepted: archive.setDestinationUrl(selectedFolder)
    }

    NewArchiveDialog {
        id: newDialog
        objectName: "newDialog"
        onCreated: (path, message) => {
            window.say(message);
            window.openPath(path);
        }
        onFailed: (heading, message, details) => messageDialog.show(heading, message, details)
    }

    UI.Dialog {
        id: passwordDialog
        property bool wrong: false
        function ask(wasWrong) {
            wrong = wasWrong;
            passwordField.text = "";
            passwordField.error = wasWrong;
            open();
            passwordField.forceActiveFocus();
        }
        title: qsTr("Enter the password")
        text: wrong ? qsTr("That password isn't right. Check it and try again.") : qsTr("%1 is protected with a password.").arg(archive.fileName)

        UI.TextField {
            id: passwordField
            Layout.fillWidth: true
            echoMode: TextInput.Password
            revealable: true
            placeholderText: qsTr("Password")
            onTextEdited: error = false
            onAccepted: {
                if (text.length > 0) {
                    archive.setPassword(text);
                    text = "";
                    passwordDialog.close();
                }
            }
        }

        buttons: [
            UI.Button {
                text: qsTr("Cancel")
                onClicked: {
                    archive.forgetPending();
                    passwordField.text = "";
                    passwordDialog.close();
                }
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Unlock")
                enabled: passwordField.text.length > 0
                onClicked: {
                    archive.setPassword(passwordField.text);
                    passwordField.text = "";
                    passwordDialog.close();
                }
            }
        ]
    }

    UI.Dialog {
        id: messageDialog
        property var details: []
        function show(heading, message, files) {
            title = heading;
            text = message;
            details = files ?? [];
            open();
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(messageDialog.details.length, 6) * 26 + 12
            visible: messageDialog.details.length > 0
            radius: 8
            color: ArchiveStyle.fieldFill
            border.width: 1
            border.color: ArchiveStyle.fieldEdge

            ListView {
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: messageDialog.details
                boundsBehavior: Flickable.StopAtBounds
                C.ScrollBar.vertical: UI.ScrollBar {}
                delegate: UI.Label {
                    required property string modelData
                    width: ListView.view.width - 12
                    height: 26
                    leftPadding: 6
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    size: 12.5
                    elide: Text.ElideMiddle
                }
            }
        }

        buttons: [
            UI.Button {
                kind: "primary"
                text: qsTr("OK")
                onClicked: messageDialog.close()
            }
        ]
    }
}
