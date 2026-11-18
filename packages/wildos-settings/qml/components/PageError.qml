import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// What shows when a Wild OS page can't load. It used to fall back to KDE's
// own module for the setting, which put KDE's look in front of people and
// hid the fault; now the fault is visible and can be reported. What went
// wrong is said in plain words; the loader's own text is for whoever fixes
// it, so it waits behind Details.
PageScroll {
    id: page

    property string pageTitle
    property string detail
    property bool detailShown: false

    StatusCard {
        warning: true
        iconName: "warning"
        title: page.pageTitle.length > 0 ? qsTr("%1 couldn't open").arg(page.pageTitle) : qsTr("This page couldn't open")
        description: qsTr("Something this page needs is missing or broken. Updating Wild OS usually fixes it. If it doesn't, please tell us what you were doing.")

        UI.Button {
            text: qsTr("Report the problem")
            onClicked: Qt.openUrlExternally("https://help.wildos.io/troubleshooting/report-a-problem/")
        }

        UI.Button {
            visible: page.detail.length > 0
            text: page.detailShown ? qsTr("Hide details") : qsTr("Details")
            onClicked: page.detailShown = !page.detailShown
        }
    }

    UI.Label {
        Layout.fillWidth: true
        visible: page.detailShown && page.detail.length > 0
        text: page.detail
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
}
