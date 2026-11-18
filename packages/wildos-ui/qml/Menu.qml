import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T

// Wild OS menus: right-click menus everywhere, drop-down menus off command
// bar buttons, the power and account menus. Drawn as the right-click menu
// artboards draw every one of them (MenusShell.dc, MenusApps.dc): radius
// 12, rgba(23,25,29,0.86), a 0.12 edge and a 0 20px 50px shadow, 6px of
// padding, 34px rows with 7px corners (icon column at 10, label at 38,
// shortcuts and submenus on the right). Opens with the lock screen
// GlassMenu's short fade and scale.
//
// Declare MenuItem / MenuSeparator / MenuHeader / Menu children, or hand
// it `entries`, the array wildos-files already builds:
//   { text, icon, themeIcon, iconSource, shortcut, run, checked, radio,
//     enabled, danger, bold, submenu: [entries], submenuWidth }
//   or { separator: true } or { header: "Recent" }
// `themeIcon` may list fallbacks, "x-office-spreadsheet,text-x-generic";
// `iconSource` takes a QIcon or URL from a model.
//
// A menu that belongs to a small window (the taskbar, a title bar) sets
// `popupType: Popup.Window` to open in a window of its own; its submenus
// follow.
// Keyboard (arrows, Enter, Escape, Left and Right for submenus), cascading
// and accessibility come from QtQuick.Templates.
T.Menu {
    id: control

    property var entries: []
    property real minimumWidth: 200
    property real maximumWidth: 380
    // Ticks and dots in their own column left of the icons (Files).
    property bool markColumn: false
    // Icon-only actions in a row above the list, as the Windows 11 file
    // menu draws Cut, Copy, Rename, Share and Delete (DialogFileMenu.dc):
    // [{ icon, tip, run, enabled }].
    property var quickActions: []
    property var built: []
    // The panel, for the one surface whose artboard draws its menu
    // differently (the desktop, DesktopIcons.dc: r12, rgba(23,25,29,0.82)
    // with a 40px backdrop blur, 0.12 edge). Submenus inherit all of it.
    property real panelRadius: Style.radiusMd
    property color panelColor: Style.glassContextMenu
    property color panelBorder: Style.borderContextMenu
    // Off where shader effects cannot be drawn: KWin's own windows, which
    // the window menu opens in, render MultiEffect as nothing at all.
    property bool shadow: true
    // An item in the same window to show blurred through the glass, for a
    // menu over something the compositor's blur cannot reach because it is
    // in the menu's own window (the desktop's wallpaper and icons).
    property Item backdrop: null
    property rect backdropRect: Qt.rect(0, 0, 0, 0)

    function updateBackdrop() {
        if (backdrop && background) {
            backdropRect = background.mapToItem(backdrop, 0, 0, background.width, background.height);
        }
    }

    onOpened: updateBackdrop()

    // A menu drawn inside its window never sees a click on another window
    // or the desktop, so it closes when its window stops being the active
    // one, as menus on Windows do. Menus in a window of their own are
    // closed by the compositor.
    Connections {
        target: control.popupType === T.Popup.Item && control.parent ? control.parent.Window.window : null
        function onActiveChanged() {
            if (control.visible && !control.parent.Window.window.active) {
                control.dismiss();
            }
        }
    }
    onXChanged: updateBackdrop()
    onYChanged: updateBackdrop()
    onWidthChanged: updateBackdrop()
    onHeightChanged: updateBackdrop()

    readonly property real widest: {
        let w = 0;
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i);
            if (item) {
                w = Math.max(w, item.implicitWidth);
            }
        }
        return w;
    }

    implicitWidth: Math.max(minimumWidth, Math.min(maximumWidth, widest + leftPadding + rightPadding))
    implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 640)
    padding: 6
    margins: 6
    overlap: 6
    delegate: MenuItem {
        markColumn: control.markColumn
    }

    onEntriesChanged: build()

    function build() {
        for (const old of built) {
            if (old.isMenu) {
                control.removeMenu(old.object);
            } else {
                control.removeItem(old.object);
            }
        }
        const made = [];
        for (const entry of entries) {
            if (entry.separator) {
                const sep = separatorComponent.createObject(null);
                control.addItem(sep);
                made.push({ object: sep, isMenu: false });
            } else if (entry.header !== undefined) {
                const head = headerComponent.createObject(null, { text: entry.header });
                control.addItem(head);
                made.push({ object: head, isMenu: false });
            } else if (entry.submenu) {
                const sub = Qt.createComponent(Qt.resolvedUrl("Menu.qml")).createObject(null, {
                    title: entry.text ?? "",
                    popupType: control.popupType,
                    markColumn: control.markColumn,
                    minimumWidth: entry.submenuWidth ?? control.minimumWidth,
                    panelRadius: control.panelRadius,
                    panelColor: control.panelColor,
                    panelBorder: control.panelBorder,
                    shadow: control.shadow,
                    backdrop: control.backdrop,
                    overlap: control.overlap,
                    topMargin: control.topMargin,
                    bottomMargin: control.bottomMargin,
                    leftMargin: control.leftMargin,
                    rightMargin: control.rightMargin,
                    entries: entry.submenu,
                    enabled: entry.enabled !== false
                });
                control.addMenu(sub);
                // The row that opens the submenu is made by the delegate,
                // which knows nothing of the entry's icon.
                const opener = control.itemAt(control.count - 1);
                if (opener && entry.icon) {
                    opener.iconName = entry.icon;
                }
                made.push({ object: sub, isMenu: true });
            } else {
                const item = itemComponent.createObject(null, {
                    text: entry.text ?? "",
                    iconName: entry.icon ?? "",
                    themeIcon: entry.themeIcon ?? "",
                    iconSource: entry.iconSource,
                    shortcutText: entry.shortcut ?? "",
                    checkable: entry.checked !== undefined,
                    checked: entry.checked === true,
                    radio: entry.radio === true,
                    markColumn: control.markColumn,
                    enabled: entry.enabled !== false,
                    danger: entry.danger === true,
                    bold: entry.bold === true
                });
                if (entry.run) {
                    item.triggered.connect(entry.run);
                }
                control.addItem(item);
                made.push({ object: item, isMenu: false });
            }
        }
        built = made;
    }

    Component {
        id: itemComponent
        MenuItem {}
    }

    Component {
        id: separatorComponent
        MenuSeparator {}
    }

    Component {
        id: headerComponent
        MenuHeader {}
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: contentHeight + control.topPadding + control.bottomPadding > control.height
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: ScrollBar {}
        header: control.quickActions.length > 0 ? quickRow : null
    }

    Component {
        id: quickRow

        Item {
            width: ListView.view ? ListView.view.width : 0
            height: 32 + 6 + 5

            Repeater {
                model: control.quickActions

                delegate: IconButton {
                    required property var modelData
                    required property int index
                    readonly property int count: control.quickActions.length
                    x: 4 + (count > 1 ? index * (parent.width - 8 - width) / (count - 1) : 0)
                    y: 2
                    width: 36
                    height: 32
                    iconName: modelData.icon
                    tip: modelData.tip
                    enabled: modelData.enabled !== false
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        const run = modelData.run;
                        control.close();
                        if (run) {
                            run();
                        }
                    }
                }
            }

            Rectangle {
                x: 8
                y: parent.height - 5
                width: parent.width - 16
                height: 1
                color: Style.tone(0.10)
            }
        }
    }

    background: Item {
        implicitWidth: control.minimumWidth

        Item {
            anchors.fill: parent
            visible: control.backdrop !== null

            ShaderEffectSource {
                id: behind
                anchors.fill: parent
                visible: false
                sourceItem: control.backdrop
                sourceRect: control.backdropRect
                live: control.visible
            }

            MultiEffect {
                anchors.fill: parent
                source: behind
                blurEnabled: true
                blur: 1.0
                blurMax: 40
                saturation: 0.5
                maskEnabled: true
                maskSource: frostMask
            }

            Rectangle {
                id: frostMask
                anchors.fill: parent
                radius: control.panelRadius
                visible: false
                layer.enabled: true
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: control.panelRadius
            color: control.panelColor
            border.width: 1
            border.color: control.panelBorder
            layer.enabled: control.shadow
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Style.shadowContextMenu
                shadowVerticalOffset: 20
                shadowBlur: 1.0
                blurMax: 50
            }
        }
    }

    // As Dialog: the Style durations follow the system's animation speed and
    // are 0 with reduced motion, when the menu also stops scaling.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.motionFast; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: Style.reducedMotion ? 1 : 0.97; to: 1; duration: Style.motionFast; easing.type: Style.easeEnter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.motionFast; easing.type: Style.easeExit }
    }
}
