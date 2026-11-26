import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// A package file someone opened (.deb, .flatpakref or .flatpak), laid out
// as an app's own page: the icon, name and button with the progress beside
// it, the facts strip, then where it comes from, what it needs and what it
// says it does. Nothing in the file has been checked, and the page says so
// in as many words; the name, maker and description are what the file says
// about itself.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind

    readonly property bool isDeb: pkg.kind === "deb"
    readonly property bool working: pkg.state === "working"
    readonly property bool installable: pkg.state === "get" || pkg.state === "update" || pkg.state === "failed"
    // A .flatpakref that only points at Flathub has a proper Store page,
    // with its permissions and whether Flathub checked it.
    readonly property string flathubKey: pkg.flathubId.length > 0 && Catalog.ready ? Catalog.resolve(pkg.flathubId) : ""

    LocalPackage {
        id: pkg
        path: page.arg
    }

    // The Store's own button, driven by the file instead of a catalogue
    // app, so it looks and moves the same.
    QtObject {
        id: buttonApp
        readonly property string name: pkg.name
        readonly property int progress: pkg.progress
        readonly property string statusLine: ""
        readonly property bool cancellable: false
        readonly property bool installed: pkg.state === "installed" || pkg.state === "done" || pkg.state === "newer"
        readonly property string source: ""
        readonly property string buttonState: {
            switch (pkg.state) {
            case "get": return "get";
            case "update": return "update";
            case "failed": return "retry";
            case "working": return "installing";
            case "installed":
            case "done":
            case "newer":
                return pkg.canOpen ? "open" : "installed";
            }
            return "nospace";
        }
        function install() { pkg.install(); }
        function update() { pkg.install(); }
        function open() { pkg.open(); }
        function cancel() {}
        function pause() {}
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
                source: pkg.iconSource
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(0, 0, 0, 0.35)
                    shadowVerticalOffset: 14
                    shadowBlur: 0.8
                    blurMax: 40
                }
            }

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
                text: pkg.name
                size: 32
                font.weight: UI.Style.weightBold
                font.letterSpacing: -0.64
                Accessible.role: Accessible.Heading
            }

            UI.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: pkg.summary
                size: 17
                color: Metrics.inkSecondary
            }

            UI.Label {
                Layout.fillWidth: true
                text: qsTr("From the file %1").arg(pkg.fileName)
                size: 13
                color: Metrics.inkTertiary
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 14

                StoreButton {
                    id: button
                    Layout.alignment: Qt.AlignVCenter
                    big: true
                    app: buttonApp
                    onLanded: if (!UI.Style.reducedMotion) bounce.restart()
                }

                // Beside the ring while it works, as on an app's page.
                Column {
                    visible: page.working
                    Layout.preferredWidth: 260
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6

                    UI.Label {
                        text: {
                            switch (pkg.phase) {
                            case "auth": return qsTr("Waiting for the password");
                            case "wait": return qsTr("Waiting for another install to finish");
                            case "download": return qsTr("Downloading");
                            case "install": return qsTr("Installing");
                            }
                            return qsTr("Getting ready");
                        }
                        size: 13
                        font.weight: UI.Style.weightSemiBold
                    }

                    Rectangle {
                        id: track
                        width: parent.width
                        height: 5
                        radius: 3
                        color: UI.Style.tone(0.12)
                        clip: true

                        readonly property bool known: pkg.progress >= 0 && pkg.phase !== "auth" && pkg.phase !== "wait"
                        property real shown: Math.max(0, pkg.progress)
                        Behavior on shown {
                            enabled: !UI.Style.reducedMotion
                            NumberAnimation { duration: 600; easing.type: Easing.OutCubic }
                        }

                        Rectangle {
                            visible: track.known
                            width: track.width * Math.min(100, track.shown) / 100
                            height: parent.height
                            radius: 3
                            color: UI.Style.accent
                        }

                        Rectangle {
                            id: sweep
                            visible: !track.known
                            height: parent.height
                            width: parent.width * 0.3
                            radius: 3
                            color: UI.Style.accent
                            NumberAnimation on x {
                                running: sweep.visible && page.working
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
                        text: page.isDeb ? qsTr("Its own setup runs as part of this, which can take a minute.") : qsTr("You can keep browsing.")
                        size: 12
                        color: Metrics.inkTertiary
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                }

                UI.Label {
                    visible: !page.working && text.length > 0
                    Layout.fillWidth: true
                    readonly property bool bad: ["unreadable", "blocked", "failed"].indexOf(pkg.state) >= 0
                    text: {
                        switch (pkg.state) {
                        case "reading": return qsTr("Reading the file");
                        case "unreadable":
                        case "blocked":
                        case "failed":
                            return pkg.problem;
                        case "update": return qsTr("Version %1 is installed. This file has %2.").arg(pkg.installedVersion).arg(pkg.version);
                        case "installed": return qsTr("This version is installed.");
                        case "newer": return qsTr("A newer version is installed (%1).").arg(pkg.installedVersion);
                        case "done": return pkg.canOpen ? qsTr("Installed. It's in Start too.") : qsTr("Installed.");
                        }
                        return "";
                    }
                    size: 12.5
                    color: bad ? Metrics.danger : Metrics.inkTertiary
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    maximumLineCount: 5
                }

                Item {
                    Layout.fillWidth: true
                    visible: page.working
                }
            }
        }
    }

    FactStrip {
        Layout.fillWidth: true
        visible: cells.length > 0
        cells: {
            if (pkg.state === "reading" || pkg.state === "unreadable") {
                return [];
            }
            const out = [];
            if (pkg.version.length > 0) {
                out.push({ label: qsTr("Version"), value: pkg.version, sub: pkg.installedVersion.length > 0 && pkg.installedVersion !== pkg.version ? qsTr("You have %1").arg(pkg.installedVersion) : "" });
            }
            if (pkg.installedSizeText.length > 0) {
                out.push({ label: qsTr("Size"), value: pkg.installedSizeText, sub: qsTr("Once installed") });
            } else if (pkg.fileSizeText.length > 0) {
                out.push({ label: qsTr("Size"), value: pkg.fileSizeText, sub: qsTr("The file") });
            }
            out.push(page.isDeb ? { label: qsTr("Kind"), value: qsTr("Debian package"), sub: qsTr("Not sandboxed") } : { label: qsTr("Kind"), value: "Flatpak", sub: qsTr("Sandboxed") });
            if (pkg.maker.length > 0) {
                out.push({ label: qsTr("Made by"), value: pkg.maker, sub: qsTr("As the file says") });
            }
            if (!page.isDeb) {
                out.push({ label: qsTr("Updates from"), value: pkg.origin.length > 0 ? pkg.origin : qsTr("None"), sub: pkg.origin.length > 0 ? "" : qsTr("It won't update itself") });
            }
            return out;
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14
        visible: pkg.state !== "reading" && pkg.state !== "unreadable"

        SectionHeader {
            title: qsTr("Where it comes from")
        }

        ReachCard {
            visible: page.flathubKey.length > 0
            title: qsTr("This app is on Flathub")
            iconName: "info"
            tint: UI.Style.accent
            text: qsTr("Its page in the Store shows what it can reach and whether Flathub has checked who makes it.")
        }

        StoreLink {
            visible: page.flathubKey.length > 0
            Layout.leftMargin: -4
            Layout.topMargin: -6
            text: qsTr("See it in the Store")
            onClicked: page.store.openApp(page.flathubKey)
        }

        ReachCard {
            visible: page.flathubKey.length === 0
            title: page.isDeb ? qsTr("Not from Wild OS, and not checked") : qsTr("Not from the Store, and not checked")
            iconName: "info"
            tint: Metrics.danger
            text: {
                if (page.isDeb) {
                    return qsTr("Wild OS didn't make this package and nothing has checked it. Installing it runs its own setup with full access to your computer, and the app it installs can open any of your files. Wild OS won't update it, unless the package adds its own source of updates. Only install it if you trust where it came from.");
                }
                const updates = pkg.origin.length > 0 ? qsTr("It downloads from %1, and its updates come from there too.").arg(pkg.origin) : qsTr("It won't get updates.");
                return qsTr("Nothing has checked this app. It runs in a sandbox, but the app itself decides how much that sandbox lets it reach. %1 Only install it if you trust where it came from.").arg(updates);
            }
        }
    }

    // What a .deb needs from the Wild OS repositories, as PackageKit works
    // it out before anything is installed.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14
        visible: page.isDeb && pkg.depends.length > 0 && pkg.state !== "unreadable"

        SectionHeader {
            title: qsTr("What it needs")
        }

        ReachCard {
            readonly property string needs: qsTr("It asks for %1.").arg(pkg.depends.join(", "))
            title: pkg.checkingDepends ? qsTr("Checking what it needs")
                : !page.installable ? qsTr("Parts it needs")
                : pkg.extras.length > 0 ? (pkg.extras.length === 1 ? qsTr("Also installs 1 part from Wild OS") : qsTr("Also installs %1 parts from Wild OS").arg(pkg.extras.length))
                : qsTr("Everything it needs is already here")
            iconName: pkg.state === "blocked" ? "close" : "tick"
            tint: pkg.state === "blocked" ? Metrics.danger : Metrics.success
            text: page.installable && pkg.extras.length > 0 ? pkg.extras.join(", ") + ".\n\n" + needs : needs
        }

        ReachCard {
            visible: page.installable && pkg.removes.length > 0
            title: pkg.removes.length === 1 ? qsTr("Removes 1 part of your system") : qsTr("Removes %1 parts of your system").arg(pkg.removes.length)
            iconName: "close"
            tint: Metrics.danger
            text: qsTr("To make room, installing it takes %1 off your computer. Anything that uses them stops working.").arg(pkg.removes.join(", "))
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14
        visible: pkg.description.length > 0 && pkg.state !== "unreadable"

        SectionHeader {
            title: qsTr("What it says it does")
        }

        Text {
            Layout.fillWidth: true
            Layout.maximumWidth: 820
            text: pkg.description
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Metrics.inkSecondary
            font.family: UI.Style.fontFamily
            font.letterSpacing: UI.Style.tracking(UI.Style.px(14))
            font.pixelSize: UI.Style.px(14)
            lineHeight: 1.6
        }
    }

    Flow {
        Layout.fillWidth: true
        Layout.topMargin: -10
        spacing: 18

        StoreLink {
            visible: pkg.homepage.length > 0
            text: qsTr("Website")
            Accessible.description: qsTr("Opens the website the file names")
            onClicked: Qt.openUrlExternally(pkg.homepage)
        }

        UI.Label {
            visible: pkg.id.length > 0
            height: 26
            text: (page.isDeb ? qsTr("Package %1") : qsTr("App ID %1")).arg(pkg.id)
            size: 13
            color: Metrics.inkTertiary
        }
    }
}
