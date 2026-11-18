import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Create, Work, Play and Develop, as StoreCreate.dc.html: the title and a
// line about it, filters at the right, a feature card, the top picks
// numbered in three columns, then everything else in the category. The
// filters are freedesktop categories; an app is in a filter when it has
// any of `any`, and also one of `also` when that is given, and none of
// `not`.
AppGrid {
    id: page

    property var store
    property string arg
    property string kind
    property int selected: 0

    readonly property var spec: {
        switch (kind) {
        case "work": return {
            title: qsTr("Work"),
            subtitle: qsTr("Documents, email, calls and money."),
            filters: [
                { label: qsTr("All"), any: ["Office", "Email", "Finance", "ProjectManagement", "Calendar", "ContactManagement", "WordProcessor", "Spreadsheet", "Presentation", "VideoConference"] },
                { label: qsTr("Documents"), any: ["WordProcessor", "Spreadsheet", "Presentation", "Publishing", "Office"], not: ["Email", "Finance", "ProjectManagement", "Calendar"] },
                { label: qsTr("Email and calls"), any: ["Email", "VideoConference", "InstantMessaging", "Telephony", "Chat"] },
                { label: qsTr("Planning"), any: ["ProjectManagement", "Calendar", "ContactManagement"] },
                { label: qsTr("Money"), any: ["Finance"] }
            ],
            hero: {
                eyebrow: qsTr("Office files"),
                photo: "qrc:/qt/qml/WildOS/Store/data/heroes/contours.jpg",
                photoY: 0.5,
                title: qsTr("Open Word, Excel and PowerPoint files"),
                body: qsTr("Write, Tables and Slides open them and save back to them, so you can keep working with people who use Office."),
                apps: ["org.wildos.write", "org.wildos.tables", "org.wildos.slides"],
                section: "wildos"
            }
        };
        case "play": return {
            title: qsTr("Play"),
            subtitle: qsTr("Free games, and Steam for the ones you bought on Windows."),
            filters: [
                { label: qsTr("All"), any: ["Game"] },
                { label: qsTr("Action"), any: ["ActionGame", "Shooter"] },
                { label: qsTr("Adventure"), any: ["AdventureGame", "RolePlaying"] },
                { label: qsTr("Puzzles"), any: ["LogicGame", "BlocksGame"] },
                { label: qsTr("Strategy"), any: ["StrategyGame"] },
                { label: qsTr("Racing and sports"), any: ["SportsGame"] },
                { label: qsTr("Board and cards"), any: ["BoardGame", "CardGame"] }
            ],
            hero: {
                eyebrow: qsTr("Your Steam games"),
                photo: "qrc:/qt/qml/WildOS/Store/data/heroes/waves-green.jpg",
                photoY: 0.5,
                title: qsTr("Bring your Steam library"),
                body: qsTr("Steam plays most Windows games here. Sign in and your library is there."),
                apps: ["steam"],
                open: "steam"
            }
        };
        case "develop": return {
            title: qsTr("Develop"),
            subtitle: qsTr("Code editors, version control and the tools around them."),
            filters: [
                { label: qsTr("All"), any: ["Development"] },
                { label: qsTr("Editors"), any: ["IDE", "TextEditor"], also: ["Development"] },
                { label: qsTr("Version control"), any: ["RevisionControl"] },
                { label: qsTr("Debugging"), any: ["Debugger", "Profiling"] },
                { label: qsTr("Databases"), any: ["Database"] },
                { label: qsTr("Web"), any: ["WebDevelopment"] }
            ],
            hero: {
                eyebrow: qsTr("Start here"),
                photo: "qrc:/qt/qml/WildOS/Store/data/heroes/dusk-hills.jpg",
                photoY: 0.4,
                title: qsTr("Write code in a proper editor"),
                body: qsTr("Free code editors, from quick and simple to full development environments."),
                apps: [],
                topApps: 2,
                filter: 1
            }
        };
        }
        return {
            title: qsTr("Create"),
            subtitle: qsTr("Paint, edit photos, cut video, make music."),
            filters: [
                { label: qsTr("All"), any: ["Graphics", "2DGraphics", "RasterGraphics", "VectorGraphics", "Photography", "3DGraphics", "AudioVideoEditing", "Music", "Midi", "Sequencer", "Mixer"] },
                { label: qsTr("Photo"), any: ["Photography", "RasterGraphics"] },
                { label: qsTr("Drawing"), any: ["2DGraphics", "VectorGraphics"] },
                { label: qsTr("Video"), any: ["AudioVideoEditing"], not: ["Audio"] },
                { label: qsTr("Audio"), any: ["Audio"], also: ["AudioVideoEditing", "Music", "Midi", "Sequencer", "Mixer", "Recorder"] },
                { label: "3D", any: ["3DGraphics"] }
            ],
            hero: {
                eyebrow: qsTr("Get started"),
                photo: "qrc:/qt/qml/WildOS/Store/data/photos/create-timeline.jpg",
                photoY: 0.5,
                title: qsTr("Make your first video tonight"),
                body: qsTr("Kdenlive and Shotcut are free and never put a watermark on your videos."),
                apps: ["kdenlive", "org.shotcut.Shotcut"],
                filter: 3
            }
        };
    }

    function keysFor(f) {
        let keys = Catalog.matching(f.any);
        if (f.also) {
            const also = new Set(Catalog.matching(f.also));
            keys = keys.filter(k => also.has(k));
        }
        if (f.not) {
            const not = new Set(Catalog.matching(f.not));
            keys = keys.filter(k => !not.has(k));
        }
        return keys;
    }

    readonly property var allKeys: Catalog.catalogRevision >= 0 ? keysFor(spec.filters[Math.min(selected, spec.filters.length - 1)]) : []
    readonly property var topKeys: allKeys.slice(0, 9)
    readonly property var heroKeys: {
        if (Catalog.catalogRevision < 0) {
            return [];
        }
        if (spec.hero.topApps) {
            return keysFor(spec.filters[0]).slice(0, spec.hero.topApps);
        }
        return spec.hero.apps.map(id => Catalog.resolve(id)).filter(k => k.length > 0);
    }

    keys: allKeys.slice(9)
    onActivated: key => store.openApp(key)

    header: Column {
        width: page.innerWidth
        spacing: Metrics.sectionGap

        PageTitle {
            width: parent.width
            title: page.spec.title
            subtitle: page.spec.subtitle

            Repeater {
                model: page.spec.filters

                FilterPill {
                    required property var modelData
                    required property int index
                    text: modelData.label
                    selected: page.selected === index
                    onClicked: page.selected = index
                }
            }
        }

        HeroCard {
            width: parent.width
            height: 220
            eyebrow: page.spec.hero.eyebrow
            title: page.spec.hero.title
            body: page.spec.hero.body
            photo: page.spec.hero.photo || ""
            photoY: page.spec.hero.photoY !== undefined ? page.spec.hero.photoY : 0.5
            keys: page.heroKeys
            shadeStrength: 0.85
            onClicked: {
                const hero = page.spec.hero;
                if (hero.open) {
                    const key = Catalog.resolve(hero.open);
                    if (key.length > 0) {
                        page.store.openApp(key);
                    }
                } else if (hero.section) {
                    page.store.open(hero.section);
                } else if (hero.filter !== undefined) {
                    page.selected = hero.filter;
                }
            }
        }

        Column {
            width: parent.width

            SectionHeader {
                width: parent.width
                title: qsTr("Top picks")
            }

            AppList {
                width: parent.width
                keys: page.topKeys
                numbered: true
                onActivated: key => page.store.openApp(key)
            }

            UI.Label {
                visible: page.topKeys.length === 0
                topPadding: 16
                text: Catalog.ready ? qsTr("Nothing here yet.") : qsTr("Getting the list of apps")
                color: Metrics.inkSecondary
            }
        }

        SectionHeader {
            visible: page.keys.length > 0
            width: parent.width
            title: page.selected > 0 ? qsTr("More in %1").arg(page.spec.filters[page.selected].label) : page.kind === "play" ? qsTr("More games") : qsTr("More apps")
        }
    }
}
