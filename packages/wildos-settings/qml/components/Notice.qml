import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// The outcome of something a page just did ("Printer added", "The account
// was not created"), as a status card at the top of the page that stays
// until dismissed, the way Installed apps reports an uninstall.
StatusCard {
    id: notice

    function show(heading, message, isError) {
        title = heading;
        description = message || "";
        warning = isError === true;
        visible = true;
    }

    function hide() {
        visible = false;
    }

    visible: false
    iconName: warning ? "warning" : "check"

    UI.Button {
        text: qsTr("Dismiss")
        onClicked: notice.hide()
    }
}
