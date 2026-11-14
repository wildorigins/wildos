/* The install itself (Installing.dc), the second part of the Install step.
 * Calamares' own progress bar under the slideshow is hidden (stylesheet.qss);
 * this page shows the progress from the JobQueue instead, as five stages.
 *
 * The stages come from which module's jobs are running, never from the job
 * names: ExecutionViewStep (wildos-installer-sheet.patch) stores each
 * module's share of the overall progress as execModuleWeights, so the
 * overall percentage says which module, and so which stage, is at work.
 *
 * Slideshow API 2: Calamares sets activatedInCalamares and calls
 * onActivate() and onLeave().
 */
import io.calamares.core 1.0
import io.calamares.ui 1.0
import WildOS.UI as UI

import QtQuick
import QtQuick.Layouts

Item {
    id: show

    property bool activatedInCalamares: false
    property real progress: 0
    property double startedAt: 0
    property int minutesLeft: -1
    property bool encrypted: true
    property bool keptWindows: false
    // [{ stage, from, to }] in overall progress (0 to 1), from execModuleWeights.
    property var spans: []
    // When the progress last moved, and seconds since then. Some steps
    // report nothing while they work (making the encrypted partition, the
    // reset copy), and on some computers they take minutes: the page then
    // says it is still working rather than sitting on one number.
    property double movedAt: 0
    property int stillFor: 0
    readonly property bool waiting: stillFor >= 20

    // The five stages, and the modules (settings.conf exec) in each.
    readonly property var stages: [
        { label: qsTr("Prepare disk"), doing: qsTr("Preparing the disk"),
          modules: ["partition"] },
        { label: qsTr("Encryption"), doing: qsTr("Locking the disk with your password"),
          modules: ["wildosrecovery", "mount"] },
        { label: qsTr("System files"), doing: qsTr("Copying system files"),
          modules: ["unpackfs", "machineid", "removeuser", "fstab"] },
        { label: qsTr("Settings"), doing: qsTr("Applying your keyboard, language and time zone"),
          modules: ["localeq", "keyboardq", "localecfg", "networkcfg", "hwclock", "packages"] },
        { label: qsTr("Start-up menu"), doing: qsTr("Setting up the start-up menu"),
          modules: ["bootloader-packages", "bootloader", "finalize", "umount"] }
    ]

    function stageOf(instanceKey) {
        // "shellprocess@finalize" -> "finalize", "partition@partition" -> "partition"
        var parts = String(instanceKey).split("@");
        var names = [parts[0], parts[parts.length - 1]];
        for (var s = 0; s < stages.length; ++s) {
            for (var n = 0; n < names.length; ++n) {
                if (stages[s].modules.indexOf(names[n]) >= 0)
                    return s;
            }
        }
        return -1;
    }

    function buildSpans() {
        var weights = Global.value("execModuleWeights") || [];
        var total = 0;
        for (var i = 0; i < weights.length; ++i)
            total += weights[i].weight;
        var out = [];
        var at = 0;
        for (var j = 0; j < weights.length; ++j) {
            var share = total > 0 ? weights[j].weight / total : 0;
            // A module not in the table counts with the stage before it.
            var stage = stageOf(weights[j].module);
            if (stage < 0)
                stage = out.length ? out[out.length - 1].stage : 0;
            if (out.length && out[out.length - 1].stage === stage)
                out[out.length - 1].to = at + share;
            else
                out.push({ stage: stage, from: at, to: at + share });
            at += share;
        }
        spans = out;
    }

    // The stage at work, and how far through it.
    readonly property int currentStage: {
        for (var i = 0; i < spans.length; ++i) {
            if (progress < spans[i].to - 0.0001)
                return spans[i].stage;
        }
        return spans.length ? stages.length : 0;
    }

    function onActivate() {
        startedAt = Date.now();
        movedAt = startedAt;
        stillFor = 0;
        encrypted = Global.value("wildosEncrypt") !== false;
        var plan = Global.value("wildosPlan") || {};
        keptWindows = plan.mode === "alongside" && plan.other === "Windows";
        buildSpans();
    }
    function onLeave() { }

    Connections {
        target: JobQueue
        function onProgress(percent, prettyName) {
            if (show.spans.length === 0)
                show.buildSpans();
            if (percent > show.progress) {
                show.movedAt = Date.now();
                show.stillFor = 0;
            }
            show.progress = Math.max(show.progress, percent);
            // After the first few per cent the pace is steady enough to go by.
            var elapsed = (Date.now() - show.startedAt) / 1000;
            if (show.startedAt > 0 && percent >= 0.05 && elapsed >= 30)
                show.minutesLeft = Math.round(elapsed * (1 - percent) / percent / 60);
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: show.activatedInCalamares && show.startedAt > 0 && show.progress < 1
        onTriggered: show.stillFor = Math.round((Date.now() - show.movedAt) / 1000)
    }

    function stillText() {
        var minutes = Math.floor(show.stillFor / 60);
        var why = qsTr("Some computers take a few minutes here.");
        if (minutes < 1)
            return qsTr("Still working on this step.") + " " + why;
        return (minutes === 1 ? qsTr("Still working on this step, 1 minute so far.")
                              : qsTr("Still working on this step, %1 minutes so far.").arg(minutes)) + " " + why;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: 22
        spacing: 22

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: qsTr("Installing Wild OS")
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.pixelSize: 28
                font.weight: UI.Style.weightSemiBold
                font.letterSpacing: -0.28
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("This takes about 10 minutes. You can leave it running and come back.")
                color: Qt.rgba(232 / 255, 236 / 255, 248 / 255, 0.8)
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(14)
                font.pixelSize: 14
                lineHeightMode: Text.FixedHeight
                lineHeight: 21
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: progressCard.implicitHeight + 44
            radius: 14
            color: UI.Style.surfaceRaised
            border.width: 1
            border.color: UI.Style.borderSubtle

            ColumnLayout {
                id: progressCard
                anchors.fill: parent
                anchors.topMargin: 22
                anchors.bottomMargin: 22
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: show.currentStage < show.stages.length ? show.stages[show.currentStage].doing
                                                                     : qsTr("Finishing")
                        elide: Text.ElideRight
                        color: UI.Style.ink
                        font.family: UI.Style.fontFamily
                        font.letterSpacing: UI.Style.tracking(13)
                        font.pixelSize: 13
                        font.weight: UI.Style.weightMedium
                    }
                    Text {
                        text: show.minutesLeft < 0 ? qsTr("Working out the time left")
                              : (show.minutesLeft <= 1 ? qsTr("About a minute left")
                                                       : qsTr("About %1 minutes left").arg(show.minutesLeft))
                        color: UI.Style.inkMuted
                        font.family: UI.Style.fontFamily
                        font.letterSpacing: UI.Style.tracking(13)
                        font.pixelSize: 13
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    radius: 4
                    color: UI.Style.surfaceTrack
                    Accessible.role: Accessible.ProgressBar
                    Accessible.name: qsTr("Installation progress")

                    Rectangle {
                        id: fill
                        width: parent.width * Math.min(1, show.progress)
                        height: parent.height
                        radius: 4
                        color: UI.Style.accent
                        Behavior on width { NumberAnimation { duration: 400 } }

                        // A step that reports nothing still shows it is at work.
                        SequentialAnimation on opacity {
                            running: show.waiting
                            loops: Animation.Infinite
                            onRunningChanged: if (!running) fill.opacity = 1
                            NumberAnimation { to: 0.45; duration: 900; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1; duration: 900; easing.type: Easing.InOutSine }
                        }
                    }
                }
                Text {
                    text: Math.round(show.progress * 100) + "%"
                    color: UI.Style.inkMuted
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    visible: show.waiting
                    text: show.stillText()
                    wrapMode: Text.WordWrap
                    color: UI.Style.inkMuted
                    font.family: UI.Style.fontFamily
                    font.letterSpacing: UI.Style.tracking(12)
                    font.pixelSize: 12
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: qsTr("While you wait")
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(15)
                font.pixelSize: 15
                font.weight: UI.Style.weightSemiBold
            }

            Row {
                id: cardRow

                // The tallest card sets the height of all three, as in the design.
                property real tallest: {
                    var h = 0;
                    for (var i = 0; i < cards.count; ++i) {
                        var item = cards.itemAt(i);
                        if (item)
                            h = Math.max(h, item.contentHeight);
                    }
                    return h;
                }

                Layout.fillWidth: true
                Layout.preferredHeight: tallest
                spacing: 12

                Repeater {
                    id: cards
                    model: [
                        {
                            title: qsTr("Secure by default"),
                            body: show.encrypted
                                  ? qsTr("Your disk is encrypted, and app confinement and kernel hardening are on before you ever sign in.")
                                  : qsTr("App confinement and kernel hardening are on before you ever sign in. There is nothing to set up."),
                            icon: "M12 3l7 3v6c0 4.4-3 7.6-7 9-4-1.4-7-4.6-7-9V6z M9 12l2 2 4-4"
                        },
                        {
                            title: qsTr("Runs Windows apps"),
                            body: qsTr("Double-click a Windows installer and Wild OS builds a sandbox for it."),
                            icon: "M3 6a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z M3 9h18"
                        },
                        show.keptWindows ? {
                            title: qsTr("Your files come too"),
                            body: qsTr("After the restart you can copy documents, photos and bookmarks across from Windows."),
                            icon: "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"
                        } : {
                            title: qsTr("Your files come too"),
                            body: qsTr("After the restart you can bring documents, photos and bookmarks across from a USB drive or your old computer."),
                            icon: "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"
                        }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        readonly property real contentHeight: card.implicitHeight + 36
                        width: (cardRow.width - 2 * cardRow.spacing) / 3
                        height: cardRow.tallest
                        radius: 14
                        color: UI.Style.surfaceRaised
                        border.width: 1
                        border.color: UI.Style.borderSubtle

                        ColumnLayout {
                            id: card
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 18
                            spacing: 10

                            RowLayout {
                                spacing: 12
                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    radius: 10
                                    color: UI.Style.accentChip
                                    WIcon {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        path: modelData.icon
                                    }
                                }
                                Text {
                                    text: modelData.title
                                    color: UI.Style.ink
                                    font.family: UI.Style.fontFamily
                                    font.letterSpacing: UI.Style.tracking(14)
                                    font.pixelSize: 14
                                    font.weight: UI.Style.weightSemiBold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.body
                                color: UI.Style.inkMuted
                                font.family: UI.Style.fontFamily
                                font.letterSpacing: UI.Style.tracking(12)
                                font.pixelSize: 12
                                lineHeightMode: Text.FixedHeight
                                lineHeight: 18
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: qsTr("What's happening")
                color: UI.Style.ink
                font.family: UI.Style.fontFamily
                font.letterSpacing: UI.Style.tracking(15)
                font.pixelSize: 15
                font.weight: UI.Style.weightSemiBold
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: tracker.height + 34
                radius: 14
                color: UI.Style.surfaceRest
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)

                Row {
                    id: tracker
                    height: 64
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 18
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    readonly property real cell: width / show.stages.length

                    Repeater {
                        model: show.stages.length
                        delegate: Item {
                            id: step
                            required property int index
                            readonly property bool done: index < show.currentStage
                            readonly property bool current: index === show.currentStage
                            width: tracker.cell
                            height: tracker.height

                            // The line to the next stage, lit once this one is done.
                            Rectangle {
                                visible: step.index < show.stages.length - 1
                                x: step.width / 2 + 11
                                y: 10
                                width: step.width - 22
                                height: 2
                                color: step.done ? UI.Style.accent : Qt.rgba(1, 1, 1, 0.14)
                            }
                            Rectangle {
                                id: dot
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 22
                                height: 22
                                radius: 11
                                color: step.done ? UI.Style.accent : "#27292d"
                                border.width: step.done ? 0 : 2
                                border.color: step.current ? UI.Style.accent : Qt.rgba(1, 1, 1, 0.22)
                                WIcon {
                                    visible: step.done
                                    anchors.centerIn: parent
                                    width: 12
                                    height: 12
                                    strokeWidth: 3
                                    stroke: UI.Style.textOnAccent
                                    path: "M5 12l5 5 9-10"
                                }
                                Rectangle {
                                    visible: step.current
                                    anchors.centerIn: parent
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: UI.Style.accent
                                }
                            }
                            Text {
                                id: label
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: dot.bottom
                                anchors.topMargin: 8
                                text: show.stages[step.index].label
                                color: step.current ? UI.Style.ink
                                       : (step.done ? UI.Style.inkMuted : Qt.rgba(232 / 255, 236 / 255, 248 / 255, 0.6))
                                font.family: UI.Style.fontFamily
                                font.letterSpacing: UI.Style.tracking(13)
                                font.pixelSize: 13
                                font.weight: step.current ? UI.Style.weightSemiBold : UI.Style.weightMedium
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: label.bottom
                                anchors.topMargin: 4
                                text: step.done ? (step.index === 1 && !show.encrypted ? qsTr("Off") : qsTr("Done"))
                                      : step.current ? qsTr("Now")
                                      : step.index === show.stages.length - 1 ? qsTr("Last")
                                      : step.index === show.currentStage + 1 ? qsTr("Next") : ""
                                color: Qt.rgba(232 / 255, 236 / 255, 248 / 255, 0.6)
                                font.family: UI.Style.fontFamily
                                font.letterSpacing: UI.Style.tracking(12)
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
