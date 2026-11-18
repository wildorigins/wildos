.pragma library

// The line icons boards 10.11, 10.15 and 10.16 draw for parts and their
// controls, on the same 24px grid as CoolerIcons.

function r(x, y, w, h, rad) {
    return "M" + (x + rad) + " " + y + "H" + (x + w - rad)
        + "A" + rad + " " + rad + " 0 0 1 " + (x + w) + " " + (y + rad)
        + "V" + (y + h - rad)
        + "A" + rad + " " + rad + " 0 0 1 " + (x + w - rad) + " " + (y + h)
        + "H" + (x + rad)
        + "A" + rad + " " + rad + " 0 0 1 " + x + " " + (y + h - rad)
        + "V" + (y + rad)
        + "A" + rad + " " + rad + " 0 0 1 " + (x + rad) + " " + y + "Z";
}

function c(cx, cy, rad) {
    return "M" + (cx - rad) + " " + cy
        + "A" + rad + " " + rad + " 0 1 0 " + (cx + rad) + " " + cy
        + "A" + rad + " " + rad + " 0 1 0 " + (cx - rad) + " " + cy + "Z";
}

var bulb = "M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 2.5M9 18h6M10 22h4";

var paths = {
    "everything": "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z"
        + "M22 17.65l-9.17 4.16a2 2 0 0 1-1.66 0L2 17.65M22 12.65l-9.17 4.16a2 2 0 0 1-1.66 0L2 12.65",
    "cooler": "M12 22a7 7 0 0 0 7-7c0-2-1-3.9-3-5.5s-3.5-4-4-6.5c-.5 2.5-2 4.9-4 6.5C6 11.1 5 13 5 15a7 7 0 0 0 7 7z",
    "motherboard": r(3, 3, 18, 18, 2) + "M11 9h4a2 2 0 0 0 2-2V3" + c(9, 9, 2) + "M7 21v-4a2 2 0 0 1 2-2h4" + c(15, 15, 2),
    "graphics": r(5, 2, 14, 20, 2) + "M15 14h.01M9 6h6M9 10h6",
    "memory": "M6 19v-3M10 19v-3M14 19v-3M18 19v-3M8 11V9M16 11V9M12 11V9M2 15h20"
        + "M2 7a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v1.1a2 2 0 0 0 0 3.837V17a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2v-5.1a2 2 0 0 0 0-3.837Z",
    "keyboard": r(2, 4, 20, 16, 2) + "M10 8h.01M12 12h.01M14 8h.01M16 12h.01M18 8h.01M6 8h.01M7 16h10M8 12h.01",
    "keypad": r(2, 4, 20, 16, 2) + "M10 8h.01M12 12h.01M14 8h.01M16 12h.01M18 8h.01M6 8h.01M7 16h10M8 12h.01",
    "mouse": r(5, 2, 14, 20, 7) + "M12 6v4",
    "headset": "M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3",
    "monitor": r(2, 3, 20, 14, 2) + "M8 21h8M12 17v4",
    "laptop": r(4, 5, 16, 11, 2) + "M2 20h20",
    "light": bulb,
    "flash": "M4 14a1 1 0 0 1-.78-1.63l9.9-10.2a.5.5 0 0 1 .86.46l-1.92 6.02A1 1 0 0 0 13 10h7a1 1 0 0 1 .78 1.63l-9.9 10.2a.5.5 0 0 1-.86-.46l1.92-6.02A1 1 0 0 0 11 14z",
    "pencil": "M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497zM15 5l4 4",
    "link": "M9 17H7A5 5 0 0 1 7 7h2M15 7h2a5 5 0 1 1 0 10h-2M8 12h8",
    "minus": "M5 12h14",
    "plus": "M5 12h14M12 5v14",
    "info": c(12, 12, 10) + "M12 16v-4M12 8h.01",
    "eye": "M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0" + c(12, 12, 3)
};

function path(name) {
    return paths[name] || bulb;
}

// How many lights each section of a zone has: as evenly as they go, the
// longer ones first, as wildos-lighting splits them.
function sections(lights, split) {
    var n = Math.max(1, Math.min(split, Math.max(1, lights)));
    var out = [];
    for (var i = 0; i < n; ++i) {
        out.push(Math.floor(lights / n) + (i < lights % n ? 1 : 0));
    }
    return out;
}
