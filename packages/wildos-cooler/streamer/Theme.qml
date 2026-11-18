import QtQml
import WildOS.UI as UI
import WildOS.Cooler.Faces

// "Use my theme colours" for the cooler's own screen: the same accent the
// Cooler window reads (WildOS.UI follows Settings) through the same rule
// (Faces.themeColours), kept on the face as either changes.
QtObject {
    id: theme

    property QtObject view
    property bool use: true

    property Binding colours: Binding {
        target: theme.view
        property: "themeColours"
        value: theme.use ? Faces.themeColours(UI.Style.accent, UI.Style.glacier) : null
    }
}
