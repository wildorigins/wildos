import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// One row of a grouped list: an optional line icon, theme icon or icon on a
// glass tile, a 14px title over a 12px muted description, a status word,
// whatever control belongs on the right, and a chevron when the row opens
// a page (or the external arrow when it opens another app). 48px tall, 62
// with a description, 18px side padding, 16px gaps (Settings.dc rows). Rows
// after the first draw the divider above themselves, so a group never ends
// in a stray line whichever rows are hidden. From wildos-settings
// SettingRow; Welcome's PrivacyRow, FilesStep and WelcomeStep rows and the
// Security flyout's SecurityRow are the same row.
//
// A row is clickable only when it has a chevron, opens something
// (`external`) or sets `interactive`; a row holding a toggle is not, and
// its title dims with the control when `enabled` is false. A clickable row
// is reached with Tab, opened with Enter or Space, and shows the focus ring.
//
// A switch, drop-down, slider or field placed on the right is announced by
// the row's title, unless it was given an `accessibleName` of its own.
T.AbstractButton {
    id: row

    property string iconName
    property string themeIcon
    property bool iconTile: false
    property string title
    property string description
    property string status
    property color statusColor: Style.inkMuted
    // A description that is itself the news (Security's "This disk isn't
    // encrypted") can take the warning or accent text colour.
    property color descriptionColor: Style.inkMuted
    property bool chevron: false
    property bool external: false
    property bool interactive: chevron || external
    property bool divider: y > 0
    property real radius: 0
    property int minHeight: description.length > 0 ? 62 : 48
    property bool previewHover: false
    default property alias trailing: trailingRow.data
    property alias below: belowFlow.data
    // A control that belongs before the title, such as the radio of a row
    // that is one choice of a set (radios sit left of their label).
    property alias leading: leadingRow.data

    readonly property bool lit: interactive && (hovered || previewHover) && enabled
    property bool namesControls: false

    function nameControls(): void {
        for (const control of Array.from(trailingRow.children)) {
            if (control.accessibleName === "") {
                control.accessibleName = Qt.binding(() => row.title);
            }
        }
    }

    // Not before this: a name the page binds is still empty while the row
    // is being put together, and would be replaced.
    Component.onCompleted: {
        namesControls = true;
        nameControls();
    }

    implicitWidth: 360
    // The text column carries the 10px above and below, so larger text
    // grows the row from 62 without padding it twice.
    // Layouts size a wrapped description by its one-line height, so the
    // laid-out text height is counted too; without it larger text or a
    // narrow row overflows into the next row.
    readonly property real textHeight: titleLabel.contentHeight
        + (descriptionLabel.visible ? 2 + descriptionLabel.contentHeight : 0)
        + (belowFlow.visible ? 4 + belowFlow.height : 0)
    implicitHeight: Math.max(minHeight, layout.implicitHeight, textHeight + 20)
    width: parent ? parent.width : implicitWidth
    padding: 0
    hoverEnabled: interactive
    focusPolicy: interactive ? Qt.StrongFocus : Qt.NoFocus
    Accessible.role: interactive ? Accessible.Button : Accessible.ListItem
    // Space already presses a button; Enter opens a row too, as on Windows.
    Keys.onReturnPressed: event => row.pressFromKey(event)
    Keys.onEnterPressed: event => row.pressFromKey(event)

    function pressFromKey(event: var): void {
        event.accepted = interactive;
        if (interactive && !event.isAutoRepeat) {
            click();
        }
    }
    Accessible.name: title
    Accessible.description: description

    background: Rectangle {
        radius: row.radius
        color: row.interactive && row.down ? Style.surfacePressed : row.lit ? Style.surfaceHover : "transparent"
        Behavior on color {
            ColorAnimation { duration: Style.hoverDuration }
        }

        Rectangle {
            visible: row.divider && row.radius === 0
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Style.divider
        }

        FocusRing {
            visible: row.visualFocus
            anchors.margins: 2
            targetRadius: Math.max(row.radius - 2, 4)
        }
    }

    contentItem: RowLayout {
        id: layout
        spacing: 16

        Item {
            Layout.leftMargin: 18 - layout.spacing
            Layout.preferredWidth: 0
        }

        Row {
            id: leadingRow
            visible: children.length > 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 12
        }

        Icon {
            visible: row.iconName.length > 0 && row.themeIcon.length === 0 && !row.iconTile
            name: row.iconName
            size: 20
            opacity: row.enabled ? 1 : Style.disabledOpacity
        }

        Rectangle {
            visible: row.iconTile
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            radius: 10
            color: Style.surfaceTile

            Icon {
                anchors.centerIn: parent
                visible: row.themeIcon.length === 0
                name: row.iconName
                size: 20
            }

            ThemeIcon {
                anchors.centerIn: parent
                visible: row.themeIcon.length > 0
                width: 24
                height: 24
                name: row.themeIcon
            }
        }

        ThemeIcon {
            visible: row.themeIcon.length > 0 && !row.iconTile
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            name: row.themeIcon
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 2
            opacity: row.enabled ? 1 : Style.disabledOpacity

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: row.title
                size: 14
            }

            Label {
                id: descriptionLabel
                Layout.fillWidth: true
                visible: row.description.length > 0
                text: row.description
                size: 12
                color: row.descriptionColor
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }

            Flow {
                id: belowFlow
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: children.length > 0
                spacing: 6
            }
        }

        Label {
            visible: row.status.length > 0
            text: row.status
            size: 12
            color: row.statusColor
        }

        Row {
            id: trailingRow
            Layout.alignment: Qt.AlignVCenter
            spacing: 12
            onChildrenChanged: if (row.namesControls) row.nameControls()
        }

        Icon {
            visible: row.chevron || row.external
            name: row.external ? "external" : "chevron"
            size: 14
            strokeWidth: 2
            color: Style.iconMuted
        }

        Item {
            Layout.rightMargin: 18 - layout.spacing
            Layout.preferredWidth: 0
        }
    }
}
