import QtQuick
import QtQuick.Layouts
import WildOS.Welcome
import WildOS.UI as UI

// Step 5, from BringFiles.dc.html (3.7). The drive card names the Windows drive
// and user folder that were found; the list shows each folder's real size
// and the copy runs in the background once started.
StepPage {
    id: page

    signal next()
    signal back()

    readonly property string state: WindowsImport.state
    readonly property bool found: state === "found"
    readonly property var source: WindowsImport.sources[WindowsImport.current]
    readonly property bool tooBig: WindowsImport.selectedBytes > WindowsImport.freeBytes

    title: qsTr("Bring your files across")
    subtitle: found ? qsTr("We found Windows on this computer. Choose what to copy into Wild OS. Nothing is removed from Windows.")
                    : state === "scanning" ? qsTr("Looking for Windows on this computer. Nothing on it will be changed.")
                    : qsTr("Copy your documents, photos and music from Windows. Nothing is removed from Windows.")
    note: {
        if (WindowsImport.started) {
            return WindowsImport.planning ? qsTr("Copies in the background once you're signed in. You'll get a notification when it's done.")
                                          : qsTr("Copying in the background. You'll get a notification when it's done.");
        }
        if (!found) {
            return "";
        }
        if (WindowsImport.sizing) {
            return qsTr("Working out how much there is to copy.");
        }
        if (tooBig) {
            return qsTr("That's more than the %1 free on this computer. Untick something to make room.").arg(WindowsImport.formatBytes(WindowsImport.freeBytes, true));
        }
        return WindowsImport.estimate();
    }
    noteColor: tooBig && !WindowsImport.sizing ? UI.Style.warning : UI.Style.inkMuted

    UI.Card {
        Layout.fillWidth: true

        UI.ListRow {
            minHeight: 78
            title: page.found ? page.source.label
                : page.state === "scanning" ? qsTr("Looking for Windows")
                : page.state === "locked" ? qsTr("Windows is locked")
                : page.state === "error" ? qsTr("Windows can't be read right now")
                : qsTr("No Windows found")
            description: page.found ? page.source.detail
                : page.state === "scanning" ? qsTr("This takes a few seconds.")
                : WindowsImport.message

            leading: Rectangle {
                width: 44
                height: 44
                radius: UI.Style.radiusMd
                color: UI.Style.accentAlpha(0.16, 0.12)

                UI.Icon {
                    anchors.centerIn: parent
                    name: page.found || page.state === "scanning" ? "drive" : page.state === "locked" ? "lock" : "alert"
                    size: 22
                    strokeWidth: 1.8
                    color: UI.Style.accentText
                }
            }

            UI.Button {
                id: chooseButton
                visible: (page.found && WindowsImport.sources.length > 1) || page.state === "error" || page.state === "locked"
                enabled: !WindowsImport.started
                text: page.found ? qsTr("Choose another drive") : qsTr("Look again")
                dropDown: page.found
                onClicked: {
                    if (page.found) {
                        chooser.popup(chooseButton, chooseButton.width - chooser.width, chooseButton.height + 6);
                    } else {
                        WindowsImport.scan();
                    }
                }
            }
        }
    }

    UI.Card {
        Layout.fillWidth: true
        visible: page.found

        Column {
            width: parent.width

            Repeater {
                model: WindowsImport.folders

                UI.ListRow {
                    id: row

                    required property var modelData

                    minHeight: 50
                    title: modelData.name
                    status: modelData.detail ? modelData.detail
                        : modelData.sized ? (modelData.also ? qsTr("%1, also from %2").arg(WindowsImport.formatBytes(modelData.size)).arg(modelData.also) : WindowsImport.formatBytes(modelData.size))
                        : qsTr("Measuring")
                    interactive: true
                    enabled: modelData.enabled && !WindowsImport.started
                    Accessible.role: Accessible.CheckBox
                    Accessible.checkable: true
                    Accessible.checked: modelData.checked
                    onClicked: WindowsImport.setChecked(modelData.key, !modelData.checked)

                    // The whole row ticks the box, so the box itself takes
                    // no focus of its own.
                    leading: UI.CheckBox {
                        checked: row.modelData.checked
                        // The model is the truth: a click asks for the
                        // change and the tick follows it.
                        checkable: false
                        focusPolicy: Qt.NoFocus
                        onClicked: WindowsImport.setChecked(row.modelData.key, !row.modelData.checked)
                    }
                }
            }
        }
    }

    buttons: [
        UI.Button {
            text: page.found && !WindowsImport.started ? qsTr("Skip") : qsTr("Back")
            onClicked: {
                if (page.found && !WindowsImport.started) {
                    WindowsImport.release();
                    page.next();
                } else {
                    page.back();
                }
            }
        },
        UI.Button {
            id: primaryButton
            kind: "primary"
            text: page.found && !WindowsImport.started ? qsTr("Copy and continue") : qsTr("Continue")
            enabled: !page.found || WindowsImport.started
                     || (!WindowsImport.sizing && !page.tooBig && WindowsImport.selectedBytes > 0)
                     || (!WindowsImport.sizing && WindowsImport.folders.some(f => f.key === "bookmarks" && f.checked))
            onClicked: {
                if (page.found && !WindowsImport.started) {
                    if (WindowsImport.start()) {
                        page.next();
                    }
                } else {
                    page.next();
                }
            }
        }
    ]

    // The other Windows drives and user folders that were found, under the
    // button that opens it. A menu closes on Escape and on a click outside,
    // and takes the arrow keys.
    UI.Menu {
        id: chooser
        minimumWidth: 320
        entries: WindowsImport.sources.map((source, index) => ({
            text: qsTr("%1, user folder \"%2\"").arg(source.label).arg(source.user),
            icon: "drive",
            checked: index === WindowsImport.current,
            run: () => WindowsImport.choose(index)
        }))
    }
}
