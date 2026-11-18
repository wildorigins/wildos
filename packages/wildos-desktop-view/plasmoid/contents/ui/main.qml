import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.containmentlayoutmanager as ContainmentLayoutManager
import WildOS.Desktop as Desktop

// The Wild OS desktop containment. Plasma draws the wallpaper behind it
// (the org.kde.image plugin set by the look-and-feel layout); everything in
// front, icons and menus, is Wild OS's own (WildOS.Desktop).
//
// Plasma widgets can sit on it too, once they are allowed in Settings >
// Personalisation. They are off by default, but never blocked outright:
// widgets added while allowed are kept, and only hidden while they're not.
ContainmentItem {
    id: root

    readonly property bool widgetsAllowed: Plasmoid.configuration.allowWidgets

    // The widget under a point of the desktop view, for its right-click menu.
    function widgetAt(x, y) {
        const p = desktop.mapToItem(appletsLayout, x, y);
        for (let i = appletsLayout.children.length - 1; i >= 0; --i) {
            const c = appletsLayout.children[i];
            if (c.applet && c.visible && p.x >= c.x && p.x < c.x + c.width && p.y >= c.y && p.y < c.y + c.height) {
                return c.applet.plasmoid;
            }
        }
        return null;
    }

    function addWidgets() {
        const action = Plasmoid.internalAction("add widgets");
        if (action) {
            action.trigger();
        }
    }

    Connections {
        target: Plasmoid.containment.corona
        ignoreUnknownSignals: true

        function onEditModeChanged() {
            appletsLayout.editMode = root.widgetsAllowed && Plasmoid.containment.corona.editMode;
        }
    }

    // KDE's own desktop layout for widgets: free placement on a grid, kept
    // per screen size, moved and resized after a press and hold.
    ContainmentLayoutManager.AppletsLayout {
        id: appletsLayout
        anchors.fill: parent
        relayoutLock: width !== root.availableScreenRect.width || height !== root.availableScreenRect.height
        configKey: "ItemGeometries-" + Math.round(root.screenGeometry.width) + "x" + Math.round(root.screenGeometry.height)
        fallbackConfigKey: root.availableScreenRect.width > root.availableScreenRect.height ? "ItemGeometriesHorizontal" : "ItemGeometriesVertical"

        containment: Plasmoid
        containmentItem: root
        editModeCondition: Plasmoid.immutable || !root.widgetsAllowed
            ? ContainmentLayoutManager.AppletsLayout.Locked
            : ContainmentLayoutManager.AppletsLayout.AfterPressAndHold
        onEditModeChanged: Plasmoid.containment.corona.editMode = editMode

        minimumItemWidth: Kirigami.Units.gridUnit * 3
        minimumItemHeight: minimumItemWidth
        cellWidth: Kirigami.Units.iconSizes.small
        cellHeight: cellWidth
        defaultItemWidth: cellWidth * 6
        defaultItemHeight: cellHeight * 6

        appletContainerComponent: ContainmentLayoutManager.BasicAppletContainer {
            visible: root.widgetsAllowed
            editModeCondition: Plasmoid.immutable || !root.widgetsAllowed
                ? ContainmentLayoutManager.ItemContainer.Locked
                : ContainmentLayoutManager.ItemContainer.AfterPressAndHold
            configOverlaySource: "ConfigOverlay.qml"
            onAppletChanged: applet.visible = true
        }

        placeHolder: ContainmentLayoutManager.PlaceHolder {}

        // The icons, under any widgets: clicks a widget doesn't take reach
        // them as before.
        Desktop.DesktopView {
            id: desktop
            anchors.fill: parent
            config: Plasmoid.configuration
            primary: Plasmoid.screen === 0
            backdrop: root
            widgetsAllowed: root.widgetsAllowed
            widgetAt: root.widgetAt
            onAddWidgetsRequested: root.addWidgets()
            area: root.availableScreenRect
                ? Qt.rect(root.availableScreenRect.x, root.availableScreenRect.y, root.availableScreenRect.width, root.availableScreenRect.height)
                : Qt.rect(0, 0, width, height)
        }
    }

    // Anything that still asks Plasma to configure the desktop (its
    // Alt+D, Alt+S shortcut, scripts) lands on Personalisation in Wild OS
    // Settings instead of Plasma's Desktop and Wallpaper dialog.
    PlasmaCore.Action {
        id: configureAction
        text: qsTr("Personalise")
        icon.name: "preferences-desktop-wallpaper"
        onTriggered: Desktop.DeskOps.openSettings("personalisation")
    }

    Component.onCompleted: Plasmoid.setInternalAction("configure", configureAction)
}
