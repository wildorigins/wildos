import QtQuick
import WildOS.Cooler
import WildOS.UI as UI

// The Cooler window as boards 10.9 to 10.14 draw it: one sheet, 1384 by
// 802, with a 44px title bar (icon, "Cooler", window buttons), the page list
// in a 214px column on the left with the readings strip at its foot, and
// the page on the right. With no supported cooler the pages give way to
// one message (10.13).
UI.AppWindow {
    id: window

    // The page asked for on the command line (--page), if any.
    property string requestedPage
    property string currentPage: "screen"
    readonly property var coolerPages: [
        { id: "screen", title: qsTr("Screen"), icon: "screen" },
        { id: "cooling", title: qsTr("Cooling"), icon: "fan" },
        { id: "about", title: qsTr("About this cooler"), icon: "info" }
    ]
    readonly property var pcPages: [
        { id: "lighting", title: qsTr("Lighting"), icon: "light" }
    ]
    // 10.13 lists all four with nothing chosen, Lighting before About.
    readonly property var allPages: [coolerPages[0], coolerPages[1], pcPages[0], coolerPages[2]]

    width: 1384
    height: 802
    minimumWidth: 940
    minimumHeight: 600
    title: qsTr("Cooler")
    deep: true

    function showPage(id) {
        currentPage = allPages.some(p => p.id === id) ? id : "screen";
    }

    Component.onCompleted: if (requestedPage.length > 0) showPage(requestedPage)

    function toast(message) {
        toastItem.show(message);
    }

    UI.TitleBar {
        id: titleBar
        width: parent.width
        tall: true
        frame: window.frame

        Row {
            x: -2
            height: parent.height
            spacing: 10

            UI.ThemeIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                name: "wildos-cooler"
            }

            Words {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Cooler")
                font.weight: UI.Style.weightMedium
                color: window.active ? UI.Style.ink : UI.Style.inkMuted
            }
        }
    }

    Item {
        id: body
        y: titleBar.height
        width: parent.width
        height: parent.height - y

        Rectangle {
            id: nav
            width: 214
            height: parent.height
            color: Colours.navShade
            bottomLeftRadius: window.maximised ? 0 : UI.Style.windowRadius - 1

            Column {
                y: 4
                width: parent.width

                NavHeading {
                    text: CoolerApp.present ? CoolerApp.model : qsTr("No cooler found")
                }

                Repeater {
                    model: CoolerApp.present ? window.coolerPages : window.allPages

                    NavItem {
                        required property var modelData
                        text: modelData.title
                        iconName: modelData.icon
                        enabled: CoolerApp.present
                        current: CoolerApp.present && window.currentPage === modelData.id
                        onClicked: window.showPage(modelData.id)
                    }
                }

                NavHeading {
                    visible: CoolerApp.present
                    text: qsTr("This PC")
                }

                Repeater {
                    model: CoolerApp.present ? window.pcPages : []

                    NavItem {
                        required property var modelData
                        text: modelData.title
                        iconName: modelData.icon
                        current: window.currentPage === modelData.id
                        onClicked: window.showPage(modelData.id)
                    }
                }
            }

            StatusStrip {
                visible: CoolerApp.present
                x: 12
                y: parent.height - height - 12
                width: parent.width - 24
            }
        }

        Rectangle {
            x: nav.width - 1
            width: 1
            height: parent.height
            color: UI.Style.borderSubtle
        }

        Item {
            id: content
            x: nav.width
            width: parent.width - x
            height: parent.height
            clip: true
            // Each page rises a little as it fades in, as the other Wild OS
            // apps with a page list do.
            transform: UI.Reveal {
                item: content
                key: CoolerApp.present ? window.currentPage : "none"
                dy: UI.Style.travelMedium
            }

            Loader {
                anchors.fill: parent
                active: !CoolerApp.present
                visible: active
                sourceComponent: NonePage {
                    onCloseRequested: window.close()
                }
            }

            Loader {
                anchors.fill: parent
                active: CoolerApp.present && window.currentPage === "screen"
                visible: active
                sourceComponent: ScreenPage {}
            }

            Loader {
                anchors.fill: parent
                active: CoolerApp.present && window.currentPage === "cooling"
                visible: active
                sourceComponent: CoolingPage {}
            }

            Loader {
                anchors.fill: parent
                active: CoolerApp.present && window.currentPage === "about"
                visible: active
                sourceComponent: AboutPage {
                    onMessage: text => window.toast(text)
                }
            }

            Loader {
                anchors.fill: parent
                active: CoolerApp.present && window.currentPage === "lighting"
                visible: active
                sourceComponent: LightingPage {}
            }
        }
    }

    UI.Toast {
        id: toastItem
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: nav.width / 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }
}
