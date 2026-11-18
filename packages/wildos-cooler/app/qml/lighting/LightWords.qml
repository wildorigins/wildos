pragma Singleton

import QtQuick
import WildOS.UI as UI

// The words and colours the Lighting pages share (10.11, 10.15, 10.16).
QtObject {
    // The lights are drawn on a dark stage in either scheme, as lights in
    // a PC are seen against its inside, with their own inks on it.
    readonly property color stage: "#0d0e13"
    readonly property color stageEdge: Qt.rgba(1, 1, 1, 0.08)
    // The same edge laid over the stage itself, for a card's outline.
    readonly property color stageRim: "#202126"
    readonly property color stageRimLit: "#3a3b40"
    readonly property color stageInk: "#ffffff"
    readonly property color stageMuted: Qt.rgba(1, 1, 1, 0.786)
    readonly property color stageFaint: Qt.rgba(1, 1, 1, 0.545)
    readonly property color hub: "#0a0b0f"
    readonly property color unlit: "#1b1d23"

    // The colours "Follow my theme" runs through, as wildos-lighting
    // works them out from the accent.
    readonly property var themeStops: {
        const a = UI.Style.dark ? UI.Style.accent : UI.Style.systemAccent;
        const turned = (degrees, s, l) => Qt.hsla((a.hslHue + degrees / 360) % 1, Math.min(1, a.hslSaturation * s), Math.min(1, a.hslLightness * l), 1);
        return a.hslHue < 0 ? [a, a, a] : [a, turned(29, 0.76, 0.96), turned(131, 0.72, 1.01)];
    }
    readonly property var spectrum: ["#ff3d6e", "#ffb02e", "#4dff9a", "#2ec5ff", "#a35bff", "#ff3d6e"]
    readonly property var temperature: ["#5fa8ff", "#b58cff", "#ff6b6b"]

    function zones(count) {
        return count === 1 ? qsTr("1 zone") : qsTr("%1 zones").arg(count);
    }

    function lights(count) {
        return count === 1 ? qsTr("1 light") : qsTr("%1 lights").arg(count);
    }

    function parts(count) {
        return count === 1 ? qsTr("1 part") : qsTr("%1 parts").arg(count);
    }

    // A part is called by what it is. Where that says too little (a light
    // controller, an accessory) or there are two of a kind, it goes by
    // the name it gives itself.
    function kindName(kind) {
        switch (kind) {
        case "motherboard": return qsTr("Motherboard");
        case "memory": return qsTr("Memory");
        case "graphics": return qsTr("Graphics card");
        case "cooler": return qsTr("Cooler");
        case "keyboard": return qsTr("Keyboard");
        case "mouse": return qsTr("Mouse");
        case "mousemat": return qsTr("Mouse mat");
        case "headset": return qsTr("Headset");
        case "stand": return qsTr("Headset stand");
        case "gamepad": return qsTr("Controller");
        case "speaker": return qsTr("Speaker");
        case "storage": return qsTr("Drive");
        case "case": return qsTr("Case");
        case "microphone": return qsTr("Microphone");
        case "keypad": return qsTr("Keypad");
        case "laptop": return qsTr("Laptop");
        case "monitor": return qsTr("Monitor");
        }
        return "";
    }

    function title(part, all) {
        const kind = kindName(part.kind);
        if (kind === "" || all.filter(p => p.kind === part.kind).length > 1) {
            return part.name;
        }
        return kind;
    }

    function reportedBy(kind) {
        switch (kind) {
        case "motherboard": return qsTr("reported by the board");
        case "graphics": return qsTr("reported by the card");
        case "cooler": return qsTr("reported by the cooler");
        case "memory": return qsTr("reported by the memory");
        case "keyboard": return qsTr("reported by the keyboard");
        case "mouse": return qsTr("reported by the mouse");
        }
        return qsTr("reported by the part");
    }

    function cannotCount(kind) {
        return kind === "motherboard" ? qsTr("The board cannot count these itself.") : qsTr("The part cannot count these itself.");
    }

    function icon(kind) {
        return !kind || kind === "strip" || kind === "light" || kind === "accessory" || kind === "other" || kind === "virtual" ? "light" : kind;
    }

    function summary(part) {
        if (!part.controlled) {
            return qsTr("%1 · %2").arg(zones(part.zones.length)).arg(qsTr("not controlled"));
        }
        if (part.white) {
            return qsTr("%1 · %2").arg(zones(part.zones.length)).arg(qsTr("white only"));
        }
        return qsTr("%1 · %2").arg(zones(part.zones.length)).arg(lights(part.lights));
    }

    function effectName(id) {
        switch (id) {
        case "theme": return qsTr("Follow my theme");
        case "taskbar": return qsTr("Taskbar light");
        case "temperature": return qsTr("Temperature");
        case "breathe": return qsTr("Breathe");
        case "steady": return qsTr("Steady");
        }
        return qsTr("Off");
    }

    // The name a light of a part that is only seen goes by: such a part
    // reports nothing, so there is no name to pass on.
    function seenLight(zone) {
        return zone.white ? qsTr("White light") : qsTr("Colour light");
    }
}
