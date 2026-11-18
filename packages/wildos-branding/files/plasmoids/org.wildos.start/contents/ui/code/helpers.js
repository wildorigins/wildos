.pragma library

// The design names apps by what they do, not by their project names, so the
// pinned grid reads "Files" rather than "Dolphin". Keyed by desktop file id.
const friendlyNames = {
    "org.kde.dolphin": QT_TR_NOOP("Files"),
    "brave-browser": "Brave",
    "mullvad-browser": "Mullvad Browser",
    "org.kde.discover": QT_TR_NOOP("Store"),
    "org.wildos.files": QT_TR_NOOP("Files"),
    "org.wildos.store": QT_TR_NOOP("Store"),
    "systemsettings": QT_TR_NOOP("Settings"),
    "org.wildos.settings": QT_TR_NOOP("Settings"),
    "org.wildos.terminal": QT_TR_NOOP("Terminal"),
    "org.kde.konsole": QT_TR_NOOP("Terminal"),
    "org.kde.kcalc": QT_TR_NOOP("Calculator"),
    "org.kde.kwrite": QT_TR_NOOP("Text editor"),
    "libreoffice-writer": "Write",
    "libreoffice-calc": "Tables",
    "libreoffice-impress": "Slides",
    "org.kde.okular": QT_TR_NOOP("Documents"),
    "org.kde.gwenview": QT_TR_NOOP("Pictures"),
    "org.keepassxc.KeePassXC": QT_TR_NOOP("Password manager"),
};

// The default Start pins before Write and Tables, which every account made
// until then was given.
const pinsBeforeOffice = ["org.wildos.mail.desktop", "org.wildos.files.desktop", "org.wildos.calendar.desktop", "brave-browser.desktop", "mullvad-browser.desktop", "org.wildos.store.desktop", "org.wildos.settings.desktop", "org.wildos.terminal.desktop", "org.kde.kcalc.desktop", "org.kde.kwrite.desktop", "org.kde.okular.desktop", "org.kde.gwenview.desktop", "org.keepassxc.KeePassXC.desktop"];

// A default pin that takes over from an app pinned before it: an account
// that still has the old one pinned gets the new one in its place, once.
// The Text editor was drawn as the generic notes app until Notes came.
const replacesPin = {
    "org.wildos.notes.desktop": "org.kde.kwrite.desktop",
};

// Default pins only new accounts get: an account made before one of these
// was added keeps its pins as they are, and the pin is counted as offered.
const newAccountPins = ["org.wildos.music.desktop", "org.wildos.videos.desktop"];

// Apps that make up the system itself. Windows does not offer to uninstall
// these from Start either.
const coreApps = ["org.wildos.files", "org.wildos.store", "org.kde.dolphin", "org.kde.discover", "systemsettings", "org.wildos.settings", "org.kde.konsole",
    "org.wildos.report", "org.wildos.taskmanager", "org.wildos.welcome", "org.wildos.winmode", "org.wildos.run", "org.wildos.security", "org.wildos.access"];

// Wild OS's own apps, which the Store does not list: Settings removes them.
function isOwnApp(favoriteId) {
    const id = storageId(favoriteId);
    return id.indexOf("org.wildos.") === 0 || id === "libreoffice-writer" || id === "libreoffice-calc" || id === "libreoffice-impress";
}

function isCoreApp(favoriteId) {
    return coreApps.includes(storageId(favoriteId));
}

// KDE settings modules as a Windows user would know them: the page title,
// the Settings category it lives in, and the Wild OS Settings page to open.
// Modules missing from this list are developer or Plasma internals, or have
// no Wild OS Settings page yet (phone link, firewall and the like), and are
// left out of search rather than landing on a page that does not show them.
const settingsPages = {
    "kcm_kscreen": [QT_TR_NOOP("Display"), QT_TR_NOOP("System"), "display"],
    "kcm_nightlight": [QT_TR_NOOP("Night light"), QT_TR_NOOP("System › Display"), "nightlight"],
    "kcm_pulseaudio": [QT_TR_NOOP("Sound"), QT_TR_NOOP("System"), "sound"],
    "kcm_soundtheme": [QT_TR_NOOP("System sounds"), QT_TR_NOOP("System › Sound"), "sound"],
    "kcm_notifications": [QT_TR_NOOP("Notifications"), QT_TR_NOOP("System"), "notifications"],
    "kcm_powerdevilprofilesconfig": [QT_TR_NOOP("Power and battery"), QT_TR_NOOP("System"), "power"],
    "kcm_energyinfo": [QT_TR_NOOP("Battery usage"), QT_TR_NOOP("System › Power and battery"), "power"],
    "kcm_about-distro": [QT_TR_NOOP("About"), QT_TR_NOOP("System"), "about"],
    "kcm_updates": [QT_TR_NOOP("Update"), QT_TR_NOOP("Settings"), "update"],
    "kcm_bluetooth": [QT_TR_NOOP("Bluetooth"), QT_TR_NOOP("Bluetooth and devices"), "bluetooth"],
    "kcm_mouse": [QT_TR_NOOP("Mouse"), QT_TR_NOOP("Bluetooth and devices"), "mouse"],
    "kcm_touchpad": [QT_TR_NOOP("Touchpad"), QT_TR_NOOP("Bluetooth and devices"), "touchpad"],
    "kcm_keyboard": [QT_TR_NOOP("Keyboard"), QT_TR_NOOP("Bluetooth and devices"), "keyboard"],
    "kcm_printer_manager": [QT_TR_NOOP("Printers and scanners"), QT_TR_NOOP("Bluetooth and devices"), "printers"],
    "kcm_networkmanagement": [QT_TR_NOOP("Network and internet"), QT_TR_NOOP("Settings"), "network"],
    "kcm_proxy": [QT_TR_NOOP("Proxy"), QT_TR_NOOP("Network and internet"), "networkadvanced"],
    "kcm_wallpaper": [QT_TR_NOOP("Background"), QT_TR_NOOP("Personalisation"), "background"],
    "kcm_lookandfeel": [QT_TR_NOOP("Themes"), QT_TR_NOOP("Personalisation"), "personalisation"],
    "kcm_colors": [QT_TR_NOOP("Colours"), QT_TR_NOOP("Personalisation"), "colours"],
    "kcm_fonts": [QT_TR_NOOP("Fonts"), QT_TR_NOOP("Personalisation"), "fonts"],
    "kcm_cursortheme": [QT_TR_NOOP("Mouse pointer"), QT_TR_NOOP("Accessibility"), "accessibility"],
    "kcm_access": [QT_TR_NOOP("Accessibility"), QT_TR_NOOP("Settings"), "accessibility"],
    "kcm_virtualkeyboard": [QT_TR_NOOP("On-screen keyboard"), QT_TR_NOOP("Accessibility"), "accessibility"],
    "kcm_screenlocker": [QT_TR_NOOP("Lock screen"), QT_TR_NOOP("Personalisation"), "lockscreen"],
    "kcm_componentchooser": [QT_TR_NOOP("Default apps"), QT_TR_NOOP("Apps"), "defaultapps"],
    "kcm_autostart": [QT_TR_NOOP("Startup apps"), QT_TR_NOOP("Apps"), "startup"],
    "kcm_users": [QT_TR_NOOP("Your info"), QT_TR_NOOP("Accounts"), "users"],
    "kcm_regionandlang": [QT_TR_NOOP("Language and region"), QT_TR_NOOP("Time and language"), "language"],
    "kcm_clock": [QT_TR_NOOP("Date and time"), QT_TR_NOOP("Time and language"), "datetime"],
    "kcm_flatpak": [QT_TR_NOOP("App permissions"), QT_TR_NOOP("Privacy and security"), "apppermissions"],
    "kcm_app-permissions": [QT_TR_NOOP("App permissions"), QT_TR_NOOP("Privacy and security"), "apppermissions"],
    "kcm_kwin_virtualdesktops": [QT_TR_NOOP("Desktops"), QT_TR_NOOP("System › Multitasking"), "desktops"],
    "kcm_device_automounter": [QT_TR_NOOP("AutoPlay"), QT_TR_NOOP("Bluetooth and devices"), "autoplay"],
    "kcm_gamecontroller": [QT_TR_NOOP("Game controllers"), QT_TR_NOOP("Bluetooth and devices"), "gamecontrollers"],
    "kcm_tablet": [QT_TR_NOOP("Pen and tablet"), QT_TR_NOOP("Bluetooth and devices"), "pen"],
    "kcm_touchscreen": [QT_TR_NOOP("Touchscreen"), QT_TR_NOOP("Bluetooth and devices"), "touchscreen"],
    "kcm_bolt": [QT_TR_NOOP("Thunderbolt"), QT_TR_NOOP("Bluetooth and devices"), "thunderbolt"],
    "kcm_fontinst": [QT_TR_NOOP("Fonts"), QT_TR_NOOP("Personalisation"), "fonts"],
    "kcmspellchecking": [QT_TR_NOOP("Typing"), QT_TR_NOOP("Time and language"), "typing"],
    "kcm_filetypes": [QT_TR_NOOP("Default apps by file type"), QT_TR_NOOP("Apps › Default apps"), "filetypes"],
    "kcm_smserver": [QT_TR_NOOP("Sign-in options"), QT_TR_NOOP("Accounts"), "signin"],
    "kcm_baloofile": [QT_TR_NOOP("Searching files"), QT_TR_NOOP("Privacy and security"), "searching"],
    "kcm_recentFiles": [QT_TR_NOOP("Activity history"), QT_TR_NOOP("Privacy and security"), "activityhistory"],
    "kcm_kwinxwayland": [QT_TR_NOOP("Old app compatibility"), QT_TR_NOOP("Apps"), "oldapps"],
    "kcm_access": [QT_TR_NOOP("Accessibility"), QT_TR_NOOP("Settings"), "accessibility"],
    "kcm_virtualkeyboard": [QT_TR_NOOP("On-screen keyboard"), QT_TR_NOOP("Accessibility"), "accessibility"],
};

// Wild OS Settings pages with no KDE module behind them, which the search
// above can never return, matched here on their own words.
const wildosPages = [
    { page: "history", title: QT_TR_NOOP("History"), category: QT_TR_NOOP("System"),
      keywords: QT_TR_NOOP("history event viewer events event log logs what happened reliability crash crashes closed unexpectedly problems errors sign in sign-ins signed in failed password wrong password fingerprint shut down shutdown restart restarted sleep woke lid start-up startup boot usb drive firewall blocked updates installed restore point backup") },
    { page: "hardware", title: QT_TR_NOOP("Devices"), category: QT_TR_NOOP("System"),
      keywords: QT_TR_NOOP("device manager devices hardware drivers driver missing driver unknown device firmware graphics card gpu wifi adapter network adapter sound card webcam camera fingerprint reader touchpad battery health disk health drive health smart usb device pci") },
    { page: "taskbar", title: QT_TR_NOOP("Taskbar"), category: QT_TR_NOOP("Personalisation"),
      keywords: QT_TR_NOOP("taskbar panel auto hide autohide clock seconds full date battery percentage show desktop peek end task kill force close force quit not responding frozen hung windows mode status") },
    { page: "shortcuts", title: QT_TR_NOOP("Keyboard shortcuts"), category: QT_TR_NOOP("Bluetooth and devices"),
      keywords: QT_TR_NOOP("keyboard shortcuts shortcut hotkey hotkeys hot key key binding keybinding key combination global shortcuts custom shortcut change shortcut windows key win key super open app with keys run command") },
    { page: "firewall", title: QT_TR_NOOP("Firewall"), category: QT_TR_NOOP("Privacy and security"),
      keywords: QT_TR_NOOP("firewall windows defender firewall network protection public network home network allow app allow port open port blocked connections incoming connections network discovery file sharing remote desktop rdp ssh") },
    { page: "advancedprotection", title: QT_TR_NOOP("Advanced protection"), category: QT_TR_NOOP("Privacy and security"),
      keywords: QT_TR_NOOP("advanced protection extra protection hardening locked usb devices quiet lock screen fingerprint password restart when locked wi-fi bluetooth switch off encrypted dns dns over tls quad9 mullvad cloudflare vpn only kill switch clipboard passwords presenting mode screen sharing personal details metadata password-locked usb sticks encrypt drive guest session safe box panic key reset to defaults") },
];

// Pages from wildosPages for a query: every word typed starts a word of
// the page's title or keywords, in English or the display language; named
// when they all start its title.
function wildosPageMatches(query) {
    const words = String(query ?? "").toLowerCase().trim().split(/\s+/).filter(w => w.length > 0);
    if (words.length === 0 || words.join(" ").length < 2) {
        return [];
    }
    const pages = wildosPages.map(p => ({
        page: p.page,
        title: qsTr(p.title),
        category: qsTr(p.category),
        titles: " " + (p.title + " " + qsTr(p.title)).toLowerCase(),
        hay: " " + [p.title, p.keywords, qsTr(p.title), qsTr(p.keywords)].join(" ").toLowerCase(),
    }));
    const found = pages.filter(p => words.every(w => p.hay.includes(" " + w)));
    const titled = p => words.every(w => p.titles.includes(" " + w));
    return found.map(p => ({ page: p.page, title: p.title, category: p.category, named: titled(p) }));
}

function settingsPage(url) {
    const text = String(url ?? "");
    const id = text.substring(text.lastIndexOf("/") + 1).replace(/\.desktop$/, "");
    const entry = settingsPages[id];
    return entry ? { title: qsTr(entry[0]), category: qsTr(entry[1]), page: entry[2], kcm: id } : null;
}

// Single quotes for a POSIX shell argument.
function shellQuote(text) {
    return "'" + String(text).replace(/'/g, "'\\''") + "'";
}

function storageId(favoriteId) {
    if (!favoriteId) {
        return "";
    }
    let id = String(favoriteId);
    if (id.startsWith("applications:")) {
        id = id.substring("applications:".length);
    }
    if (id.endsWith(".desktop")) {
        id = id.substring(0, id.length - ".desktop".length);
    }
    return id;
}

// The taskbar launcher URL for an app in Start. Kicker gives an app's id
// bare in All apps ("org.wildos.report.desktop") but already prefixed in
// search results and the pinned grid ("applications:org.wildos.report.desktop"),
// so the prefix is put back on the bare id rather than added to whatever
// came. An app known only by its file keeps a file URL.
function taskbarUrl(favoriteId) {
    const id = String(favoriteId ?? "");
    if (id.startsWith("file:")) {
        return id;
    }
    if (id.startsWith("/")) {
        return "file://" + encodeURI(id);
    }
    return "applications:" + storageId(id) + ".desktop";
}

function appName(favoriteId, display) {
    const id = storageId(favoriteId);
    return id in friendlyNames ? qsTr(friendlyNames[id]) : (display ?? "");
}

// Wild OS hides file extensions in search and recents.
function withoutExtension(name) {
    const text = String(name ?? "");
    const dot = text.lastIndexOf(".");
    if (dot <= 0 || text.length - dot > 6) {
        return text;
    }
    return text.substring(0, dot);
}

function localPath(url) {
    const text = String(url ?? "");
    if (!text.startsWith("file://")) {
        return "";
    }
    return decodeURIComponent(text.substring("file://".length));
}

// "/home/alex/Documents/Invoices/x.pdf" becomes "Documents › Invoices".
function folderTrail(url, homePath) {
    let path = localPath(url);
    if (path.length === 0) {
        return "";
    }
    if (path.endsWith("/")) {
        path = path.substring(0, path.length - 1);
    }
    const parent = path.substring(0, path.lastIndexOf("/"));
    const home = String(homePath ?? "").replace(/\/$/, "");
    let parts;
    if (home.length > 0 && (parent === home || parent.startsWith(home + "/"))) {
        parts = [qsTr("Home")].concat(parent.substring(home.length).split("/").filter(p => p.length > 0));
        if (parts.length > 1) {
            parts.shift();
        }
    } else {
        parts = parent.split("/").filter(p => p.length > 0);
        if (parts.length === 0) {
            return qsTr("Computer");
        }
    }
    if (parts.length > 3) {
        parts = parts.slice(parts.length - 3);
    }
    return parts.join(" › ");
}

const imageExtensions = ["jpg", "jpeg", "png", "webp", "gif", "bmp", "avif", "heic", "tif", "tiff"];

function isImage(url) {
    const path = localPath(url).toLowerCase();
    const dot = path.lastIndexOf(".");
    return dot > 0 && imageExtensions.includes(path.substring(dot + 1));
}

// KDE ships some action labels in title case. The design system wants sentence
// case, so known labels are rewritten and common words lowered in the rest,
// leaving names such as "Brave" alone.
const actionLabels = {
    "openParentFolder": QT_TR_NOOP("Open file location"),
    "_kicker_fileItem_properties": QT_TR_NOOP("Properties"),
    "forget": QT_TR_NOOP("Remove from list"),
    "forgetAll": QT_TR_NOOP("Clear this list"),
    "addToTaskManager": QT_TR_NOOP("Pin to taskbar"),
    "addToDesktop": QT_TR_NOOP("Add to desktop"),
    "addToPanel": QT_TR_NOOP("Add to panel"),
};

const lowerWords = ["a", "an", "and", "all", "as", "containing", "copy", "document", "documents", "edit", "file", "files", "folder", "for", "from", "history", "in", "incognito", "location", "mode", "new", "of", "on", "open", "private", "recent", "remove", "run", "settings", "show", "tab", "terminal", "the", "this", "to", "window", "with"];

function sentenceCase(text) {
    const words = String(text ?? "").replace(/…$/, "").split(" ");
    return words.map((word, i) => {
        if (i === 0) {
            return word;
        }
        return lowerWords.includes(word.toLowerCase()) ? word.toLowerCase() : word;
    }).join(" ");
}

function actionLabel(action) {
    return action.actionId in actionLabels ? qsTr(actionLabels[action.actionId]) : sentenceCase(action.text);
}

function groupThousands(plain) {
    if (plain.includes("e")) {
        return plain;
    }
    const negative = plain.startsWith("-");
    const [whole, fraction] = (negative ? plain.substring(1) : plain).split(".");
    const grouped = whole.replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    return (negative ? "-" : "") + grouped + (fraction ? "." + fraction : "");
}

// A small calculator that runs entirely here. Plasma's calculator runner can
// download exchange rates, and search must never reach the network.
function calculate(input) {
    let text = String(input ?? "").trim();
    if (text.startsWith("=")) {
        text = text.substring(1);
    }
    text = text.replace(/×/g, "*").replace(/÷/g, "/").replace(/,/g, "").replace(/\s+/g, "");
    if (!/^[0-9+\-*\/^%().]+$/.test(text) || !/[0-9]/.test(text) || !/[+\-*\/^%]/.test(text.replace(/^[-+]/, ""))) {
        return null;
    }

    let pos = 0;
    const peek = () => text[pos];
    const fail = () => { throw new Error("syntax"); };

    function number() {
        const start = pos;
        while (pos < text.length && /[0-9.]/.test(text[pos])) {
            pos++;
        }
        const slice = text.substring(start, pos);
        if (slice.length === 0 || (slice.match(/\./g) || []).length > 1) {
            fail();
        }
        return parseFloat(slice);
    }
    function primary() {
        if (peek() === "(") {
            pos++;
            const value = sum();
            if (peek() !== ")") {
                fail();
            }
            pos++;
            return value;
        }
        if (peek() === "-") {
            pos++;
            return -primary();
        }
        if (peek() === "+") {
            pos++;
            return primary();
        }
        return number();
    }
    function postfix() {
        let value = primary();
        while (peek() === "%") {
            pos++;
            value = value / 100;
        }
        return value;
    }
    function power() {
        const base = postfix();
        if (peek() === "^") {
            pos++;
            return Math.pow(base, power());
        }
        return base;
    }
    function product() {
        let value = power();
        while (peek() === "*" || peek() === "/") {
            const op = text[pos++];
            const rhs = power();
            value = op === "*" ? value * rhs : value / rhs;
        }
        return value;
    }
    function sum() {
        let value = product();
        while (peek() === "+" || peek() === "-") {
            const op = text[pos++];
            const rhs = product();
            value = op === "+" ? value + rhs : value - rhs;
        }
        return value;
    }

    try {
        const value = sum();
        if (pos !== text.length || !isFinite(value)) {
            return null;
        }
        const plain = String(parseFloat(value.toPrecision(12)));
        return {
            expression: String(input).trim().replace(/^=/, "").trim(),
            value: groupThousands(plain),
            plain: plain,
        };
    } catch (e) {
        return null;
    }
}
