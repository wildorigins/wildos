import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// App permissions, after Windows 11's Privacy and security list: one row
// for each thing an app can reach, saying how many apps can, each opening
// the list of apps with their switches.
PageScroll {
    id: page

    readonly property var summary: AppAccess.summary

    function line(kind, noun) {
        const allowed = summary[kind] || 0;
        const asked = summary[kind + "Asked"] || 0;
        if (asked === 0) {
            return qsTr("No apps have asked to use your %1").arg(noun);
        }
        if (allowed === 0) {
            return qsTr("Turned off for every app that asked");
        }
        return allowed === 1 ? qsTr("1 app can use your %1").arg(noun) : qsTr("%1 apps can use your %2").arg(allowed).arg(noun);
    }

    Component.onCompleted: AppAccess.reload()

    UI.Label {
        Layout.fillWidth: true
        text: qsTr("Apps from the Store run in a sandbox and can only reach what they asked for when you installed them. Turn any of it off here. They can always open a file you choose for them.")
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    UI.Card {
        Layout.fillWidth: true

        Column {
            width: parent.width

            UI.SettingRow {
                iconName: "camera"
                title: qsTr("Camera")
                description: page.line("camera", qsTr("camera"))
                chevron: true
                onClicked: Navigator.open("camera")
            }

            UI.SettingRow {
                iconName: "mic"
                title: qsTr("Microphone")
                description: page.line("microphone", qsTr("microphone"))
                chevron: true
                onClicked: Navigator.open("microphone")
            }

            UI.SettingRow {
                iconName: "location"
                title: qsTr("Location")
                description: !System.locationServices ? qsTr("Off for all apps, as this computer has no location service")
                    : page.line("location", qsTr("location"))
                chevron: true
                onClicked: Navigator.open("location")
            }

            UI.SettingRow {
                iconName: "folder"
                title: qsTr("Files and folders")
                description: (page.summary.files || 0) === 0 ? qsTr("Apps ask before opening anything outside their own folder")
                    : page.summary.files === 1 ? qsTr("1 app can open your files without asking")
                    : qsTr("%1 apps can open your files without asking").arg(page.summary.files)
                chevron: true
                onClicked: Navigator.open("appfiles")
            }

            UI.SettingRow {
                iconName: "globe"
                title: qsTr("Internet")
                description: AppAccess.appCount === 0 ? qsTr("No apps from the Store yet")
                    : (page.summary.network || 0) === 1 ? qsTr("1 app can go online")
                    : qsTr("%1 apps can go online").arg(page.summary.network || 0)
                chevron: true
                onClicked: Navigator.open("appnetwork")
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: Pages.exists("apppermissionsadvanced")
        spacing: 10

        SectionTitle {
            text: qsTr("Related settings")
        }

        UI.Card {
            Layout.fillWidth: true

            Column {
                width: parent.width

                UI.SettingRow {
                    iconName: "sandbox"
                    title: qsTr("Everything an app can reach")
                    description: qsTr("Every sandbox permission, app by app, for experts")
                    chevron: true
                    onClicked: Navigator.open("apppermissionsadvanced")
                }
            }
        }
    }
}
