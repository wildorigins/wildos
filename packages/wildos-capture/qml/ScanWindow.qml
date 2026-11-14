pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI

// Capture's Scan tab (CaptureScan.dc, board 15.30): Screenshot, Record and
// Scan along the toolbar with Rotate, Crop, Mark up and Remove page; the
// scanner and its settings on the left; the page just scanned in the
// middle; the pages so far on the right with Add page; Save as pictures,
// Open in PDF to sign and Save as PDF along the foot.
UI.AppWindow {
    id: win

    readonly property var scanner: CaptureApp.scanner
    readonly property var pages: scanner.pages
    readonly property var page: scanner.current >= 0 && scanner.current < pages.length ? pages[scanner.current] : null
    readonly property bool ready: scanner.device.length > 0 && !scanner.opening
    readonly property bool canEdit: page !== null && !scanner.scanning && !scanner.busy
    property bool cropping: false
    // Pages scanned or changed since the last save, which closing asks
    // about.
    property bool unsaved: false
    property bool closeAfterSave: false
    property bool closingNow: false
    property real lastLook: 0

    signal done()

    function band(darkAlpha: real, lightAlpha: real): color {
        return UI.Style.dark ? UI.Style.shade(darkAlpha, 0) : UI.Style.layer(0, false, lightAlpha);
    }
    function megabytes(bytes: real): string {
        return bytes >= 1024 * 1024 ? qsTr("%1 MB").arg((bytes / 1024 / 1024).toLocaleString(Qt.locale(), "f", 1))
            : qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)));
    }
    function scanLabel(): string {
        if (scanner.source === "feeder" && scanner.hasFeeder) {
            return pages.length > 0 ? qsTr("Scan more from the feeder") : qsTr("Scan the feeder");
        }
        return pages.length > 0 ? qsTr("Scan the next page") : qsTr("Scan");
    }
    function startCapture(record: bool): void {
        cropping = false;
        win.showMinimized();
        CaptureApp.startCapture(record);
    }
    function markUp(): void {
        cropping = false;
        CaptureApp.markUpScanPage(scanner.pageFile());
    }
    function closeNow(): void {
        closingNow = true;
        close();
    }

    deep: true
    title: qsTr("Scan")
    width: Math.round(Math.min(1384, Screen.desktopAvailableWidth - 56))
    height: Math.round(Math.min(802, Screen.desktopAvailableHeight - 56))
    minimumWidth: 980
    minimumHeight: 620

    onClosing: close => {
        if (closingNow) {
            scanner.cancel();
            scanner.forget();
            done();
            return;
        }
        if (unsaved && pages.length > 0) {
            close.accepted = false;
            unsavedDialog.open();
            return;
        }
        scanner.cancel();
        scanner.forget();
        done();
    }

    Connections {
        target: win.scanner
        function onPagesChanged() {
            win.unsaved = win.pages.length > 0;
        }
        function onSaid(message, file) {
            toast.file = file;
            toast.show(message, file === "undo" ? qsTr("Undo") : file.length > 0 ? qsTr("Show in folder") : "");
            if (file.length > 0 && file !== "undo") {
                win.unsaved = false;
                if (win.closeAfterSave) {
                    win.closeNow();
                }
            }
            win.closeAfterSave = false;
        }
        // With pages on screen a problem is a note; with none it fills the
        // middle, where the pages would be.
        function onProblemChanged() {
            if (win.scanner.problem.title && win.pages.length > 0) {
                toast.file = "";
                toast.show(win.scanner.problem.title, "");
            }
        }
    }

    Shortcut { sequences: [StandardKey.Undo]; enabled: win.scanner.canUndo && !win.scanner.scanning; onActivated: win.scanner.undo() }
    Shortcut { sequences: [StandardKey.Delete]; enabled: win.canEdit; onActivated: win.scanner.removePage() }
    Shortcut { sequences: [StandardKey.Save]; enabled: win.pages.length > 0 && !win.scanner.busy; onActivated: win.scanner.savePdf() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: win.close() }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (win.cropping) {
                win.cropping = false;
            } else if (win.scanner.scanning) {
                win.scanner.cancel();
            } else {
                win.close();
            }
        }
    }

    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 22
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        color: UI.Style.divider
    }

    component FieldLabel: UI.Label {
        size: 12.5
        color: UI.Style.inkMuted
    }

    // The toolbar's Screenshot and Record pill, with a third choice.
    component Choices: Rectangle {
        id: choices
        property var options: []
        property string value
        property var available: []
        signal chosen(string value)

        Layout.fillWidth: true
        Layout.preferredHeight: 32
        radius: 7
        color: UI.Style.surfaceRest
        border.width: 1
        border.color: UI.Style.borderField

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 3
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: choices.options
                delegate: UI.SegmentButton {
                    required property var modelData
                    height: 26
                    checkable: false
                    text: modelData.text
                    checked: choices.value === modelData.value
                    enabled: choices.available.length === 0 || choices.available.indexOf(modelData.value) >= 0
                    onClicked: choices.chosen(modelData.value)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: win.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: win.frame
            showTitle: false

            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/WildOS/Capture/data/wildos-capture.svg"
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Scan")
                    font.weight: UI.Style.weightMedium
                    color: win.active ? UI.Style.ink : UI.Style.inkMuted
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Capture")
                    color: UI.Style.inkSubtle
                }
            }
        }

        // The toolbar, as the editor's: 50px on a deeper band.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: win.band(0.2, 0.01)

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: UI.Style.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 4

                ToolButton {
                    iconName: "camera"
                    text: qsTr("Screenshot")
                    enabled: !win.scanner.scanning
                    onClicked: win.startCapture(false)
                }
                ToolButton {
                    iconName: "record"
                    text: qsTr("Record")
                    enabled: CaptureApp.canRecord && !win.scanner.scanning
                    onClicked: win.startCapture(true)
                }
                ToolButton {
                    iconName: "scan"
                    text: qsTr("Scan")
                    active: true
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    iconName: "rotate"
                    text: qsTr("Rotate")
                    tip: qsTr("Turn the page a quarter to the right")
                    enabled: win.canEdit
                    onClicked: {
                        win.cropping = false;
                        win.scanner.rotate();
                    }
                }
                ToolButton {
                    iconName: "crop"
                    text: qsTr("Crop")
                    tip: win.cropping ? qsTr("Drag over the part to keep") : ""
                    active: win.cropping
                    enabled: win.canEdit
                    onClicked: win.cropping = !win.cropping
                }
                ToolButton {
                    iconName: "pen"
                    text: qsTr("Mark up")
                    enabled: win.canEdit
                    onClicked: win.markUp()
                }
                Divider {}
                ToolButton {
                    iconName: "remove"
                    text: qsTr("Remove page")
                    enabled: win.canEdit
                    onClicked: {
                        win.cropping = false;
                        win.scanner.removePage();
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // The scanner and how to scan.
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: win.band(0.12, 0.01)

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: UI.Style.divider
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    anchors.rightMargin: 19
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FieldLabel { text: qsTr("Scanner") }
                        UI.ComboBox {
                            id: scannerBox
                            Layout.fillWidth: true
                            readonly property var choices: win.scanner.devices.map(d => ({ value: d.id, text: d.name }))
                            model: choices.length > 0 ? choices
                                : [{ value: "", text: win.scanner.looking ? qsTr("Looking for scanners…") : qsTr("No scanner found") }]
                            currentIndex: Math.max(0, model.findIndex(o => o.value === win.scanner.device))
                            enabled: choices.length > 0 && !win.scanner.scanning
                            onActivated: win.scanner.choose(currentValue)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FieldLabel { text: qsTr("Scan from") }
                        Choices {
                            options: [{ value: "glass", text: qsTr("Glass") }, { value: "feeder", text: qsTr("Feeder") }]
                            value: win.scanner.hasFeeder ? win.scanner.source : "glass"
                            available: win.ready ? [win.scanner.hasGlass ? "glass" : "", win.scanner.hasFeeder ? "feeder" : ""] : ["glass"]
                            enabled: !win.scanner.scanning
                            onChosen: value => win.scanner.source = value
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FieldLabel { text: qsTr("Colour") }
                        Choices {
                            options: [{ value: "colour", text: qsTr("Colour") }, { value: "grey", text: qsTr("Grey") }, { value: "bw", text: qsTr("Black and white") }]
                            value: win.scanner.colour
                            enabled: !win.scanner.scanning
                            onChosen: value => win.scanner.colour = value
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FieldLabel { text: qsTr("Quality") }
                        UI.ComboBox {
                            Layout.fillWidth: true
                            model: win.scanner.qualities
                            currentIndex: Math.max(0, model.findIndex(o => o.value === win.scanner.dpi))
                            enabled: !win.scanner.scanning
                            onActivated: win.scanner.dpi = currentValue
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FieldLabel { text: qsTr("Paper size") }
                        UI.ComboBox {
                            Layout.fillWidth: true
                            model: win.scanner.paperSizes
                            currentIndex: Math.max(0, model.findIndex(o => o.value === win.scanner.paper))
                            enabled: !win.scanner.scanning
                            onActivated: win.scanner.paper = currentValue
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        UI.Label {
                            Layout.fillWidth: true
                            text: qsTr("Straighten and crop each page")
                        }
                        UI.Toggle {
                            Accessible.name: qsTr("Straighten and crop each page")
                            checked: win.scanner.straighten
                            onToggled: win.scanner.straighten = checked
                        }
                    }

                    Item { Layout.fillHeight: true }

                    ToolButton {
                        Layout.fillWidth: true
                        visible: !win.scanner.scanning
                        primary: true
                        iconName: "scan"
                        text: win.scanLabel()
                        enabled: win.ready && !win.scanner.busy
                        onClicked: {
                            win.cropping = false;
                            win.scanner.scan();
                        }
                    }
                    UI.Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        visible: win.scanner.scanning
                        text: win.scanner.cancelling ? qsTr("Stopping…") : qsTr("Cancel")
                        enabled: !win.scanner.cancelling
                        onClicked: win.scanner.cancel()
                    }

                    UI.Label {
                        Layout.fillWidth: true
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                        lineHeight: 1.25
                        text: qsTr("Scans stay on this PC.")
                    }
                }
            }

            // The page, centred on the deeper ground.
            Rectangle {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: win.band(0.22, 0.03)
                clip: true

                readonly property bool showPage: win.page !== null && !win.scanner.scanning

                UI.Label {
                    x: 18
                    y: 14
                    visible: stage.showPage
                    size: 12.5
                    color: UI.Style.inkMuted
                    text: win.page ? qsTr("Page %1 of %2").arg(win.scanner.current + 1).arg(win.pages.length)
                        + (win.page.note.length > 0 ? " · " + win.page.note : "") : ""
                }

                Rectangle {
                    id: card
                    x: view.x
                    y: view.y
                    width: view.width
                    height: view.height
                    radius: 3
                    color: "#fbfbfd"
                    visible: false
                }
                MultiEffect {
                    anchors.fill: card
                    visible: stage.showPage
                    source: card
                    shadowEnabled: true
                    shadowColor: UI.Style.shadowWindow
                    shadowBlur: 0.8
                    shadowVerticalOffset: 8
                    autoPaddingEnabled: true
                }

                Item {
                    id: view
                    readonly property real pw: win.page ? win.page.width : 1
                    readonly property real ph: win.page ? win.page.height : 1
                    readonly property real fit: Math.max(0.01, Math.min((stage.width - 96) / pw, (stage.height - 96) / ph))
                    visible: stage.showPage
                    width: Math.round(pw * fit)
                    height: Math.round(ph * fit)
                    x: Math.round((stage.width - width) / 2)
                    y: Math.round((stage.height - height) / 2)

                    Image {
                        anchors.fill: parent
                        source: win.page ? win.page.source : ""
                        sourceSize.width: Math.min(view.pw, Math.ceil(view.width * Screen.devicePixelRatio))
                        asynchronous: true
                        cache: false
                        smooth: true
                        mipmap: true
                    }

                    // Crop: the part kept stays clear and the rest dims, as
                    // in the editor.
                    Item {
                        id: cropLayer
                        anchors.fill: parent
                        visible: win.cropping
                        property rect r: Qt.rect(0, 0, 0, 0)
                        readonly property color dim: Qt.rgba(0, 0, 0, 0.5)

                        Rectangle { width: parent.width; height: cropLayer.r.height > 0 ? cropLayer.r.y : parent.height; color: cropLayer.dim }
                        Rectangle { visible: cropLayer.r.height > 0; y: cropLayer.r.y; width: cropLayer.r.x; height: cropLayer.r.height; color: cropLayer.dim }
                        Rectangle { visible: cropLayer.r.height > 0; x: cropLayer.r.x + cropLayer.r.width; y: cropLayer.r.y; width: parent.width - x; height: cropLayer.r.height; color: cropLayer.dim }
                        Rectangle { visible: cropLayer.r.height > 0; y: cropLayer.r.y + cropLayer.r.height; width: parent.width; height: parent.height - y; color: cropLayer.dim }
                        Rectangle {
                            x: cropLayer.r.x
                            y: cropLayer.r.y
                            width: cropLayer.r.width
                            height: cropLayer.r.height
                            color: "transparent"
                            border.width: 1.5
                            border.color: UI.Style.inkOnWallpaper
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.CrossCursor
                            property point start
                            onPressed: mouse => {
                                start = Qt.point(mouse.x, mouse.y);
                                cropLayer.r = Qt.rect(mouse.x, mouse.y, 0, 0);
                            }
                            onPositionChanged: mouse => {
                                const x = Math.max(0, Math.min(width, mouse.x));
                                const y = Math.max(0, Math.min(height, mouse.y));
                                cropLayer.r = Qt.rect(Math.min(start.x, x), Math.min(start.y, y), Math.abs(x - start.x), Math.abs(y - start.y));
                            }
                            onReleased: {
                                // A copy: a rect read from a property follows
                                // the property, which is cleared next.
                                const r = Qt.rect(cropLayer.r.x, cropLayer.r.y, cropLayer.r.width, cropLayer.r.height);
                                cropLayer.r = Qt.rect(0, 0, 0, 0);
                                if (r.width >= 8 && r.height >= 8) {
                                    win.scanner.crop(Qt.rect(r.x / width, r.y / height, r.width / width, r.height / height));
                                    win.cropping = false;
                                }
                            }
                        }
                    }
                }

                // Instead of a page: looking, no scanner, a problem, nothing
                // scanned yet, or a page on its way.
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(400, parent.width - 48)
                    visible: !stage.showPage
                    spacing: 10

                    readonly property string state: win.scanner.scanning ? "scanning"
                        : win.scanner.problem.title ? "problem"
                        : win.scanner.looking && win.scanner.devices.length === 0 ? "looking"
                        : win.scanner.devices.length === 0 ? "none"
                        : win.scanner.opening ? "opening" : "ready"

                    UI.Spinner {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 4
                        visible: parent.state === "looking" || parent.state === "opening" || (parent.state === "scanning" && win.scanner.progress < 0)
                        size: 28
                    }
                    CaptureIcon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 4
                        visible: parent.state === "none" || parent.state === "ready" || parent.state === "problem"
                        name: parent.state === "problem" ? "warning" : "scan"
                        size: 32
                        strokeWidth: 1.6
                        color: UI.Style.inkSubtle
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        size: UI.Style.fontSection
                        font.weight: UI.Style.weightSemiBold
                        wrapMode: Text.Wrap
                        text: {
                            switch (parent.state) {
                            case "scanning": return win.scanner.cancelling ? qsTr("Stopping the scan") : qsTr("Scanning page %1").arg(win.scanner.scanningPage);
                            case "problem": return win.scanner.problem.title;
                            case "looking": return qsTr("Looking for scanners");
                            case "opening": return qsTr("Getting the scanner ready");
                            case "none": return qsTr("No scanner found");
                            default: return win.scanner.source === "feeder" && win.scanner.hasFeeder ? qsTr("Put the pages in the feeder") : qsTr("Put a page face down on the glass");
                            }
                        }
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                        lineHeight: 1.25
                        text: {
                            switch (parent.state) {
                            case "scanning": return !win.scanner.cancelling && win.scanner.source === "feeder" && win.scanner.hasFeeder ? qsTr("Each page shows here as it comes through.") : "";
                            case "problem": return win.scanner.problem.text;
                            case "looking": return qsTr("On your network and connected to this PC.");
                            case "opening": return "";
                            case "none": return qsTr("Switch your scanner on and connect it to this PC or your network. Most made since 2014 work without a driver.");
                            default: return qsTr("Then press %1.").arg(win.scanLabel());
                            }
                        }
                    }
                    UI.ProgressBar {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 240
                        Layout.topMargin: 6
                        visible: parent.state === "scanning" && win.scanner.progress >= 0
                        value: Math.max(0, win.scanner.progress)
                    }
                    UI.Button {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 6
                        visible: parent.state === "none" || (parent.state === "problem" && win.scanner.devices.length === 0)
                        text: qsTr("Look again")
                        onClicked: {
                            win.scanner.clearProblem();
                            win.look();
                        }
                    }
                }
            }

            // The pages so far, then Add page.
            Rectangle {
                Layout.preferredWidth: 138
                Layout.fillHeight: true
                color: win.band(0.12, 0.01)

                Rectangle {
                    width: 1
                    height: parent.height
                    color: UI.Style.divider
                }

                ListView {
                    id: strip
                    anchors.fill: parent
                    anchors.leftMargin: 1
                    topMargin: 18
                    bottomMargin: 18
                    spacing: 14
                    clip: true
                    model: win.pages
                    currentIndex: win.scanner.current
                    boundsBehavior: Flickable.StopAtBounds
                    highlightFollowsCurrentItem: false
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                    T.ScrollBar.vertical: UI.ScrollBar {}

                    delegate: Item {
                        id: thumb
                        required property var modelData
                        required property int index
                        readonly property bool current: index === win.scanner.current
                        width: strip.width
                        height: Math.round(90 * Math.min(2, modelData.height / Math.max(1, modelData.width)))

                        Rectangle {
                            id: sheet
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 90
                            height: parent.height
                            radius: 2
                            color: "#fbfbfd"
                            visible: false
                        }
                        MultiEffect {
                            anchors.fill: sheet
                            source: sheet
                            shadowEnabled: true
                            shadowColor: UI.Style.shadowWindow
                            shadowBlur: 0.3
                            shadowVerticalOffset: 2
                            autoPaddingEnabled: true
                        }
                        Image {
                            anchors.fill: sheet
                            source: thumb.modelData.source
                            sourceSize.width: 180
                            asynchronous: true
                            cache: false
                            smooth: true
                            fillMode: Image.PreserveAspectFit
                        }
                        Rectangle {
                            anchors.fill: sheet
                            anchors.margins: -4
                            radius: 5
                            color: "transparent"
                            visible: thumb.current
                            border.width: 2
                            border.color: UI.Style.accent
                        }
                        Rectangle {
                            x: sheet.x + 6
                            anchors.bottom: sheet.bottom
                            anchors.bottomMargin: 6
                            width: number.implicitWidth + 12
                            height: number.implicitHeight + 2
                            radius: 4
                            color: Qt.rgba(0, 0, 0, 0.6)
                            UI.Label {
                                id: number
                                anchors.centerIn: parent
                                size: 11
                                color: "#ffffff"
                                text: thumb.index + 1
                            }
                        }
                        MouseArea {
                            anchors.fill: sheet
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Page %1").arg(thumb.index + 1)
                            onClicked: {
                                win.cropping = false;
                                win.scanner.current = thumb.index;
                            }
                        }
                    }

                    footer: Item {
                        width: strip.width
                        height: 127 + (win.pages.length > 0 ? 14 : 0)

                        Item {
                            id: add
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            width: 90
                            height: 127
                            opacity: addArea.enabled ? 1 : 0.45

                            Shape {
                                anchors.fill: parent
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: addArea.containsMouse ? UI.Style.borderControl : UI.Style.borderField
                                    strokeWidth: 1.5
                                    strokeStyle: ShapePath.DashLine
                                    dashPattern: [3, 2.5]
                                    fillColor: addArea.containsMouse ? UI.Style.surfaceHover : "transparent"
                                    PathRectangle {
                                        x: 0.75
                                        y: 0.75
                                        width: add.width - 1.5
                                        height: add.height - 1.5
                                        radius: 4
                                    }
                                }
                            }
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                CaptureIcon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "plus"
                                    size: 16
                                    color: UI.Style.inkMuted
                                }
                                UI.Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    size: 12
                                    color: UI.Style.inkMuted
                                    text: qsTr("Add page")
                                }
                            }
                            MouseArea {
                                id: addArea
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: win.ready && !win.scanner.scanning && !win.scanner.busy
                                cursorShape: Qt.PointingHandCursor
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Add page")
                                onClicked: {
                                    win.cropping = false;
                                    win.scanner.scan();
                                }
                            }
                        }
                    }
                }
            }
        }

        // The foot: how many pages and the PDF's size; the three ways out.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: win.band(0.3, 0.02)
            bottomLeftRadius: win.maximised ? 0 : UI.Style.windowRadius - 1
            bottomRightRadius: bottomLeftRadius

            Rectangle {
                width: parent.width
                height: 1
                color: UI.Style.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10

                UI.Label {
                    Layout.fillWidth: true
                    size: 12.5
                    color: UI.Style.inkMuted
                    text: win.pages.length === 0 ? qsTr("No pages yet")
                        : (win.pages.length === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(win.pages.length)) + (win.scanner.pdfSize > 0 ? " · " + qsTr("about %1 as a PDF").arg(win.megabytes(win.scanner.pdfSize)) : "")
                }
                UI.Button {
                    Layout.preferredHeight: 32
                    text: qsTr("Save as pictures")
                    enabled: win.pages.length > 0 && !win.scanner.busy && !win.scanner.scanning
                    onClicked: win.scanner.savePictures()
                }
                UI.Button {
                    Layout.preferredHeight: 32
                    visible: win.scanner.canSign
                    iconName: "pencil"
                    text: qsTr("Open in PDF to sign")
                    enabled: win.pages.length > 0 && !win.scanner.busy && !win.scanner.scanning
                    onClicked: win.scanner.openToSign()
                }
                UI.Button {
                    Layout.preferredHeight: 32
                    kind: "primary"
                    text: win.scanner.busy ? qsTr("Saving…") : qsTr("Save as PDF")
                    enabled: win.pages.length > 0 && !win.scanner.busy && !win.scanner.scanning
                    onClicked: win.scanner.savePdf()
                }
            }
        }
    }

    UI.Toast {
        id: toast
        property string file
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 72
        onActionClicked: {
            if (file === "undo") {
                win.scanner.undo();
            } else if (file.length > 0) {
                CaptureApp.showInFolder("file://" + file);
            }
            hide();
        }
    }

    UI.Dialog {
        id: unsavedDialog
        title: qsTr("Save these pages?")
        text: qsTr("The pages you scanned are lost if you don't save them.")
        buttons: [
            UI.Button {
                text: qsTr("Don't save")
                onClicked: {
                    unsavedDialog.close();
                    win.closeNow();
                }
            },
            UI.Button {
                text: qsTr("Cancel")
                onClicked: unsavedDialog.close()
            },
            UI.Button {
                kind: "primary"
                text: qsTr("Save as PDF")
                onClicked: {
                    unsavedDialog.close();
                    win.closeAfterSave = true;
                    win.scanner.savePdf();
                }
            }
        ]
    }

    function look(): void {
        lastLook = Date.now();
        scanner.lookAgain();
    }

    // A scanner switched on or plugged in while Capture was in the
    // background shows up when the window comes back to the front.
    onActiveChanged: {
        if (active && !scanner.scanning && !scanner.busy && Date.now() - lastLook > 20000) {
            look();
        }
    }

    Component.onCompleted: look()
}
