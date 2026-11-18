import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.Files
import WildOS.UI as UI

// Format... for a USB stick, SD card or external drive, after Windows 11's
// Format dialog: a file system (exFAT first, as it works everywhere), the
// label, Quick format and, beyond Windows, encryption. Then progress, then
// done or what went wrong, in the same dialog.
UI.Dialog {
    id: dialog

    property string udi
    property string driveName
    property var info: ({})
    property string fs: "exfat"
    // "form", "working", "done" or "failed"
    property string stage: "form"
    property string message
    property string mountPath
    property bool tried: false

    signal openDrive(string path)

    readonly property bool encrypt: encryptBox.checked
    readonly property string passwordProblem: !encrypt ? ""
        : passwordField.text.length < 8 ? qsTr("Use at least 8 characters.")
        : passwordField.text !== confirmField.text ? qsTr("The passwords don't match.")
        : ""
    readonly property int labelLimit: fs === "vfat" ? 11 : fs === "exfat" ? 15 : fs === "ntfs" ? 32 : 16

    function sizeText(bytes) {
        // In the units the drive tiles use (1024-based, as Windows counts),
        // so the dialog and the tile give the same size.
        if (!(bytes > 0)) {
            return "";
        }
        const gb = bytes / 1073741824;
        if (gb >= 1024) {
            return qsTr("%1 TB").arg((gb / 1024).toLocaleString(Qt.locale(), "f", gb >= 10240 ? 0 : 1).replace(/\.0$/, ""));
        }
        if (gb < 1) {
            return qsTr("%1 MB").arg(Math.round(bytes / 1048576));
        }
        return qsTr("%1 GB").arg(gb.toLocaleString(Qt.locale(), "f", gb < 10 ? 1 : 0).replace(/\.0$/, ""));
    }

    function show(target, name) {
        udi = target;
        info = DriveFormat.describe(target);
        driveName = name.length > 0 ? name : info.label && info.label.length > 0 ? info.label : (info.kind ?? qsTr("Drive"));
        fs = "exfat";
        stage = "form";
        tried = false;
        message = "";
        mountPath = "";
        labelField.text = info.label ?? "";
        quickBox.checked = true;
        encryptBox.checked = false;
        passwordField.text = "";
        confirmField.text = "";
        open();
        startButton.forceActiveFocus();
    }

    function begin() {
        tried = true;
        if (passwordProblem.length > 0) {
            passwordField.forceActiveFocus();
            return;
        }
        stage = "working";
        DriveFormat.start(udi, fs, labelField.text, quickBox.checked, encrypt ? passwordField.text : "");
    }

    Connections {
        target: DriveFormat
        enabled: dialog.stage === "working"
        function onFinished(ok, text, path) {
            if (ok) {
                dialog.mountPath = path;
                dialog.stage = "done";
            } else if (text.length === 0) {
                // Cancelled at the password prompt: back to the choices.
                dialog.stage = "form";
            } else {
                dialog.message = text;
                dialog.stage = "failed";
            }
        }
    }

    title: stage === "working" ? qsTr("Formatting %1").arg(driveName)
        : stage === "done" ? qsTr("Format complete")
        : stage === "failed" ? qsTr("Format didn't finish")
        : qsTr("Format %1").arg(driveName)
    text: stage === "working" ? (quickBox.checked ? qsTr("This takes a few seconds. Don't unplug the drive.")
                                                   : qsTr("Writing over the whole drive. This can take a long time. Don't unplug the drive."))
        : stage === "done" ? qsTr("%1 is ready to use.").arg(labelField.text.trim().length > 0 ? labelField.text.trim() : driveName)
        : stage === "failed" ? message
        : [sizeText(info.size), info.kind].filter(s => s && s.length > 0).join(" ")
    closePolicy: stage === "working" ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape

    component Hint: UI.Label {
        Layout.fillWidth: true
        font.pixelSize: UI.Style.px(12)
        color: UI.Style.inkMuted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    component Heading: UI.Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        font.weight: UI.Style.weightMedium
    }

    component Choice: UI.RadioButton {
        property string value
        property bool available: true
        T.ButtonGroup.group: fileSystems
        enabled: available
        checked: dialog.fs === value
        onToggled: if (checked) dialog.fs = value
    }

    T.ButtonGroup {
        id: fileSystems
    }

    // The choices.
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.stage === "form"
        spacing: 8

        UI.Label {
            Layout.fillWidth: true
            visible: dialog.info.readOnly === true
            text: qsTr("This drive is write-protected, so it can't be formatted. If it has a lock switch, slide it off and plug it back in.")
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        Heading {
            Layout.topMargin: 0
            text: qsTr("File system")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 22
            Choice {
                value: "exfat"
                text: "exFAT"
                available: dialog.info.tools?.exfat ?? true
            }
            Choice {
                value: "ntfs"
                text: "NTFS"
                available: dialog.info.tools?.ntfs ?? true
            }
            Choice {
                value: "vfat"
                text: "FAT32"
                available: (dialog.info.fatAllowed ?? true) && (dialog.info.tools?.vfat ?? true)
            }
            Choice {
                value: "ext4"
                text: "ext4"
                available: dialog.info.tools?.ext4 ?? true
            }
        }
        Hint {
            text: dialog.fs === "exfat" ? qsTr("Works on Windows, Mac and Linux. Best for most drives.")
                : dialog.fs === "ntfs" ? qsTr("Best for Windows. Macs can open it but can't save to it.")
                : dialog.fs === "vfat" ? qsTr("For older cameras, TVs and car stereos. No file can be over 4 GB.")
                : qsTr("Linux only.")
        }
        Hint {
            visible: dialog.info.fatAllowed === false
            text: qsTr("FAT32 is only for drives up to 32 GB, and this one is bigger.")
        }

        Heading {
            text: qsTr("Volume label")
        }
        UI.TextField {
            id: labelField
            Layout.fillWidth: true
            Accessible.name: qsTr("Volume label")
            placeholderText: qsTr("A name for the drive")
            maximumLength: dialog.labelLimit
        }

        UI.CheckBox {
            id: quickBox
            Layout.topMargin: 6
            text: qsTr("Quick format")
            checked: true
        }
        Hint {
            Layout.leftMargin: 30
            visible: !quickBox.checked
            text: qsTr("Writes over the whole drive so nothing on it can be recovered. This takes much longer.")
        }

        UI.CheckBox {
            id: encryptBox
            visible: dialog.info.canEncrypt === true
            text: qsTr("Encrypt this drive")
        }
        Hint {
            Layout.leftMargin: 30
            visible: encryptBox.checked
            text: qsTr("You'll need this password to open the drive. Only Linux PCs, like this one, can open it. If you forget the password, nothing on it can be recovered.")
        }
        UI.TextField {
            id: passwordField
            Layout.fillWidth: true
            Layout.leftMargin: 30
            visible: encryptBox.checked
            Accessible.name: qsTr("Password")
            placeholderText: qsTr("Password")
            revealable: true
            UI.NoPaste {}
            error: dialog.tried && dialog.passwordProblem.length > 0
        }
        UI.TextField {
            id: confirmField
            Layout.fillWidth: true
            Layout.leftMargin: 30
            visible: encryptBox.checked
            Accessible.name: qsTr("Type the password again")
            placeholderText: qsTr("Type the password again")
            revealable: true
            UI.NoPaste {}
            error: dialog.tried && dialog.passwordProblem.length > 0
        }
        UI.Label {
            Layout.fillWidth: true
            Layout.leftMargin: 30
            visible: encryptBox.checked && dialog.tried && dialog.passwordProblem.length > 0
            text: dialog.passwordProblem
            font.pixelSize: UI.Style.px(12)
            color: UI.Style.danger
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 10
            UI.Icon {
                Layout.alignment: Qt.AlignTop
                name: "warning"
                size: 18
                color: UI.Style.warning
            }
            UI.Label {
                Layout.fillWidth: true
                text: qsTr("Everything on %1 will be erased.").arg(dialog.driveName)
                font.weight: UI.Style.weightMedium
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }

    UI.ProgressBar {
        Layout.fillWidth: true
        Layout.topMargin: 4
        visible: dialog.stage === "working"
        indeterminate: DriveFormat.progress < 0
        value: Math.max(0, DriveFormat.progress)
    }
    UI.Label {
        Layout.fillWidth: true
        visible: dialog.stage === "working" && DriveFormat.progress >= 0
        text: qsTr("%1%").arg(Math.floor(DriveFormat.progress * 100))
        color: UI.Style.inkMuted
    }

    buttons: [
        UI.Button {
            visible: dialog.stage === "form"
            text: qsTr("Cancel")
            onClicked: dialog.close()
        },
        UI.Button {
            id: startButton
            visible: dialog.stage === "form"
            kind: "primary"
            text: qsTr("Start")
            enabled: dialog.info.ok === true && dialog.info.readOnly !== true
            onClicked: dialog.begin()
        },
        UI.Button {
            visible: dialog.stage === "done" || dialog.stage === "failed"
            kind: dialog.stage === "failed" || dialog.mountPath.length === 0 ? "primary" : "secondary"
            text: qsTr("Close")
            onClicked: dialog.close()
        },
        UI.Button {
            visible: dialog.stage === "done" && dialog.mountPath.length > 0
            kind: "primary"
            text: qsTr("Open drive")
            onClicked: {
                dialog.close();
                dialog.openDrive(dialog.mountPath);
            }
        }
    ]
}
