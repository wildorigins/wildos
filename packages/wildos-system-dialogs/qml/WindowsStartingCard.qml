import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// A Windows app was opened while Windows is off or asleep. Windows mode
// shows this until the app's window is up, so a start that takes a minute
// never looks like nothing happened. The bar follows Windows mode's own
// estimate, slowing as it nears the end rather than stopping, and the card
// never takes the keyboard: it is only there to watch.
PromptCard {
    id: card

    property string appName
    property string iconName
    property int seconds: 60
    property bool cold: false

    signal hidden()

    width: 440
    title: cold ? qsTr("Starting Windows") : qsTr("Waking up Windows")
    // A start from off is the slow one, and rare now that Windows is saved
    // as the PC shuts down: said plainly, so the wait is expected.
    text: (cold ? coldWhen(appName.length > 0 ? appName : qsTr("Your app")) : when(appName.length > 0 ? appName : qsTr("Your app")))
          + (cold ? " " + qsTr("Windows only starts up from off now and then. After this, its apps open in seconds.") : "")

    function coldWhen(app) {
        if (seconds < 90)
            return qsTr("%1 opens once Windows has started up, in about a minute.").arg(app);
        if (seconds < 150)
            return qsTr("%1 opens once Windows has started up, in a minute or two.").arg(app);
        return qsTr("%1 opens once Windows has started up, in about %2 minutes.").arg(app).arg(Math.round(seconds / 60));
    }

    function when(app) {
        if (seconds <= 5)
            return qsTr("%1 opens in a few seconds.").arg(app);
        if (seconds < 45)
            return qsTr("%1 opens in about %2 seconds.").arg(app).arg(Math.round(seconds / 5) * 5);
        if (seconds < 90)
            return qsTr("%1 opens as soon as Windows is ready, in about a minute.").arg(app);
        return qsTr("%1 opens as soon as Windows is ready, in about %2 minutes.").arg(app).arg(Math.round(seconds / 60));
    }

    property real elapsed: 0
    readonly property real progress: 0.95 * (1 - Math.exp(-2.2 * elapsed / Math.max(1, seconds)))

    Timer {
        interval: 200
        running: true
        repeat: true
        triggeredOnStart: true
        property double startedAt: Date.now()
        onTriggered: card.elapsed = (Date.now() - startedAt) / 1000
    }

    picture: AppPicture {
        iconName: card.iconName.length > 0 ? card.iconName : "wildos-windows-app"
    }

    extra: [
        UI.ProgressBar {
            Layout.fillWidth: true
            Layout.topMargin: 6
            from: 0
            to: 1
            value: card.progress
            Accessible.name: card.title
        }
    ]

    buttons: [
        UI.Button {
            text: qsTr("Hide")
            onClicked: card.hidden()
        }
    ]

    Keys.onEscapePressed: card.hidden()
    function takeFocus() {
    }
}
