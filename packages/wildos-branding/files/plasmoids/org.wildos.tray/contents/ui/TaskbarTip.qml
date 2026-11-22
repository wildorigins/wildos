import QtQuick
import WildOS.UI as UI
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

// The hover label of a taskbar item: one plain line of 12 px text, as
// Windows shows. It has to leave the taskbar's window, which is only as
// tall as the bar, and only Plasma's tooltip can do that, so it carries a
// Wild OS label. Fills the item it is declared in.
PlasmaCore.ToolTipArea {
    id: tip

    property string text

    anchors.fill: parent
    active: text.length > 0
    mainText: text
    textFormat: Text.PlainText
    // The taskbar's own edge, so the tip opens away from it.
    location: Plasmoid.location
    // Plasma sizes the tooltip to whole pixels and the label's own width is
    // a fraction more, which was enough to cut the end off the text. So the
    // size is rounded up and the label never shortens itself.
    mainItem: Item {
        implicitWidth: Math.ceil(tipLabel.implicitWidth)
        implicitHeight: Math.ceil(tipLabel.implicitHeight)

        UI.Label {
            id: tipLabel
            leftPadding: 8
            rightPadding: 8
            topPadding: 6
            bottomPadding: 6
            size: 12
            elide: Text.ElideNone
            text: tip.text
        }
    }
}
