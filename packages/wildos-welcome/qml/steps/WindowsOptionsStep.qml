import QtQuick
import WildOS.Welcome
import WildOS.WinMode.Setup as WinMode

// Step 7, page 2, from FirstWinOptions.dc.html (3.10): the setup window's
// Choose options page (9.4), the same component, with Finish for Set up.
// Finish goes to the desktop (9.11); setup starts in the background.
WinMode.OptionsPage {
    id: page

    property string error

    signal finished()

    title: qsTr("Windows mode options")
    mainText: qsTr("Finish")
    busy: FirstBoot.state === "finishing" || FirstBoot.state === "signing-in"
    note: error
    onConfirmed: page.finished()
}
