import QtQuick
import QtCore
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Settings
import WildOS.UI as UI

// Accessibility, laid out as SettingsAccessibility.dc.html: a line about the
// shortcuts, then Seeing on the left beside Hearing and Keyboard and touch
// on the right (one column when the window is narrow). Every switch is bound
// to Accessibility, which reads the system each time this page is made, so
// leaving and coming back always shows what is really set. Each row with a
// shortcut shows its keys; wildos-access makes them work on the lock and
// sign-in screens as well.
PageScroll {
    id: page

    readonly property bool wide: width >= 760

    function onOff(on) {
        return on ? "On" : "Off";
    }

    readonly property var blurSteps: ["Lightest", "Light", "Medium", "Strong", "Strongest"]
    readonly property string windowEffectsReason: Accessibility.windowEffectsInstalled
        ? "This needs graphics acceleration, which this computer isn't using"
        : "The window effects aren't installed on this computer"

    Component.onCompleted: {
        Accessibility.refresh();
        Accessibility.refreshMagnifier();
    }

    // KWin says nothing when zoom changes (Super and plus from the
    // keyboard), so the switch looks while the page is showing.
    Timer {
        interval: 1000
        repeat: true
        running: page.visible && Qt.application.state === Qt.ApplicationActive
        onTriggered: Accessibility.refreshMagnifier()
    }

    UI.Label {
        Layout.fillWidth: true
        Layout.topMargin: -6
        text: "Every shortcut here also works on the sign-in and unlock screens, so help is there before you've signed in."
        size: 13
        color: UI.Style.inkStatus
        wrapMode: Text.Wrap
        elide: Text.ElideNone
        lineHeight: 1.5
    }

    GridLayout {
        Layout.fillWidth: true
        columns: page.wide ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 108
            Layout.alignment: Qt.AlignTop
            spacing: 10

            SectionTitle {
                text: "Seeing"
            }

            UI.Card {
                Layout.fillWidth: true

                Column {
                    width: parent.width

                    UI.SettingRow {
                        title: "Text size"
                        description: "Larger text everywhere, without changing scale"

                        TextSizeBox {
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    UI.SettingRow {
                        id: pointerRow

                        readonly property var sizes: Accessibility.pointerSizes

                        function nearest(size) {
                            let best = 0;
                            for (let i = 1; i < sizes.length; ++i) {
                                if (Math.abs(sizes[i] - size) < Math.abs(sizes[best] - size)) {
                                    best = i;
                                }
                            }
                            return best;
                        }

                        title: "Pointer"
                        description: "Size and colour"

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 16

                            UI.Slider {
                                id: pointerSize
                                anchors.verticalCenter: parent.verticalCenter
                                implicitWidth: 90
                                implicitHeight: 20
                                flat: true
                                from: 0
                                to: Math.max(1, pointerRow.sizes.length - 1)
                                stepSize: 1
                                snapMode: T.Slider.SnapAlways
                                wheelEnabled: false
                                value: pointerRow.nearest(Accessibility.pointerSize)
                                accessibleName: "Pointer size"
                                Accessible.description: pointerRow.sizes[Math.round(value)] + " pixels"
                                // Applied once the slider settles, so dragging
                                // across it does not reload the pointer at
                                // every step.
                                onMoved: pointerApply.restart()

                                Timer {
                                    id: pointerApply
                                    interval: 250
                                    onTriggered: {
                                        if (pointerSize.pressed) {
                                            restart();
                                            return;
                                        }
                                        Accessibility.setPointerSize(pointerRow.sizes[Math.round(pointerSize.value)]);
                                        pointerSize.value = Qt.binding(() => pointerRow.nearest(Accessibility.pointerSize));
                                    }
                                }
                            }

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                Accessible.role: Accessible.Grouping
                                Accessible.name: "Pointer colour"

                                Repeater {
                                    model: Accessibility.pointerColours

                                    UI.Swatch {
                                        required property var modelData
                                        colour: modelData.colour
                                        name: modelData.name
                                        selected: Accessibility.pointerColour === modelData.id
                                        onClicked: Accessibility.setPointerColour(modelData.id)
                                    }
                                }

                                // A pointer of your own, as Windows lets you
                                // browse for one: a plus until there is one,
                                // then its picture.
                                UI.Swatch {
                                    colour: UI.Style.surfaceSunken
                                    name: "Your own"
                                    selected: Accessibility.pointerColour === "custom"
                                    enabled: !Accessibility.pointerBusy
                                    onClicked: Accessibility.customPointer ? ownPointerDialog.open() : pointerFile.open()

                                    contentItem: Item {
                                        Image {
                                            anchors.centerIn: parent
                                            width: 16
                                            height: 16
                                            visible: Accessibility.customPointer
                                            source: Accessibility.customPointerPreview
                                            sourceSize: Qt.size(32, 32)
                                            fillMode: Image.PreserveAspectFit
                                            cache: false
                                        }

                                        UI.Icon {
                                            anchors.centerIn: parent
                                            visible: !Accessibility.customPointer
                                            name: "plus"
                                            size: 12
                                            color: UI.Style.iconMuted
                                        }
                                    }
                                }
                            }
                        }
                    }

                    UI.SettingRow {
                        title: "Magnifier"
                        description: "Zoom into any part of the screen"
                        status: page.onOff(Accessibility.magnifier)
                        below: UI.ShortcutKeys {
                            keys: ["Super", "+"]
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.magnifier
                            Accessible.name: "Magnifier"
                            onToggled: Accessibility.setMagnifier(checked)
                        }
                    }

                    UI.SettingRow {
                        title: "Screen reader"
                        description: "Reads out what's on screen"
                        status: page.onOff(Accessibility.screenReader)
                        below: UI.ShortcutKeys {
                            keys: ["Super", "Alt", "S"]
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.screenReader
                            locked: !Accessibility.screenReaderInstalled
                            lockedReason: "The screen reader isn't installed on this computer"
                            Accessible.name: "Screen reader"
                            onToggled: Accessibility.setScreenReader(checked)
                        }
                    }

                    UI.SettingRow {
                        title: "High contrast"
                        description: "Strong colours and clear edges"
                        status: page.onOff(Accessibility.highContrast)
                        below: UI.ShortcutKeys {
                            keys: ["Super", "Alt", "H"]
                        }

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.highContrast
                            enabled: !Accessibility.highContrastBusy
                            locked: !Accessibility.highContrastInstalled
                            lockedReason: "The high contrast theme isn't installed on this computer"
                            Accessible.name: "High contrast"
                            onToggled: Accessibility.setHighContrast(checked)
                        }
                    }

                    UI.SettingRow {
                        title: "Outline windows"
                        description: "A clear edge round every window, and a thick one round the window you're using"
                        status: page.onOff(Accessibility.outlineWindows)

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.outlineWindows
                            locked: !Accessibility.windowEffectsSupported
                            lockedReason: page.windowEffectsReason
                            Accessible.name: "Outline windows"
                            onToggled: Accessibility.setOutlineWindows(checked)
                        }
                    }

                    UI.SettingRow {
                        visible: Accessibility.outlineWindows && Accessibility.windowEffectsSupported
                        title: "Outline maximised windows"
                        description: "Off leaves windows that fill the screen without an edge"
                        status: page.onOff(Accessibility.outlineFillingScreen)

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.outlineFillingScreen
                            Accessible.name: "Outline maximised windows"
                            onToggled: Accessibility.setOutlineFillingScreen(checked)
                        }
                    }

                    UI.SettingRow {
                        title: "Stronger text on glass"
                        description: "Solid backing and brighter text on the taskbar, Start, flyouts and Wild OS apps"
                        status: page.onOff(Accessibility.strongerText)

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.strongerText
                            Accessible.name: "Stronger text on glass"
                            onToggled: Accessibility.setStrongerText(checked)
                        }
                    }

                    UI.SettingRow {
                        title: "Blur windows in the background"
                        description: "Every window but the one you're using goes soft and a little darker"
                        status: page.onOff(Accessibility.blurBackground)

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.blurBackground
                            locked: !Accessibility.windowEffectsSupported
                            lockedReason: page.windowEffectsReason
                            Accessible.name: "Blur windows in the background"
                            onToggled: Accessibility.setBlurBackground(checked)
                        }
                    }

                    UI.SettingRow {
                        visible: Accessibility.blurBackground && Accessibility.windowEffectsSupported
                        title: "Blur strength"
                        status: page.blurSteps[Math.round(blurStrength.value) - 1]

                        UI.Slider {
                            id: blurStrength
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: 140
                            implicitHeight: 20
                            flat: true
                            from: 1
                            to: page.blurSteps.length
                            stepSize: 1
                            snapMode: T.Slider.SnapAlways
                            wheelEnabled: false
                            value: Accessibility.blurStrength
                            accessibleName: "Blur strength"
                            Accessible.description: page.blurSteps[Math.round(value) - 1]
                            // KWin redraws the blur at each step, so dragging
                            // shows each strength as it is passed.
                            onMoved: {
                                Accessibility.setBlurStrength(Math.round(value));
                                value = Qt.binding(() => Accessibility.blurStrength);
                            }
                        }
                    }

                    UI.SettingRow {
                        visible: Accessibility.blurBackground && Accessibility.windowEffectsSupported
                        title: "Blur the desktop too"
                        description: "Whenever a window is in use"
                        status: page.onOff(Accessibility.blurDesktop)

                        UI.Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: Accessibility.blurDesktop
                            Accessible.name: "Blur the desktop too"
                            onToggled: Accessibility.setBlurDesktop(checked)
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 100
            Layout.alignment: Qt.AlignTop
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                SectionTitle {
                    text: "Hearing"
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        UI.SettingRow {
                            title: "Flash the screen for alert sounds"
                            description: "For when you can't hear them"
                            status: page.onOff(Accessibility.visibleBell)

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Accessibility.visibleBell
                                Accessible.name: "Visual alerts"
                                onToggled: Accessibility.setVisibleBell(checked)
                            }
                        }

                        UI.SettingRow {
                            title: "Mono audio"
                            description: "Both ears get the same sound"
                            status: page.onOff(Accessibility.monoAudio)

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Accessibility.monoAudio
                                locked: !Accessibility.monoAudioInstalled
                                lockedReason: "Mono audio isn't available on this computer"
                                Accessible.name: "Mono audio"
                                onToggled: Accessibility.setMonoAudio(checked)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                SectionTitle {
                    text: "Keyboard and touch"
                }

                UI.Card {
                    Layout.fillWidth: true

                    Column {
                        width: parent.width

                        UI.SettingRow {
                            title: "On-screen keyboard"
                            description: "Type with a mouse, touch or switch"
                            status: page.onOff(Accessibility.onScreenKeyboard)
                            below: UI.ShortcutKeys {
                                keys: ["Super", "Ctrl", "O"]
                            }

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Accessibility.onScreenKeyboard
                                locked: !Accessibility.onScreenKeyboardInstalled
                                lockedReason: "No on-screen keyboard is installed on this computer"
                                Accessible.name: "On-screen keyboard"
                                onToggled: Accessibility.setOnScreenKeyboard(checked)
                            }
                        }

                        UI.SettingRow {
                            title: "Sticky keys"
                            description: "Press shortcuts one at a time"
                            status: page.onOff(Accessibility.stickyKeys)
                            below: UI.ShortcutKeys {
                                keys: ["Shift ×5"]
                            }

                            UI.Toggle {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Accessibility.stickyKeys
                                Accessible.name: "Sticky keys"
                                onToggled: Accessibility.setStickyKeys(checked)
                            }
                        }

                        UI.SettingRow {
                            readonly property var info: Pages.page("slowbouncekeys")
                            title: info.title
                            description: info.description
                            chevron: true
                            onClicked: Navigator.open("slowbouncekeys")
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: pointerFile
        title: "Choose a pointer"
        nameFilters: ["Pointers and pictures (*.cur *.ani *.zip *.inf *.png *.svg)", "Windows pointers (*.cur *.ani)",
            "Windows pointer packs (*.zip *.inf)", "Pictures (*.png *.svg)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: {
            if (/\.(png|svg)$/i.test(selectedFile.toString())) {
                hotspotDialog.choose(selectedFile);
            } else {
                Accessibility.addPointer(selectedFile);
            }
        }
    }

    UI.Dialog {
        id: ownPointerDialog
        title: "Your own pointer"
        text: (Accessibility.customPointerSource.length > 0 ? "Made from " + Accessibility.customPointerSource + ". " : "")
            + (Accessibility.pointerColour === "custom"
                ? "Choose another file to replace it, or remove it to go back to the white pointer."
                : "Use it again, choose another file to replace it, or remove it.")

        Image {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            source: Accessibility.customPointerPreview
            sourceSize: Qt.size(96, 96)
            fillMode: Image.PreserveAspectFit
            cache: false
        }

        buttons: [
            UI.Button {
                text: "Use it"
                kind: "primary"
                visible: Accessibility.pointerColour !== "custom"
                onClicked: {
                    ownPointerDialog.close();
                    Accessibility.setPointerColour("custom");
                }
            },
            UI.Button {
                text: "Choose another file"
                onClicked: {
                    ownPointerDialog.close();
                    pointerFile.open();
                }
            },
            UI.Button {
                text: "Remove"
                onClicked: {
                    ownPointerDialog.close();
                    Accessibility.removeCustomPointer();
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: ownPointerDialog.close()
            }
        ]
    }

    // A picture has no hotspot of its own: it points from the top-left
    // corner unless you click somewhere else on it.
    UI.Dialog {
        id: hotspotDialog

        property url file
        property real hotX: 0
        property real hotY: 0

        function choose(url) {
            file = url;
            hotX = 0;
            hotY = 0;
            open();
        }

        title: "Where does it point?"
        text: "Click the spot on the picture that does the clicking."

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 160
            Layout.preferredHeight: 160

            Rectangle {
                anchors.fill: parent
                radius: 8
                color: UI.Style.surfaceSunken
            }

            Image {
                anchors.centerIn: parent
                width: 136
                height: 136
                source: hotspotDialog.file
                sourceSize: Qt.size(272, 272)
                fillMode: Image.PreserveAspectFit
                cache: false

                MouseArea {
                    x: (parent.width - parent.paintedWidth) / 2
                    y: (parent.height - parent.paintedHeight) / 2
                    width: parent.paintedWidth
                    height: parent.paintedHeight
                    cursorShape: Qt.CrossCursor
                    onClicked: mouse => {
                        hotspotDialog.hotX = Math.max(0, Math.min(1, mouse.x / width));
                        hotspotDialog.hotY = Math.max(0, Math.min(1, mouse.y / height));
                    }

                    Rectangle {
                        x: hotspotDialog.hotX * parent.width - width / 2
                        y: hotspotDialog.hotY * parent.height - height / 2
                        width: 12
                        height: 12
                        radius: 6
                        color: "transparent"
                        border.width: 2
                        border.color: UI.Style.accent
                    }
                }
            }
        }

        buttons: [
            UI.Button {
                text: "Use it"
                kind: "primary"
                onClicked: {
                    hotspotDialog.close();
                    Accessibility.addPointer(hotspotDialog.file, hotspotDialog.hotX, hotspotDialog.hotY);
                }
            },
            UI.Button {
                text: "Cancel"
                onClicked: hotspotDialog.close()
            }
        ]
    }

    UI.Dialog {
        id: pointerError
        title: "Couldn't use that file"
        buttons: [
            UI.Button {
                text: "OK"
                kind: "primary"
                onClicked: pointerError.close()
            }
        ]
    }

    Connections {
        target: Accessibility

        function onPointerFailed(message) {
            pointerError.text = message;
            pointerError.open();
        }
    }
}
