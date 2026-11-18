import QtQuick
import org.kde.plasma.plasma5support as P5Support

// The accessibility switches Windows puts behind its quick settings tile,
// as far as Wild OS has them, all KWin effects:
//   Magnifier       the zoom effect, magnifying through its own shortcuts
//   Invert colours  the invert effect, through its own shortcut
//   Colour filters  colour blindness correction, on while it is loaded
// KWin loads zoom and invert all the time so their keyboard shortcuts work,
// and has no call that says whether they are zoomed or inverted. Those two
// switches therefore start off with the session, as the screen does, and
// follow what was switched here. Colour filters is read back from KWin and
// kept for the next sign-in in kwinrc.
Item {
    id: access

    readonly property var features: [
        { id: "zoom", title: qsTr("Magnifier"), description: qsTr("Zooms in on the screen. Meta and plus or minus change how far.") },
        { id: "invert", title: qsTr("Invert colours"), description: qsTr("Swaps light and dark across the whole screen.") },
        { id: "colorblindnesscorrection", title: qsTr("Colour filters"), description: qsTr("Adjusts colours to make them easier to tell apart.") }
    ]

    // feature id -> on.
    property var loaded: ({})
    readonly property bool anyOn: Object.keys(loaded).some(id => loaded[id])

    readonly property string effects: "qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects"
    readonly property string shortcut: "qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut "

    P5Support.DataSource {
        id: shell
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            if (source.indexOf("isEffectLoaded colorblindnesscorrection") >= 0) {
                access.remember("colorblindnesscorrection", String(data.stdout).trim() === "true");
            }
        }
    }

    function remember(id: string, on: bool): void {
        const next = Object.assign({}, loaded);
        next[id] = on;
        loaded = next;
    }

    function refresh(): void {
        shell.connectSource(effects + ".isEffectLoaded colorblindnesscorrection");
    }

    function setOn(id: string, on: bool): void {
        let command = "";
        switch (id) {
        case "zoom":
            // Like Windows' Magnifier: on starts magnifying, off goes back
            // to normal size.
            command = effects + ".loadEffect zoom; " + shortcut + (on ? "view_zoom_in" : "view_actual_size");
            break;
        case "invert":
            command = effects + ".loadEffect invert; " + shortcut + "Invert";
            break;
        case "colorblindnesscorrection":
            command = "kwriteconfig6 --file kwinrc --group Plugins --key colorblindnesscorrectionEnabled --type bool " + on + "; "
                + effects + (on ? ".loadEffect " : ".unloadEffect ") + id;
            break;
        default:
            return;
        }
        shell.connectSource(command);
        remember(id, on);
    }

    Component.onCompleted: refresh()
}
