/* Keyboard (InstallerKeyboard.dc): the layout, picked from the boot language
 * before the page shows, with a search and a box to try the keys. Picking a
 * layout applies it straight away (Calamares sets it in the live session),
 * so the try box and the disk password on the next pages use it.
 *
 * The list shows the picked layout with its variants under it, then every
 * other layout; searching filters them all.
 */
import io.calamares.core 1.0
import io.calamares.ui 1.0
import WildOS.Installer
import WildOS.UI as UI

import QtQuick
import QtQuick.Layouts

WPage {
    id: page

    readonly property var layouts: config.keyboardLayoutsModel
    readonly property var variants: config.keyboardVariantsModel
    // Qt.UserRole + 1 and Qt.UserRole: where the models keep the xkb names.
    readonly property int layoutKeyRole: 257
    readonly property int variantKeyRole: 256
    property string detectedKey: ""
    property var rows: []

    title: qsTr("Choose your keyboard")
    subtitle: qsTr("We picked this from your language. Try typing below to check it matches the keys on your keyboard.")
    gap: 16

    // Layouts for languages whose keyboard is not named after the country
    // or the language code.
    // What people search for besides a layout's name: its countries,
    // languages and the letter order on the keys ("Belgium" or "AZERTY"
    // find Belgian). Settings gets these from xkeyboard-config itself.
    readonly property var searchWords: ({
        "be": "belgium belgique belgie dutch flemish french german azerty",
        "fr": "france french azerty", "ca": "canada canadian french",
        "de": "germany deutschland german qwertz", "at": "austria german qwertz",
        "ch": "switzerland swiss schweiz suisse german french italian qwertz",
        "gb": "uk united kingdom britain british england scotland wales english", "ie": "ireland irish",
        "us": "usa united states america english", "nl": "netherlands holland dutch",
        "es": "spain espana spanish", "latam": "latin america mexico argentina colombia chile spanish",
        "pt": "portugal portuguese", "br": "brazil brasil portuguese", "it": "italy italia italian",
        "pl": "poland polska polish", "cz": "czechia czech republic czech qwertz", "sk": "slovakia slovak qwertz",
        "hu": "hungary magyar hungarian qwertz", "ro": "romania romanian", "gr": "greece greek",
        "tr": "turkey turkiye turkish", "ru": "russia russian", "ua": "ukraine ukrainian", "by": "belarus belarusian",
        "se": "sweden swedish", "no": "norway norwegian", "dk": "denmark danish", "fi": "finland finnish",
        "jp": "japan japanese", "kr": "korea korean", "cn": "china chinese", "tw": "taiwan chinese",
        "in": "india hindi", "vn": "vietnam vietnamese", "ara": "arabic", "id": "indonesia indonesian"
    })

    readonly property var layoutFor: ({
        "en": "us", "en_US": "us", "en_GB": "gb", "en_AU": "au", "en_CA": "ca", "en_IE": "ie",
        "en_IN": "in", "en_NZ": "nz", "en_ZA": "za", "ast": "es", "ca": "es", "eu": "es", "gl": "es",
        "cs": "cz", "da": "dk", "el": "gr", "eo": "epo", "et": "ee", "fa": "ir", "he": "il", "hi": "in",
        "as": "in", "bn": "in", "ml": "in", "mr": "in", "ja": "jp", "ka": "ge", "ko": "kr", "nb": "no",
        "oc": "fr", "fur": "it", "si": "lk", "sl": "si", "sq": "al", "sr": "rs", "sv": "se", "tg": "tj",
        "uk": "ua", "vi": "vn", "zh_CN": "cn", "zh_TW": "tw", "be": "by", "ar": "ara", "id": "us",
        "nl": "us", "pt_BR": "br", "pt_PT": "pt", "es_AR": "latam", "es_MX": "latam"
    })

    function cell(model, row, role) {
        return model.data(model.index(row, 0), role);
    }

    function pickFromLanguage() {
        var conf = Global.value("localeConf");
        var lang = conf && conf.LANG ? String(conf.LANG).split(".")[0] : "en_GB";
        var candidates = [layoutFor[lang], lang.split("_")[1], lang.split("_")[0], "gb"];
        for (var i = 0; i < candidates.length; ++i) {
            var key = candidates[i] ? String(candidates[i]).toLowerCase() : "";
            for (var r = 0; key !== "" && r < layouts.rowCount(); ++r) {
                if (cell(layouts, r, layoutKeyRole) === key) {
                    detectedKey = key;
                    layouts.currentIndex = r;
                    return;
                }
            }
        }
    }

    // One list: the current layout, its variants, then the other layouts.
    function rebuildRows() {
        var out = [];
        var query = search.text.trim().toLowerCase();
        var current = layouts.currentIndex;
        function matches(label, key) {
            if (query === "" || label.toLowerCase().indexOf(query) >= 0)
                return true;
            return !!key && (key === query || (searchWords[key] || "").indexOf(query) >= 0);
        }
        function note(key, label) {
            if (key === detectedKey)
                return qsTr("Detected from your language");
            if (/mac/i.test(label))
                return qsTr("For Apple keyboards");
            return "";
        }
        if (current >= 0) {
            var label = cell(layouts, current, Qt.DisplayRole);
            var key = cell(layouts, current, layoutKeyRole);
            if (matches(label, key))
                out.push({ layout: current, variant: -1, label: label, note: note(key, label) });
            for (var v = 0; v < variants.rowCount(); ++v) {
                var vkey = cell(variants, v, variantKeyRole);
                var vlabel = cell(variants, v, Qt.DisplayRole);
                if (vkey && matches(vlabel, key))
                    out.push({ layout: current, variant: v, label: vlabel, note: note("", vlabel) });
            }
        }
        var others = [];
        for (var r = 0; r < layouts.rowCount(); ++r) {
            if (r === current)
                continue;
            var l = cell(layouts, r, Qt.DisplayRole);
            if (matches(l, cell(layouts, r, layoutKeyRole)))
                others.push({ layout: r, variant: -1, label: l, note: "" });
        }
        others.sort(function(a, b) {
            var au = cell(layouts, a.layout, layoutKeyRole) === "us" ? 0 : 1;
            var bu = cell(layouts, b.layout, layoutKeyRole) === "us" ? 0 : 1;
            return au - bu || a.label.localeCompare(b.label);
        });
        // English (US) right under the picked layout, as the next most
        // likely keyboard, then the picked layout's variants.
        if (others.length && cell(layouts, others[0].layout, layoutKeyRole) === "us" && out.length)
            out.splice(1, 0, others.shift());
        rows = out.concat(others);
    }

    function isPicked(row) {
        if (row.layout !== layouts.currentIndex)
            return false;
        var vkey = variants.currentIndex >= 0 ? cell(variants, variants.currentIndex, variantKeyRole) : "";
        return row.variant < 0 ? !vkey : row.variant === variants.currentIndex;
    }

    function pick(row) {
        if (row.layout !== layouts.currentIndex)
            layouts.currentIndex = row.layout;
        if (row.variant >= 0) {
            variants.currentIndex = row.variant;
        } else {
            for (var v = 0; v < variants.rowCount(); ++v) {
                if (!cell(variants, v, variantKeyRole))
                    variants.currentIndex = v;
            }
        }
        rebuildRows();
    }

    Component.onCompleted: {
        Installer.keyboard = config;
        pickFromLanguage();
        rebuildRows();
    }

    Connections {
        target: page.layouts
        function onCurrentIndexChanged() { Qt.callLater(page.rebuildRows); }
    }
    Connections {
        target: page.variants
        function onModelReset() { Qt.callLater(page.rebuildRows); }
    }

    Item {
        Layout.fillWidth: true
        implicitHeight: search.implicitHeight

        UI.TextField {
            id: search
            anchors.left: parent.left
            anchors.right: parent.right
            size: "large"
            leftPadding: 40
            placeholderText: qsTr("Search layouts")
            Accessible.name: qsTr("Search keyboard layouts")
            onTextChanged: page.rebuildRows()
        }
        WIcon {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            stroke: UI.Style.inkMuted
            strokeWidth: 2
            path: "M18 11a7 7 0 1 1-14 0a7 7 0 1 1 14 0 M16 16l4.5 4.5"
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: UI.Style.radiusMd
        color: UI.Style.surfaceRaised
        border.width: 1
        border.color: UI.Style.borderSubtle
        clip: true

        Text {
            anchors.centerIn: parent
            visible: page.rows.length === 0
            text: qsTr("No layout matches \"%1\"").arg(search.text)
            color: UI.Style.inkMuted
            font.family: UI.Style.fontFamily
            font.letterSpacing: UI.Style.tracking(13)
            font.pixelSize: 13
        }

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.rows
            activeFocusOnTab: true
            keyNavigationEnabled: true

            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool picked: page.isPicked(modelData)
                width: ListView.view.width
                height: Math.max(48, texts.implicitHeight + 16)
                color: picked ? UI.Style.accentTint : (hover.containsMouse ? UI.Style.surfaceHover : "transparent")

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: UI.Style.divider
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        radius: 9
                        color: row.picked ? UI.Style.accentDot : "transparent"
                        border.width: row.picked ? 5.5 : 1.5
                        border.color: row.picked ? UI.Style.accent : UI.Style.radioRing
                    }
                    ColumnLayout {
                        id: texts
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.label
                            elide: Text.ElideRight
                            color: UI.Style.ink
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(14)
                            font.pixelSize: 14
                            font.weight: row.picked ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }
                        Text {
                            visible: text !== ""
                            text: row.modelData.note
                            color: UI.Style.inkMuted
                            font.family: UI.Style.fontFamily
                            font.letterSpacing: UI.Style.tracking(12)
                            font.pixelSize: 12
                        }
                    }
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: page.pick(row.modelData)
                }
            }
        }
    }

    WField {
        Layout.fillWidth: true
        label: qsTr("Try it")
        placeholder: qsTr("Type here, for example £ @ \" # ~ \\ |")
    }
}
