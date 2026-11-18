import QtQuick
import QtQuick.Layouts
import WildOS.Store
import WildOS.UI as UI

// Swap from Windows: each Windows app people know, and the free app that
// does its job here.
PageFlick {
    id: page

    property var store
    property string arg
    property string kind

    readonly property var essentials: Catalog.catalogRevision >= 0 ? Catalog.essentials() : []
    readonly property int essentialsLeft: {
        let n = 0;
        for (const k of essentials) {
            const a = Catalog.app(k);
            if (a && !a.installed) {
                n++;
            }
        }
        return n;
    }
    readonly property bool essentialsBusy: {
        for (const k of essentials) {
            const a = Catalog.app(k);
            if (a && (a.busy || a.queued)) {
                return true;
            }
        }
        return false;
    }

    PageTitle {
        title: qsTr("Swap from Windows")
        subtitle: qsTr("Free apps that do what your Windows apps did. Most open the same files, so nothing gets left behind.")

        PillButton {
            big: true
            filled: true
            enabled: page.essentialsLeft > 0 && !page.essentialsBusy
            text: page.essentialsBusy ? qsTr("Installing the essentials") : page.essentialsLeft === 0 && page.essentials.length > 0 ? qsTr("Essentials installed") : qsTr("Install the essentials")
            onClicked: Catalog.installEssentials()
        }
    }

    SwapGrid {
        keys: Catalog.catalogRevision >= 0 ? Catalog.swaps() : []
        onActivated: key => page.store.openApp(key)
    }
}
