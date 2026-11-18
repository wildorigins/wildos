import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Discover, StoreDiscover.dc.html: the big swap card with two smaller ones
// beside it, then the essentials in three columns, and the curated lists
// for making things and playing below.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind

    readonly property var swapKeys: Catalog.catalogRevision >= 0 ? Catalog.swaps() : []
    readonly property var createKeys: Catalog.catalogRevision >= 0 ? Catalog.featured("create") : []
    readonly property var gameKeys: Catalog.catalogRevision >= 0 ? Catalog.featured("games") : []
    // The essentials, topped up from the popular list to fill three rows.
    readonly property var essentialKeys: {
        if (Catalog.catalogRevision < 0) {
            return [];
        }
        const out = Catalog.essentials().slice();
        for (const k of Catalog.featured("popular")) {
            if (out.length >= 9) {
                break;
            }
            if (out.indexOf(k) < 0) {
                out.push(k);
            }
        }
        return out.slice(0, 9);
    }

    PageTitle {
        title: qsTr("Discover")
    }

    Item {
        id: heroes
        Layout.fillWidth: true
        Layout.preferredHeight: 330
        // The board's 640 of 1136, kept in proportion on a narrower window.
        readonly property real bigWidth: Math.round((width - 22) * 0.5745)

        HeroCard {
            width: heroes.bigWidth
            height: parent.height
            eyebrow: qsTr("The big swap")
            title: qsTr("Leave Windows behind.\nKeep everything you do.")
            body: qsTr("Free apps that open your Office and Photoshop files, picked by us.")
            photo: "qrc:/qt/qml/WildOS/Store/data/photos/swap-desk.jpg"
            photoY: 0.7
            keys: page.swapKeys.slice(0, 4)
            iconSize: 64
            titleSize: 28
            bodySize: 15
            onClicked: page.store.open("swap")
        }

        Column {
            x: heroes.bigWidth + 22
            width: parent.width - x
            spacing: 22

            HeroCard {
                width: parent.width
                height: 154
                eyebrow: qsTr("Make something")
                title: qsTr("Paint, edit and record")
                photo: "qrc:/qt/qml/WildOS/Store/data/photos/make-krita.jpg"
                photoY: 0.4
                shadeFromLeft: true
                body: qsTr("Free tools for pictures, video and sound.")
                keys: page.createKeys.slice(0, 1)
                iconsRight: true
                onClicked: page.store.open("create")
            }

            HeroCard {
                width: parent.width
                height: 154
                eyebrow: qsTr("Play")
                title: qsTr("Games, free and on Steam")
                photo: "qrc:/qt/qml/WildOS/Store/data/photos/play-controller.jpg"
                photoY: 0.62
                shadeFromLeft: true
                body: qsTr("Free games to start with, and Steam for the ones you own.")
                keys: page.gameKeys.slice(0, 2)
                iconsRight: true
                onClicked: page.store.open("play")
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        SectionHeader {
            title: qsTr("Essentials for a new PC")
            linkText: qsTr("See all")
            onLinkClicked: page.store.open("swap")
        }

        AppList {
            keys: page.essentialKeys
            onActivated: key => page.store.openApp(key)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        visible: page.createKeys.length > 0

        SectionHeader {
            title: qsTr("Make something")
            linkText: qsTr("See all")
            onLinkClicked: page.store.open("create")
        }

        AppList {
            keys: page.createKeys
            maxItems: 6
            onActivated: key => page.store.openApp(key)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        visible: page.gameKeys.length > 0

        SectionHeader {
            title: qsTr("Play")
            linkText: qsTr("See all")
            onLinkClicked: page.store.open("play")
        }

        AppList {
            keys: page.gameKeys
            maxItems: 6
            onActivated: key => page.store.openApp(key)
        }
    }

    UI.Label {
        visible: !Catalog.ready
        text: qsTr("Getting the list of apps")
        color: Metrics.inkSecondary
    }
}
