pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.UI as UI

// Every WildOS.UI control in every state, on the window glass over the
// wallpaper. Pages: "controls" (the default), "overlays" (menus, the
// drop-down list, tooltip, toast) and "dialog". Run with
// WILDOS_UI_THEME=light to see the light scheme and --text-size 12.5 to
// check the text size contract.
Window {
    id: root

    readonly property string page: typeof galleryPage === "string" ? galleryPage : "controls"

    width: 1440
    height: page === "controls" ? 1560 : 900
    visible: true
    color: UI.Style.lakeNavy
    title: "Wild OS controls"

    Image {
        anchors.fill: parent
        source: typeof galleryWallpaper !== "undefined" ? galleryWallpaper : ""
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignVCenter
    }

    component Caption: UI.Label {
        size: 12
        color: UI.Style.inkMuted
    }

    component Section: ColumnLayout {
        id: section
        property string heading
        default property alias content: body.data
        Layout.fillWidth: true
        spacing: 10

        UI.Label {
            text: section.heading
            size: 15
            font.weight: UI.Style.weightSemiBold
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 10
        }
    }

    component States: RowLayout {
        id: states
        property string label
        default property alias content: cells.data
        spacing: 16

        Caption {
            Layout.preferredWidth: 104
            text: states.label
        }

        RowLayout {
            id: cells
            spacing: 14
        }
    }

    component Labelled: ColumnLayout {
        id: labelled
        property string caption
        default property alias content: holder.data
        spacing: 6

        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }

        Caption {
            text: labelled.caption
            size: 11
        }
    }

    Rectangle {
        id: sheet
        x: 40
        y: 30
        width: parent.width - 80
        height: parent.height - 60
        radius: UI.Style.windowRadius
        color: UI.Style.glassWindowDeep
        border.width: 1
        border.color: UI.Style.windowEdge

        UI.TitleBar {
            id: titleBar
            anchors.left: parent.left
            anchors.right: parent.right
            tinted: true
            showTitle: false

            UI.TabBar {
                anchors.bottom: parent.bottom
                width: Math.min(implicitWidth, parent.width)
                room: 560

                UI.TabButton {
                    text: "Controls"
                    iconName: "apps"
                    checked: true
                }
                UI.TabButton {
                    text: "Documents"
                    iconName: "folder"
                    previewHover: true
                }
                UI.TabButton {
                    text: "Pictures"
                    iconName: "pictures"
                }
            }
        }

        Loader {
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 28
            sourceComponent: root.page === "overlays" ? overlays : root.page === "dialog" ? dialogPage : controls
        }
    }

    Component {
        id: controls

        GridLayout {
            columns: 2
            columnSpacing: 40
            rowSpacing: 22

            Section {
                heading: "Buttons " + (UI.Style.dark ? "(dark)" : "(light)") + ", text scale " + UI.Style.textScale.toFixed(2)
                Layout.columnSpan: 2

                // The five Style weights should read as five distinct steps;
                // 480 and 550 need the variable Albert Sans.
                States {
                    label: "Weights"
                    Repeater {
                        model: [UI.Style.weightLight, UI.Style.weightRegular, UI.Style.weightMedium, UI.Style.weightSemiBold, UI.Style.weightBold]
                        UI.Label {
                            required property int modelData
                            text: "Wild OS " + modelData
                            size: 18
                            font.weight: modelData
                        }
                    }
                }
                States {
                    label: "Primary"
                    UI.Button { kind: "primary"; text: "Install" }
                    UI.Button { kind: "primary"; text: "Hover"; previewHover: true }
                    UI.Button { kind: "primary"; text: "Pressed"; down: true }
                    UI.Button { kind: "primary"; text: "Focus"; previewFocus: true }
                    UI.Button { kind: "primary"; text: "Disabled"; enabled: false }
                    UI.Button { kind: "primary"; text: "Busy"; busy: true }
                    UI.Button { kind: "primary"; text: "Small"; size: "small" }
                    UI.Button { kind: "primary"; text: "Continue"; size: "large" }
                }
                States {
                    label: "Secondary"
                    UI.Button { text: "Check now" }
                    UI.Button { text: "Hover"; previewHover: true }
                    UI.Button { text: "Pressed"; down: true }
                    UI.Button { text: "Focus"; previewFocus: true }
                    UI.Button { text: "Disabled"; enabled: false }
                    UI.Button { text: "Browse"; iconName: "folder" }
                    UI.Button { text: "Remove"; danger: true }
                    UI.Button { text: "Small"; size: "small" }
                    UI.Button { text: "Back"; size: "large" }
                }
                States {
                    label: "Text"
                    UI.Button { kind: "text"; text: "New"; iconName: "new"; dropDown: true }
                    UI.Button { kind: "text"; text: "Sort"; iconName: "sort"; previewHover: true }
                    UI.Button { kind: "text"; text: "View"; iconName: "view"; down: true }
                    UI.Button { kind: "text"; text: "Details"; iconName: "pane"; checked: true }
                    UI.Button { kind: "text"; text: "Focus"; iconName: "share"; previewFocus: true }
                    UI.Button { kind: "text"; text: "Disabled"; iconName: "cut"; enabled: false }
                }
                States {
                    label: "Icon and link"
                    UI.IconButton { iconName: "back"; tip: "Back" }
                    UI.IconButton { iconName: "forward"; tip: "Forward"; previewHover: true }
                    UI.IconButton { iconName: "up"; tip: "Up"; down: true }
                    UI.IconButton { iconName: "refresh"; tip: "Refresh"; previewFocus: true }
                    UI.IconButton { iconName: "cut"; tip: "Cut"; enabled: false }
                    UI.IconButton { iconName: "pane"; tip: "Details pane"; checked: true }
                    UI.LinkButton { text: "What's new" }
                    UI.LinkButton { text: "Hover"; previewHover: true }
                    UI.LinkButton { text: "Focus"; previewFocus: true }
                }
            }

            Section {
                heading: "Toggle, check box, radio"

                States {
                    label: "Toggle"
                    UI.Toggle {}
                    UI.Toggle { checked: true }
                    UI.Toggle { previewHover: true }
                    UI.Toggle { checked: true; previewHover: true }
                    UI.Toggle { checked: true; previewFocus: true }
                    UI.Toggle { locked: true; lockedReason: "Needs an internet connection" }
                    UI.Toggle { checked: true; enabled: false }
                    UI.Toggle { text: "On"; checked: true }
                }
                States {
                    label: "Check box"
                    UI.CheckBox {}
                    UI.CheckBox { checked: true }
                    UI.CheckBox { tristate: true; checkState: Qt.PartiallyChecked }
                    UI.CheckBox { previewHover: true }
                    UI.CheckBox { checked: true; previewFocus: true }
                    UI.CheckBox { checked: true; enabled: false }
                    UI.CheckBox { text: "Documents"; checked: true }
                }
                States {
                    label: "Radio"
                    UI.RadioButton { autoExclusive: false }
                    UI.RadioButton { autoExclusive: false; checked: true }
                    UI.RadioButton { autoExclusive: false; previewHover: true }
                    UI.RadioButton { autoExclusive: false; checked: true; previewFocus: true }
                    UI.RadioButton { autoExclusive: false; enabled: false }
                    UI.RadioButton { autoExclusive: false; text: "Centred"; checked: true }
                }
            }

            Section {
                heading: "Slider, progress"

                States {
                    label: "Slider"
                    UI.Slider { implicitWidth: 120; value: 30 }
                    UI.Slider { implicitWidth: 120; value: 70; previewHover: true }
                    UI.Slider { implicitWidth: 120; value: 50; previewFocus: true }
                }
                States {
                    label: "Slider, muted"
                    UI.Slider { implicitWidth: 120; value: 45; dimmed: true }
                    UI.Slider { implicitWidth: 120; value: 60; enabled: false }
                }
                States {
                    label: "Progress"
                    UI.ProgressBar { implicitWidth: 120; value: 0.4 }
                    UI.ProgressBar { implicitWidth: 120; indeterminate: true }
                    UI.ProgressBar { implicitWidth: 120; value: 0.92; warning: true }
                    UI.Spinner {}
                }
            }

            Section {
                heading: "Text fields and search"

                States {
                    label: "Text field"
                    UI.TextField { implicitWidth: 170; placeholderText: "Computer name" }
                    UI.TextField { implicitWidth: 170; text: "alex-laptop" }
                    UI.TextField { implicitWidth: 170; text: "Typing"; previewFocus: true }
                }
                States {
                    label: ""
                    UI.TextField { implicitWidth: 170; text: "bad name!"; error: true }
                    UI.TextField { implicitWidth: 170; text: "Disabled"; enabled: false }
                }
                States {
                    label: "Large, password"
                    UI.TextField { implicitWidth: 200; size: "large"; placeholderText: "Your name" }
                    UI.TextField { implicitWidth: 200; size: "large"; text: "correcthorse"; revealable: true }
                }
                States {
                    label: "Search"
                    UI.SearchField { implicitWidth: 170; placeholderText: "Find a setting" }
                    UI.SearchField { implicitWidth: 170; text: "blue"; previewFocus: true }
                    UI.SearchField { implicitWidth: 200; pill: true; placeholderText: "Search apps" }
                }
                States {
                    label: "Several lines"
                    UI.TextArea { implicitWidth: 240; placeholderText: "What went wrong?" }
                    UI.TextArea { implicitWidth: 240; text: "Right click for Cut, Copy and Paste"; previewFocus: true }
                }
            }

            Section {
                heading: "Drop-down, chips, avatar"

                States {
                    label: "Drop-down"
                    UI.ComboBox { model: [{ value: "c", text: "Centred" }, { value: "l", text: "Left" }] }
                    UI.ComboBox { model: [{ value: "a", text: "Ask me first" }]; previewHover: true }
                    UI.ComboBox { model: [{ value: "m", text: "Medium" }]; previewFocus: true }
                    UI.ComboBox { model: [{ value: "x", text: "Off" }]; enabled: false }
                }
                States {
                    label: "Chips"
                    UI.Chip { text: "Your Home folder only"; iconName: "folder" }
                    UI.Chip { text: "No internet"; granted: false }
                }
                States {
                    label: "Shortcut keys"
                    UI.KeyChip { text: "Super" }
                    UI.ShortcutKeys { keys: ["Super", "Alt", "S"] }
                    UI.ShortcutKeys { keys: ["Shift ×5"] }
                }
                States {
                    label: "Colour swatches"
                    UI.Swatch { colour: "#ffffff"; name: "White"; selected: true }
                    UI.Swatch { colour: "#111111"; name: "Black" }
                    UI.Swatch { colour: "#f2d34a"; name: "Yellow" }
                    UI.Swatch { colour: "#7fd6c8"; name: "Sea glass" }
                }
                States {
                    label: "Avatar"
                    UI.Avatar { size: 60 }
                    UI.Avatar { size: 60; source: typeof galleryPhoto !== "undefined" ? galleryPhoto : "" }
                    UI.Avatar { size: 32; source: typeof galleryPhoto !== "undefined" ? galleryPhoto : "" }
                    UI.Avatar { size: 60; ring: true; source: typeof galleryPhoto !== "undefined" ? galleryPhoto : "" }
                }
            }

            Section {
                heading: "Window buttons"

                States {
                    label: "Active"
                    UI.WindowButton { kind: "minimise" }
                    UI.WindowButton { kind: "maximise"; previewHover: true }
                    UI.WindowButton { kind: "restore"; previewPressed: true }
                    UI.WindowButton { kind: "close"; previewHover: true }
                    UI.WindowButton { kind: "close"; previewPressed: true }
                }
                States {
                    label: "Inactive"
                    UI.WindowButton { kind: "minimise"; windowActive: false }
                    UI.WindowButton { kind: "maximise"; windowActive: false }
                    UI.WindowButton { kind: "close"; windowActive: false }
                }
            }

            Section {
                heading: "Rows in a card"

                UI.Card {
                    Layout.preferredWidth: 540

                    Column {
                        width: 540

                        UI.SettingRow {
                            iconName: "moon"
                            title: "Night light"
                            description: "Warmer colours after sunset"
                            status: "Off"
                            UI.Toggle {}
                        }
                        UI.SettingRow {
                            iconName: "palette"
                            title: "Colours"
                            description: "Accent colour, transparency effects"
                            chevron: true
                            previewHover: true
                        }
                        UI.SettingRow {
                            iconName: "external"
                            title: "Advanced display settings"
                            external: true
                        }
                        UI.SettingRow {
                            iconName: "location"
                            title: "Location"
                            description: "Not available on this computer yet"
                            enabled: false
                            UI.Toggle { enabled: false }
                        }
                        UI.SettingRow {
                            iconTile: true
                            iconName: "windowsapps"
                            title: "Invoice Maker"
                            description: "Windows app"
                            chevron: true
                            below: [
                                UI.Chip { text: "Documents" },
                                UI.Chip { text: "No internet"; granted: false }
                            ]
                        }
                    }
                }
            }

            Section {
                heading: "Choice cards, toast"

                RowLayout {
                    spacing: 12

                    UI.ChoiceCard {
                        Layout.preferredWidth: 190
                        title: "Standard"
                        description: "Strong protection that keeps everything working."
                        showIndicator: true
                        checked: true
                    }
                    UI.ChoiceCard {
                        Layout.preferredWidth: 190
                        title: "Strict"
                        description: "Expect some things to stop working."
                        showIndicator: true
                        previewHover: true
                        note: "Coming later"
                    }
                    UI.ChoiceCard {
                        Layout.preferredWidth: 150
                        title: "Firefox"
                        exclusive: false
                        showIndicator: true
                        checked: true
                        previewFocus: true
                    }
                }

                UI.Toast {
                    Component.onCompleted: show("Firefox is installed", "Open")
                    iconName: "check"
                    duration: 1e9
                }
                UI.Toast {
                    Component.onCompleted: show("Link copied")
                    duration: 1e9
                }
            }

            Section {
                heading: "Icons (" + Object.keys(UI.Icons.paths).length + ")"
                Layout.columnSpan: 2

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: Object.keys(UI.Icons.paths)

                        Item {
                            required property string modelData
                            width: 30
                            height: 30

                            UI.Icon {
                                anchors.centerIn: parent
                                name: parent.modelData
                                size: 20
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: overlays

        Item {
            UI.Label {
                text: "Overlays " + (UI.Style.dark ? "(dark)" : "(light)")
                size: 15
                font.weight: UI.Style.weightSemiBold
            }

            UI.Button {
                id: menuAnchor
                y: 40
                kind: "text"
                text: "More"
                iconName: "more"
                dropDown: true
            }

            UI.Menu {
                id: menu
                parent: menuAnchor
                y: menuAnchor.height + 4
                entries: [
                    { text: "Open", icon: "open", shortcut: "Enter" },
                    { text: "Open with", icon: "open-with", submenu: [
                        { text: "Text editor", icon: "text-document" },
                        { text: "Choose another app", icon: "apps" }
                    ] },
                    { separator: true },
                    { text: "Cut", icon: "cut", shortcut: "Ctrl+X" },
                    { text: "Copy", icon: "copy", shortcut: "Ctrl+C" },
                    { text: "Paste", icon: "paste", shortcut: "Ctrl+V", enabled: false },
                    { separator: true },
                    { text: "Show hidden files", checked: true },
                    { text: "Show file name extensions", checked: false },
                    { separator: true },
                    { text: "Delete", icon: "delete", shortcut: "Del", danger: true }
                ]
                Component.onCompleted: open()
                onClosed: open()
            }

            UI.ComboBox {
                id: combo
                x: 420
                y: 40
                model: [
                    { value: 1, text: "Small" }, { value: 2, text: "Medium" },
                    { value: 3, text: "Large" }, { value: 4, text: "Larger" }
                ]
                currentIndex: 1
                Component.onCompleted: popup.open()
            }

            UI.Menu {
                id: declared
                x: 700
                y: 40
                UI.MenuItem { text: "Sleep"; iconName: "moon" }
                UI.MenuItem { text: "Shut down"; iconName: "power"; previewHover: true }
                UI.MenuItem { text: "Restart"; iconName: "restore" }
                UI.MenuSeparator {}
                UI.MenuItem { text: "Sign out"; iconName: "account" }
                Component.onCompleted: open()
                onClosed: open()
            }

            UI.IconButton {
                id: tipped
                x: 1000
                y: 40
                iconName: "share"
                tip: "Share"
                UI.Tooltip {
                    text: "Share with a nearby device"
                    visible: true
                    delay: 0
                    timeout: -1
                }
            }

            UI.Toast {
                x: 1000
                y: 160
                iconName: "check"
                duration: 1e9
                Component.onCompleted: show("Firefox is installed", "Open")
            }
        }
    }

    Component {
        id: dialogPage

        Item {
            UI.Dialog {
                id: medium
                title: "Delete 3 items?"
                text: "They go to the Recycle Bin, where you can restore them until you empty it."
                closePolicy: T.Popup.NoAutoClose
                modal: false
                x: 40
                y: 40
                anchors.centerIn: undefined
                buttons: [
                    UI.Button { kind: "primary"; text: "Delete" },
                    UI.Button { text: "Cancel" }
                ]
                Component.onCompleted: open()
            }

            UI.Dialog {
                title: "Install Invoice Maker?"
                text: "This is a Windows app. Wild OS can run it in a sandbox, so it only reaches your Documents folder and cannot use the internet unless you allow it."
                size: "large"
                iconName: "windowsapps"
                modal: false
                closePolicy: T.Popup.NoAutoClose
                x: 560
                y: 40
                anchors.centerIn: undefined

                UI.CheckBox {
                    text: "Let it use the internet"
                }

                buttons: [
                    UI.Button { kind: "primary"; size: "large"; text: "Install" },
                    UI.Button { size: "large"; text: "Not now" }
                ]
                Component.onCompleted: open()
            }
        }
    }
}
