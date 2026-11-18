import QtQuick
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// A message in a window of its own, from a double-click in the list: the
// reading pane with Reply, Reply all, Forward, Delete and the rest above it.
// Replies are written in the main window.
UI.AppWindow {
    id: window

    property string messageKey
    property var main: null

    stateKey: "message"
    width: 900
    height: 760
    minimumWidth: 560
    minimumHeight: 480
    visible: true
    title: pane.view.subject.length > 0 ? pane.view.subject : qsTr("Message")

    function reply(mode) {
        if (main) {
            main.compose(pane.view.replyData(mode));
            main.raise();
            main.requestActivate();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.maximised ? 0 : 1
        spacing: 0

        UI.TitleBar {
            Layout.fillWidth: true
            title: window.title
            tinted: true
            frame: window.frame
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            implicitHeight: 46
            radius: UI.Style.radiusPanel
            color: UI.Style.surfaceRest
            border.width: 1
            border.color: UI.Style.borderSubtle

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 2

                CommandButton {
                    text: qsTr("Reply")
                    iconName: "reply"
                    onClicked: window.reply("reply")
                }
                CommandButton {
                    text: qsTr("Reply all")
                    iconName: "reply-all"
                    onClicked: window.reply("replyAll")
                }
                CommandButton {
                    text: qsTr("Forward")
                    iconName: "forward"
                    onClicked: window.reply("forward")
                }
                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 22
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    color: UI.Style.borderSubtle
                }
                CommandButton {
                    text: qsTr("Delete")
                    iconName: "trash"
                    onClicked: {
                        MailEngine.removeKeys([window.messageKey]);
                        window.close();
                    }
                }
                CommandButton {
                    text: qsTr("Archive")
                    iconName: "archive"
                    onClicked: {
                        MailEngine.archiveKeys([window.messageKey]);
                        window.close();
                    }
                }
                CommandButton {
                    text: qsTr("Flag")
                    iconName: "flag"
                    onClicked: MailEngine.toggleFlag(window.messageKey)
                }
                CommandButton {
                    text: qsTr("Print")
                    iconName: "printer"
                    enabled: pane.hasMessage
                    onClicked: pane.print()
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }

        ReadingPane {
            id: pane
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            messageKey: window.messageKey
            onReply: function (mode) {
                window.reply(mode);
            }
            onOpenLink: function (url) {
                if (window.main) {
                    window.main.openLink(url);
                }
            }
            onPrintFailed: printToast.show(qsTr("The printer couldn't be reached."))
        }
    }

    UI.Toast {
        id: printToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 60
    }

    Shortcut {
        sequence: StandardKey.Print
        enabled: pane.hasMessage
        onActivated: pane.print()
    }

    Component.onCompleted: {
        raise();
        requestActivate();
    }

    Shortcut {
        sequence: "Escape"
        onActivated: window.close()
    }
}
