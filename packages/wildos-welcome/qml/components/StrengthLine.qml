import QtQuick
import QtQuick.Layouts
import WildOS.Welcome
import WildOS.UI as UI

// How strong the password being chosen looks: three bars and a line of
// text that says the same thing, so the colour is never the only signal.
// Advice only. No button waits on it and a weak password can still be used.
RowLayout {
    id: line

    property string password
    property string oldPassword
    property var names: []

    readonly property var result: password.length > 0 ? PasswordCheck.check(password, oldPassword, names) : ({})
    readonly property string level: result.level || ""
    readonly property int bars: level === "strong" ? 3 : level === "fair" ? 2 : 1
    readonly property color tone: level === "strong" ? UI.Style.success : level === "fair" ? UI.Style.warning : UI.Style.danger

    visible: level.length > 0
    spacing: 10

    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: 3
        Accessible.ignored: true

        Repeater {
            model: 3

            Rectangle {
                required property int index
                width: 16
                height: 4
                radius: 2
                color: index < line.bars ? line.tone : UI.Style.borderControl
            }
        }
    }

    UI.Label {
        Layout.fillWidth: true
        text: line.level === "strong" ? qsTr("Strong password")
            : line.level === "fair" ? qsTr("Fair password")
            : qsTr("Weak password, %1.").arg(line.result.reason)
        size: 12
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }
}
