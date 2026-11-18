pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import WildOS.Terminal
import WildOS.UI as UI

// Terminal's settings, opened as a tab, laid out as a Wild OS Settings
// page: navigation on the left, the page title as large breadcrumbs, and
// grouped cards of rows below.
Item {
    id: view

    property real radius: 0
    property bool glass: true
    property string page: "startup"
    // The profile being edited, when the Profiles page is showing one.
    property string profileId: ""

    readonly property var pages: [
        { id: "startup", title: qsTr("Startup"), icon: "startup" },
        { id: "appearance", title: qsTr("Appearance"), icon: "palette" },
        { id: "profiles", title: qsTr("Profiles"), icon: "profiles" },
        { id: "interaction", title: qsTr("Interaction"), icon: "pointer" },
        { id: "keyboard", title: qsTr("Keyboard shortcuts"), icon: "keyboard" }
    ]

    function titleOf(id) {
        for (const p of pages) {
            if (p.id === id) {
                return p.title;
            }
        }
        return "";
    }

    function open(id) {
        page = id;
        profileId = "";
    }

    function openProfile(id) {
        page = "profiles";
        profileId = id;
    }

    // A deleted profile leaves its page.
    Connections {
        target: Config
        function onProfilesChanged() {
            if (view.profileId.length > 0 && Config.profile(view.profileId).id !== view.profileId) {
                view.profileId = "";
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: view.glass ? UI.Style.glassWindowDeep : UI.Style.windowOpaque
        topLeftRadius: 0
        topRightRadius: 0
        bottomLeftRadius: view.radius
        bottomRightRadius: view.radius
    }

    // Clicks on the empty page must not fall through to the terminal
    // underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    Flickable {
        id: nav
        width: Metrics.navWidth
        height: parent.height
        contentHeight: navColumn.implicitHeight + 20
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: navColumn
            x: 14
            y: 14
            width: Metrics.navWidth - 28
            spacing: 2

            Repeater {
                model: view.pages

                NavItem {
                    required property var modelData
                    iconName: modelData.icon
                    text: modelData.title
                    current: view.page === modelData.id
                    onClicked: view.open(modelData.id)
                }
            }
        }
    }

    Item {
        id: content
        x: Metrics.navWidth
        width: parent.width - Metrics.navWidth
        height: parent.height

        Row {
            id: header
            x: Metrics.contentLeft
            y: 8
            height: Math.max(34, UI.Style.px(28) + 6)
            spacing: 10

            Repeater {
                id: crumbs
                model: {
                    const trail = [{ id: view.page, title: view.titleOf(view.page) }];
                    if (view.profileId.length > 0) {
                        trail.push({ id: "", title: Config.profile(view.profileId).name || qsTr("Profile") });
                    }
                    return trail;
                }

                Row {
                    id: crumb
                    required property var modelData
                    required property int index
                    readonly property bool last: index === crumbs.count - 1
                    spacing: 10
                    anchors.verticalCenter: parent.verticalCenter

                    T.AbstractButton {
                        id: crumbButton
                        anchors.verticalCenter: parent.verticalCenter
                        width: crumbText.implicitWidth
                        height: header.height
                        padding: 0
                        enabled: !crumb.last
                        hoverEnabled: true
                        focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                        Accessible.role: crumb.last ? Accessible.Heading : Accessible.Link
                        Accessible.name: crumb.modelData.title
                        onClicked: view.open(crumb.modelData.id)

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

        Loader {
            id: pageLoader
            y: header.y + header.height + 18
            width: parent.width
            height: parent.height - y
            // Each page rises a little as it fades in, as Windows Terminal's
            // settings do.
            transform: UI.Reveal {
                item: pageLoader
                key: view.page + "/" + view.profileId
                dy: UI.Style.travelMedium
            }
            sourceComponent: view.page === "appearance" ? appearancePage
                           : view.page === "profiles" ? (view.profileId.length > 0 ? profilePage : profilesPage)
                           : view.page === "interaction" ? interactionPage
                           : view.page === "keyboard" ? keyboardPage
                           : startupPage
        }
    }

    Component {
        id: startupPage
        StartupPage {}
    }

    Component {
        id: appearancePage
        AppearancePage {}
    }

    Component {
        id: profilesPage
        ProfilesPage {
            onOpenProfile: id => view.openProfile(id)
        }
    }

    Component {
        id: profilePage
        ProfilePage {
            profileId: view.profileId
            onOpenProfile: id => view.openProfile(id)
        }
    }

    Component {
        id: interactionPage
        InteractionPage {}
    }

    Component {
        id: keyboardPage
        KeyboardPage {}
    }
}
