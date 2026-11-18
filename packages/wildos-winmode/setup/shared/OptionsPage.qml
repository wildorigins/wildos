import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

// Choose options: 9.4 in the setup window and 3.10 in first run, the same
// page with its own title and main button ("Set up", or "Finish"). The
// choices are the ones Settings > Windows apps > Windows mode keeps, so
// they can all change later; they live in Setup, which builds the request
// from them.
Page {
    id: page

    property string mainText: qsTr("Set up")
    property bool busy: false

    signal back()
    signal confirmed()

    title: qsTr("Choose options")
    text: qsTr("These suit most people. You can change any of them later in Settings, under Windows apps.")
    spacing: 16

    readonly property string editionName: Setup.edition === "pro" ? "Windows 11 Pro" : Setup.edition === "home" ? qsTr("Windows 11 Home") : ""
    readonly property bool keyChosen: Setup.licenceChoice === "key"

    // Setup.folders are the names Windows mode keeps them by; shown in
    // the language chosen in Settings.
    function folderName(name) {
        switch (name) {
        case "Documents": return qsTr("Documents");
        case "Downloads": return qsTr("Downloads");
        case "Pictures": return qsTr("Pictures");
        case "Desktop": return qsTr("Desktop");
        case "Music": return qsTr("Music");
        case "Videos": return qsTr("Videos");
        default: return name;
        }
    }

    function link(href, words) {
        return "<a href=\"" + href + "\" style=\"color: " + UI.Style.accentText + "; text-decoration: none;\">" + words + "</a>";
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            OptionRow {
                width: parent.width
                title: qsTr("Windows licence")
                description: {
                    if (Setup.firmwareUsable) {
                        return page.editionName.length > 0 ? qsTr("%1 found in this PC. Or enter a product key, or activate later.").arg(page.editionName)
                            : qsTr("A licence found in this PC. Or enter a product key, or activate later.");
                    }
                    if (Setup.edition === "home" && (Setup.licence === "firmware" || Setup.licence === "inuse")) {
                        return qsTr("This PC's Windows 11 Home licence can't activate Windows mode. Enter a Pro key, or activate later.");
                    }
                    return qsTr("No licence found in this PC. Enter a product key, or activate later.");
                }

                UI.ComboBox {
                    Accessible.name: qsTr("Windows licence")
                    model: (Setup.firmwareUsable ? [{ value: "firmware", text: qsTr("Built into this PC") }] : [])
                        .concat([{ value: "key", text: qsTr("Enter a product key") }, { value: "none", text: qsTr("Activate later") }])
                    currentIndex: model.findIndex(o => o.value === Setup.licenceChoice)
                    onActivated: Setup.licenceChoice = currentValue
                }
            }

            OptionRow {
                width: parent.width
                visible: page.keyChosen
                title: qsTr("Product key")
                description: keyField.error ? qsTr("That isn't the right shape. A key has 25 letters and numbers in five groups.")
                    : qsTr("Windows 11 Pro, from a sticker or an email")
                descriptionColor: keyField.error ? UI.Style.danger : UI.Style.inkMuted
                leading: Item {}

                KeyField {
                    id: keyField
                    implicitWidth: 300
                    text: Setup.productKey
                    onTextChanged: if (Setup.productKey !== text) Setup.productKey = text
                }
            }

            OptionRow {
                width: parent.width
                interactive: true
                onClicked: Setup.office = !Setup.office
                title: qsTr("Install Microsoft Office")
                description: qsTr("Optional. Word, Excel, PowerPoint and Outlook. Needs a subscription or an Office licence.")
                leading: UI.CheckBox {
                    checked: Setup.office
                    Accessible.name: qsTr("Install Microsoft Office")
                    onToggled: Setup.office = checked
                }

                UI.Label {
                    text: qsTr("%1 GB").arg(4)
                    size: 12
                    color: UI.Style.inkMuted
                }
            }

            OptionRow {
                width: parent.width
                visible: Setup.office
                title: qsTr("Your Office plan")
                description: qsTr("Personal or Family, Business, Work or school, or a product key")
                leading: Item {}

                UI.ComboBox {
                    Accessible.name: qsTr("Office plan")
                    model: [
                        { value: "personal", text: qsTr("Personal or Family") },
                        { value: "business", text: qsTr("Business") },
                        { value: "work", text: qsTr("Work or school") },
                        { value: "key", text: qsTr("Product key") }
                    ]
                    currentIndex: model.findIndex(o => o.value === Setup.officePlan)
                    onActivated: Setup.officePlan = currentValue
                }
            }

            OptionRow {
                width: parent.width
                interactive: true
                onClicked: Setup.openFiles = !Setup.openFiles
                title: qsTr("Open Windows files in Windows apps")
                description: qsTr("Double-clicking a .docx, .xlsx or .pptx opens it in Word, Excel or PowerPoint")
                leading: UI.CheckBox {
                    checked: Setup.openFiles
                    Accessible.name: qsTr("Open Windows files in Windows apps")
                    onToggled: Setup.openFiles = checked
                }
            }
        }
    }

    component Heading: UI.Label {
        size: 15
        font.weight: UI.Style.weightSemiBold
        Accessible.role: Accessible.Heading
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        Heading {
            text: qsTr("Speed and memory")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                OptionRow {
                    width: parent.width
                    title: qsTr("When no Windows app is open")
                    // The board's line, with the 15 minutes the service now waits
                    // before saving to disk.
                    description: qsTr("Kept ready in memory for short breaks, saved to disk after 15 minutes or on battery")

                    UI.ComboBox {
                        Accessible.name: qsTr("When no Windows app is open")
                        model: [
                            { value: "automatic", text: qsTr("Automatic") },
                            { value: "memory", text: qsTr("Keep ready in memory") },
                            { value: "disk", text: qsTr("Save to disk") },
                            { value: "shutdown", text: qsTr("Shut down Windows") }
                        ]
                        currentIndex: model.findIndex(o => o.value === Setup.whenIdle)
                        onActivated: Setup.whenIdle = currentValue
                    }
                }

                OptionRow {
                    width: parent.width
                    title: qsTr("Memory Windows mode can use")
                    description: Setup.memoryLabel.length > 0 ? qsTr("This PC has %1. More helps big spreadsheets. Unused memory is handed back.").arg(Setup.memoryLabel)
                        : qsTr("More helps big spreadsheets. Unused memory is handed back.")

                    UI.ComboBox {
                        Accessible.name: qsTr("Memory limit")
                        model: Setup.memoryChoices
                        currentIndex: model.findIndex(o => o.value === Setup.memoryGb)
                        onActivated: Setup.memoryGb = currentValue
                    }
                }

                OptionRow {
                    width: parent.width
                    visible: Setup.hyperThreadingAvailable
                    title: qsTr("Use every processor thread")
                    description: qsTr("Faster heavy apps, one less CPU-flaw protection for the whole PC. Needs a restart.")

                    UI.Toggle {
                        text: checked ? qsTr("On") : qsTr("Off")
                        checked: Setup.hyperThreading
                        onToggled: Setup.hyperThreading = checked
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        Heading {
            text: qsTr("Your files")
        }

        UI.Card {
            Layout.fillWidth: true

            OptionRow {
                width: parent.width
                title: qsTr("Folders Windows apps can see")
                description: qsTr("Word and Excel save straight into these")

                Repeater {
                    model: Setup.folders

                    Rectangle {
                        required property string modelData
                        implicitWidth: folderName.implicitWidth + 20
                        implicitHeight: 24
                        radius: 12
                        color: UI.Style.tone(0.061)

                        UI.Label {
                            id: folderName
                            anchors.centerIn: parent
                            text: page.folderName(parent.modelData)
                            size: 12
                            color: UI.Style.accentText
                        }
                    }
                }
            }
        }
    }

    // Windows' own licence screen is skipped by the answer file, and the
    // Office Deployment Tool's by its configuration, so this is where the
    // terms are accepted; nothing starts until they are.
    footerLeading: Item {
        anchors.fill: parent

        MouseArea {
            x: -10
            width: terms.width + 20
            height: 40
            anchors.verticalCenter: parent.verticalCenter
            onClicked: Setup.termsAccepted = !Setup.termsAccepted
        }

        Row {
            id: terms
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            UI.CheckBox {
                anchors.verticalCenter: parent.verticalCenter
                checked: Setup.termsAccepted
                onToggled: Setup.termsAccepted = checked
                Accessible.name: termsText.plain
            }

            UI.Label {
                id: termsText
                readonly property string plain: Setup.office ? qsTr("I accept Microsoft's licence terms for Windows 11 and Office") : qsTr("I accept Microsoft's licence terms for Windows 11")
                anchors.verticalCenter: parent.verticalCenter
                // Rich text, for links in the accent without an underline.
                textFormat: Text.RichText
                size: 13
                elide: Text.ElideNone
                readonly property string windowsLink: page.link("https://www.microsoft.com/en-us/useterms/retail/windows/11/useterms_retail_windows_11_english.htm", "Windows 11")
                text: Setup.office
                    ? qsTr("I accept Microsoft's licence terms for %1 and %2").arg(windowsLink).arg(page.link("https://www.microsoft.com/en-gb/useterms", "Office"))
                    : qsTr("I accept Microsoft's licence terms for %1").arg(windowsLink)
                onLinkActivated: link => Setup.openUrl(link)

                HoverHandler {
                    cursorShape: termsText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
        }
    }

    buttons: [
        UI.Button {
            text: qsTr("Back")
            enabled: !page.busy
            onClicked: page.back()
        },
        UI.Button {
            kind: "primary"
            text: page.mainText
            enabled: Setup.canStart && !page.busy
            busy: page.busy
            onClicked: page.confirmed()
        }
    ]
}
