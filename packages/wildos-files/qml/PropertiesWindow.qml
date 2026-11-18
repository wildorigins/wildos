pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.Files
import WildOS.UI as UI

// Properties (Alt+Enter), DialogFileMenu.dc.html: a window of its own, as on
// Windows, with General, Sharing, Details and Previous versions. Changes on
// General and Sharing wait for OK; Cancel or closing leaves the item as it
// was.
UI.AppWindow {
    id: props

    required property var view
    property var urls: []
    property int page: 0

    readonly property bool single: info.single
    readonly property bool local: urls.length > 0 && urls[0].toString().startsWith("file:")
    readonly property var share: single && info.isDir && local ? FileOps.networkShare(urls[0]) : ({ available: false, shared: false })
    readonly property var versions: single && local && page === 3 ? FileOps.previousVersions(urls[0]) : []

    // What OK applies.
    property string typedName: ""
    property bool wantReadOnly: false
    property bool wantHidden: false
    property bool wantOthers: false
    property bool wantShared: false

    deep: true
    stateKey: "properties"
    width: 460
    height: 640
    minimumWidth: 420
    minimumHeight: 480
    title: qsTr("%1 properties").arg(info.name)

    function reset() {
        typedName = info.name;
        wantReadOnly = info.readOnly;
        wantHidden = info.hidden;
        wantOthers = info.othersCanOpen;
        wantShared = props.share.shared === true;
    }

    function apply() {
        if (single && local) {
            if (info.canChangeReadOnly && wantReadOnly !== info.readOnly) {
                info.readOnly = wantReadOnly;
            }
            if (info.ownedByMe && wantOthers !== info.othersCanOpen) {
                info.othersCanOpen = wantOthers;
            }
            if (props.share.available && wantShared !== (props.share.shared === true)) {
                FileOps.setNetworkShare(urls[0], wantShared);
            }
            if (info.canRename && (typedName.trim() !== info.name || wantHidden !== info.hidden)) {
                FileOps.applyProperties(urls[0], info.fileName, info.isDir, typedName, wantHidden);
            }
        }
        close();
    }

    onClosing: destroy()

    ItemInfo {
        id: info
        urls: props.urls
        onChanged: props.reset()
    }

    FileMeta {
        id: meta
        url: props.single && props.page === 2 ? props.urls[0] : ""
    }

    // The rows of a tab: a muted label in a 120px column, the value beside it.
    component Field: RowLayout {
        property string label
        property string value
        default property alias extra: valueRow.data
        visible: value.length > 0 || valueRow.children.length > 1
        Layout.fillWidth: true
        Layout.maximumWidth: pages.width
        spacing: 12

        UI.Label {
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignTop
            text: parent.label
            color: UI.Style.inkMuted
        }

        RowLayout {
            id: valueRow
            Layout.fillWidth: true
            spacing: 8

            UI.Label {
                visible: text.length > 0
                Layout.fillWidth: valueRow.children.length <= 1
                text: parent.parent.value
                lineHeight: 1.1
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }

    component Rule: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Metrics.rule
    }

    component PageTab: T.TabButton {
        id: tab
        property int index
        implicitHeight: 32
        implicitWidth: tabLabel.implicitWidth + 28
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        checked: props.page === index
        onClicked: props.page = index
        Accessible.role: Accessible.PageTab

        background: Rectangle {
            radius: UI.Style.radiusSm
            color: tab.checked || tab.down ? UI.Style.surfaceHover : tab.hovered ? Metrics.hoverSoft : "transparent"

            UI.FocusRing {
                visible: tab.visualFocus
                targetRadius: UI.Style.radiusSm
            }
        }

        contentItem: UI.Label {
            id: tabLabel
            horizontalAlignment: Text.AlignHCenter
            text: tab.text
            font.weight: tab.checked ? UI.Style.weightSemiBold : UI.Style.weightRegular
            color: tab.checked ? UI.Style.ink : UI.Style.iconMuted
        }
    }

    // An empty tab says why in one line and offers one thing to do.
    component Empty: ColumnLayout {
        property string iconName
        property string heading
        property string text
        default property alias action: actionSlot.data
        Layout.fillWidth: true
        Layout.topMargin: 40
        spacing: 10

        UI.Icon {
            Layout.alignment: Qt.AlignHCenter
            name: parent.iconName
            size: 32
            color: UI.Style.iconMuted
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: parent.heading
            size: 15
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            horizontalAlignment: Text.AlignHCenter
            text: parent.text
            color: UI.Style.inkMuted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Item {
            id: actionSlot
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: props.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: props.frame
            buttons.showMinimise: false
            buttons.showMaximise: false

            Row {
                x: -2
                height: parent.height
                spacing: 10

                UI.ThemeIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    name: info.iconName
                }

                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: props.title
                    font.weight: UI.Style.weightMedium
                    color: props.active ? UI.Style.ink : UI.Style.inkMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.divider
        }

        Row {
            Layout.leftMargin: 16
            Layout.topMargin: 12
            spacing: 4

            PageTab { index: 0; text: qsTr("General") }
            PageTab { index: 1; text: qsTr("Sharing"); visible: props.single && props.local }
            PageTab { index: 2; text: qsTr("Details"); visible: props.single }
            PageTab { index: 3; text: qsTr("Previous versions"); visible: props.single && props.local }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: pages.implicitHeight + 36
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: UI.ScrollBar {}

            StackLayout {
                id: pages
                x: 20
                y: 18
                width: flick.width - 40
                currentIndex: props.page

                // General
                ColumnLayout {
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: pages.width
                        spacing: 14

                        FileIcon {
                            size: 44
                            iconName: info.iconName
                            url: info.single ? info.urls[0] : ""
                            preview: info.preview
                            stamp: info.stamp
                        }

                        UI.TextField {
                            visible: info.single
                            Layout.fillWidth: true
                            enabled: info.canRename
                            text: props.typedName
                            Accessible.name: qsTr("Name")
                            onTextEdited: props.typedName = text
                            Keys.onReturnPressed: props.apply()
                            Keys.onEnterPressed: props.apply()
                        }

                        UI.Label {
                            visible: !info.single
                            Layout.fillWidth: true
                            text: info.name
                            size: 15
                            font.weight: UI.Style.weightSemiBold
                        }
                    }

                    Rule {}

                    Field { label: qsTr("Type"); value: info.type }
                    Field {
                        label: qsTr("Opens with")
                        value: ""
                        visible: info.single && !info.isDir

                        UI.Label {
                            text: info.opensWith.length > 0 ? info.opensWith : qsTr("No app chosen yet")
                        }
                        UI.LinkButton {
                            text: qsTr("Change")
                            onClicked: {
                                props.view.chooseApp(props.urls);
                                props.close();
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Rule {}

                    Field { label: qsTr("Location"); value: info.location }
                    Field { label: qsTr("Size"); value: info.size }
                    Field { label: qsTr("Size on disk"); value: info.sizeOnDisk }
                    Field { label: qsTr("Contains"); value: info.contains }

                    Rule {}

                    Field { label: qsTr("Created"); value: info.created }
                    Field { label: qsTr("Modified"); value: info.modified }
                    Field { label: qsTr("Opened"); value: info.accessed }

                    Rule { visible: info.single && props.local }

                    Field {
                        label: qsTr("Access")
                        value: ""
                        visible: info.single && props.local

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            UI.CheckBox {
                                visible: !info.isDir
                                text: qsTr("Read-only")
                                enabled: info.canChangeReadOnly
                                checked: props.wantReadOnly
                                onToggled: props.wantReadOnly = checked
                            }
                            UI.CheckBox {
                                text: qsTr("Hidden")
                                enabled: info.canRename
                                checked: props.wantHidden
                                onToggled: props.wantHidden = checked
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                UI.Label {
                                    size: 12
                                    color: UI.Style.inkMuted
                                    text: !info.ownedByMe ? (info.isDir ? qsTr("%1 owns this folder.") : qsTr("%1 owns this file.")).arg(info.owner)
                                        : props.wantOthers ? qsTr("Other people on this computer can open it.")
                                        : info.isDir ? qsTr("Only you can open this folder.") : qsTr("Only you can open this file.")
                                }
                                UI.LinkButton {
                                    y: -3
                                    size: 12
                                    text: qsTr("Change who can")
                                    onClicked: props.page = 1
                                }
                            }
                        }
                    }
                }

                // Sharing
                ColumnLayout {
                    spacing: 14

                    UI.Label {
                        text: qsTr("On this computer")
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.CheckBox {
                        text: qsTr("Other people who use this computer can open it")
                        enabled: info.ownedByMe
                        checked: props.wantOthers
                        onToggled: props.wantOthers = checked
                    }
                    UI.Label {
                        visible: !info.ownedByMe
                        Layout.fillWidth: true
                        text: qsTr("Only %1, who owns it, can change this.").arg(info.owner)
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }

                    Rule {}

                    UI.Label {
                        text: qsTr("On your network")
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.CheckBox {
                        visible: info.isDir && props.share.available === true
                        text: qsTr("Share this folder on your network")
                        checked: props.wantShared
                        onToggled: props.wantShared = checked
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        lineHeight: 1.3
                        text: !info.isDir ? qsTr("Only folders can be shared on the network. Share the folder it is in instead.")
                            : props.share.available !== true ? qsTr("Sharing folders on the network needs Windows file sharing (Samba), which is not installed on this computer.")
                            : props.wantShared ? qsTr("Other computers on your network can open it as %1, read only, after signing in with an account on this computer.").arg("\\\\" + props.share.computer + "\\" + (props.share.name ?? info.name))
                            : qsTr("Not shared. Other computers on your network cannot see it.")
                    }
                    UI.LinkButton {
                        visible: !info.isDir
                        Layout.leftMargin: -4
                        size: 12
                        text: qsTr("Open the folder's properties")
                        onClicked: props.view.properties([info.folderUrl])
                    }

                    Rule { visible: !info.isDir }

                    UI.Label {
                        visible: !info.isDir
                        text: qsTr("Send a copy")
                        font.weight: UI.Style.weightSemiBold
                    }
                    UI.Button {
                        id: shareButton
                        visible: !info.isDir
                        text: qsTr("Share")
                        iconName: "share"
                        onClicked: {
                            const targets = FileOps.shareTargets(props.urls);
                            const entries = targets.map(t => ({ text: t.name, themeIcon: t.icon + ",document-share", run: () => FileOps.share(t.row) }));
                            if (entries.length > 0) {
                                entries.push({ separator: true });
                            }
                            entries.push({ text: qsTr("Copy as path"), icon: "link", run: () => FileOps.copyPaths(props.urls) });
                            shareMenu.entries = entries;
                            shareMenu.popup(shareButton, 0, shareButton.height + 4);
                        }
                    }
                }

                // Details
                ColumnLayout {
                    spacing: 14

                    Field { label: qsTr("Name"); value: info.fileName }
                    Field { label: qsTr("Type"); value: info.type }
                    Field { label: qsTr("Size"); value: info.size }
                    Field { label: qsTr("Contains"); value: info.contains }

                    Rule { visible: meta.fields.length > 0 || meta.loading }

                    UI.Label {
                        visible: meta.loading
                        text: qsTr("Reading the file…")
                        color: UI.Style.inkMuted
                    }

                    Repeater {
                        model: meta.fields

                        delegate: Field {
                            required property var modelData
                            label: modelData.label
                            value: modelData.value
                        }
                    }

                    UI.Label {
                        visible: !meta.loading && meta.fields.length === 0 && !info.isDir
                        Layout.fillWidth: true
                        text: qsTr("This file does not carry any other details, such as an author or a camera.")
                        size: 12
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }
                }

                // Previous versions
                ColumnLayout {
                    spacing: 8

                    UI.Label {
                        visible: props.versions.length > 0
                        Layout.fillWidth: true
                        Layout.bottomMargin: 6
                        text: qsTr("Earlier copies kept by the drive's snapshots, newest first.")
                        color: UI.Style.inkMuted
                        wrapMode: Text.Wrap
                    }

                    UI.Card {
                        visible: props.versions.length > 0
                        Layout.fillWidth: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 0

                            Repeater {
                                model: props.versions

                                delegate: RowLayout {
                                    id: version
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 62
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 16
                                        spacing: 2
                                        UI.Label { text: version.modelData.label }
                                        UI.Label {
                                            visible: text.length > 0
                                            text: version.modelData.detail
                                            size: 12
                                            color: UI.Style.inkMuted
                                        }
                                    }
                                    UI.Button {
                                        size: "small"
                                        text: qsTr("Open")
                                        onClicked: FileOps.open([version.modelData.url])
                                    }
                                    UI.Button {
                                        visible: !info.isDir && info.canRename
                                        Layout.rightMargin: 12
                                        size: "small"
                                        text: qsTr("Restore")
                                        onClicked: FileOps.restoreVersion(version.modelData.url, props.urls[0])
                                    }
                                }
                            }
                        }
                    }

                    Empty {
                        visible: props.versions.length === 0
                        iconName: "restore"
                        heading: qsTr("No previous versions")
                        text: info.isDir ? qsTr("This drive does not keep snapshots, so there are no earlier copies of this folder. Make a copy now to keep one by hand.") : qsTr("This drive does not keep snapshots, so there are no earlier copies of this file. Make a copy now to keep one by hand.")

                        UI.Button {
                            enabled: info.canRename
                            text: qsTr("Make a copy now")
                            onClicked: FileOps.duplicate(props.urls[0])
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: UI.Style.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            Item { Layout.fillWidth: true }

            UI.Button {
                text: qsTr("Cancel")
                onClicked: props.close()
            }
            UI.Button {
                kind: "primary"
                text: qsTr("OK")
                onClicked: props.apply()
            }
        }
    }

    UI.Menu {
        id: shareMenu
    }

    Shortcut {
        sequences: ["Escape"]
        onActivated: props.close()
    }
}
