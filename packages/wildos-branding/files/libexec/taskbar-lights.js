// Puts the camera and microphone lights on a taskbar made before they
// existed, just left of the security chip. New accounts get them from the
// Wild OS layout, and a taskbar that has them already, or has no security
// chip to sit beside, is left alone. taskbar-items runs this once per
// account, on a running shell.
panels().forEach(function (p) {
    var chips = p.widgets("org.wildos.protected");
    if (chips.length === 0 || p.widgets("org.wildos.lights").length > 0) {
        return;
    }
    var chip = chips[0].geometry;
    // Plasma's scripting can't say where in a panel a widget goes, but a
    // panel puts one dropped on the right half of another just after it.
    // The chip is too narrow to aim at its left half (the bar's coordinates
    // sit a few pixels off the ones a drop is measured in), so the drop goes
    // near the right end of whatever is before it: the spacer that centres
    // the apps, which is wide.
    var before = null;
    var all = p.widgets();
    for (var i = 0; i < all.length; i++) {
        var g = all[i].geometry;
        if (g.x + g.width <= chip.x + 2 && (before === null || g.x + g.width > before.x + before.width)) {
            before = g;
        }
    }
    if (before === null) {
        return;
    }
    // The layout locks the bar, and Plasma adds nothing to a locked one.
    var locked = p.locked;
    p.locked = false;
    var x = before.x + before.width - Math.min(12, before.width / 4);
    // The size has to be given too: without one the position is ignored
    // and the widget lands at the start of the bar.
    var item = p.addWidget("org.wildos.lights", x, chip.y + chip.height / 2, 44, chip.height);
    if (item) {
        // Locked like the rest of the bar (see the layout), so it can't be
        // removed or moved by accident.
        item.currentConfigGroup = [];
        item.writeConfig("immutability", 2);
        item.locked = true;
    }
    p.locked = locked;
});
