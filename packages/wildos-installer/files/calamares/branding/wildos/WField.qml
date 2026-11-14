/* A labelled WildOS.UI text field in the large size (42px, radius 10, 14px)
 * of the account form, with an optional hint or error line underneath.
 */
import QtQuick
import QtQuick.Layouts
import WildOS.UI as UI

ColumnLayout {
    id: field

    property string label: ""
    property string hint: ""
    property string error: ""
    property alias text: input.text
    // Password fields get the show and hide button; UI.TextField then owns echoMode.
    property int echoMode: TextInput.Normal
    property alias validator: input.validator
    property alias input: input
    property alias placeholder: input.placeholderText

    spacing: 6

    Text {
        text: field.label
        color: UI.Style.ink
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(13)
        font.pixelSize: 13
        font.weight: UI.Style.weightMedium
    }

    UI.TextField {
        id: input
        Layout.fillWidth: true
        size: "large"
        error: field.error !== ""
        revealable: field.echoMode === TextInput.Password
        UI.NoPaste { enabled: field.echoMode === TextInput.Password }
        Accessible.name: field.label
    }

    Text {
        Layout.fillWidth: true
        visible: text !== ""
        text: field.error !== "" ? field.error : field.hint
        color: field.error !== "" ? UI.Style.danger : UI.Style.inkMuted
        font.family: UI.Style.fontFamily
        font.letterSpacing: UI.Style.tracking(12)
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
