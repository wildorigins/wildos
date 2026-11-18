import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import WildOS.Store
import WildOS.UI as UI

// One app, as StoreAppPage.dc.html and StoreDownloading.dc.html: the icon,
// name and button, with the download's progress beside the button while it
// installs; the facts strip; screenshots; what it can reach; then what it
// does and the rest of its details.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind
    readonly property var app: Catalog.app(arg)
    readonly property bool sandboxed: app && app.source === "flatpak"
    readonly property string step: button.shown
    readonly property bool working: ["waiting", "preparing", "downloading", "installing", "removing", "paused"].indexOf(step) >= 0
    property bool expanded: false
    // Flathub answers within seconds; if nothing comes back (no network, no
    // metadata for this app) the page says so instead of checking for ever.
    property bool checkGaveUp: false

    Timer {
        running: page.sandboxed && page.app && !page.app.reachKnown && !page.app.permissionsUnknown
        interval: 20000
        onTriggered: page.checkGaveUp = true
    }

    Component.onCompleted: if (app) app.loadDetails()

    Connections {
        target: page.app
        function onDataChanged() {
            page.app.loadDetails();
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 24

        Item {
            Layout.preferredWidth: 128
            Layout.preferredHeight: 128
            Layout.alignment: Qt.AlignTop

            AppIcon {
                id: bigIcon
                anchors.fill: parent
                size: 128
                source: page.app ? page.app.iconSource : ""
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(0, 0, 0, 0.35)
                    shadowVerticalOffset: 14
                    shadowBlur: 0.8
                    blurMax: 40
                }
            }

            // A small hop when the app has just arrived.
            SequentialAnimation {
                id: bounce
                NumberAnimation { target: bigIcon; property: "scale"; to: 1.07; duration: 140; easing.type: Easing.OutQuad }
                NumberAnimation { target: bigIcon; property: "scale"; to: 1; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 2.6 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 5

            UI.Label {
                Layout.fillWidth: true
                text: page.app ? page.app.name : ""
                size: 32
                font.weight: UI.Style.weightBold
                font.letterSpacing: -0.64
                Accessible.role: Accessible.Heading
            }

            UI.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: page.app ? page.app.tagline || page.app.summary : ""
                size: 17
                color: Metrics.inkSecondary
            }

            StoreLink {
                Layout.leftMargin: -4
                visible: page.app && page.app.developer.length > 0
                enabled: page.app && page.app.homepage.length > 0
                text: page.app ? page.app.developer : ""
                Accessible.description: qsTr("Opens the website")
                onClicked: Qt.openUrlExternally(page.app.homepage)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 14

                StoreButton {
                    id: button
                    Layout.alignment: Qt.AlignVCenter
                    big: true
                    app: page.app
                    onLanded: if (!UI.Style.reducedMotion) bounce.restart()
                }

                // Beside the ring while it works: what is happening, how far
                // it has got and how long is left (StoreDownloading).
                Column {
                    id: progress
                    visible: page.working
                    Layout.preferredWidth: 260
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6

                    Item {
                        width: parent.width
                        height: stepLabel.implicitHeight

                        UI.Label {
                            id: stepLabel
                            text: {
                                switch (page.step) {
                                case "waiting": return qsTr("Waiting");
                                case "preparing": return qsTr("Getting ready");
                                case "installing": return qsTr("Installing");
                                case "removing": return qsTr("Uninstalling");
                                case "paused": return qsTr("Paused");
                                }
                                return page.app && page.app.action === "update" ? qsTr("Downloading the update") : qsTr("Downloading");
                            }
                            size: 13
                            font.weight: UI.Style.weightSemiBold
                        }

                        UI.Label {
                            anchors.right: parent.right
                            visible: page.step === "downloading" || page.step === "paused"
                            text: page.app ? page.app.bytesText : ""
                            size: 13
                            font.features: { "tnum": 1 }
                            color: Metrics.inkSecondary
                        }
                    }

                    Rectangle {
                        id: track
                        width: parent.width
                        height: 5
                        radius: 3
                        color: UI.Style.tone(0.12)
                        clip: true

                        readonly property bool known: page.step === "downloading" || page.step === "paused"
                        property real shown: page.app ? Math.max(0, page.app.progress) : 0
                        Behavior on shown {
                            enabled: !UI.Style.reducedMotion
                            NumberAnimation { duration: 600; easing.type: Easing.OutCubic }
                        }

                        Rectangle {
                            visible: track.known
                            width: track.width * Math.min(100, track.shown) / 100
                            height: parent.height
                            radius: 3
                            color: page.step === "paused" ? Metrics.inkSecondary : UI.Style.accent
                        }

                        // While the amount of work is unknown a short band
                        // drifts along the bar instead of it standing still.
                        Rectangle {
                            id: sweep
                            visible: !track.known && page.step !== "waiting"
                            height: parent.height
                            width: parent.width * 0.3
                            radius: 3
                            color: UI.Style.accent
                            NumberAnimation on x {
                                running: sweep.visible
                                loops: Animation.Infinite
                                from: -sweep.width
                                to: track.width
                                duration: UI.Style.reducedMotion ? 4000 : 1400
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }

                    UI.Label {
                        width: parent.width
                        text: {
                            if (page.step === "waiting") {
                                return qsTr("It starts when the apps before it finish.");
                            }
                            if (page.step === "paused") {
                                return qsTr("Press play to carry on.");
                            }
                            const left = page.app ? page.app.timeLeftText : "";
                            return left.length > 0 ? qsTr("%1. You can keep browsing.").arg(left) : qsTr("You can keep browsing.");
                        }
                        size: 12
                        color: Metrics.inkTertiary
                    }

                    UI.Label {
                        width: parent.width
                        visible: text.length > 0 && page.step !== "removing"
                        text: page.app ? page.app.sharedNote : ""
                        size: 12
                        color: Metrics.inkTertiary
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                }

                UI.Label {
                    visible: !page.working && text.length > 0
                    Layout.fillWidth: true
                    text: {
                        if (!page.app) {
                            return "";
                        }
                        if (page.app.error.length > 0) {
                            return page.app.error;
                        }
                        if (page.step === "nospace") {
                            return page.app.statusLine;
                        }
                        return page.step === "get" ? qsTr("Free") : "";
                    }
                    size: 12.5
                    color: page.app && page.app.error.length > 0 ? Metrics.danger : Metrics.inkTertiary
                }

                UI.IconButton {
                    visible: !page.working && page.app && page.app.error.length > 0
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    iconName: "close"
                    iconSize: 12
                    tip: qsTr("Dismiss")
                    onClicked: page.app.clearError()
                }

                Item {
                    Layout.fillWidth: true
                    visible: page.working
                }
            }
        }

        UI.IconButton {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            iconName: "share"
            iconSize: 18
            iconColor: Metrics.inkSecondary
            enabled: page.app && page.app.appstreamId.length > 0
            tip: qsTr("Copy link")
            onClicked: page.store.copyLink(page.app)
        }
    }

    // Price, size, category, maker, licence and, for Flathub apps, safety.
    FactStrip {
        Layout.fillWidth: true
        cells: {
            const a = page.app;
            if (!a) {
                return [];
            }
            const out = [{ label: qsTr("Price"), value: qsTr("Free"), sub: "" }];
            const size = a.installed ? a.sizeText : a.downloadText;
            if (size.length > 0) {
                out.push({ label: qsTr("Size"), value: size, sub: a.installed ? qsTr("Installed") : qsTr("Download") });
            }
            if (a.categoryLabel.length > 0) {
                out.push({ label: qsTr("Category"), value: a.categoryLabel, sub: "" });
            }
            if (a.developer.length > 0) {
                out.push({ label: qsTr("Made by"), value: a.developer, sub: "" });
            }
            out.push(a.openSource ? { label: qsTr("Licence"), value: qsTr("Open source"), sub: a.licence } : { label: qsTr("Licence"), value: qsTr("Free to use"), sub: "" });
            if (page.sandboxed) {
                out.push({ label: qsTr("Safety"), value: "", icon: "shield-check", sub: a.checked ? qsTr("Checked and sandboxed") : qsTr("Sandboxed") });
            }
            return out;
        }
    }

    ListView {
        id: shots
        Layout.fillWidth: true
        // The strip runs on to the window's edge, as the board's does.
        Layout.rightMargin: -Metrics.sidePadding
        Layout.preferredHeight: 262
        visible: count > 0
        orientation: ListView.Horizontal
        spacing: 18
        clip: true
        // Only the pictures in view load, and the next two after them.
        cacheBuffer: 2 * (440 + 18)
        boundsBehavior: Flickable.StopAtBounds
        model: page.app ? page.app.screenshots : []
        ScrollBar.horizontal: UI.ScrollBar {}

        delegate: T.AbstractButton {
            id: shot
            required property var modelData
            required property int index
            height: 262
            readonly property int naturalWidth: modelData.width > 0 && modelData.height > 0 ? Math.round(262 * modelData.width / modelData.height) : 466
            width: failed ? 0 : naturalWidth
            visible: !failed
            // A picture the server no longer has takes no room at all.
            readonly property bool failed: shotImage.status === Image.Error
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.Button
            Accessible.name: modelData.caption || qsTr("Screenshot %1").arg(index + 1)
            onClicked: viewer.show(page.app.screenshots, index)

            background: Rectangle {
                radius: UI.Style.radiusMd
                color: UI.Style.surfaceRaised

                UI.FocusRing {
                    visible: shot.visualFocus
                    targetRadius: UI.Style.radiusMd
                }
            }

            contentItem: Item {
                Image {
                    id: shotImage
                    anchors.fill: parent
                    anchors.margins: 1
                    // The catalogue's size closest to the card, never the
                    // original, decoded at the size it is drawn.
                    source: Catalog.pickImage(shot.modelData.images, Math.round(shot.naturalWidth * Screen.devicePixelRatio), Math.round(262 * Screen.devicePixelRatio))
                    sourceSize.width: Math.round(shot.naturalWidth * Screen.devicePixelRatio)
                    sourceSize.height: Math.round(262 * Screen.devicePixelRatio)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    visible: false
                }

                RoundedImage {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: shotImage
                    radius: UI.Style.radiusMd - 1
                }

                Rectangle {
                    anchors.fill: parent
                    radius: UI.Style.radiusMd
                    color: shot.hovered ? UI.Style.tone(0.06) : "transparent"
                    border.width: 1
                    border.color: Metrics.divider
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        SectionHeader {
            title: qsTr("What it can reach")
            linkText: page.sandboxed && page.app && page.app.permissions.length > 0 ? qsTr("Details") : ""
            onLinkClicked: permissionsDialog.open()
        }

        // Flathub apps: what the sandbox lets it reach and what it keeps
        // it from, from the app's own metadata.
        RowLayout {
            visible: page.sandboxed && page.app && page.app.reachKnown
            Layout.fillWidth: true
            spacing: 16

            ReachCard {
                visible: page.app && page.app.canReach.length > 0
                title: qsTr("Can reach")
                iconName: "tick"
                tint: Metrics.success
                rows: page.app ? page.app.canReach : []
            }

            ReachCard {
                visible: page.app && page.app.cantReach.length > 0
                title: qsTr("Can't reach")
                iconName: "close"
                tint: Metrics.danger
                rows: page.app ? page.app.cantReach : []
            }

            ReachCard {
                visible: page.app && page.app.worksOffline
                title: qsTr("Good to know")
                iconName: "info"
                tint: UI.Style.accent
                rows: [{ tick: true, label: qsTr("Works fully offline") }]
            }
        }

        ReachCard {
            visible: page.sandboxed && page.app && !page.app.reachKnown
            readonly property bool failed: page.app && (page.app.permissionsUnknown || page.checkGaveUp)
            title: failed ? qsTr("Couldn't check what it asks for") : qsTr("Checking what it asks for")
            iconName: "info"
            tint: UI.Style.accent
            text: failed ? qsTr("The app library can't be reached right now. Try again when you're online.") : ""
        }

        ReachCard {
            visible: page.app && !page.sandboxed && page.app.source.length > 0
            title: qsTr("Runs without a sandbox")
            iconName: "info"
            tint: UI.Style.accent
            text: page.app ? qsTr("%1 runs like any program on your computer, so it can open any of your files. It is updated along with the rest of Wild OS.").arg(page.app.name) : ""
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14
        visible: page.app && page.app.description.length > 0

        SectionHeader {
            title: qsTr("About")
        }

        Item {
            id: aboutClip
            Layout.fillWidth: true
            Layout.maximumWidth: 820
            implicitHeight: page.expanded || !about.truncatedText ? about.implicitHeight : 6 * about.lineHeightPx
            clip: true
            // Fades the cut-off line instead of slicing it in half.
            layer.enabled: !page.expanded && about.truncatedText
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: fadeMask
                maskSpreadAtMin: 1.0
            }

            Text {
                id: about
                readonly property real lineHeightPx: font.pixelSize * 1.6
                readonly property bool truncatedText: implicitHeight > 6 * lineHeightPx + 4
                width: parent.width
                text: page.app ? page.app.description : ""
                textFormat: Text.RichText
                wrapMode: Text.Wrap
                color: Metrics.inkSecondary
                linkColor: UI.Style.accent
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(UI.Style.px(14))
                font.pixelSize: UI.Style.px(14)
                lineHeight: 1.6
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }

        Item {
            id: fadeMask
            width: aboutClip.width
            height: aboutClip.height
            visible: false
            layer.enabled: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: "white" }
                    GradientStop { position: 0.75; color: "white" }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
        }

        StoreLink {
            visible: about.truncatedText
            Layout.leftMargin: -4
            Layout.topMargin: -8
            text: page.expanded ? qsTr("Show less") : qsTr("Read more")
            onClicked: page.expanded = !page.expanded
        }
    }

    Rectangle {
        visible: page.app && page.app.swapTitle.length > 0
        Layout.fillWidth: true
        implicitHeight: swapRow.implicitHeight + 36
        radius: Metrics.radiusCard
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: UI.Style.accentAlpha(0.16, 0.12) }
            GradientStop { position: 1; color: Qt.rgba(196 / 255, 122 / 255, 138 / 255, UI.Style.dark ? 0.12 : 0.09) }
        }
        border.width: 1
        border.color: UI.Style.accentAlpha(0.22, 0.20)

        Row {
            id: swapRow
            x: 22
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 44
            spacing: 18

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 44
                height: 44
                radius: 12
                color: UI.Style.accentAlpha(0.18, 0.14)

                StoreIcon {
                    anchors.centerIn: parent
                    name: "swap-windows"
                    size: 22
                    color: UI.Style.accent
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 62
                spacing: 4

                UI.Label {
                    width: parent.width
                    text: page.app ? page.app.swapTitle : ""
                    size: 16
                    font.weight: UI.Style.weightBold
                }

                UI.Label {
                    width: parent.width
                    text: page.app ? page.app.swapText : ""
                    size: 13.5
                    lineHeight: 1.45
                    color: Metrics.inkSecondary
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        Layout.topMargin: -10
        spacing: 18

        StoreLink {
            visible: page.app && page.app.homepage.length > 0
            text: qsTr("Website")
            onClicked: Qt.openUrlExternally(page.app.homepage)
        }

        StoreLink {
            visible: page.app && page.app.installed && !page.working
            text: qsTr("Uninstall")
            onClicked: page.store.askRemove(page.app)
        }

        UI.Label {
            visible: page.app && page.app.version.length > 0
            height: 26
            text: page.app ? qsTr("Version %1").arg(page.app.version) : ""
            size: 13
            color: Metrics.inkTertiary
        }
    }

    // Some apps come both from Flathub and from Wild OS's own (Debian)
    // packages. Which one is about to install, and what that means for the
    // person, in plain words, with the switch to the other.
    Column {
        Layout.fillWidth: true
        visible: page.app && !page.app.installed && !page.working && page.app.hasFlatpak && page.app.hasApt
        spacing: 4

        UI.Label {
            width: parent.width
            text: page.sandboxed ? qsTr("Installing the sandboxed version, from Flathub") : qsTr("Installing the Wild OS version, from Debian")
            size: 13.5
            font.weight: UI.Style.weightSemiBold
        }

        UI.Label {
            width: parent.width
            text: page.sandboxed
                ? qsTr("Usually the newest release. It runs in a sandbox, so it only reaches what you allow. It updates on its own, separately from Wild OS.")
                : qsTr("Tested with Wild OS and updated with the rest of your system. It isn't sandboxed, so it can reach everything your account can. Often an older release.")
            size: 13
            lineHeight: 1.45
            color: Metrics.inkSecondary
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        StoreLink {
            text: page.sandboxed ? qsTr("Install the Wild OS version instead") : qsTr("Install the sandboxed version instead")
            onClicked: page.app.useSource(page.sandboxed ? "apt" : "flatpak")
        }
    }

    UI.Dialog {
        id: permissionsDialog
        title: page.app ? qsTr("What %1 can reach").arg(page.app.name) : ""
        text: !page.app ? ""
            : page.app.installed ? qsTr("Changes apply the next time %1 opens. %1 will ask if it ever needs something new.").arg(page.app.name)
            : qsTr("This is what %1 asks for. You can change it once it's installed.").arg(page.app.name)

        Repeater {
            model: page.sandboxed && page.app ? page.app.permissions : []

            Item {
                id: permission
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: Math.max(36, permissionLabel.implicitHeight + 12)

                UI.Label {
                    id: permissionLabel
                    anchors.left: parent.left
                    anchors.right: permissionState.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: permission.modelData.label
                    size: 14
                }

                Row {
                    id: permissionState
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    UI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: permission.modelData.fixed ? qsTr("Always") : permission.modelData.allowed ? qsTr("On") : qsTr("Off")
                        size: 12
                        color: Metrics.inkSecondary
                    }

                    UI.Toggle {
                        visible: !permission.modelData.fixed
                        anchors.verticalCenter: parent.verticalCenter
                        checked: permission.modelData.allowed
                        locked: !permission.modelData.editable
                        lockedReason: qsTr("Change this once the app is installed")
                        Accessible.name: permission.modelData.label
                        onToggled: page.app.setPermission(permission.modelData.token, checked)
                    }
                }
            }
        }

        buttons: [
            UI.Button {
                kind: "primary"
                text: qsTr("Done")
                onClicked: permissionsDialog.close()
            }
        ]
    }

    ScreenshotViewer {
        id: viewer
    }
}
