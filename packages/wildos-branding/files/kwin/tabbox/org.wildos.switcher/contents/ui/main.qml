pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import org.kde.kwin as KWin
import org.kde.plasma.core as PlasmaCore
import WildOS.UI as UI

// Wild OS window switcher (Alt+Tab). Visual spec: AltTab.dc.html.
//
// The panel is a Plasma dialog rather than a plain window because only the
// dialog asks KWin to blur behind it, and the Wild OS desktop theme already
// draws dialogs as the design's glass (35,37,41 at 0.96, 22px corners,
// 1px edge at 0.12). The theme frame contributes 14px of the design's 20px
// padding; the remaining 6px is added here.
KWin.TabBoxSwitcher {
    id: tabBox

    readonly property int itemWidth: 230
    readonly property int itemHeight: 202
    readonly property int gap: 10
    readonly property int framePadding: 6

    // The design dims the desktop by 30% while switching. Without a
    // compositor a translucent window would paint solid, so it is skipped.
    // Created before the panel so the panel maps above it.
    Instantiator {
        active: tabBox.visible && tabBox.compositing

        delegate: Window {
            flags: Qt.Popup | Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint
                   | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
            color: Qt.rgba(UI.Style.scrim.r, UI.Style.scrim.g, UI.Style.scrim.b, UI.Style.dark ? 0.30 : 0.18)
            x: tabBox.screenGeometry.x
            y: tabBox.screenGeometry.y
            width: tabBox.screenGeometry.width
            height: tabBox.screenGeometry.height
            visible: true
        }
    }

    Instantiator {
        active: tabBox.visible

        delegate: PlasmaCore.Dialog {
            id: dialog

            location: PlasmaCore.Types.Floating
            visible: true
            flags: Qt.Popup | Qt.X11BypassWindowManagerHint
            onVisibleChanged: if (visible) raise()
            x: tabBox.screenGeometry.x + Math.round((tabBox.screenGeometry.width - width) / 2)
            y: tabBox.screenGeometry.y + Math.round((tabBox.screenGeometry.height - height) / 2)

            mainItem: FocusScope {
                id: panel

                focus: true

                readonly property int count: grid.count

                // As many columns as fit across 90% of the screen, then balanced
                // so a second row is never a single straggler.
                readonly property int maxColumns: Math.max(1, Math.floor((tabBox.screenGeometry.width * 0.9 - 40 + tabBox.gap) / (tabBox.itemWidth + tabBox.gap)))
                readonly property int rows: Math.max(1, Math.ceil(count / maxColumns))
                readonly property int columns: Math.max(1, Math.ceil(count / rows))
                readonly property int maxVisibleRows: Math.max(1, Math.floor((tabBox.screenGeometry.height * 0.8 - 70 + tabBox.gap) / (tabBox.itemHeight + tabBox.gap)))

                width: Math.max(columns * (tabBox.itemWidth + tabBox.gap) - tabBox.gap, 300) + tabBox.framePadding * 2
                height: scroller.height + 14 + hint.height + tabBox.framePadding * 2

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Switch windows")

                Keys.onPressed: event => {
                    const n = panel.count;
                    if (n === 0) {
                        return;
                    }
                    let next = tabBox.currentIndex;
                    if (event.key === Qt.Key_Left) {
                        next = (next - 1 + n) % n;
                    } else if (event.key === Qt.Key_Right) {
                        next = (next + 1) % n;
                    } else if (event.key === Qt.Key_Up) {
                        next = next - panel.columns >= 0 ? next - panel.columns : next;
                    } else if (event.key === Qt.Key_Down) {
                        next = next + panel.columns < n ? next + panel.columns : next;
                    } else if (event.key === Qt.Key_Delete) {
                        // Closes the selected window, as the close button on
                        // its card does.
                        const card = repeater.itemAt(tabBox.currentIndex);
                        if (card && card.canClose) {
                            tabBox.model.close(tabBox.currentIndex);
                        }
                        event.accepted = true;
                        return;
                    } else {
                        return;
                    }
                    tabBox.currentIndex = next;
                    event.accepted = true;
                }

                Flickable {
                    id: scroller

                    x: tabBox.framePadding
                    y: tabBox.framePadding
                    width: parent.width - tabBox.framePadding * 2
                    height: panel.count === 0
                            ? 120
                            : Math.min(panel.rows, panel.maxVisibleRows) * (tabBox.itemHeight + tabBox.gap) - tabBox.gap
                    contentWidth: width
                    contentHeight: grid.height
                    interactive: contentHeight > height
                    clip: interactive
                    boundsBehavior: Flickable.StopAtBounds
                    T.ScrollBar.vertical: UI.ScrollBar {
                        // Sits in the panel's padding, clear of the cards.
                        parent: panel
                        x: panel.width - width
                        y: scroller.y
                        height: scroller.height
                    }

                    // Keeps the selection in view when there are more rows than fit,
                    // and the scroll inside the list's real bounds: the
                    // switcher opens at a placeholder height before the windows
                    // are counted, and a scroll made to fit that left every
                    // card's top cut off once the list grew.
                    readonly property real selectedTop: Math.floor(tabBox.currentIndex / panel.columns) * (tabBox.itemHeight + tabBox.gap)
                    function keepSelectionInView() {
                        let y = contentY;
                        if (selectedTop < y) {
                            y = selectedTop;
                        } else if (selectedTop + tabBox.itemHeight > y + height) {
                            y = selectedTop + tabBox.itemHeight - height;
                        }
                        contentY = Math.max(0, Math.min(y, contentHeight - height));
                    }
                    onSelectedTopChanged: keepSelectionInView()
                    onHeightChanged: keepSelectionInView()
                    onContentHeightChanged: keepSelectionInView()

                    Grid {
                        id: grid

                        readonly property int count: repeater.count

                        anchors.horizontalCenter: parent.horizontalCenter
                        columns: panel.columns
                        spacing: tabBox.gap

                        Repeater {
                            id: repeater

                            model: tabBox.model

                            delegate: SwitcherItem {
                                required property int index
                                required property string caption
                                required property var icon
                                required property var windowId
                                required property bool closeable

                                width: tabBox.itemWidth
                                height: tabBox.itemHeight
                                title: caption
                                iconSource: icon
                                windowUuid: windowId
                                canClose: closeable && typeof tabBox.model.close !== "undefined"
                                selected: index === tabBox.currentIndex

                                onActivated: tabBox.model.activate(index)
                                onCloseRequested: tabBox.model.close(index)
                            }
                        }
                    }
                }

                UI.Label {
                    anchors.centerIn: scroller
                    visible: panel.count === 0
                    text: qsTr("No open windows")
                    size: 14
                    font.weight: UI.Style.weightMedium
                }

                UI.Label {
                    id: hint

                    x: tabBox.framePadding
                    y: scroller.y + scroller.height + 14
                    width: parent.width - tabBox.framePadding * 2
                    horizontalAlignment: Text.AlignHCenter
                    // When the switcher was opened without holding a modifier
                    // there is nothing to release, so Enter confirms instead.
                    text: tabBox.noModifierGrab
                          ? qsTr("Use the arrow keys to move along. Press Enter to switch.")
                          : qsTr("Hold Alt and press Tab to move along. Release to switch.")
                    color: UI.Style.inkMuted
                    size: 12
                }
            }
        }
    }
}
