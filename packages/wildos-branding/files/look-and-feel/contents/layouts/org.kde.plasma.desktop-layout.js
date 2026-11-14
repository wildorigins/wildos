// The Wild OS desktop: the taskbar docked along the bottom, Windows mode's
// status at the left end (it takes no room until Windows mode is set up),
// Start and the apps centred on the screen, and the camera and microphone
// lights, the security chip, the Updates icon, a divider, quick settings and
// the clock at the right end. Sizes follow the Taskbar artboard
// in the design. Settings > Personalisation can switch it to floating glass
// pills instead.
//
// Both pills come from one full-width panel. On Wayland, Plasma 6 panels are
// layer-shell surfaces, and two panels on the same edge stack on top of each
// other in whichever order they map at login, so the pills cannot be two
// panels. Instead wildos-shell replaces Plasma's panel view with one that
// draws no bar of its own and puts a glass pill (with blur behind it) around
// each run of widgets between expanding spacers.
//
// The panel must stay "Always visible": it is what keeps maximised windows
// clear of the taskbar.

// Each desktop is already the Wild OS desktop (org.wildos.desktop, its icons
// and right-click menu): Plasma made it from the look-and-feel defaults
// before this script ran, and a script cannot change a desktop's kind. Only
// the wallpaper underneath is set here.
var desktops = desktopsForActivity(currentActivity());
for (var i = 0; i < desktops.length; i++) {
    var desk = desktops[i];
    desk.wallpaperPlugin = "org.kde.image";
    desk.currentConfigGroup = ["Wallpaper", "org.kde.image", "General"];
    desk.writeConfig("Image", "file:///usr/share/wallpapers/WildOS/");
}

var bar = new Panel;
bar.location = "bottom";
bar.lengthMode = "fill";
bar.floating = false;
bar.hiding = "none";
// The docked bar is 52 px; floating pills (62 px, 14 px above the screen
// edge) make it 76.
bar.height = 52;

// Where Windows 11 puts its weather. It keeps 8 px from the screen edge
// itself, so no spacer goes before it: Settings takes the leftmost spacer to
// be the one that centres the apps.
bar.addWidget("org.wildos.winmode");

// Two expanding spacers either side of the centre group let Plasma centre it
// on the screen rather than in the space left of the tray.
var leftSpacer = bar.addWidget("org.kde.plasma.panelspacer");
leftSpacer.currentConfigGroup = ["General"];
leftSpacer.writeConfig("expanding", "true");

var start = bar.addWidget("org.wildos.start");

// Task view straight after Start, as on Windows: Overview with the virtual
// desktops. Win+Tab opens the same view.
bar.addWidget("org.wildos.taskview");

var appsDivider = bar.addWidget("org.wildos.divider");
appsDivider.currentConfigGroup = ["General"];
appsDivider.writeConfig("lineHeight", "28");
appsDivider.writeConfig("sideMargin", "6");

var tasks = bar.addWidget("org.wildos.tasks");
tasks.currentConfigGroup = ["General"];
tasks.writeConfig("launchers", [
    "applications:org.wildos.files.desktop",
    "applications:brave-browser.desktop",
    "applications:org.wildos.mail.desktop",
    "applications:org.wildos.store.desktop",
    "applications:org.wildos.settings.desktop"
]);
// A small speaker on apps playing sound, which mutes them when clicked.
tasks.writeConfig("indicateAudioStreams", "true");

var rightSpacer = bar.addWidget("org.kde.plasma.panelspacer");
rightSpacer.currentConfigGroup = ["General"];
rightSpacer.writeConfig("expanding", "true");

// The camera and microphone lights, left of the shield. They take no room
// until an app uses one or one is switched off.
bar.addWidget("org.wildos.lights");
bar.addWidget("org.wildos.protected");
// Only there while an update downloads or installs.
bar.addWidget("org.wildos.updates");

// The design leaves 2 px between items in the status pill against the
// panel's 4, so this divider takes the difference out of its own margins.
var statusDivider = bar.addWidget("org.wildos.divider");
statusDivider.currentConfigGroup = ["General"];
statusDivider.writeConfig("lineHeight", "24");
statusDivider.writeConfig("sideMargin", "2");

// App status icons, with a Windows-style overflow; Plasma's own tray is not
// loaded at all, so its network, volume and notification widgets can never
// appear twice.
bar.addWidget("org.wildos.tray");

bar.addWidget("org.wildos.quicksettings");
bar.addWidget("org.wildos.clock");

// Holds the status pill 16 px in from the right edge of the screen.
var endSpacer = bar.addWidget("org.kde.plasma.panelspacer");
endSpacer.currentConfigGroup = ["General"];
endSpacer.writeConfig("expanding", "false");
endSpacer.writeConfig("length", "8");

// Plasma 6 dropped the switch that locks the whole bar, but each widget can
// still be locked on its own. Locked widgets lose "Configure…", "Add widgets"
// and "Show panel configuration" from their right-click menus, so the bar
// cannot be taken apart by accident, while pinning and unpinning apps keeps
// working (the task manager only refuses that when an administrator locks
// the system).
//
// Widgets given settings above are reloaded from their settings group when
// this script ends, which would unlock them again, so the lock is written
// there too.
bar.locked = true;
var barWidgets = bar.widgets();
for (var w = 0; w < barWidgets.length; w++) {
    barWidgets[w].currentConfigGroup = [];
    barWidgets[w].writeConfig("immutability", 2);
    barWidgets[w].locked = true;
}
