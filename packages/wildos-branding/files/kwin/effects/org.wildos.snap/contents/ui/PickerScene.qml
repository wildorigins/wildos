import QtQuick
import org.kde.kwin as KWin
import WildOS.UI as UI

import "layouts.js" as Layouts

// What one screen shows while Meta+Z or snap assist is open. A KWin scene
// effect draws instead of the desktop, so the desktop is redrawn here from
// live window previews (the way KWin's own tile editor does it), lightly
// dimmed, with the picker's panel under the target window's maximise button,
// or snap assist's window choice over the empty zones. Running as an effect
// is what gives them the keyboard; KWin scripts cannot have it.
FocusScope {
    id: scene

    readonly property var owner: KWin.SceneView.effect
    readonly property var screen: KWin.SceneView.screen
    readonly property var target: owner.target
    readonly property bool hasTarget: target !== null && target.output === screen
    readonly property bool assisting: owner.mode === "assist"
    // Worked out once: the window cannot move while the overlay is up, and
    // the title bar geometry it is derived from has no change signal.
    property rect anchorRect: Qt.rect(0, 0, 0, 0)

    focus: true

    Keys.onPressed: event => {
        // Keys reach the scene on the screen KWin gives them to, which need
        // not be the one showing assist, so its state lives in the effect.
        if (scene.assisting) {
            switch (event.key) {
            case Qt.Key_Escape:
                scene.owner.close();
                break;
            case Qt.Key_Left:
            case Qt.Key_Up:
            case Qt.Key_Backtab:
                scene.owner.assistMove(-1);
                break;
            case Qt.Key_Right:
            case Qt.Key_Down:
            case Qt.Key_Tab:
                scene.owner.assistMove(1);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                if (scene.owner.assistIndex >= 0) {
                    scene.owner.assistChoose(scene.owner.assistWindows[scene.owner.assistIndex]);
                }
                break;
            default:
                return;
            }
            event.accepted = true;
            return;
        }
        if (!scene.hasTarget) {
            if (event.key === Qt.Key_Escape) {
                scene.owner.close();
            }
            return;
        }
        switch (event.key) {
        case Qt.Key_Escape:
            scene.owner.close();
            break;
        case Qt.Key_Left:
            panel.grid.moveBy(-1, 0);
            break;
        case Qt.Key_Right:
        case Qt.Key_Tab:
            panel.grid.moveBy(1, 0);
            break;
        case Qt.Key_Backtab:
            panel.grid.moveBy(-1, 0);
            break;
        case Qt.Key_Up:
            panel.grid.moveBy(0, -1);
            break;
        case Qt.Key_Down:
            panel.grid.moveBy(0, 1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (panel.grid.activeLayout >= 0) {
                scene.owner.choose(panel.grid.activeLayout, panel.grid.activeZone);
            }
            break;
        default: {
            // Number keys work as in Windows: the first picks a layout,
            // the second picks a zone in it and snaps.
            const n = event.key - Qt.Key_1;
            if (n < 0 || n > 8) {
                return;
            }
            if (scene.layoutPicked) {
                if (n < Layouts.layouts[panel.grid.activeLayout].zones.length) {
                    scene.owner.choose(panel.grid.activeLayout, n);
                }
            } else if (n < Layouts.layouts.length) {
                panel.grid.select(n, 0);
                scene.layoutPicked = true;
            }
            break;
        }
        }
        event.accepted = true;
    }

    property bool layoutPicked: false

    Component.onCompleted: {
        if (owner && owner.target) {
            anchorRect = owner.anchorRectFor(owner.target);
        }
        panel.grid.select(0, 0);
    }

    Item {
        id: desktop
        anchors.fill: parent

        Repeater {
            model: KWin.WindowFilterModel {
                activity: KWin.Workspace.currentActivity
                desktop: KWin.Workspace.currentDesktop
                screenName: scene.screen ? scene.screen.name : ""
                windowModel: KWin.WindowModel {}
            }

            KWin.WindowThumbnail {
                required property var model

                wId: model.window.internalId
                x: model.window.x - scene.screen.geometry.x
                y: model.window.y - scene.screen.geometry.y
                width: model.window.width
                height: model.window.height
                z: model.window.stackingOrder
                visible: !model.window.minimized
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(UI.Style.scrim.r, UI.Style.scrim.g, UI.Style.scrim.b, UI.Style.dark ? 0.18 : 0.10)
        opacity: 0
        Component.onCompleted: opacity = 1

        Behavior on opacity {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: scene.owner.close()
    }

    SnapPanel {
        id: panel

        readonly property rect area: scene.owner.areaFor(scene.screen)

        visible: scene.hasTarget && !scene.assisting
        hint: qsTr("Use the arrow keys to pick a zone, then press Enter.")

        // Centred under the maximise button when there is one, otherwise
        // under the middle of the window's top edge; kept on screen.
        x: {
            const g = scene.screen.geometry;
            const centre = scene.anchorRect.width > 0
                ? scene.anchorRect.x + scene.anchorRect.width / 2
                : (scene.target ? scene.target.x + scene.target.width / 2 : g.x + g.width / 2);
            const wanted = centre - width / 2;
            return Math.round(Math.max(area.x + 8, Math.min(wanted, area.x + area.width - width - 8)) - g.x);
        }
        y: {
            const g = scene.screen.geometry;
            const top = scene.anchorRect.width > 0
                ? scene.anchorRect.y + scene.anchorRect.height + 6
                : (scene.target ? scene.target.y + 48 : g.y + 48);
            return Math.round(Math.max(area.y + 8, Math.min(top, area.y + area.height - height - 8)) - g.y);
        }

        opacity: 0
        scale: 0.97
        Component.onCompleted: {
            opacity = 1;
            scale = 1;
        }

        Behavior on opacity {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
    }

    AssistLayer {
        anchors.fill: parent
        visible: scene.assisting && scene.owner.assistOutput === scene.screen
        owner: scene.owner
        screen: scene.screen
    }

    Connections {
        target: panel.grid
        function onZoneChosen(layoutIndex: int, zoneIndex: int): void {
            scene.owner.choose(layoutIndex, zoneIndex);
        }
    }
}
