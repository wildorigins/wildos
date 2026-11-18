import QtQuick
import QtQuick.Templates as T
import WildOS.UI as UI

// A page of the window (10.9 to 10.12): 20px above and 24px either side,
// its name in 26px weight 600, then its parts 14px apart. It scrolls when
// the window is too short for it.
Flickable {
    id: page

    property string title
    default property alias content: parts.data
    readonly property real innerWidth: width - 48

    contentWidth: width
    contentHeight: column.height + 40
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    T.ScrollBar.vertical: UI.ScrollBar {}

    Column {
        id: column
        x: 24
        y: 20
        width: page.innerWidth
        spacing: 14

        Words {
            width: parent.width
            text: page.title
            size: 26
            font.weight: UI.Style.weightBold
            Accessible.role: Accessible.Heading
        }

        Column {
            id: parts
            width: parent.width
            spacing: 14
        }
    }
}
