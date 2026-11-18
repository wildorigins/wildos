import QtQuick
import QtQuick.Layouts
import WildOS.Archive
import WildOS.UI as UI

// Files' Extract here: no window at all for a quick one; a small window
// with the progress and Cancel once it takes longer than a moment, or to
// ask for a password or say what went wrong.
UI.AppWindow {
    id: window

    required property string archivePath
    property string problemTitle
    property string problemText
    property var problemDetails: []
    property bool asking: false
    property string clash
    // Never sized from an empty layout: shown before its column had been
    // laid out, the window opened as a bare strip with only its frame.
    readonly property real fullHeight: Math.max(150, column.implicitHeight + 2)

    width: 460
    height: fullHeight
    minimumWidth: 400
    minimumHeight: fullHeight
    maximumHeight: fullHeight
    visible: false
    deep: true
    title: qsTr("Extracting %1").arg(archive.fileName)

    function finish() {
        window.close();
    }
    function appear() {
        window.show();
        window.requestActivate();
    }
    function problem(heading, message, details) {
        problemTitle = heading;
        problemText = message;
        problemDetails = details ?? [];
        appear();
    }
    function choose(choice) {
        clash = "";
        archive.extractHere(choice);
    }

    onClosing: {
        archive.cancel();
        ArchiveApp.windowClosed(window);
    }

    Archive {
        id: archive
        onLoaded: {
            if (state === "failed") {
                window.problem(qsTr("Archive couldn't open %1").arg(fileName), errorText, []);
            } else if (state === "ready") {
                if (lockedFormat) {
                    window.problem(qsTr("Archive can't unlock this RAR"),
                        qsTr("%1 is protected with the older RAR 4 encryption, which Archive can't unlock. Ask whoever sent it for a 7z or ZIP copy.").arg(fileName), []);
                } else {
                    // Asks for the password first when there is one, then
                    // carries on.
                    extractHere();
                }
            }
        }
        onPasswordNeeded: wrong => {
            window.asking = true;
            passwordField.text = "";
            passwordField.error = wrong;
            window.appear();
            passwordField.forceActiveFocus();
            wrongLabel.visible = wrong;
        }
        onAlreadyThere: name => {
            window.clash = name;
            window.appear();
        }
        onPasswordChanged: {
            if (hasPassword) {
                window.asking = false;
            }
        }
        onFinished: (job, ok, heading, message, details) => {
            if (ok) {
                window.finish();
            } else if (heading.length > 0) {
                window.problem(heading, message, details);
            } else {
                window.finish();
            }
        }
    }

    Component.onCompleted: archive.open(archivePath)

    // Only shown when extracting takes a while.
    Timer {
        interval: 700
        running: archive.busy && !window.visible
        onTriggered: window.appear()
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            frame: window.frame
            showTitle: false
            buttons.showMaximise: false
            buttons.showMinimise: true

            Row {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: ArchiveStyle.icon
                    sourceSize.width: 40
                    sourceSize.height: 40
                }
                UI.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Archive")
                    font.weight: UI.Style.weightMedium
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.topMargin: 8
            spacing: 12

            UI.Label {
                Layout.fillWidth: true
                text: window.problemTitle.length > 0 ? window.problemTitle : window.clash.length > 0 ? qsTr("“%1” is already here").arg(window.clash) : window.asking ? qsTr("Enter the password") : qsTr("Extracting %1").arg(archive.fileName)
                size: 15
                font.weight: UI.Style.weightSemiBold
                elide: Text.ElideMiddle
            }

            // Progress.
            ColumnLayout {
                Layout.fillWidth: true
                visible: window.problemTitle.length === 0 && !window.asking && window.clash.length === 0
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    UI.Label {
                        Layout.fillWidth: true
                        text: archive.job === "extract" ? archive.jobText : qsTr("Reading the archive")
                        size: 12.5
                        color: UI.Style.inkMuted
                        elide: Text.ElideMiddle
                    }
                    UI.Label {
                        text: Math.round(archive.jobProgress * 100) + "%"
                        size: 12.5
                        color: UI.Style.inkMuted
                    }
                }
                UI.ProgressBar {
                    Layout.fillWidth: true
                    value: archive.jobProgress
                }
                UI.Label {
                    Layout.fillWidth: true
                    text: qsTr("To %1").arg(archive.destinationText)
                    size: 12.5
                    color: UI.Style.inkSubtle
                    elide: Text.ElideLeft
                }
            }

            // The password.
            ColumnLayout {
                Layout.fillWidth: true
                visible: window.asking
                spacing: 8
                UI.Label {
                    id: wrongLabel
                    Layout.fillWidth: true
                    text: qsTr("That password isn't right. Check it and try again.")
                    visible: false
                    size: 13
                    color: UI.Style.danger
                    wrapMode: Text.Wrap
                }
                UI.Label {
                    Layout.fillWidth: true
                    visible: !wrongLabel.visible
                    text: qsTr("%1 is protected with a password.").arg(archive.fileName)
                    size: 13
                    color: UI.Style.inkMuted
                    wrapMode: Text.Wrap
                }
                UI.TextField {
                    id: passwordField
                    Layout.fillWidth: true
                    revealable: true
                    placeholderText: qsTr("Password")
                    onTextEdited: error = false
                    onAccepted: {
                        if (text.length > 0) {
                            archive.setPassword(text);
                            text = "";
                        }
                    }
                }
            }

            // The folder it would extract to is already there.
            UI.Label {
                Layout.fillWidth: true
                visible: window.clash.length > 0
                text: qsTr("A folder with this name is already beside the archive. Replace the files in it with the ones from the archive, or keep both and extract to a new folder.")
                size: 13
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }

            // What went wrong.
            UI.Label {
                Layout.fillWidth: true
                visible: window.problemTitle.length > 0
                text: window.problemText
                size: 13
                color: UI.Style.inkMuted
                wrapMode: Text.Wrap
            }
            Repeater {
                model: window.problemDetails.slice(0, 6)
                UI.Label {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    size: 12.5
                    elide: Text.ElideMiddle
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                Item {
                    Layout.fillWidth: true
                }
                UI.Button {
                    visible: window.problemTitle.length > 0 && archive.destination.length > 0 && archive.state === "ready" && !window.asking
                    text: qsTr("Show folder")
                    onClicked: {
                        ArchiveApp.showFolder(archive.destination);
                        window.finish();
                    }
                }
                UI.Button {
                    visible: window.clash.length > 0
                    text: qsTr("Replace")
                    onClicked: window.choose("replace")
                }
                UI.Button {
                    visible: window.clash.length > 0
                    kind: "primary"
                    text: qsTr("Keep both")
                    onClicked: window.choose("both")
                }
                UI.Button {
                    visible: window.asking
                    kind: "primary"
                    text: qsTr("Unlock")
                    enabled: passwordField.text.length > 0
                    onClicked: {
                        archive.setPassword(passwordField.text);
                        passwordField.text = "";
                    }
                }
                UI.Button {
                    kind: window.problemTitle.length > 0 && window.clash.length === 0 ? "primary" : "secondary"
                    text: window.problemTitle.length > 0 ? qsTr("OK") : qsTr("Cancel")
                    onClicked: {
                        archive.cancel();
                        window.finish();
                    }
                }
            }
        }
    }
}
