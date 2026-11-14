import QtQuick
import QtQuick.Window
import WildOS.UI as UI

// Everything Capture shows: the frozen overlay on every screen, the
// countdown for a delayed capture, the bar while recording, the editor,
// the Share window and the Scan tab's window.
QtObject {
    id: root

    property list<QtObject> parts: [
        Instantiator {
            model: Qt.application.screens
            delegate: Overlay {
                required property var modelData
                screenInfo: modelData
            }
        },
        Countdown {},
        RecordingBar {},
        Connections {
            target: CaptureApp
            function onEditRequested(source, key, path, saved) {
                root.openEditor(source, key, path, saved, false);
            }
            function onScanPageEditRequested(source, path) {
                root.openEditor(source, "", path, true, true);
            }
            function onScanRequested() {
                if (!root.scanWindow) {
                    root.scanWindow = scanComponent.createObject(root);
                }
                const w = root.scanWindow;
                // On Wayland a window is never told it was minimised, and
                // can't bring itself back by asking; shown afresh, it comes
                // back to the front.
                if (!w.active) {
                    w.hide();
                    w.show();
                }
                w.raise();
                w.requestActivate();
            }
            function onShareRequested(file) {
                const window = shareWindow.createObject(root, { file: file });
                window.show();
                window.requestActivate();
            }
        }
    ]

    property var opening: []
    property var scanWindow: null

    // The editor is built a little at a time behind the splash (SplashDaily.dc
    // in Capture's colours), which only comes up if that takes more than a
    // third of a second, so a quick open never flashes.
    function openEditor(source: url, key: string, path: string, saved: bool, scanPage: bool): void {
        const name = path.substring(path.lastIndexOf("/") + 1);
        const splash = splashComponent.createObject(root, { status: qsTr("Opening %1").arg(name) });
        const incubator = editorComponent.incubateObject(root, { source: source, key: key, path: path, saved: saved, scanPage: scanPage, visible: false }, Qt.Asynchronous);
        // Held until the window is made, so the incubator is not collected
        // half way.
        root.opening = root.opening.concat([incubator]);
        const ready = () => {
            root.opening = root.opening.filter(i => i !== incubator);
            const window = incubator.object;
            if (!window) {
                CaptureApp.editorClosed(key);
                splash?.destroy();
                return;
            }
            if (!splash) {
                window.show();
                window.requestActivate();
                return;
            }
            splash.finished.connect(() => {
                window.show();
                window.requestActivate();
                splash.destroy(1000);
            });
            splash.finish();
        };
        if (incubator.status === Component.Ready || incubator.status === Component.Error) {
            ready();
        } else {
            incubator.onStatusChanged = status => {
                if (status === Component.Ready || status === Component.Error) {
                    ready();
                }
            };
        }
    }

    property Component splashComponent: Component {
        UI.LoadingSplash {
            appName: qsTr("Capture")
            iconSource: "file:///usr/share/icons/hicolor/scalable/apps/wildos-capture.svg"
            accent: "#7d8cff"
            accentDeep: "#3a40d4"
            noteStart: qsTr("Mark it up, then save or share")
            noteEnd: qsTr("Nothing leaves this PC")
            showDelay: 333
        }
    }

    property Component editorComponent: Component {
        Editor {
            onWrote: path => {
                if (scanPage) {
                    CaptureApp.scanner.markedUp(path);
                }
            }
            onDone: {
                CaptureApp.editorClosed(key);
                destroy(1000);
            }
        }
    }

    property Component scanComponent: Component {
        ScanWindow {
            onDone: {
                CaptureApp.scanClosed();
                root.scanWindow = null;
                destroy(1000);
            }
        }
    }

    property Component shareWindow: Component {
        ShareWindow {
            onClosing: {
                CaptureApp.windowClosed();
                destroy(1000);
            }
        }
    }
}
