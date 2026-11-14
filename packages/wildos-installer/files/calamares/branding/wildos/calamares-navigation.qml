/* Wild OS installer footer: a note on the left, the step's buttons on the
 * right. Leaving is in the sidebar ("Close the installer").
 *
 * Step indexes follow the sequence in settings.conf, as in the sidebar.
 */
import io.calamares.ui 1.0
import io.calamares.core 1.0
import WildOS.Installer
import WildOS.UI as UI

import QtQuick
import QtQuick.Layouts

Item {
    id: footer
    // Calamares takes the panel's height from here (see setDimension()).
    height: 76

    readonly property int current: ViewManager.currentStepIndex
    readonly property int licenceStep: 0
    readonly property int checksStep: 1
    readonly property int keyboardStep: 3
    readonly property int diskStep: 4
    readonly property int encryptionStep: 5
    readonly property int summaryStep: 6
    readonly property int installStep: 7
    readonly property bool installing: current === installStep
    readonly property bool atEnd: current > installStep

    Binding {
        target: Installer
        property: "exitQuickly"
        value: footer.current < footer.installStep
    }
    readonly property bool failed: Installer.finished ? Installer.finished.failed : false
    // The disk step has two pages; the editor's note differs. Global storage
    // has no change signal QML can safely use, so this looks while there.
    property bool manual: false
    property var plan: ({})

    Timer {
        running: footer.current === footer.diskStep
        interval: 400
        repeat: true
        triggeredOnStart: true
        onTriggered: footer.manual = Global.value("wildosEditorOpen") === true
    }
    // The recovery key has to be saved or written down before leaving.
    property bool keyNeeded: false
    readonly property bool canLeave: !keyNeeded || Installer.keySaved
    onCurrentChanged: {
        plan = Global.value("wildosPlan") || {};
        keyNeeded = !!Global.value("wildosRecoveryAdded");
    }

    readonly property string note: {
        switch (current) {
        case licenceStep:
            return qsTr("Nothing on this PC has changed yet.");
        case checksStep:
            return Installer.alongsideBlocked !== "" ? qsTr("You can still replace Windows completely without changing anything.") : "";
        case keyboardStep:
            return qsTr("You can add more layouts later in Settings.");
        case diskStep:
            return manual ? qsTr("Every partition must have a role before you can continue.")
                          : qsTr("Nothing changes on your disk until you press Install on the last step.");
        case encryptionStep:
            return qsTr("You will create your account after the restart.");
        case summaryStep:
            return qsTr("Nothing has changed yet.");
        case installStep:
            if (plan.mode === "alongside")
                return plan.other === "Windows"
                    ? qsTr("Windows stays exactly as it was. You will choose which to start each time you switch on.")
                    : qsTr("Your other system stays exactly as it was. You will choose which to start each time you switch on.");
            return qsTr("Keep this computer switched on until the install finishes.");
        }
        return "";
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Qt.rgba(1, 1, 1, 0.08)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 40
        anchors.rightMargin: 40
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: footer.note
            elide: Text.ElideRight
            color: UI.Style.inkMuted
            font.family: UI.Style.fontFamily
            font.letterSpacing: UI.Style.tracking(12)
            font.pixelSize: 12
        }

        WButton {
            text: Installer.checking ? qsTr("Checking") : qsTr("Check again")
            visible: footer.current === footer.checksStep
            enabled: !Installer.checking
            onClicked: Installer.runChecks()
        }

        // Nothing has been chosen yet, so Quit closes without asking.
        WButton {
            text: qsTr("Quit")
            visible: footer.current === footer.licenceStep
            onClicked: Installer.quitNow()
        }

        WButton {
            text: qsTr("Back")
            visible: ViewManager.backAndNextVisible && footer.current > footer.licenceStep && footer.current < footer.installStep
            // Calamares' welcome step always refuses Back, because it expects
            // to be first; here the licence comes before it.
            enabled: ViewManager.backEnabled || footer.current === footer.checksStep
            onClicked: ViewManager.back()
        }

        WButton {
            primary: true
            text: ViewManager.nextIcon === "run-install" ? qsTr("Install") : qsTr("Continue")
            visible: ViewManager.backAndNextVisible && footer.current < footer.installStep
            enabled: ViewManager.nextEnabled
                     && (footer.current !== footer.licenceStep || Installer.licenceAccepted)
                     && (footer.current !== footer.encryptionStep || Installer.encryptionReady)
                     && (footer.current !== footer.checksStep || !Installer.checking)
            onClicked: ViewManager.next()
        }

        // Greyed out while installing so people can see what comes next.
        WButton {
            primary: true
            text: qsTr("Restart now")
            visible: footer.installing
            enabled: false
        }

        WButton {
            primary: true
            text: qsTr("Restart now")
            visible: footer.atEnd && !footer.failed
            enabled: Installer.finished !== null && footer.canLeave
            onClicked: Installer.finished.doRestart(true)
        }

        WButton {
            primary: true
            text: qsTr("Close the installer")
            visible: footer.atEnd && footer.failed
            onClicked: {
                Installer.finished.restartNowWanted = false;
                ViewManager.quit();
            }
        }
    }
}
