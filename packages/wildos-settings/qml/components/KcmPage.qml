import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import WildOS.Settings
import WildOS.UI as UI

// Shows a KDE settings module inside the Settings window under a Wild OS
// page title. The module's own pages stack the way System Settings stacks
// them, and changes that need confirming get an Apply bar at the bottom.
Item {
    id: page

    property string pluginId
    property QtObject module: null
    property string error
    readonly property bool dirty: module !== null && module.needsSave
    readonly property int depth: host.pageStack.depth
    // The module's toolbar actions ("Add device", "Add printer"), shown as
    // Wild OS buttons beside the page title instead of in a KDE toolbar.
    readonly property var actions: {
        const item = host.pageStack.currentItem;
        const list = [];
        if (item && item.actions) {
            for (let i = 0; i < item.actions.length; ++i) {
                const action = item.actions[i];
                if (action && action.visible !== false && action.text && action.text.length > 0) {
                    list.push(action);
                }
            }
        }
        return list;
    }
    readonly property var subTitles: {
        const titles = [];
        for (let i = 1; i < host.pageStack.depth; ++i) {
            const item = host.pageStack.get(i);
            if (item && item.title) {
                titles.push(item.title);
            }
        }
        return titles;
    }

    function back() {
        if (module && host.pageStack.depth > 1) {
            module.pop();
            return true;
        }
        return false;
    }

    // Pages sit on the window itself, so they read as one sheet with the
    // navigation instead of a panel set into it.
    function clearBackground(item) {
        if (item && item.background !== undefined) {
            item.background = null;
        }
    }

    function apply() {
        KcmHost.apply(module);
    }

    function discard() {
        KcmHost.revert(module);
    }

    Component.onCompleted: {
        module = KcmHost.load(pluginId);
        if (!module) {
            error = KcmHost.lastError();
            return;
        }
        // The Wild OS page title already names the page, so the module's
        // own first heading would only repeat it.
        module.mainUi.title = "";
        clearBackground(module.mainUi);
        host.pageStack.initialPage = module.mainUi;
    }

    Component.onDestruction: {
        host.pageStack.clear();
        KcmHost.release(module);
    }

    Connections {
        target: page.module
        function onPagePushed(item) {
            page.clearBackground(item);
            host.pageStack.push(item);
        }
        function onPageRemoved() {
            host.pageStack.pop();
        }
    }

    Kirigami.ApplicationItem {
        id: host
        anchors.fill: parent
        // Kirigami pads pages by about 8px; this lines the module's content
        // up with the page title and the design's 40px right margin.
        anchors.leftMargin: Metrics.contentLeft - 8
        anchors.rightMargin: Metrics.contentRight - 8
        anchors.bottomMargin: applyBar.visible ? applyBar.height : 0
        visible: page.module !== null
        pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None
        pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.NoNavigationButtons
        pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn
        pageStack.separatorVisible: false
    }

    ColumnLayout {
        visible: page.module === null
        x: Metrics.contentLeft
        width: Math.min(parent.width - Metrics.contentLeft - Metrics.contentRight, 1000)
        spacing: Metrics.sectionGap

        StatusCard {
            iconName: "info"
            warning: true
            title: qsTr("This setting isn't available")
            description: KcmHost.available(page.pluginId)
                ? qsTr("Wild OS could not open it. Restarting Settings may help.")
                : qsTr("The part of Wild OS that provides it is not installed on this computer.")
        }

        // The reason the module gave, for anyone helping to fix it.
        UI.Label {
            Layout.fillWidth: true
            visible: page.error.length > 0 && KcmHost.available(page.pluginId)
            text: qsTr("Technical details: %1").arg(page.error)
            size: 12
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    Rectangle {
        id: applyBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 64
        visible: page.dirty
        color: UI.Style.windowOpaque

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: UI.Style.divider
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Metrics.contentLeft
            anchors.rightMargin: Metrics.contentRight
            spacing: 8

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("You have changes that are not applied yet.")
                color: UI.Style.inkMuted
            }

            UI.Button {
                text: qsTr("Apply")
                kind: "primary"
                onClicked: page.apply()
            }

            UI.Button {
                text: qsTr("Discard changes")
                onClicked: page.discard()
            }
        }
    }
}
