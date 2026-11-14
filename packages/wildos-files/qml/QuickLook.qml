pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import QtQuick.Pdf
import WildOS.Files
import WildOS.UI as UI

// Quick Look (Space on a selected item), as on a Mac or PowerToys Peek: a
// window of its own over Files with a preview of the item. Space or Escape
// closes it, the arrow keys move the selection in Files and the preview
// follows, Enter opens the item.
UI.AppWindow {
    id: ql

    required property var view
    property url url
    readonly property string kind: info.isDir ? "folder" : peek.kind
    readonly property string appName: url.toString().length > 0 && !info.isDir ? FileOps.defaultAppName(url) : ""

    deep: true
    width: 920
    height: 660
    minimumWidth: 480
    minimumHeight: 360
    title: info.name

    function show(itemUrl) {
        url = itemUrl;
        visible = true;
        raise();
        requestActivate();
        body.forceActiveFocus();
    }

    // Opens the item shown, as Enter on it in Files would, even when more
    // than one item is selected there.
    function openItem() {
        const f = view.folder;
        const row = f.rowForUrl(url);
        close();
        f.activate(row);
    }

    function openWithApp() {
        if (info.isDir || appName.length > 0) {
            openItem();
            return;
        }
        const target = url;
        close();
        view.chooseApp([target]);
    }

    function clock(ms) {
        const total = Math.max(0, Math.floor(ms / 1000));
        const h = Math.floor(total / 3600);
        const m = Math.floor(total / 60) % 60;
        const s = String(total % 60).padStart(2, "0");
        return h > 0 ? h + ":" + String(m).padStart(2, "0") + ":" + s : m + ":" + s;
    }

    // Hiding unloads the preview, which stops a video or song playing on.
    onVisibleChanged: {
        if (!visible) {
            url = "";
            if (view.Window.window) {
                view.Window.window.requestActivate();
            }
            view.focusContent();
        }
    }

    ItemInfo {
        id: info
        urls: ql.url.toString().length > 0 ? [ql.url] : []
    }

    PreviewSource {
        id: peek
        url: ql.url
    }

    // The preview follows the item the arrow keys or a click land on in
    // Files, and goes away when Files moves to another folder or tab.
    Connections {
        target: ql.visible ? ql.view.folder : null
        function onCurrentIndexChanged() {
            const f = ql.view.folder;
            if (f.currentIndex >= 0 && f.isSelected(f.currentIndex)) {
                ql.url = f.urlAt(f.currentIndex);
            }
        }
        function onUrlChanged() {
            ql.close();
        }
    }

    Connections {
        target: ql.view
        function onFolderChanged() {
            if (ql.visible) {
                ql.close();
            }
        }
    }

    Shortcut {
        sequences: ["Space", "Escape"]
        onActivated: ql.close()
    }
    Shortcut {
        sequences: ["Left"]
        onActivated: ql.view.quickLookStep(-1, false)
    }
    Shortcut {
        sequences: ["Right"]
        onActivated: ql.view.quickLookStep(1, false)
    }
    Shortcut {
        sequences: ["Up"]
        onActivated: ql.view.quickLookStep(-1, true)
    }
    Shortcut {
        sequences: ["Down"]
        onActivated: ql.view.quickLookStep(1, true)
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        onActivated: ql.openItem()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ql.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: ql.frame
            buttons.showMinimise: false

            Row {
                x: -2
                width: parent.width
                height: parent.height
                spacing: 10

                UI.ThemeIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    name: info.iconName
                }

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 26
                    text: info.name
                    font.weight: UI.Style.weightMedium
                    color: ql.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.divider
        }

        FocusScope {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Loader {
                anchors.fill: parent
                sourceComponent: ({
                    "": loadingComponent,
                    "image": imageComponent,
                    "animated": animatedComponent,
                    "pdf": pdfComponent,
                    "video": videoComponent,
                    "audio": audioComponent,
                    "text": textComponent,
                    "folder": aboutComponent,
                    "other": aboutComponent
                })[ql.kind] ?? aboutComponent
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 18
            Layout.rightMargin: 14
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: [info.type, info.isDir ? info.contains : info.size, info.modified].filter(s => s.length > 0).join("   ·   ")
                color: UI.Style.inkMuted
                font.pixelSize: UI.Style.px(12)
            }

            UI.Button {
                focusPolicy: Qt.TabFocus
                kind: "primary"
                text: info.isDir ? "Open" : ql.appName.length > 0 ? "Open with " + ql.appName : "Open with…"
                visible: ql.url.toString().length > 0
                onClicked: ql.openWithApp()
            }
        }
    }

    Component {
        id: loadingComponent
        Item {
            UI.Spinner {
                anchors.centerIn: parent
                size: 24
            }
        }
    }

    // Pictures at their own size, or shrunk to fit the window; never
    // stretched past it, so an icon-sized PNG stays crisp. A photo is
    // decoded at no more than 4K, which keeps a 50 megapixel camera file
    // from taking hundreds of megabytes.
    Component {
        id: imageComponent
        Item {
            Image {
                id: picture
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, implicitWidth)
                height: Math.min(parent.height - 32, implicitHeight)
                source: ql.url
                sourceSize: Qt.size(3840, 3840)
                fillMode: Image.PreserveAspectFit
                autoTransform: true
                asynchronous: true
                cache: false
                smooth: true
                mipmap: true
            }
            UI.Spinner {
                anchors.centerIn: parent
                visible: picture.status === Image.Loading
                size: 24
            }
            Loader {
                anchors.fill: parent
                active: picture.status === Image.Error
                sourceComponent: aboutComponent
            }
        }
    }

    Component {
        id: animatedComponent
        Item {
            AnimatedImage {
                id: animation
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, implicitWidth)
                height: Math.min(parent.height - 32, implicitHeight)
                source: ql.url
                fillMode: Image.PreserveAspectFit
                cache: false
                smooth: true
            }
            Loader {
                anchors.fill: parent
                active: animation.status === Image.Error
                sourceComponent: aboutComponent
            }
        }
    }

    Component {
        id: pdfComponent
        Item {
            PdfDocument {
                id: pdf
                source: ql.url
                onStatusChanged: {
                    if (pdf.status === PdfDocument.Ready) {
                        Qt.callLater(() => pages.scaleToWidth(pages.width, pages.height));
                    }
                }
            }
            PdfMultiPageView {
                id: pages
                anchors.fill: parent
                document: pdf
                visible: pdf.status === PdfDocument.Ready
            }
            UI.Spinner {
                anchors.centerIn: parent
                visible: pdf.status === PdfDocument.Loading
                size: 24
            }
            // Locked or damaged files get the icon and details.
            Loader {
                anchors.fill: parent
                active: pdf.status === PdfDocument.Error
                sourceComponent: aboutComponent
            }
        }
    }

    // Play, the time so far, a seek bar and the length, under the video or
    // the song's artwork.
    component MediaBar: RowLayout {
        id: bar
        required property MediaPlayer player
        spacing: 10

        UI.IconButton {
            focusPolicy: Qt.NoFocus
            tip: bar.player.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
            iconName: "play"
            glyph: bar.player.playbackState === MediaPlayer.PlayingState ? "M9 5v14M15 5v14" : ""
            onClicked: bar.player.playbackState === MediaPlayer.PlayingState ? bar.player.pause() : bar.player.play()
        }
        UI.Label {
            text: ql.clock(bar.player.position)
            color: UI.Style.inkMuted
            font.pixelSize: UI.Style.px(12)
            font.features: { "tnum": 1 }
        }
        UI.Slider {
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            accessibleName: "Position"
            enabled: bar.player.seekable
            from: 0
            to: Math.max(1, bar.player.duration)
            stepSize: 0
            value: bar.player.position
            onMoved: bar.player.position = value
        }
        UI.Label {
            text: ql.clock(bar.player.duration)
            color: UI.Style.inkMuted
            font.pixelSize: UI.Style.px(12)
            font.features: { "tnum": 1 }
        }
    }

    Component {
        id: videoComponent
        Item {
            MediaPlayer {
                id: movie
                source: ql.url
                videoOutput: videoOut
                audioOutput: AudioOutput {}
                Component.onCompleted: play()
            }
            VideoOutput {
                id: videoOut
                anchors.fill: parent
                anchors.bottomMargin: controls.height + 16
                visible: movie.error === MediaPlayer.NoError
            }
            MouseArea {
                anchors.fill: videoOut
                onClicked: movie.playbackState === MediaPlayer.PlayingState ? movie.pause() : movie.play()
            }
            MediaBar {
                id: controls
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                player: movie
                visible: movie.error === MediaPlayer.NoError
            }
            Loader {
                anchors.fill: parent
                active: movie.error !== MediaPlayer.NoError
                sourceComponent: aboutComponent
            }
        }
    }

    Component {
        id: audioComponent
        Item {
            MediaPlayer {
                id: song
                source: ql.url
                audioOutput: AudioOutput {}
                Component.onCompleted: play()
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 480)
                spacing: 18

                FileIcon {
                    Layout.alignment: Qt.AlignHCenter
                    size: 160
                    iconName: info.iconName
                    url: ql.url
                    preview: info.preview
                    stamp: info.stamp
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: info.name
                    font.pixelSize: UI.Style.px(15)
                    font.weight: UI.Style.weightSemiBold
                    horizontalAlignment: Text.AlignHCenter
                }
                MediaBar {
                    Layout.fillWidth: true
                    player: song
                    visible: song.error === MediaPlayer.NoError
                }
                UI.Label {
                    Layout.fillWidth: true
                    visible: song.error !== MediaPlayer.NoError
                    text: "This cannot be played here."
                    color: UI.Style.inkMuted
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Component {
        id: textComponent
        Item {
            Flickable {
                id: page
                anchors.fill: parent
                anchors.bottomMargin: note.visible ? note.height : 0
                contentWidth: Math.max(width, code.implicitWidth)
                contentHeight: code.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: UI.ScrollBar {}
                ScrollBar.horizontal: UI.ScrollBar {}

                TextEdit {
                    id: code
                    width: page.contentWidth
                    padding: 18
                    text: peek.text
                    readOnly: true
                    selectByMouse: true
                    activeFocusOnPress: false
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.NoWrap
                    font.family: "JetBrains Mono"
                    font.pixelSize: UI.Style.px(12)
                    color: UI.Style.ink
                    selectionColor: UI.Style.textSelection
                    selectedTextColor: UI.Style.ink
                    Keys.onPressed: event => {
                        if (codeMenu.isMenuKey(event)) {
                            codeMenu.openAtCaret();
                            event.accepted = true;
                        }
                    }

                    UI.TextMenu {
                        id: codeMenu
                    }
                }
            }
            UI.Label {
                id: note
                anchors.bottom: parent.bottom
                width: parent.width
                height: 30
                visible: peek.truncated
                text: "Showing the first 200 KB. Open the file to read the rest."
                color: UI.Style.inkMuted
                font.pixelSize: UI.Style.px(12)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Folders, and anything with no preview of its own: a large icon (or
    // the thumbnail KIO makes for it) with what the details pane says.
    Component {
        id: aboutComponent
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 420)
                spacing: 8

                FileIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 10
                    size: info.preview ? 220 : 128
                    iconName: info.iconName
                    url: ql.url
                    preview: info.preview
                    stamp: info.stamp
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: info.name
                    font.pixelSize: UI.Style.px(17)
                    font.weight: UI.Style.weightSemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                }
                Repeater {
                    model: [info.type, info.isDir ? info.contains : "", info.size, info.modified.length > 0 ? "Modified " + info.modified : ""]
                    delegate: UI.Label {
                        required property string modelData
                        Layout.fillWidth: true
                        visible: modelData.length > 0
                        text: modelData
                        color: UI.Style.inkMuted
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
