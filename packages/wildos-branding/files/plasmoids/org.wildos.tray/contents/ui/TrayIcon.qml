import QtQuick
import WildOS.UI as UI
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore

// One app's status icon at 16 px, as Windows draws them. A left click does
// what the app asks for (usually showing its window), a right click opens
// the app's own menu, a middle click its secondary action.
MouseArea {
    id: icon

    required property var model
    property real fillWidth: width
    property real fillHeight: height
    readonly property bool showFocus: activeFocus && Nav.keyboard
    readonly property string title: model.Title || model.ToolTipTitle || ""

    // After any action, so the overflow popup can close.
    signal used()

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: model.ToolTipSubTitle || ""
    Accessible.onPressAction: activate()

    Keys.onReturnPressed: activate()
    Keys.onEnterPressed: activate()
    Keys.onSpacePressed: activate()
    Keys.onMenuPressed: openMenu(width / 2, 0)

    function call(operationName: string, x: real, y: real): var {
        const service = model.Service;
        if (!service) {
            return null;
        }
        const operation = service.operationDescription(operationName);
        const point = icon.mapToGlobal(x, y);
        operation.x = Math.round(point.x);
        operation.y = Math.round(point.y);
        return service.startOperationCall(operation);
    }

    function activate(): void {
        if (model.ItemIsMenu) {
            openMenu(width / 2, 0);
            return;
        }
        const job = call("Activate", width / 2, 0);
        if (job) {
            // Apps built on libappindicator only have a menu, and report
            // failure when asked to activate; show their menu instead.
            job.finished.connect(() => {
                if (!job.result) {
                    openMenu(width / 2, 0);
                }
            });
        }
        used();
    }

    // The app's menu comes back as a ready-made menu, which is shown just
    // above the icon. Apps without one draw their own menu themselves.
    function openMenu(x: real, y: real): void {
        const job = call("ContextMenu", x, y);
        if (!job) {
            return;
        }
        const anchor = icon.mapToGlobal(0, 0);
        job.finished.connect(() => {
            const menu = job.result;
            if (menu && typeof menu === "object" && "sizeHint" in menu) {
                menu.pos = Qt.point(Math.round(anchor.x), Math.round(anchor.y - menu.sizeHint.height - 4));
                menu.show();
            }
        });
    }

    onPressed: label.hideImmediately()

    onClicked: mouse => {
        if (mouse.button === Qt.RightButton) {
            openMenu(mouse.x, mouse.y);
        } else if (mouse.button === Qt.MiddleButton) {
            call("SecondaryActivate", mouse.x, mouse.y);
            used();
        } else {
            activate();
        }
    }

    onWheel: wheel => {
        const service = model.Service;
        if (!service || wheel.angleDelta.y === 0) {
            return;
        }
        const operation = service.operationDescription("Scroll");
        operation.delta = wheel.angleDelta.y;
        operation.direction = "Vertical";
        service.startOperationCall(operation);
    }

    Rectangle {
        anchors.centerIn: parent
        width: icon.fillWidth
        height: icon.fillHeight
        radius: 8
        color: icon.pressed ? UI.Style.surfacePressed : icon.containsMouse ? UI.Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: UI.Style.hoverDuration }
        }

        UI.FocusRing {
            visible: icon.showFocus
            targetRadius: 8
        }
    }

    Kirigami.Icon {
        id: appIcon
        anchors.centerIn: parent
        width: 16
        height: 16
        roundToIconSize: false
        source: {
            if (icon.model.status === PlasmaCore.Types.NeedsAttentionStatus) {
                if (icon.model.AttentionIcon) {
                    return icon.model.AttentionIcon;
                }
                if (icon.model.AttentionIconName) {
                    return icon.model.AttentionIconName;
                }
            }
            return icon.model.Icon || icon.model.IconName || "";
        }
    }

    // Some apps send an empty picture alongside an icon name; try the name,
    // then a plain app icon, rather than leave a blank space.
    Kirigami.Icon {
        anchors.centerIn: parent
        width: 16
        height: 16
        roundToIconSize: false
        visible: !appIcon.valid
        source: icon.model.IconName || ""
        fallback: "application-x-executable"
    }

    TaskbarTip {
        id: label
        text: icon.title.length > 0 && icon.model.ToolTipSubTitle ? icon.title + ", " + icon.model.ToolTipSubTitle
            : icon.title || icon.model.ToolTipSubTitle || ""
    }
}
