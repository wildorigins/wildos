import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import WildOS.Mail
import WildOS.UI as UI

// The reading pane (MailMicrosoft.dc): subject, sender, the pictures
// notice, attachments, the message and the reply buttons under it.
Rectangle {
    id: pane

    property alias view: message
    // The message shown: the list's selection, or one fixed message in its
    // own window.
    property string messageKey: MailEngine.selected

    signal reply(string mode)
    signal moreMenu(real x, real y, Item from)
    signal openLink(string url)
    signal saved(string path)

    radius: UI.Style.radiusPanel
    color: UI.Style.surfaceRest
    border.width: 1
    border.color: UI.Style.borderSubtle
    clip: true

    MessageView {
        id: message
        key: pane.messageKey
        onAttachmentSaved: function (path) {
            pane.saved(path);
        }
    }

    readonly property bool hasMessage: message.key.length > 0 && !message.loading && message.error.length === 0

    signal printFailed()

    function print() {
        if (hasMessage && !html.print()) {
            pane.printFailed();
        }
    }

    // Nothing selected (MailMsStates.dc).
    ColumnLayout {
        anchors.centerIn: parent
        visible: message.key.length === 0
        spacing: 6

        MailIcon {
            Layout.alignment: Qt.AlignHCenter
            name: "mail"
            size: 40
            strokeWidth: 1.4
            color: UI.Style.inkSubtle
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Select a message to read")
            size: 15
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Nothing is selected")
            size: 13
            color: UI.Style.inkMuted
        }
    }

    UI.Spinner {
        anchors.centerIn: parent
        visible: message.loading
        size: 24
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 80
        visible: message.key.length > 0 && !message.loading && message.error.length > 0
        spacing: 8

        UI.Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("This message couldn't be opened")
            size: 15
            font.weight: UI.Style.weightSemiBold
        }
        UI.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: MailEngine.online ? message.error : qsTr("It hasn't been downloaded yet. It will open once you're back online.")
            size: 13
            color: UI.Style.inkMuted
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        visible: pane.hasMessage
        contentWidth: width
        contentHeight: header.height + html.contentHeight + footer.height + 24
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        QQC.ScrollBar.vertical: UI.ScrollBar {}

        onVisibleChanged: contentY = 0
        Connections {
            target: message
            function onKeyChanged() {
                flick.contentY = 0;
            }
        }

        ColumnLayout {
            id: header
            width: flick.width
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Layout.bottomMargin: 10
                spacing: 10

                UI.Label {
                    Layout.fillWidth: true
                    text: message.subject.length > 0 ? message.subject : qsTr("(No subject)")
                    size: 19
                    font.weight: UI.Style.weightSemiBold
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    visible: message.accountName.length > 0
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 4
                    implicitWidth: tag.implicitWidth + 16
                    implicitHeight: tag.implicitHeight + 4
                    radius: 4
                    color: UI.Style.accentChip

                    UI.Label {
                        id: tag
                        anchors.centerIn: parent
                        text: message.accountName
                        size: 11.5
                        color: UI.Style.accentText
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                implicitHeight: senderRow.implicitHeight + 24
                radius: UI.Style.radiusSm
                color: UI.Style.surfaceRaised

                RowLayout {
                    id: senderRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    SenderBadge {
                        id: senderBadge
                        size: 40
                        Component.onCompleted: refresh()
                        function refresh() {
                            initials = MailEngine.initialsOf(message.fromName.length > 0 ? message.fromName : message.fromAddress);
                            colourIndex = MailEngine.colourOf(message.fromAddress);
                        }
                        Connections {
                            target: message
                            function onLoaded() {
                                senderBadge.refresh();
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        UI.Label {
                            Layout.fillWidth: true
                            text: message.fromName
                            size: 14
                            font.weight: UI.Style.weightSemiBold
                            elide: Text.ElideRight

                            UI.Tooltip {
                                visible: fromHover.hovered
                                text: message.fromAddress
                            }
                            HoverHandler {
                                id: fromHover
                            }
                        }
                        UI.Label {
                            Layout.fillWidth: true
                            text: qsTr("To: %1").arg(message.to) + (message.cc.length > 0 ? qsTr("   Cc: %1").arg(message.cc) : "")
                            size: 12
                            color: UI.Style.inkMuted
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        spacing: 2

                        Row {
                            Layout.alignment: Qt.AlignRight
                            CommandButton {
                                iconOnly: true
                                iconName: "reply"
                                tip: qsTr("Reply")
                                onClicked: pane.reply("reply")
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "reply-all"
                                tip: qsTr("Reply all")
                                onClicked: pane.reply("replyAll")
                            }
                            CommandButton {
                                iconOnly: true
                                iconName: "forward"
                                tip: qsTr("Forward")
                                onClicked: pane.reply("forward")
                            }
                            CommandButton {
                                id: moreButton
                                iconOnly: true
                                iconName: "more"
                                tip: qsTr("More actions")
                                onClicked: pane.moreMenu(0, height + 4, moreButton)
                            }
                        }
                        UI.Label {
                            Layout.alignment: Qt.AlignRight
                            text: message.date
                            size: 12
                            color: UI.Style.inkMuted
                        }
                    }
                }
            }

            InvitationCard {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: visible ? 10 : 0
                ics: pane.hasMessage ? message.calendarData : ""
                messageKey: message.key
                accountId: message.key.split("\u001f")[0]
                myEmail: {
                    const a = MailEngine.account(accountId);
                    return a && a.email ? a.email : "";
                }
            }

            // Pictures from the web wait for the person (MailMicrosoft.dc).
            Rectangle {
                visible: message.remoteImages > 0
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: 10
                implicitHeight: notice.implicitHeight + 16
                radius: UI.Style.radiusXs
                color: message.showPictures ? UI.Style.successSoft : UI.Style.warningSoft
                border.width: 1
                border.color: message.showPictures ? Qt.alpha(UI.Style.success, 0.3) : Qt.alpha(UI.Style.warning, 0.3)

                RowLayout {
                    id: notice
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    MailIcon {
                        name: "shield"
                        size: 15
                        color: message.showPictures ? UI.Style.success : UI.Style.warning
                    }
                    UI.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        size: 12.5
                        text: !message.showPictures ? qsTr("Pictures from the web are blocked so the sender can't track when you read this.")
                            : message.trackers === 1 ? qsTr("Pictures are showing. 1 tracker is still blocked.")
                            : message.trackers > 1 ? qsTr("Pictures are showing. %1 trackers are still blocked.").arg(message.trackers)
                            : qsTr("Pictures are showing. The sender can tell you opened this.")
                    }
                    UI.LinkButton {
                        visible: !message.showPictures
                        text: qsTr("Show pictures")
                        size: 13
                        onClicked: message.showPictures = true
                    }
                }
            }

            Flow {
                visible: message.attachments.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: 12
                spacing: 8

                Repeater {
                    model: message.attachments

                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        width: 250
                        height: 52
                        radius: UI.Style.radiusXs
                        color: tileMouse.containsMouse ? UI.Style.surfaceHover : "transparent"
                        border.width: 1
                        border.color: UI.Style.borderSubtle

                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onDoubleClicked: message.openAttachment(tile.modelData.index)
                            onClicked: attachmentMenu.popup()
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            spacing: 10

                            Rectangle {
                                implicitWidth: 34
                                implicitHeight: 34
                                radius: UI.Style.radiusXs
                                color: UI.Style.successSoft

                                MailIcon {
                                    anchors.centerIn: parent
                                    name: tile.modelData.type.startsWith("image/") ? "picture" : "file"
                                    size: 18
                                    color: UI.Style.success
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                UI.Label {
                                    Layout.fillWidth: true
                                    text: tile.modelData.name
                                    size: 13
                                    elide: Text.ElideMiddle
                                }
                                UI.Label {
                                    text: tile.modelData.size
                                    size: 11.5
                                    color: UI.Style.inkMuted
                                }
                            }
                            MailIcon {
                                name: "chevron-down"
                                size: 14
                                color: UI.Style.inkMuted
                            }
                        }

                        UI.Menu {
                            id: attachmentMenu
                            entries: [
                                { text: qsTr("Open"), run: () => message.openAttachment(tile.modelData.index) },
                                { text: qsTr("Save to Downloads"), run: () => message.saveAttachment(tile.modelData.index) },
                                { separator: true },
                                { text: qsTr("Save all to Downloads"), enabled: message.attachments.length > 1, run: () => message.saveAllAttachments() }
                            ]
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: 20
            }
        }

        // Designed mail sits on white paper, as its sender saw it.
        Rectangle {
            visible: message.paper && html.contentHeight > 0
            x: 14
            y: header.height - 6
            width: flick.width - 28
            height: html.contentHeight + 32
            radius: UI.Style.radiusSm
            color: "#ffffff"
        }

        // The message itself. It stays in view while the pane scrolls and
        // draws the part of the message that is on screen.
        HtmlView {
            id: html
            readonly property real bodyTop: header.height + (message.paper ? 10 : 0)
            x: message.paper ? 30 : 26
            width: Math.min(flick.width - x * 2, message.paper ? 100000 : 680)
            y: Math.max(bodyTop, flick.contentY)
            height: Math.max(0, flick.height - Math.max(0, bodyTop - flick.contentY))
            contentY: Math.max(0, flick.contentY - bodyTop)
            message: message
            html: pane.hasMessage ? message.html : ""
            paper: message.paper
            allowRemote: message.showPictures
            textColor: UI.Style.ink
            linkColor: UI.Style.accent
            quoteColor: UI.Style.inkMuted
            fontSize: 14.5 * UI.Style.textScale
            onLinkActivated: function (url) {
                pane.openLink(url);
            }
            onContextRequested: function (link, x, y) {
                bodyMenu.link = link;
                bodyMenu.popup(html, x, y);
            }
        }

        RowLayout {
            id: footer
            y: html.bodyTop + html.contentHeight + (message.paper ? 34 : 6)
            x: 26
            spacing: 8

            UI.Button {
                text: qsTr("Reply")
                iconName: "back"
                size: "small"
                onClicked: pane.reply("reply")
            }
            UI.Button {
                text: qsTr("Reply all")
                size: "small"
                onClicked: pane.reply("replyAll")
            }
            UI.Button {
                text: qsTr("Forward")
                iconName: "forward"
                size: "small"
                onClicked: pane.reply("forward")
            }
        }
    }

    UI.Menu {
        id: bodyMenu
        property string link
        entries: [
            { text: qsTr("Open link"), icon: "external", enabled: bodyMenu.link.length > 0, run: () => pane.openLink(bodyMenu.link) },
            { text: qsTr("Copy link address"), icon: "link", enabled: bodyMenu.link.length > 0, run: () => message.copyToClipboard(bodyMenu.link.replace(/^mailto:/, "")) },
            { separator: true },
            { text: qsTr("Copy the message's text"), icon: "copy", run: () => message.copyText() }
        ]
    }

    // Where a link really goes, before it is clicked.
    Rectangle {
        visible: html.hoveredLink.length > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: Math.min(linkText.implicitWidth + 16, parent.width - 16)
        height: 26
        radius: UI.Style.radiusXs
        color: UI.Style.windowOpaque
        border.width: 1
        border.color: UI.Style.borderMenu

        UI.Label {
            id: linkText
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            text: html.hoveredLink
            size: 12
            elide: Text.ElideMiddle
        }
    }
}
