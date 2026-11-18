pragma Singleton

import QtQuick
import WildOS.Settings

// Every page Settings can show, under the names Windows 11 uses. A page is
// either a Wild OS page ("component"), a KDE module shown inside the window
// ("kcm") or a KDE module that is still built from widgets and opens in a
// window of its own ("window"). KDE-only modules such as Plasma Search,
// User Feedback or Desktop Effects are deliberately absent, so neither the
// navigation nor search can reach them.
//
// Keywords are what people type, not what the page calls things: Windows'
// names ("control panel", "night light", "default apps"), both spellings
// where they differ only by a hyphen ("wifi", "wi-fi") and plain words for
// what each page has on it.
QtObject {
    id: pages

    readonly property var navigation: ["home", "system", "devices", "network", "personalisation", "apps",
        "windowsapps", "accounts", "time", "accessibility", "privacy", "update"]

    readonly property var all: ({
        "home": { title: QT_TR_NOOP("Home"), icon: "home", component: "HomePage",
            keywords: "start overview recommended welcome control panel all settings" },

        "system": { title: QT_TR_NOOP("System"), icon: "system", component: "CategoryPage",
            children: ["display", "sound", "notifications", "power", "storage", "hardware", "multitasking", "startupdisk", "recovery", "history", "about"],
            keywords: "system computer pc" },
        "display": { title: QT_TR_NOOP("Display"), parent: "system", icon: "display", component: "DisplayPage", fallback: "kcm_kscreen",
            description: QT_TR_NOOP("Monitors, brightness, resolution and scale"),
            keywords: "screen screens monitor monitors resolution scale scaling zoom dpi hidpi 4k brightness dim refresh rate hz 60hz 144hz orientation rotate portrait landscape hdr second screen external monitor multiple monitors dual monitors projector tv arrange identify detect extend duplicate mirror main display primary variable refresh vrr freesync gsync" },
        "graphicsdriver": { title: QT_TR_NOOP("Graphics driver"), parent: "display", icon: "chip", component: "GraphicsDriverPage",
            description: QT_TR_NOOP("Open driver or NVIDIA's own"),
            keywords: "graphics card video card gpu driver drivers nvidia geforce rtx proprietary open source nouveau nvk mesa cuda gaming" },
        "nightlight": { title: QT_TR_NOOP("Night light"), parent: "display", icon: "moon", component: "NightLightPage", fallback: "kcm_nightlight",
            description: QT_TR_NOOP("Warmer colours in the evening"),
            keywords: "night mode night shift blue light filter warm warmer colour color temperature evening sleep eye strain reading schedule sunset sunrise strength redshift flux" },
        "sound": { title: QT_TR_NOOP("Sound"), parent: "system", icon: "sound", component: "SoundPage", fallback: "kcm_pulseaudio",
            description: QT_TR_NOOP("Volume, output and input devices"),
            keywords: "volume audio speaker speakers headphones headset earphones microphone mic input output device mute unmute loud quiet balance mixer volume mixer app volume per app system sounds notification sounds windows apps hdmi bluetooth sound scheme sound theme startup sound start-up sound login sound beeps test" },
        "notifications": { title: QT_TR_NOOP("Notifications"), parent: "system", icon: "bell", component: "NotificationsPage", fallback: "kcm_notifications",
            description: QT_TR_NOOP("Alerts from apps and Wild OS, do not disturb"),
            keywords: "alerts do not disturb dnd focus focus assist quiet hours urgent banners popups toasts badges notification centre notification sounds how long" },
        "power": { title: QT_TR_NOOP("Power and battery"), parent: "system", icon: "battery", component: "PowerPage", fallback: "kcm_powerdevilprofilesconfig",
            description: QT_TR_NOOP("Sleep, screen off and battery saving"),
            keywords: "battery sleep suspend hibernate lid screen off turn off screen screen timeout idle energy power mode power plan saver battery saver energy saver performance best performance balanced battery life charging processor threads smt hyperthreading" },
        "storage": { title: QT_TR_NOOP("Storage"), parent: "system", icon: "storage", component: "StoragePage",
            description: QT_TR_NOOP("Disk space and the Recycle Bin"),
            keywords: "disk disks space drive drives free space free up space full bin trash recycle recycle bin empty hard drive hard disk ssd clean up cleanup disk cleanup storage sense volumes partitions usage" },
        "freespace": { title: QT_TR_NOOP("Free up space"), parent: "storage", icon: "storage", component: "FreeSpacePage",
            description: QT_TR_NOOP("Clear what Wild OS and your apps no longer need"),
            keywords: "free up space clean up cleanup disk cleanup storage sense disk full low disk space cache caches app caches temporary files temp files thumbnails recycle bin trash old restore points update files downloaded updates apt cache old kernels flatpak unused runtimes leftovers removed apps windows mode disk compact shrink large files downloads tidy up automatically" },
        "recovery": { title: QT_TR_NOOP("Recovery"), parent: "system", icon: "restore", component: "RecoveryPage",
            description: QT_TR_NOOP("Restore points, reset and start-up options"),
            keywords: "recovery restore restore point restore points system restore go back roll back rollback undo update earlier version previous version snapshot snapshots reset reset this pc factory reset reinstall fresh start start-up options startup options advanced startup safe mode repair boot shift restart backup" },
        // Only on a PC with wildos-startup (exists).
        "startupdisk": { title: QT_TR_NOOP("Start-up disk"), parent: "system", icon: "drive", component: "StartupDiskPage",
            description: QT_TR_NOOP("What this PC starts by default, and restarting into Windows"),
            keywords: "startup disk start-up disk start up boot boot disk boot order boot menu boot manager start-up manager startup manager dual boot dual-boot multi boot grub default operating system default os choose operating system other system another system windows restart into windows boot into windows linux usb stick boot from usb hold space countdown timeout bios uefi firmware firmware settings boot password" },
        "history": { title: QT_TR_NOOP("History"), parent: "system", icon: "clock", component: "HistoryPage",
            description: QT_TR_NOOP("What happened on this PC, in plain words"),
            keywords: "history event viewer events event log log logs reliability monitor reliability history what happened crash crashes crashed closed unexpectedly app crash problems errors error warnings sign in sign-ins signed in logon login failed password wrong password usb drive removed firewall blocked updates installed update history restore point backup installed apps removed apps audit security log journal" },
        // Device Manager. Its id isn't "devices", which is Bluetooth and devices.
        "hardware": { title: QT_TR_NOOP("Devices"), parent: "system", icon: "chip", component: "DevicesPage",
            description: QT_TR_NOOP("Every device in this PC, its driver and what needs you"),
            keywords: "device manager devices hardware drivers driver missing driver no driver unknown device yellow exclamation firmware missing firmware not working graphics card video card gpu wifi wi-fi adapter network adapter ethernet bluetooth sound card audio speakers microphone webcam camera fingerprint reader keyboard touchpad mouse touchscreen pen tablet game controller battery health drive health disk health smart ssd hard drive nvme storage printer scanner usb device pci model check again rescan scan for hardware changes" },
        "multitasking": { title: QT_TR_NOOP("Multitasking"), parent: "system", icon: "multitasking", component: "MultitaskingPage",
            description: QT_TR_NOOP("Snap windows, desktops and Alt+Tab"),
            keywords: "snap layouts snap assist snap windows window snapping suggest fill alt tab task switcher windows side by side split screen taskbar all desktops open windows screen corners hot corners corner actions mouse corner hold key modifier quick note keep awake caffeine do not disturb turn screen off app grid title bar double click maximise minimise task view" },
        "appnotifications": { title: QT_TR_NOOP("Notifications from apps"), parent: "notifications", icon: "bell", component: "AppNotificationsPage",
            description: QT_TR_NOOP("Choose which apps can notify you"),
            keywords: "app notifications notify sounds popups pop ups banners history notification list per app senders turn off mute" },
        "poweradvanced": { title: QT_TR_NOOP("More power settings"), parent: "power", icon: "battery", component: "PowerAdvancedPage",
            description: QT_TR_NOOP("Lid, power button and dimming"),
            keywords: "lid close closing laptop power button press power options dim dimming screen brightness battery critical low level critical battery hibernate shut down external monitor" },
        "desktops": { title: QT_TR_NOOP("Desktops"), parent: "multitasking", icon: "desktops", component: "DesktopsPage",
            description: QT_TR_NOOP("Extra desktops for separate sets of windows"),
            keywords: "virtual desktops multiple desktops workspaces add desktop new desktop rename remove switch task view" },
        "about": { title: QT_TR_NOOP("About"), parent: "system", icon: "info", component: "AboutPage",
            description: QT_TR_NOOP("Device specifications, rename this PC"),
            keywords: "device name rename pc computer name hostname specifications specs system information system info pc info processor cpu memory ram graphics gpu model version build operating system os wild os secure boot" },

        "devices": { title: QT_TR_NOOP("Bluetooth and devices"), icon: "bluetooth", component: "CategoryPage",
            children: ["bluetooth", "mouse", "touchpad", "touchscreen", "pen", "keyboard", "printers", "gamecontrollers", "autoplay", "thunderbolt"],
            keywords: "devices peripherals hardware usb" },
        "bluetooth": { title: QT_TR_NOOP("Bluetooth"), parent: "devices", icon: "bluetooth", component: "BluetoothPage", fallback: "kcm_bluetooth",
            description: QT_TR_NOOP("Pair headphones, speakers and other devices"),
            keywords: "pair pairing headphones speakers earbuds airpods wireless bluetooth devices add device remove device connect disconnect controller" },
        "mouse": { title: QT_TR_NOOP("Mouse"), parent: "devices", icon: "mouse", component: "MousePage", fallback: "kcm_mouse",
            description: QT_TR_NOOP("Pointer speed, buttons and scrolling"),
            keywords: "pointer cursor speed mouse speed sensitivity dpi buttons primary button left handed right handed acceleration enhance pointer precision scroll wheel scrolling lines reverse scrolling natural scrolling" },
        "touchpad": { title: QT_TR_NOOP("Touchpad"), parent: "devices", icon: "touchpad", component: "TouchpadPage", fallback: "kcm_touchpad",
            description: QT_TR_NOOP("Taps, scrolling and gestures"),
            keywords: "trackpad touch pad tap tap to click gestures scrolling natural scrolling reverse scrolling two finger cursor speed while typing palm laptop" },
        "touchpadgestures": { title: QT_TR_NOOP("Gestures"), parent: "touchpad", icon: "touchpad", component: "TouchpadGesturesPage",
            description: QT_TR_NOOP("Swipes with two, three or four fingers"),
            keywords: "touchpad trackpad gestures swipe two fingers three fingers four fingers multi finger task view show desktop switch desktops app grid all apps notifications back forward pinch zoom rotate three finger drag mac" },
        "keyboard": { title: QT_TR_NOOP("Keyboard"), parent: "devices", icon: "keyboard", component: "KeyboardPage", fallback: "kcm_keyboard",
            description: QT_TR_NOOP("Layouts, key repeat and Num Lock"),
            keywords: "keyboard layout layouts language input method typing input repeat key repeat delay rate num lock caps lock switch layout switch language" },
        "shortcuts": { title: QT_TR_NOOP("Keyboard shortcuts"), parent: "keyboard", icon: "keyboard", component: "ShortcutsPage",
            description: QT_TR_NOOP("See and change shortcuts, and add your own"),
            keywords: "keyboard shortcuts shortcut hotkey hotkeys hot key key binding keybinding key combination global shortcuts custom shortcut change shortcut windows key win key super open app with keys run command screenshot key" },
        "keyboardadvanced": { title: QT_TR_NOOP("More keyboard options"), parent: "keyboard", icon: "keyboard", component: "KeyboardAdvancedPage",
            description: QT_TR_NOOP("Keyboard model, compose key and Alt Gr"),
            keywords: "compose key keyboard model alt gr special characters accents third level xkb options menu key print screen scroll lock right ctrl" },
        "touchscreen": { title: QT_TR_NOOP("Touchscreen"), parent: "devices", icon: "display", component: "TouchscreenPage",
            description: QT_TR_NOOP("Turn touch on or off, choose its screen"),
            keywords: "touch screen touchscreen touch input finger tap map screen" },
        "pen": { title: QT_TR_NOOP("Pen and tablet"), parent: "devices", icon: "pencil", component: "PenPage",
            description: QT_TR_NOOP("Drawing tablets and pens, left-handed use"),
            keywords: "pen stylus tablet drawing tablet graphics tablet wacom left handed right handed map screen windows ink" },
        "gamecontrollers": { title: QT_TR_NOOP("Game controllers"), parent: "devices", icon: "games", component: "GameControllersPage",
            description: QT_TR_NOOP("Gamepads and joysticks, test buttons and sticks"),
            keywords: "game controller gamepad joystick xbox playstation dualshock dualsense steam controller test buttons sticks" },
        "autoplay": { title: QT_TR_NOOP("AutoPlay"), parent: "devices", icon: "play", component: "AutoPlayPage",
            description: QT_TR_NOOP("What happens when you plug in a drive or memory card"),
            keywords: "autoplay auto play usb stick usb drive flash drive memory card sd card removable drive external drive mount automount" },
        "thunderbolt": { title: QT_TR_NOOP("Thunderbolt"), parent: "devices", icon: "bolt", component: "ThunderboltPage",
            description: QT_TR_NOOP("Allow docks and other Thunderbolt devices"),
            keywords: "thunderbolt usb4 usb c dock docking station authorise authorize allow" },
        "printers": { title: QT_TR_NOOP("Printers and scanners"), parent: "devices", icon: "printer", component: "PrintersPage", fallback: "kcm_printer_manager",
            description: QT_TR_NOOP("Add and manage printers"),
            keywords: "printer printers print printing scanner scanners scan add printer remove printer default printer set as default test page print queue documents waiting paused" },
        "printersadvanced": { title: QT_TR_NOOP("More printer settings"), parent: "printers", icon: "printer", component: "PrintersAdvancedPage",
            description: QT_TR_NOOP("Printers that need a driver, USB printers and print queues"),
            keywords: "printer driver usb printer network printer ip address print queue pause printing cancel document paper size a4 letter two-sided duplex colour color black and white ppd ipp lpd printing preferences" },

        "network": { title: QT_TR_NOOP("Network and internet"), icon: "wifi", component: "NetworkPage",
            keywords: "wifi wi-fi wireless internet ethernet cable lan network networks connect connection disconnect password wifi password airplane flight mode hotspot online offline" },
        "networkadvanced": { title: QT_TR_NOOP("Advanced network settings"), parent: "network", icon: "globe", component: "NetworkAdvancedPage",
            fallback: "kcm_networkmanagement",
            description: QT_TR_NOOP("VPN, proxy, IP addresses and saved networks"),
            keywords: "vpn wireguard openvpn kill switch proxy dns encrypted dns over tls quad9 cloudflare mullvad ip address ipv4 ipv6 static ip gateway subnet mask mac address hardware address random private metered advanced connections saved networks saved wifi wi-fi forget adapter" },
        // KDE's editor for what Wild OS has no page for: work Wi-Fi that
        // signs in with a username (802.1X), other kinds of VPN, bridges.
        // Search finds it.
        "networkconnections": { title: QT_TR_NOOP("All connection settings"), parent: "networkadvanced", icon: "globe", window: "kcm_networkmanagement",
            description: QT_TR_NOOP("Every setting of every connection, in KDE's editor"),
            keywords: "802.1x eap enterprise wpa2 enterprise eduroam certificate bond bridge vlan team openconnect anyconnect l2tp pptp ipsec" },

        "personalisation": { title: QT_TR_NOOP("Personalisation"), icon: "palette", component: "PersonalisationPage",
            children: ["themes", "background", "colours", "lockscreen", "taskbar", "fonts"],
            keywords: "personalize personalise customise customize theme look appearance light dark dark mode light mode dark theme light theme night mode wallpaper background slideshow accent colour color transparency transparent glass blur taskbar layout taskbar style centred left aligned floating docked font fonts text size bigger widget widgets desktop widgets gadgets" },
        "themes": { title: QT_TR_NOOP("Themes"), parent: "personalisation", icon: "brush", component: "ThemesPage",
            description: QT_TR_NOOP("Wallpaper, light or dark, accent and glass in one go"),
            keywords: "theme themes look style appearance dawn orchard graphite overclock ember estuary gaming neon glass tint rgb lighting underglow glow keyboard lighting mouse lighting openrgb accent custom colour" },
        "background": { title: QT_TR_NOOP("Background"), parent: "personalisation", icon: "image", component: "BackgroundPage",
            description: QT_TR_NOOP("Pictures and your own photos"),
            keywords: "wallpaper background desktop background desktop picture photo image change wallpaper set wallpaper" },
        "colours": { title: QT_TR_NOOP("Colours"), parent: "personalisation", icon: "drop", component: "ColoursPage",
            description: QT_TR_NOOP("Accent colour"),
            keywords: "accent colour color colors highlight theme custom colour" },
        "lockscreen": { title: QT_TR_NOOP("Lock screen"), parent: "personalisation", icon: "lock", component: "LockScreenPage",
            description: QT_TR_NOOP("Lock screen picture and screen lock"),
            keywords: "lock screen picture wallpaper background login screen sign in screen" },
        "taskbar": { title: QT_TR_NOOP("Taskbar"), parent: "personalisation", icon: "taskbar", component: "TaskbarPage",
            description: QT_TR_NOOP("Hiding the taskbar, End task, Windows mode status"),
            keywords: "taskbar panel auto hide autohide hide taskbar clock seconds full date battery percentage show desktop peek end task kill force close force quit not responding frozen hung windows mode status virtual machine widget widgets network speed traffic monitor plasmoid unlock taskbar" },
        "fonts": { title: QT_TR_NOOP("Fonts"), parent: "personalisation", icon: "font", component: "FontsPage",
            description: QT_TR_NOOP("Install and remove fonts"),
            keywords: "font fonts typeface install font remove font ttf otf typography" },

        "apps": { title: QT_TR_NOOP("Apps"), icon: "apps", component: "CategoryPage",
            children: ["installedapps", "defaultapps", "startup", "oldapps"],
            keywords: "apps programs applications software" },
        "installedapps": { title: QT_TR_NOOP("Installed apps"), parent: "apps", icon: "list", component: "InstalledAppsPage",
            description: QT_TR_NOOP("Uninstall apps"),
            keywords: "uninstall remove delete programs apps installed software add or remove programs and features apps and features control panel" },
        "defaultapps": { title: QT_TR_NOOP("Default apps"), parent: "apps", icon: "default-apps", component: "DefaultAppsPage", fallback: "kcm_componentchooser",
            description: QT_TR_NOOP("Browser, email and other default apps"),
            keywords: "default browser default email default programs mail web browser pdf music video player open with file associations" },
        "filetypes": { title: QT_TR_NOOP("Choose defaults by file type"), parent: "defaultapps", icon: "document", component: "FileTypesPage",
            description: QT_TR_NOOP("Pick the app for .pdf, .docx and every other kind of file"),
            keywords: "file type file types extension extensions mime associations open with pdf docx" },
        "startup": { title: QT_TR_NOOP("Startup"), parent: "apps", icon: "startup", component: "StartupPage", fallback: "kcm_autostart",
            description: QT_TR_NOOP("Apps that start when you sign in"),
            keywords: "startup start up startup apps autostart run at startup launch login sign in boot" },
        "oldapps": { title: QT_TR_NOOP("Old app compatibility"), parent: "apps", icon: "app", component: "OldAppCompatibilityPage",
            description: QT_TR_NOOP("What older apps can see you type in other apps"),
            keywords: "x11 xwayland legacy older apps keyboard shortcuts push to talk eavesdrop global shortcuts" },

        "windowsapps": { title: QT_TR_NOOP("Windows apps"), icon: "windowsapps", component: "WindowsAppsPage",
            keywords: "windows exe msi programs wine run windows programs compatibility sandbox" },
        "windowsmode": { title: QT_TR_NOOP("Windows mode"), parent: "windowsapps", icon: "windowsapps", component: "WindowsModePage",
            description: QT_TR_NOOP("Real Windows in the background, for Office and other programs"),
            keywords: "windows mode virtual machine vm office word excel licence product key activation remoteapp memory repair" },

        "accounts": { title: QT_TR_NOOP("Accounts"), icon: "account", component: "AccountsPage",
            keywords: "account accounts user users profile" },
        "users": { title: QT_TR_NOOP("Your info"), parent: "accounts", icon: "account", component: "YourInfoPage", fallback: "kcm_users",
            description: QT_TR_NOOP("Your name and picture"),
            keywords: "user picture avatar photo name change name profile account picture" },
        "signin": { title: QT_TR_NOOP("Sign-in options"), parent: "accounts", icon: "lock", component: "SignInPage",
            description: QT_TR_NOOP("Password, automatic sign-in and restarting apps"),
            keywords: "password change password hint login automatic sign in auto login autologin saved passwords wallet keyring restart apps restore session reopen restartable" },
        "otherusers": { title: QT_TR_NOOP("Other users"), parent: "accounts", icon: "people", component: "OtherUsersPage",
            description: QT_TR_NOOP("Add and remove the people who share this computer"),
            keywords: "add user new user other users family administrator admin standard user account type remove account delete account" },

        "time": { title: QT_TR_NOOP("Time and language"), icon: "clock", component: "TimePage",
            keywords: "time date clock language region" },
        "datetime": { title: QT_TR_NOOP("Date and time"), parent: "time", icon: "clock", component: "DateTimePage", fallback: "kcm_clock",
            description: QT_TR_NOOP("Change the date, time, time zone and clock"),
            keywords: "date time zone timezone clock set time automatically sync wrong time 12 hour 24 hour am pm" },
        "language": { title: QT_TR_NOOP("Language and region"), parent: "time", icon: "language", component: "LanguagePage", fallback: "kcm_regionandlang",
            description: QT_TR_NOOP("Display language, formats and region"),
            keywords: "language display language region format formats regional format currency locale country" },
        "formatsadvanced": { title: QT_TR_NOOP("More format options"), parent: "language", icon: "language", component: "FormatsPage",
            description: QT_TR_NOOP("Numbers, time, money and measurements separately"),
            keywords: "number format currency format date format time format date order 12 hour 24 hour clock am pm first day of the week measurement units metric imperial paper size a4 letter file sizes" },
        "typing": { title: QT_TR_NOOP("Typing"), parent: "time", icon: "pencil", component: "TypingPage",
            description: QT_TR_NOOP("Spell checking and your dictionary"),
            keywords: "typing spelling spell check spellcheck spelling language dictionary misspelt misspelled words" },

        "accessibility": { title: QT_TR_NOOP("Accessibility"), icon: "accessibility", component: "AccessibilityPage", fallback: "kcm_access",
            keywords: "accessibility ease of access text size bigger larger pointer cursor mouse pointer colour color size magnifier magnify zoom screen reader orca narrator read aloud high contrast flash visual alerts bell deaf hearing mono audio on-screen keyboard virtual keyboard touch keyboard sticky keys slow keys bounce keys filter keys outline windows window border focus ring focused window blur background windows inactive windows dim low vision stronger text glass transparency readable contrast" },
        "slowbouncekeys": { title: QT_TR_NOOP("Slow and bounce keys"), parent: "accessibility", icon: "keyboard", component: "SlowBounceKeysPage",
            description: QT_TR_NOOP("Ignore quick or repeated presses"),
            keywords: "slow keys bounce keys filter keys repeated presses double letters tremor hold key delay" },

        "privacy": { title: QT_TR_NOOP("Privacy and security"), icon: "shield", component: "PrivacyPage",
            keywords: "security privacy protection protection level encryption firewall secure boot sandbox usb block usb usage data telemetry diagnostics windows security steam 32-bit 32 bit ia32 i386 games old apps compatibility controller controllers gamepad joystick steam input remote play performance overlay fps frame rate frametime mangohud processor cpu extra processor protection spectre meltdown mitigations hyperthreading smt threads" },
        "apppermissions": { title: QT_TR_NOOP("App permissions"), parent: "privacy", icon: "sandbox", component: "AppPermissionsPage", fallback: "kcm_flatpak",
            description: QT_TR_NOOP("What apps from the Store can reach"),
            keywords: "permissions sandbox app access store apps flatpak" },
        "camera": { title: QT_TR_NOOP("Camera"), parent: "apppermissions", icon: "camera", component: "AppAccessPage",
            description: QT_TR_NOOP("Apps that can use your camera"),
            keywords: "camera webcam video call" },
        "microphone": { title: QT_TR_NOOP("Microphone"), parent: "apppermissions", icon: "mic", component: "AppAccessPage",
            description: QT_TR_NOOP("Apps that can use your microphone"),
            keywords: "microphone mic recording voice" },
        "location": { title: QT_TR_NOOP("Location"), parent: "apppermissions", icon: "location", component: "AppAccessPage",
            description: QT_TR_NOOP("Apps that can find where you are"),
            keywords: "location gps where position" },
        "appfiles": { title: QT_TR_NOOP("Files and folders"), parent: "apppermissions", icon: "folder", component: "AppAccessPage",
            description: QT_TR_NOOP("Apps that can open your folders"),
            keywords: "files folders documents pictures file access" },
        "appnetwork": { title: QT_TR_NOOP("Internet"), parent: "apppermissions", icon: "globe", component: "AppAccessPage",
            description: QT_TR_NOOP("Apps that can go online"),
            keywords: "internet network online offline" },
        "apppermissionsadvanced": { title: QT_TR_NOOP("Everything an app can reach"), parent: "apppermissions", icon: "sandbox", component: "AppPermissionsAdvancedPage",
            description: QT_TR_NOOP("Every sandbox permission, app by app"),
            keywords: "flatpak permissions dbus d-bus environment variables devices sockets filesystem override reset advanced" },
        "firewall": { title: QT_TR_NOOP("Firewall"), parent: "privacy", icon: "firewall", component: "FirewallPage",
            description: QT_TR_NOOP("What other computers can reach on this PC"),
            keywords: "firewall network protection public network private network home network trusted network allow app allow port open port port forwarding blocked connections incoming connections network discovery file sharing smb samba remote desktop rdp ssh server windows defender firewall" },
        "advancedprotection": { title: QT_TR_NOOP("Advanced protection"), parent: "privacy", icon: "shield", component: "AdvancedProtectionPage",
            description: QT_TR_NOOP("Extra protection for people who want it"),
            keywords: "advanced protection extra protection hardening locked lock screen usb usb devices while locked quiet lock screen notifications on lock screen hide notification content fingerprint password after restart fingerprint timeout restart when locked auto restart reboot encrypted disk wi-fi bluetooth switch off automatically radios idle encrypted dns dns over tls dot quad9 mullvad cloudflare vpn only kill switch block internet without vpn camera light microphone light clipboard passwords clear clipboard presenting mode screen sharing do not disturb remove personal details metadata exif location password-locked usb sticks encrypt usb drive guest session guest account safe box sandbox panic key panic button reset to defaults" },
        // KDE's Remote Desktop module, for the accounts other PCs sign in
        // with once Remote Desktop is on (Firewall).
        "remotedesktop": { title: QT_TR_NOOP("Remote Desktop sign-in"), parent: "firewall", icon: "display", kcm: "kcm_krdpserver",
            description: QT_TR_NOOP("Who can sign in to this PC from another one"),
            keywords: "remote desktop rdp krdp sign in username password certificate" },
        "screenlock": { title: QT_TR_NOOP("Screen lock"), parent: "privacy", icon: "lock", component: "ScreenLockPage", fallback: "kcm_screenlocker",
            description: QT_TR_NOOP("Lock after a while and ask for your password"),
            keywords: "lock screen auto lock timeout password sign in wake waking sleep" },
        "searching": { title: QT_TR_NOOP("Searching files"), parent: "privacy", icon: "search", component: "SearchingFilesPage",
            description: QT_TR_NOOP("File indexing and the folders search looks in"),
            keywords: "search indexing index search index baloo find files excluded folders hidden files contents rebuild" },
        "activityhistory": { title: QT_TR_NOOP("Activity history"), parent: "privacy", icon: "clock", component: "ActivityHistoryPage",
            description: QT_TR_NOOP("Recent files and clearing your history"),
            keywords: "activity history recent files recently opened documents clear history forget" },

        "update": { title: QT_TR_NOOP("Updates"), icon: "update", component: "UpdatePage",
            keywords: "update updates upgrade patch security updates check for updates install windows update update channel beta stable whats new release notes restart" }
    })

    // The pages as shown: title and description in the display language.
    // all keeps the English, which search and the code around it match on.
    readonly property var shown: {
        const out = {};
        for (const id in all) {
            const p = all[id];
            out[id] = Object.assign({}, p, {
                title: qsTr(p.title),
                description: p.description ? qsTr(p.description) : p.description
            });
        }
        return out;
    }

    function page(id) {
        return shown[id] || null;
    }

    // Other names callers use for a page: Windows' own spellings, the ids
    // Start's search used before it knew these, and KDE module names, so
    // "wildos-settings --page kcm_bluetooth" lands on Bluetooth.
    readonly property var aliases: ({
        "personalization": "personalisation",
        "colors": "colours",
        "wallpaper": "background",
        "lock-screen": "lockscreen",
        "time-language": "time",
        "windows-apps": "windowsapps",
        "updates": "update",
        // The Updates icon on the taskbar's Install button: the page, which
        // then installs what is waiting.
        "update-install": "update",
        "security": "privacy",
        "network-advanced": "networkadvanced",
        "kcm_kscreen": "display",
        // Advanced display was KDE's module; everything it offered that Wild
        // OS keeps is on Display now.
        "displayadvanced": "display",
        "kcm_networkmanagement": "networkadvanced",
        "kcm_wallpaper": "background",
        "kcm_lookandfeel": "personalisation",
        "kcm_colors": "colours",
        "kcm_about-distro": "about",
        "kcm_updates": "update",
        "kcm_cursortheme": "accessibility",
        "kcm_proxy": "networkadvanced",
        "kcm_soundtheme": "sound",
        // The volume mixer and every device are on Sound itself now.
        "soundadvanced": "sound",
        "kcm_pulseaudio": "sound",
        "kcm_energyinfo": "power",
        "kcm_kwin_virtualdesktops": "desktops",
        "kcm_smserver": "signin",
        // From the saved passwords notification (wildos-setup-wallet).
        "signin-wallet": "signin",
        "kcm_keyboard": "keyboard",
        "kcm_device_automounter": "autoplay",
        "kcm_gamecontroller": "gamecontrollers",
        "kcm_tablet": "pen",
        "kcm_touchscreen": "touchscreen",
        "kcm_bolt": "thunderbolt",
        "kcm_baloofile": "searching",
        "kcm_recentFiles": "activityhistory",
        "kcm_kwinxwayland": "oldapps",
        "kcm_virtualkeyboard": "accessibility",
        "kcm_access": "accessibility",
        "easeofaccess": "accessibility",
        "ease-of-access": "accessibility",
        // Text size in Personalisation is the Wild OS way to change the
        // fonts apps use; installing fonts has its own page.
        "kcm_fonts": "personalisation",
        "kcm_fontinst": "fonts",
        "kcmspellchecking": "typing",
        "kcm_filetypes": "filetypes"
    })

    function resolve(id) {
        if (!id) {
            return "home";
        }
        if (all[id]) {
            return id;
        }
        if (aliases[id]) {
            return aliases[id];
        }
        // A module Wild OS has its own page for opens that page.
        for (const key in all) {
            if (all[key].fallback === id) {
                return key;
            }
        }
        for (const key in all) {
            if (all[key].kcm === id || all[key].window === id) {
                return key;
            }
        }
        return id;
    }

    function exists(id) {
        const p = all[id];
        if (!p) {
            return false;
        }
        if (p.kcm) {
            return KcmHost.available(p.kcm);
        }
        if (p.window) {
            return KcmHost.available(p.window);
        }
        if (id === "startupdisk") {
            return StartupDisk.installed;
        }
        return true;
    }

    // The navigation entry a page lives under, for highlighting.
    function category(id) {
        let p = all[id];
        let current = id;
        while (p && p.parent) {
            current = p.parent;
            p = all[current];
        }
        return current;
    }

    // Parents first, ending with the page itself.
    function trail(id) {
        const chain = [];
        let current = id;
        while (current && all[current]) {
            chain.unshift(current);
            current = all[current].parent;
        }
        return chain;
    }

    // Where a page sits, for search results: "System > Display".
    function location(id) {
        return trail(id).slice(0, -1).map(key => shown[key].title).join(" > ");
    }

    function englishLocation(id) {
        return trail(id).slice(0, -1).map(key => all[key].title).join(" > ");
    }

    function children(id) {
        const p = all[id];
        return p && p.children ? p.children.filter(exists) : [];
    }

    // Words in a question that say nothing about the setting ("how do I
    // change the wallpaper"). They are dropped unless they are all there is.
    readonly property var stopWords: ["a", "an", "and", "can", "change", "do", "does", "for", "get", "how", "i", "in",
        "is", "it", "make", "me", "my", "of", "off", "on", "option", "set", "setting", "the", "this", "to", "turn",
        "want", "what", "where", "with"]

    // British spellings that no general rule turns American. Search keeps
    // the word as typed too, so a half-typed "colou" still finds Colours.
    readonly property var spellings: ({
        "behaviour": "behavior",
        "catalogue": "catalog",
        "centre": "center",
        "centred": "centered",
        "colour": "color",
        "coloured": "colored",
        "dialogue": "dialog",
        "favourite": "favorite",
        "grey": "gray",
        "licence": "license",
        "litre": "liter",
        "metre": "meter",
        "neighbour": "neighbor",
        "programme": "program"
    })

    // Lower case, with hyphens, dots and other punctuation as spaces, so
    // "12-hour" and "12 hour", "Wi-Fi" and "wi fi" read the same.
    function fold(text) {
        return String(text || "").toLowerCase().replace(/['’]/g, "").replace(/[^a-z0-9à-ɏ]+/g, " ").trim();
    }

    function words(text) {
        const folded = fold(text);
        return folded.length > 0 ? folded.split(" ") : [];
    }

    // Plural to singular, near enough: both sides of a match go through
    // it, so it only has to be consistent, not correct English.
    function stem(word) {
        if (word.length > 4 && word.endsWith("ies")) {
            return word.slice(0, -3) + "y";
        }
        if (word.length > 4 && /(ss|x|ch|sh)es$/.test(word)) {
            return word.slice(0, -2);
        }
        if (word.length > 3 && word.endsWith("s") && !/(ss|us|is)$/.test(word)) {
            return word.slice(0, -1);
        }
        return word;
    }

    // One spelling for British and American forms: personalise and
    // personalize, colour and color, licence and license.
    function canonical(word) {
        const s = stem(word);
        if (spellings[s]) {
            return spellings[s];
        }
        if (s.length > 5) {
            return s.replace(/is(e|ed|es|er|ing|ation)$/, "iz$1").replace(/ys(e|ed|ing)$/, "yz$1");
        }
        return s;
    }

    function forms(word) {
        const list = [word];
        for (const f of [stem(word), canonical(word)]) {
            if (list.indexOf(f) < 0) {
                list.push(f);
            }
        }
        return list;
    }

    // Every form of every word, and neighbouring words run together so
    // "wifi" finds "wi-fi" and "autohide" finds "auto hide".
    function searchWords(text, joined) {
        const list = [];
        const add = word => {
            for (const f of forms(word)) {
                if (list.indexOf(f) < 0) {
                    list.push(f);
                }
            }
        };
        const w = words(text);
        for (let i = 0; i < w.length; ++i) {
            add(w[i]);
            if (joined && i > 0) {
                add(w[i - 1] + w[i]);
            }
        }
        return list;
    }

    // Built once: the page list never changes while Settings runs.
    readonly property var searchIndex: {
        const index = {};
        for (const id in all) {
            const p = all[id];
            const t = shown[id];
            // The English words and the display language's, so search
            // finds a page by either.
            index[id] = {
                title: searchWords(p.title + " " + t.title, true),
                keywords: searchWords(p.keywords, true),
                description: searchWords((p.description || "") + " " + (t.description || ""), true),
                parents: searchWords(englishLocation(id) + " " + location(id), false),
                titleText: fold(p.title),
                titleCanonical: words(p.title).map(canonical).join(" "),
                keywordPhrases: " " + words(p.keywords).map(canonical).join(" ") + " ",
                depth: trail(id).length
            };
        }
        return index;
    }

    // At most one letter wrong, missing, extra or swapped with its
    // neighbour: "bluetoth", "keybaord", "prnter".
    function nearly(a, b) {
        if (Math.abs(a.length - b.length) > 1 || a === b) {
            return a === b;
        }
        let i = 0;
        while (i < a.length && i < b.length && a[i] === b[i]) {
            ++i;
        }
        const restA = a.slice(i + 1);
        const restB = b.slice(i + 1);
        if (a.length === b.length) {
            return restA === restB || (a[i] === b[i + 1] && a[i + 1] === b[i] && a.slice(i + 2) === b.slice(i + 2));
        }
        return a.length > b.length ? restA === b.slice(i) : a.slice(i) === restB;
    }

    // 2 when a form of the query word is a word of the field, 1 when it
    // starts one, 0 when neither.
    function matchLevel(queryForms, fieldWords, minPrefix) {
        let level = 0;
        if (minPrefix < 0) {
            return fieldWords.some(w => queryForms.indexOf(w) >= 0) ? 2 : 0;
        }
        for (const w of fieldWords) {
            for (const q of queryForms) {
                if (w === q) {
                    return 2;
                }
                if (level === 0 && q.length >= minPrefix && w.startsWith(q)) {
                    level = 1;
                }
            }
        }
        return level;
    }

    // Title start > title word > keyword > keyword prefix > description,
    // then the place a page sits under when the query names it as well
    // ("display night"), and last a word one typing slip away.
    function wordScore(queryForms, entry, withParents, wholeWord) {
        const fields = [[entry.title, 60, 50, 1], [entry.keywords, 40, 30, 2], [entry.description, 20, 15, 3]];
        if (withParents) {
            fields.push([entry.parents, 12, 10, 3]);
        }
        let best = 0;
        let fieldsMatched = 0;
        for (const field of fields) {
            const level = matchLevel(queryForms, field[0], wholeWord ? -1 : field[3]);
            const score = level === 2 ? field[1] : level === 1 ? field[2] : 0;
            best = Math.max(best, score);
            fieldsMatched += score > 0 ? 1 : 0;
        }
        // A word in the keywords and the description too settles a tie,
        // never more: the gaps between the steps above are wider than this.
        if (best > 0) {
            return { score: best + 3 * (fieldsMatched - 1), slip: "" };
        }
        const typed = queryForms[0];
        if (typed.length >= 5) {
            for (const field of [[entry.title, 9], [entry.keywords, 7], [entry.description, 5]]) {
                const near = field[0].find(w => w.length >= 4 && nearly(typed, w));
                if (near) {
                    return { score: field[1], slip: near };
                }
            }
        }
        return { score: 0, slip: "" };
    }

    // Pages for what was typed, best first. Every word has to match
    // somewhere (title, keywords or description, in any order); when no
    // page has them all, the pages that have the most come back with
    // `partial` set. Only pages that exist on this computer are found.
    function find(text) {
        const typed = fold(text);
        let tokens = typed.length > 0 ? typed.split(" ") : [];
        const meaningful = tokens.filter(t => stopWords.indexOf(t) < 0 && stopWords.indexOf(stem(t)) < 0);
        if (meaningful.length > 0) {
            tokens = meaningful;
        }
        if (tokens.length === 0) {
            return { ids: [], partial: false };
        }
        const queryForms = tokens.map(forms);
        const squashed = tokens.length > 1 ? forms(tokens.join("")) : null;
        // Callers' other names for a page ("wallpaper", "colors",
        // "ease of access") lift that page above others that mention them.
        const named = aliases[tokens.join("")] || aliases[tokens.join("-")] || "";
        const found = [];
        for (const id in all) {
            if (!exists(id)) {
                continue;
            }
            const entry = searchIndex[id];
            let total = 0;
            let matched = 0;
            let slip = false;
            // What was typed with any slip put right, so a misspelt title
            // still ranks as that title.
            let meant = [];
            for (let i = 0; i < tokens.length; ++i) {
                // Earlier short words are finished words; only the one
                // being typed, or a long one, can be the start of a word.
                const wholeWord = i < tokens.length - 1 && tokens[i].length < 4;
                const result = wordScore(queryForms[i], entry, tokens.length > 1, wholeWord);
                if (result.score > 0) {
                    total += result.score;
                    ++matched;
                    slip = slip || result.slip.length > 0;
                }
                meant.push(result.slip.length > 0 ? result.slip : tokens[i]);
            }
            let score = total / tokens.length;
            // "touch pad" for Touchpad, "wi fi" for wifi.
            if (squashed) {
                const result = wordScore(squashed, entry, false, false);
                if (result.score > 0 && (matched < tokens.length || result.score > score)) {
                    score = result.score;
                    matched = tokens.length;
                    slip = result.slip.length > 0;
                    meant = [result.slip.length > 0 ? result.slip : squashed[0]];
                }
            }
            if (matched === 0) {
                continue;
            }
            if (matched === tokens.length) {
                const meantText = meant.join(" ");
                const meantCanonical = meant.map(canonical).join(" ");
                if (entry.titleCanonical === meantCanonical || entry.titleText === meantText) {
                    score += 200;
                } else if (entry.titleCanonical.startsWith(meantCanonical) || entry.titleText.startsWith(meantText)) {
                    score += 100;
                } else if (meant.length > 1 && entry.keywordPhrases.indexOf(" " + meantCanonical) >= 0) {
                    score += 30;
                }
                if (id === named) {
                    score += 25;
                }
            }
            found.push({ id: id, score: score, matched: matched, slip: slip, depth: entry.depth, title: shown[id].title });
        }
        let best = found.filter(r => r.matched === tokens.length);
        const partial = best.length === 0 && found.length > 0;
        if (partial) {
            const most = Math.max(...found.map(r => r.matched));
            best = found.filter(r => r.matched === most);
        }
        // A typing slip only counts when nothing was spelt right.
        if (best.some(r => !r.slip)) {
            best = best.filter(r => !r.slip);
        }
        best.sort((a, b) => b.score - a.score || a.depth - b.depth || a.title.localeCompare(b.title));
        return { ids: best.slice(0, 12).map(r => r.id), partial: partial };
    }

    function search(text) {
        return find(text).ids;
    }
}
