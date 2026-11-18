import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// What the backup tool itself said about a problem Backup has only a
// general sentence for. It stays behind Details, for whoever is asked to
// help; nothing shows when there is nothing more to say.
ColumnLayout {
    id: details

    property string text
    property bool shown: false

    visible: text.length > 0
    spacing: 2
    onTextChanged: shown = false

    UI.LinkButton {
        // The link's own 4px of room, so its words line up with the text above.
        Layout.leftMargin: -4
        text: details.shown ? qsTr("Hide details") : qsTr("Details")
        onClicked: details.shown = !details.shown
    }
    UI.Label {
        Layout.fillWidth: true
        visible: details.shown
        text: details.text
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        maximumLineCount: 6
    }
}
