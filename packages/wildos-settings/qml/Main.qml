import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// The Settings window as Settings.dc.html draws it: one rounded glass sheet
// with its own 42px title bar (back button, "Settings", window buttons),
// navigation with the account and search on the left and the current page
// on the right. The title bar is drawn here rather than by KWin because the
// back button belongs in it; KWin's snap layouts find its maximise button by
// the app id org.wildos.settings (46px buttons on a 42px bar).
UI.AppWindow {
    id: window

    required property string startPage
    property string currentId: ""
    // Pages visited before this one, newest last. Back takes one step down
    // it, as Back does in Windows 11 Settings.
    property var history: []
    property string pendingId: ""
    property bool pendingBack: false
    readonly property var searchFound: Pages.find(search.text)
    property int resultIndex: 0
    readonly property var current: Pages.page(currentId)
    property string currentKcm: ""
    property string pageError: ""
    readonly property bool currentIsKcm: currentKcm.length > 0
    readonly property bool canGoBack: history.length > 0 || (pageLoader.item !== null && pageLoader.item.depth !== undefined && pageLoader.item.depth > 1)

    width: 1180
    height: 760
    minimumWidth: 880
    minimumHeight: 560
    visible: true
    title: "Settings"
    deep: true
    transparency: System.transparency

    // For the D-Bus call from a second "wildos-settings --page x".
    function showPage(id) {
        openPage(id, false);
    }

    function openPage(id, fromHistory) {
        if (id === "signin-wallet") {
            Navigator.request = "wallet";
        }
        if (id === "update-install") {
            Navigator.request = "install";
        }
        // The camera and microphone lights' This week: History, showing
        // only their uses in the last seven days.
        if (id === "history-camera") {
            Navigator.request = "camera-mic";
            id = "history";
        }
        // Start's Uninstall for an app that installed itself in the home
        // folder: Installed apps opens with the app's dialog.
        if (id.startsWith("uninstall:")) {
            Navigator.request = id;
            id = "installedapps";
        }
        id = Pages.resolve(id);
        let p = Pages.page(id);
        if (!p || !Pages.exists(id)) {
            id = "home";
            p = Pages.page(id);
        }
        if (p.window) {
            KcmHost.openWindow(p.window, p.title);
            // Opened from the command line there is nothing behind the
            // window yet, so show where the setting lives.
            if (currentId.length === 0) {
                openPage(p.parent || "home");
            }
            return;
        }
        if (id === currentId) {
            return;
        }
        if (pageLoader.item && pageLoader.item.dirty) {
            pendingId = id;
            pendingBack = fromHistory === true;
            unsavedDialog.open();
            return;
        }
        // The step back is only used up once the page really changes, so
        // "Stay on this page" in the unsaved changes dialog keeps it.
        if (fromHistory === true) {
            history = history.slice(0, -1);
        } else if (currentId.length > 0) {
            history = history.concat([currentId]).slice(-50);
        }
        currentId = id;
        pageLoader.sourceComponent = null;
        // A Wild OS page that cannot load says so, rather than quietly
        // showing KDE's module for the same setting in its place.
        let page = p.component ? Qt.createComponent("WildOS.Settings", p.component) : null;
        pageError = "";
        if (page && page.status === Component.Error) {
            console.warn(page.errorString());
            pageError = page.errorString();
            page = null;
        }
        currentKcm = page ? "" : (p.kcm || "");
        pageLoader.sourceComponent = page || (currentKcm.length > 0 ? kcmComponent : errorComponent);
        search.text = "";
    }

    // KDE labels its actions in title case with a trailing ellipsis ("Add
    // New..."); beside a Wild OS title they read as sentence case instead.
    // Words in capitals, such as VPN, keep them.
    function houseStyle(label) {
        const words = label.replace("&", "").replace(/(\.\.\.|…)$/, "").trim().split(" ");
        return words.map((word, i) => {
            if (i === 0 || (word.length > 1 && word === word.toUpperCase())) {
                return word;
            }
            return word.toLowerCase();
        }).join(" ");
    }

    function goBack() {
        if (pageLoader.item && pageLoader.item.back && pageLoader.item.back()) {
            return;
        }
        // A step can lead nowhere by now: a page whose KDE module has been
        // removed since, which would otherwise leave Back stuck on Home.
        while (history.length > 0) {
            const previous = Pages.resolve(history[history.length - 1]);
            if (previous !== currentId && Pages.exists(previous)) {
                break;
            }
            history = history.slice(0, -1);
        }
        if (history.length > 0) {
            openPage(history[history.length - 1], true);
        }
    }

    function openResult(index) {
        const ids = searchFound.ids;
        if (ids.length > 0) {
            openPage(ids[Math.max(0, Math.min(index, ids.length - 1))]);
        }
        // Picking the page already showing changes nothing else, and the
        // list would otherwise stay open over it.
        search.text = "";
    }

    function moveResult(step) {
        const count = searchFound.ids.length;
        if (count > 0) {
            resultIndex = Math.max(0, Math.min(resultIndex + step, count - 1));
            resultList.positionViewAtIndex(resultIndex, ListView.Contain);
        }
    }

    Component.onCompleted: {
        Navigator.window = window;
        openPage(startPage);
        // Opened straight onto an inner page (from Start, a notification or
        // --page), Back climbs the way the page title reads, one level at a
        // time, and ends on Home.
        if (currentId !== "home") {
            history = ["home"].concat(Pages.trail(currentId).slice(0, -1));
        }
    }

    onClosing: close => {
        if (pageLoader.item && pageLoader.item.dirty) {
            close.accepted = false;
            pendingId = "";
            pendingBack = false;
            unsavedDialog.open();
        }
    }

    // StandardKey.Back is Alt+Left on Linux, so listing both registered
    // Alt+Left twice, and Qt treats a doubled shortcut as ambiguous and
    // never fires it. "Back" is the browser back key on some keyboards.
    Shortcut {
        sequences: ["Alt+Left", "Back"]
        onActivated: window.goBack()
    }

    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+E"]
        onActivated: search.forceActiveFocus()
    }

    Component {
        id: errorComponent
        PageError {
            pageTitle: (Pages.page(window.currentId) || {}).title || ""
            detail: window.pageError
        }
    }

    Component {
        id: kcmComponent
        KcmPage {
            pluginId: window.currentKcm
        }
    }

    UI.TitleBar {
        id: titleBar
        width: parent.width
        title: "Settings"
        showBack: true
        backEnabled: window.canGoBack
        frame: window.frame
        onBackClicked: window.goBack()
    }

    Flickable {
        id: nav
        y: titleBar.height
        width: Metrics.navWidth
        height: parent.height - y
        contentHeight: navColumn.implicitHeight + 20
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: navColumn
            x: 14
            y: 6
            width: Metrics.navWidth - 28
            spacing: 2

            T.AbstractButton {
                id: account
                width: parent.width
                height: 84
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: "Account, " + System.userFullName
                onClicked: window.openPage("accounts")

                background: Rectangle {
                    radius: UI.Style.radiusSm
                    color: account.down ? UI.Style.surfacePressed : account.hovered ? UI.Style.surfaceHover : "transparent"
                    Behavior on color {
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }

                    UI.FocusRing {
                        visible: account.visualFocus
                        anchors.margins: -2
                        targetRadius: UI.Style.radiusSm
                    }
                }

                contentItem: Item {
                    Row {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        UI.Avatar {
                            size: 60
                            source: System.userAvatar
                        }

                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: navColumn.width - 60 - 14 - 16
                            text: System.userFullName
                            size: 15
                            font.weight: UI.Style.weightSemiBold
                        }
                    }
                }
            }

            UI.SearchField {
                id: search
                width: parent.width
                placeholderText: "Find a setting"
                Accessible.name: "Find a setting"
                // Typing stays in the box while Up and Down pick a result,
                // as in Windows Settings; Enter opens the one picked.
                onTextChanged: window.resultIndex = 0
                onAccepted: window.openResult(window.resultIndex)
                onDownPressed: window.moveResult(1)
                Keys.onUpPressed: window.moveResult(-1)
            }

            Item {
                width: 1
                height: 8
            }

            Repeater {
                model: Pages.navigation

                NavItem {
                    required property string modelData
                    iconName: Pages.page(modelData).icon
                    text: Pages.page(modelData).title
                    current: Pages.category(window.currentId) === modelData
                    onClicked: window.openPage(modelData)
                }
            }
        }
    }

    // Search results in the menu glass, as a drop-down list under the box.
    T.Popup {
        id: results
        x: 14
        y: nav.y + search.y + search.height + 6
        width: Metrics.navWidth - 28
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: 6
        visible: search.text.trim().length > 0 && search.activeFocus
        closePolicy: T.Popup.NoAutoClose

        background: Rectangle {
            radius: UI.Style.radiusLg
            color: UI.Style.glassMenu
            border.width: 1
            border.color: UI.Style.borderMenu
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: UI.Style.shadowMenu
                shadowVerticalOffset: 12
                shadowBlur: 1.0
                blurMax: 32
            }
        }

        contentItem: Column {
            spacing: 2

            UI.Label {
                id: partialNote
                visible: window.searchFound.partial
                width: parent.width
                topPadding: 4
                bottomPadding: 4
                leftPadding: 10
                rightPadding: 10
                wrapMode: Text.Wrap
                text: "Nothing matches every word, so here are the closest"
                size: 12
                color: UI.Style.inkMuted
            }

            ListView {
                id: resultList
                width: parent.width
                // Long lists scroll inside the drop-down rather than run
                // past the bottom of the window.
                height: Math.min(contentHeight, Math.max(UI.Style.menuItemHeight * 3,
                    window.height - results.y - results.topPadding - results.bottomPadding - 24 - (partialNote.visible ? partialNote.height + 2 : 0)))
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                spacing: 2
                model: window.searchFound.ids
                T.ScrollBar.vertical: UI.ScrollBar {}

                delegate: T.AbstractButton {
                    id: result
                    required property string modelData
                    required property int index
                    readonly property var info: Pages.page(modelData)
                    readonly property string place: Pages.location(modelData)
                    readonly property bool lit: index === window.resultIndex
                    width: ListView.view.width
                    implicitHeight: Math.max(UI.Style.menuItemHeight, column.implicitHeight + 12)
                    padding: 0
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.ListItem
                    Accessible.name: place.length > 0 ? info.title + ", in " + place : info.title
                    Accessible.description: info.description || ""
                    Accessible.selected: lit
                    onHoveredChanged: {
                        if (hovered) {
                            window.resultIndex = index;
                        }
                    }
                    onClicked: window.openResult(index)

                    background: Rectangle {
                        radius: UI.Style.radiusSm
                        color: result.down ? UI.Style.surfacePressed : result.lit ? UI.Style.surfaceHover : "transparent"
                    }

                    contentItem: Item {
                        UI.Icon {
                            x: 10
                            y: column.y + (titleLabel.height - height) / 2
                            name: result.info.icon || "info"
                            size: 16
                            strokeWidth: 1.8
                        }

                        Column {
                            id: column
                            x: 36
                            width: parent.width - x - 10
                            anchors.verticalCenter: parent.verticalCenter

                            UI.Label {
                                id: titleLabel
                                width: parent.width
                                text: result.info.title
                            }

                            UI.Label {
                                width: parent.width
                                visible: text.length > 0
                                text: result.place
                                size: 12
                                color: UI.Style.inkMuted
                            }

                            UI.Label {
                                width: parent.width
                                visible: text.length > 0
                                text: result.info.description || ""
                                size: 12
                                color: UI.Style.inkSubtle
                            }
                        }
                    }
                }
            }

            Column {
                visible: resultList.count === 0
                width: parent.width
                topPadding: 6
                bottomPadding: 6
                spacing: 2

                UI.Label {
                    width: parent.width
                    leftPadding: 10
                    rightPadding: 10
                    text: "No settings match “" + search.text.trim() + "”"
                }

                UI.Label {
                    width: parent.width
                    leftPadding: 10
                    rightPadding: 10
                    wrapMode: Text.Wrap
                    text: "Check the spelling, or try fewer or more general words, such as display, sound or wifi."
                    size: 12
                    color: UI.Style.inkMuted
                }
            }
        }
    }

    Item {
        id: content
        x: Metrics.navWidth
        y: titleBar.height
        width: parent.width - Metrics.navWidth
        height: parent.height - y

        Row {
            id: header
            x: Metrics.contentLeft
            y: 8
            height: Math.max(34, UI.Style.px(28) + 6)
            spacing: 10

            // A trail too long for the window loses its first steps, as
            // Windows Settings does; the page itself always stays.
            readonly property int dropped: {
                const room = content.width - Metrics.contentLeft - Metrics.contentRight;
                const n = crumbs.count;
                const widths = [];
                let total = 0;
                for (let i = 0; i < n; ++i) {
                    const item = crumbs.itemAt(i);
                    widths.push(item ? item.implicitWidth : 0);
                    total += widths[i] + (i > 0 ? spacing : 0);
                }
                let d = 0;
                while (total > room && d < n - 1) {
                    total -= widths[d] + spacing;
                    ++d;
                }
                return d;
            }

            Repeater {
                id: crumbs
                model: {
                    const trail = Pages.trail(window.currentId).map(id => ({ id: id, title: Pages.page(id).title }));
                    const extra = pageLoader.item && pageLoader.item.subTitles ? pageLoader.item.subTitles : [];
                    return trail.concat(extra.map(t => ({ id: "", title: t })));
                }

                Row {
                    id: crumb
                    required property var modelData
                    required property int index
                    readonly property bool last: index === crumbs.count - 1
                    visible: index >= header.dropped
                    spacing: 10
                    anchors.verticalCenter: parent.verticalCenter

                    T.AbstractButton {
                        id: crumbButton
                        anchors.verticalCenter: parent.verticalCenter
                        width: crumbText.implicitWidth
                        height: header.height
                        padding: 0
                        enabled: !crumb.last && crumb.modelData.id.length > 0
                        hoverEnabled: true
                        focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                        Accessible.role: crumb.last ? Accessible.Heading : Accessible.Link
                        Accessible.name: crumb.modelData.title
                        onClicked: window.openPage(crumb.modelData.id)

                        background: UI.FocusRing {
                            visible: crumbButton.visualFocus
                            targetRadius: UI.Style.radiusXs
                        }

                        contentItem: UI.Label {
                            id: crumbText
                            text: crumb.modelData.title
                            size: 28
                            font.weight: UI.Style.weightSemiBold
                            font.letterSpacing: -0.28
                            color: crumb.last || crumbButton.hovered ? UI.Style.ink : UI.Style.inkMuted
                        }
                    }

                    UI.Icon {
                        visible: !crumb.last
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron"
                        size: 18
                        strokeWidth: 2
                        color: UI.Style.inkMuted
                    }
                }
            }
        }

        Row {
            id: pageActions
            anchors.right: parent.right
            anchors.rightMargin: Metrics.contentRight
            y: header.y + (header.height - height) / 2
            spacing: 8
            visible: header.x + header.implicitWidth + 24 < x

            Repeater {
                model: pageLoader.item && pageLoader.item.actions ? pageLoader.item.actions : []

                UI.Button {
                    required property var modelData
                    text: window.houseStyle(modelData.text)
                    enabled: modelData.enabled
                    onClicked: modelData.trigger(this)
                }
            }
        }

        Loader {
            id: pageLoader
            y: header.y + header.height + (window.currentIsKcm ? 10 : 18)
            width: parent.width
            height: parent.height - y
            // Each page rises a little as it fades in, as Windows Settings does.
            transform: UI.Reveal {
                item: pageLoader
                key: window.currentId
                dy: UI.Style.travelMedium
            }
        }
    }

    // The back button on a mouse goes back too, wherever the pointer is.
    Item {
        anchors.fill: parent

        TapHandler {
            acceptedButtons: Qt.BackButton
            onTapped: window.goBack()
        }
    }

    UI.Dialog {
        id: unsavedDialog
        title: "Apply your changes?"
        text: "Some changes on this page are not applied yet."
        buttons: [
            UI.Button {
                text: "Apply"
                kind: "primary"
                onClicked: {
                    pageLoader.item.apply();
                    unsavedDialog.leave();
                }
            },
            UI.Button {
                text: "Discard changes"
                onClicked: {
                    pageLoader.item.discard();
                    unsavedDialog.leave();
                }
            },
            UI.Button {
                text: "Stay on this page"
                onClicked: unsavedDialog.close()
            }
        ]

        function leave() {
            close();
            if (window.pendingId.length > 0) {
                window.openPage(window.pendingId, window.pendingBack);
            } else {
                window.close();
            }
        }
    }
}
