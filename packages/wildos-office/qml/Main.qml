import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import WildOS.Office
import WildOS.UI as UI

// The Write, Tables and Slides start screen (WriteStart.dc): the rail with Home,
// New from template and Open, the templates, Search documents, and the
// Recent and Pinned lists. Whatever is picked opens in LibreOffice, and
// once its window has taken over this one goes.
UI.AppWindow {
    id: window

    // "home" or "templates".
    property string page: "home"
    // "recent" or "pinned".
    property string list: "recent"
    // The name of what is opening, while LibreOffice starts.
    property string opening: ""
    // Held here so the splash is not collected while it shows.
    property var splashWindow: null
    readonly property bool searchingNow: search.text.trim().length > 0

    width: 1384
    height: 802
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("Start")

    // On a cold start the start screen makes way for the loading splash
    // (board 12.5), which goes once the document's window takes focus. With
    // LibreOffice already running the document opens at once, so the start
    // screen just waits for it.
    function started(ok, name, warm, splashName) {
        if (!ok) {
            return;
        }
        opening = name;
        if (warm) {
            giveUp.restart();
            return;
        }
        const component = Qt.createComponent("WildOS.Office", "Splash");
        if (component.status === Component.Ready) {
            splashWindow = component.createObject(null, { fileName: splashName });
            // The start screen keeps focus until the splash is up, so the
            // splash inherits it and can tell when the document takes over.
            splashWindow.visibleChanged.connect(() => {
                if (splashWindow.visible) {
                    window.visible = false;
                }
            });
        } else {
            giveUp.restart();
        }
    }
    function openDocument(path, name) {
        const warm = Office.isLibreOfficeRunning();
        started(Office.open(path), name, warm, name);
    }
    function openTemplate(entry) {
        const warm = Office.isLibreOfficeRunning();
        started(entry.blank ? Office.newBlank() : Office.openTemplate(entry.file), entry.name, warm, entry.blank ? "" : entry.name);
    }

    // The document's window taking focus is the sign LibreOffice is up.
    onActiveChanged: {
        // Once the splash is up it decides when to go. A document that
        // opens before the splash has shown ends both at once.
        if (splashWindow) {
            if (!active && !splashWindow.visible) {
                splashWindow.finish();
            }
            return;
        }
        if (!active && opening.length > 0) {
            Qt.quit();
        } else if (active && opening.length === 0) {
            Office.reload();
        }
    }
    // LibreOffice can take half a minute to show its first window on a
    // slow PC; if it never takes focus, the start screen goes anyway.
    Timer {
        id: giveUp
        interval: 60000
        onTriggered: Qt.quit()
    }

    Connections {
        target: Office
        function onFailed(message) {
            toast.show(message);
        }
    }

    Shortcut {
        sequence: StandardKey.New
        onActivated: window.openTemplate({ blank: true, name: window.blankEntry.name })
    }
    Shortcut {
        sequence: StandardKey.Open
        onActivated: openDialog.open()
    }
    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+E"]
        onActivated: {
            window.page = "home";
            search.forceActiveFocus();
        }
    }

    FileDialog {
        id: openDialog
        title: Office.tables ? qsTr("Open a spreadsheet") : Office.slides ? qsTr("Open a presentation") : qsTr("Open a document")
        fileMode: FileDialog.OpenFile
        currentFolder: Office.documentsFolder
        nameFilters: Office.nameFilters
        onAccepted: {
            const path = decodeURIComponent(selectedFile.toString());
            window.openDocument(selectedFile.toString(), path.substring(path.lastIndexOf("/") + 1));
        }
    }

    readonly property var blankEntry: ({
        blank: true,
        name: Office.tables ? qsTr("Blank workbook") : Office.slides ? qsTr("Blank presentation") : qsTr("Blank document"),
        description: Office.tables ? qsTr("An empty workbook in Carlito 11, ready for anything.")
            : Office.slides ? qsTr("An empty widescreen slide, ready for a title.")
            : qsTr("An empty A4 page with the Wild OS styles."),
        thumbnail: ""
    })

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            tinted: true
            frame: window.frame

            Row {
                height: parent.height
                spacing: 10

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 18
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    UI.Label {
                        text: qsTr("Start")
                        font.weight: UI.Style.weightMedium
                        color: window.active ? UI.Style.ink : UI.Style.inkMuted
                    }
                    UI.Label {
                        text: Office.appName
                        color: UI.Style.inkDisabled
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // The rail.
            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 250
                color: UI.Style.shade(0.30, 0.04)
                bottomLeftRadius: window.maximised ? 0 : UI.Style.windowRadius - 1

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: UI.Style.divider
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.topMargin: 22
                    anchors.bottomMargin: 22
                    spacing: 4

                    Row {
                        Layout.leftMargin: 8
                        Layout.bottomMargin: 18
                        spacing: 12
                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            size: 36
                        }
                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Office.appName
                            size: 20
                            font.weight: UI.Style.weightSemiBold
                        }
                    }

                    RailButton {
                        text: qsTr("Home")
                        iconName: "clock"
                        current: window.page === "home"
                        onClicked: window.page = "home"
                    }
                    RailButton {
                        text: qsTr("New from template")
                        iconName: "document"
                        current: window.page === "templates"
                        onClicked: window.page = "templates"
                    }
                    RailButton {
                        text: qsTr("Open")
                        iconName: "open-folder"
                        onClicked: openDialog.open()
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: note.implicitHeight + 24
                        radius: 10
                        color: UI.Style.surfaceRest
                        border.width: 1
                        border.color: UI.Style.borderSubtle

                        Column {
                            id: note
                            x: 12
                            y: 12
                            width: parent.width - 24
                            spacing: 6
                            UI.Label {
                                width: parent.width
                                text: Office.formatName
                                size: 12.5
                                font.weight: UI.Style.weightSemiBold
                            }
                            UI.Label {
                                width: parent.width
                                text: Office.formatNote
                                size: 12
                                color: UI.Style.inkMuted
                                wrapMode: Text.WordWrap
                                lineHeight: 1.3
                            }
                        }
                    }
                }
            }

            // Home.
            ColumnLayout {
                visible: window.page === "home"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 34
                Layout.rightMargin: 34
                Layout.topMargin: 26
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Start something new")
                        size: 22
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.SearchField {
                        id: search
                        Layout.preferredWidth: 280
                        implicitHeight: 32
                        iconLeading: true
                        placeholderText: Office.tables ? qsTr("Search spreadsheets") : Office.slides ? qsTr("Search presentations") : qsTr("Search documents")
                        onTextChanged: searchTimer.restart()
                        onAccepted: {
                            searchTimer.stop();
                            Office.search(text);
                        }
                        onDownPressed: documents.forceActiveFocus()
                    }
                    Timer {
                        id: searchTimer
                        interval: 250
                        onTriggered: Office.search(search.text)
                    }
                }

                ListView {
                    id: strip
                    Layout.fillWidth: true
                    Layout.leftMargin: -8
                    implicitHeight: 220
                    orientation: ListView.Horizontal
                    spacing: 6
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: [window.blankEntry].concat(Office.templates)
                    delegate: TemplateCard {
                        required property var modelData
                        name: modelData.name
                        thumbnail: modelData.thumbnail
                        blank: modelData.blank === true
                        onClicked: window.openTemplate(modelData)
                    }
                    QQC.ScrollBar.horizontal: UI.ScrollBar {}
                }

                Row {
                    Layout.topMargin: 6
                    spacing: 18

                    Repeater {
                        model: window.searchingNow ? [{ id: "results", name: qsTr("Results") }]
                            : [{ id: "recent", name: qsTr("Recent") }, { id: "pinned", name: qsTr("Pinned") }]
                        delegate: T_Tab {}
                    }
                    UI.Spinner {
                        visible: window.searchingNow && Office.searching
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    implicitHeight: 26
                    spacing: 0
                    Item {
                        implicitWidth: 52
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        text: qsTr("Name")
                        size: 12
                        color: UI.Style.inkDisabled
                    }
                    UI.Label {
                        Layout.preferredWidth: 194
                        text: qsTr("Modified")
                        size: 12
                        color: UI.Style.inkDisabled
                    }
                }

                ListView {
                    id: documents
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: -10
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    model: window.searchingNow ? Office.results : window.list === "pinned" ? Office.pinned : Office.recent
                    delegate: DocumentRow {
                        removable: !window.searchingNow && window.list === "recent"
                        onOpenRequested: (path, name) => window.openDocument(path, name)
                    }
                    QQC.ScrollBar.vertical: UI.ScrollBar {}

                    UI.Label {
                        visible: documents.count === 0 && !(window.searchingNow && Office.searching)
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 40
                        width: Math.min(parent.width - 40, 420)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: UI.Style.inkMuted
                        text: window.searchingNow
                            ? qsTr("Nothing called “%1” in Documents, Desktop or Downloads.").arg(search.text.trim())
                            : window.list === "pinned"
                                ? (Office.tables ? qsTr("Pin a spreadsheet from its … menu to keep it here.")
                                   : Office.slides ? qsTr("Pin a presentation from its … menu to keep it here.")
                                   : qsTr("Pin a document from its … menu to keep it here."))
                                : (Office.tables ? qsTr("Spreadsheets you open in Tables show up here.")
                                   : Office.slides ? qsTr("Presentations you open in Slides show up here.")
                                   : qsTr("Documents you open in Write show up here."))
                    }
                }
            }

            // New from template.
            Flickable {
                id: gallery
                visible: window.page === "templates"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: galleryColumn.implicitHeight + 52
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                QQC.ScrollBar.vertical: UI.ScrollBar {}

                Column {
                    id: galleryColumn
                    x: 34
                    y: 26
                    width: gallery.width - 68
                    spacing: 14

                    UI.Label {
                        text: qsTr("New from template")
                        size: 22
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Label {
                        width: parent.width
                        text: Office.tables
                            ? qsTr("Each one opens as a new workbook with example figures in it. Type over them; the sums keep up.")
                            : Office.slides ? qsTr("Start from a blank presentation, or from one you saved as a template.")
                            : qsTr("Each one opens as a new document with example text in it. Type over it and save it as your own.")
                        color: UI.Style.inkMuted
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        width: parent.width + 8
                        x: -8
                        spacing: 10
                        Repeater {
                            model: [window.blankEntry].concat(Office.templates)
                            delegate: TemplateCard {
                                required property var modelData
                                width: 160
                                name: modelData.name
                                thumbnail: modelData.thumbnail
                                description: modelData.description
                                blank: modelData.blank === true
                                onClicked: window.openTemplate(modelData)
                            }
                        }
                    }

                    UI.Label {
                        visible: Office.userTemplates.length > 0
                        topPadding: 12
                        text: qsTr("Your templates")
                        size: 16
                        font.weight: UI.Style.weightSemiBold
                    }
                    Flow {
                        visible: Office.userTemplates.length > 0
                        width: parent.width + 8
                        x: -8
                        spacing: 10
                        Repeater {
                            model: Office.userTemplates
                            delegate: TemplateCard {
                                required property var modelData
                                width: 160
                                name: modelData.name
                                description: modelData.description
                                onClicked: window.openTemplate(modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // While LibreOffice starts: the window stays, quietly saying so.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: UI.Style.titleBarHeight
        visible: window.opening.length > 0
        color: UI.Style.scrim
        bottomLeftRadius: window.maximised ? 0 : UI.Style.windowRadius
        bottomRightRadius: bottomLeftRadius

        MouseArea {
            anchors.fill: parent
        }
        Rectangle {
            anchors.centerIn: parent
            width: openingRow.implicitWidth + 40
            height: 56
            radius: UI.Style.radiusMd
            color: UI.Style.glassMenu
            border.width: 1
            border.color: UI.Style.borderMenu
            Row {
                id: openingRow
                anchors.centerIn: parent
                spacing: 12
                UI.Spinner {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Opening %1").arg(window.opening)
                    size: 14
                }
            }
        }
    }

    UI.Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }

    component T_Tab: Item {
        id: tab
        required property var modelData
        readonly property bool active: modelData.id === "results" || window.list === modelData.id
        implicitWidth: tabLabel.implicitWidth
        implicitHeight: 24
        Accessible.role: Accessible.PageTab
        Accessible.name: modelData.name

        UI.Label {
            id: tabLabel
            anchors.verticalCenter: parent.verticalCenter
            text: tab.modelData.name
            size: tab.active ? 16 : 13
            font.weight: tab.active ? UI.Style.weightSemiBold : UI.Style.weightRegular
            color: tab.active ? UI.Style.ink : tabMouse.containsMouse ? UI.Style.ink : UI.Style.inkMuted
        }
        MouseArea {
            id: tabMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: tab.active ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: if (tab.modelData.id !== "results") window.list = tab.modelData.id
        }
    }
}
