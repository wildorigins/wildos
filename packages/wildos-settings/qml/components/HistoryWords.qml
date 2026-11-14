pragma Singleton

import QtQuick
import WildOS.Settings
import WildOS.UI as UI

// The words for History (SettingsHistory.dc, HistoryDetail.dc): for each
// kind of event settings-history records, a title, one line for the list,
// and the three parts of Details (what happened, what Wild OS did, what you
// can do). One place, so the list, Details and Save as a file always say
// the same thing. Every sentence is checked against what Wild OS really
// does; anything it can't know (which file was being copied) isn't said.
QtObject {
    id: words

    // The board's status colours; on light, the Style's own deeper ones.
    readonly property color amber: UI.Style.dark ? "#ffc46a" : UI.Style.warning
    readonly property color green: UI.Style.dark ? "#5fd38d" : "#1d7a4c"
    readonly property color red: UI.Style.dark ? "#ff8f8f" : UI.Style.danger
    readonly property color blue: UI.Style.accent
    readonly property color plain: UI.Style.ink

    function at(date) {
        return Format.time(date);
    }

    function dayAndTime(date) {
        const day = Format.day(date);
        return (day === "today" || day === "yesterday" ? day : "on " + day) + " at " + Format.time(date);
    }

    function quoted(text) {
        return "“" + text + "”";
    }

    function countText(n, one, many) {
        return n === 1 ? one : n + " " + many;
    }

    function listNames(names) {
        const list = names.filter(n => n && n.length > 0);
        if (list.length === 0) {
            return "";
        }
        if (list.length === 1) {
            return list[0];
        }
        if (list.length === 2) {
            return list[0] + " and " + list[1];
        }
        return list[0] + ", " + list[1] + " and " + Format.plural(list.length - 2, "more", "more");
    }

    // How many times the same program stopped in the 30 days kept.
    function repeats(e, all) {
        const program = e.data.program || "";
        return all.filter(x => (x.kind === "crash" || x.kind === "service-crash") && (x.data.program || "") === program).length;
    }

    function repeatSentence(e, all) {
        const n = repeats(e, all);
        return n <= 1 ? "This is the first time in the last 30 days." : "It has happened " + n + " times in the last 30 days.";
    }

    function duration(fromSecs, to) {
        const minutes = Math.max(0, Math.round((to.getTime() / 1000 - fromSecs) / 60));
        if (minutes < 1) {
            return "less than a minute";
        }
        if (minutes < 60) {
            return Format.plural(minutes, "minute", "minutes");
        }
        const hours = Math.floor(minutes / 60);
        const rest = minutes % 60;
        return Format.plural(hours, "hour", "hours") + (rest > 0 ? " " + Format.plural(rest, "minute", "minutes") : "");
    }

    function firewallWho(kind) {
        return kind === "router" ? "your router"
            : kind === "local" ? "another PC on this network"
            : kind === "internet" ? "the internet"
            : "another network";
    }

    function portName(port, proto) {
        const service = Firewall.serviceName(port, proto);
        return service.length > 0 ? service : (proto === "udp" ? "UDP port " : "Port ") + port;
    }

    function passwordPlace(place) {
        return place === "signin" ? "at the sign-in screen"
            : place === "lock" ? "to unlock the screen"
            : place === "ssh" ? "over SSH"
            : "when an app asked for the admin password";
    }

    // {title, detail, icon, tone, problem, group, happened, did, todo}.
    // group is what the filters pick from: security, updates, apps or
    // other.
    function describe(e, all) {
        const d = e.data || {};
        const when = e.when;
        switch (e.kind) {
        case "crash": {
            const app = d.app && d.app.length > 0 ? d.app : "";
            return {
                title: app.length > 0 ? app + " closed unexpectedly" : "A program closed unexpectedly",
                detail: app.length > 0 ? "It stopped suddenly. Open it again to carry on." : "It was called " + d.program + ".",
                icon: "triangle-alert", tone: amber, problem: true, group: "apps",
                happened: (app.length > 0 ? app : d.program) + " stopped suddenly at " + at(when) + ". " + repeatSentence(e, all),
                did: "Recorded which program stopped, how and when. Nothing from its memory was kept, and nothing was sent anywhere.",
                todo: "Nothing, unless it keeps happening. If it does, send a report so it can be fixed."
            };
        }
        case "service-crash":
            return {
                title: "A part of Wild OS stopped unexpectedly",
                detail: d.program + ", which runs in the background.",
                icon: "triangle-alert", tone: amber, problem: true, group: "other",
                happened: d.program + " stopped suddenly at " + at(when) + ". It runs in the background, so you may not have seen anything happen. " + repeatSentence(e, all),
                did: "Recorded which program stopped, how and when. Nothing from its memory was kept, and nothing was sent anywhere.",
                todo: "If something stopped working at that time, restart the PC. If it keeps happening, send a report so it can be fixed."
            };
        case "oom": {
            const name = d.app && d.app.length > 0 ? d.app : d.program;
            return {
                title: name + " was closed to free up memory",
                detail: "This PC ran out of memory.",
                icon: "triangle-alert", tone: amber, problem: true, group: "apps",
                happened: "This PC ran out of memory at " + at(when) + ", so " + name + " was closed to keep everything else running.",
                did: "Closed " + name + " so the rest of the PC could carry on.",
                todo: "Close apps and browser tabs you aren't using. If it keeps happening, this PC may need more memory for what you do."
            };
        }
        case "drive-in": {
            const what = d.kind === "usb" ? "A USB drive" : "A drive";
            return {
                title: what + " was connected",
                detail: "It's called " + quoted(d.label) + ".",
                icon: "security-key", tone: plain, problem: false, group: "security",
                happened: what + " called " + quoted(d.label) + " was plugged in at " + at(when) + " and made ready to use.",
                did: "Made it ready to use, so its files show in Files.",
                todo: "Nothing, if it's yours. If you don't know where a drive came from, don't open files on it."
            };
        }
        case "drive-yanked": {
            const what = d.kind === "usb" ? "A USB drive" : "A drive";
            return {
                title: what + " was removed while it was in use",
                detail: "Anything being copied to " + quoted(d.label) + " may not have finished.",
                icon: "security-key", tone: red, problem: true, group: "other",
                happened: quoted(d.label) + " was unplugged at " + at(when) + " while it was still in use, before it was ejected.",
                did: "Tidied up after it, so it can be plugged in again.",
                todo: "Plug it back in and check that the files you last copied to it open. Next time, choose Eject in Files before you unplug it."
            };
        }
        case "signin": {
            const where = d.how === "ssh" ? "from another computer, over SSH" + (d.from ? " from " + d.from : "")
                : d.how === "console" ? "at a text console"
                : d.how === "screen" ? "at the sign-in screen" : "";
            return {
                title: d.name + " signed in",
                detail: d.finger ? "With a fingerprint." : d.how === "ssh" ? "From another computer, over SSH." : d.how === "console" ? "At a text console." : d.how === "screen" ? "At the sign-in screen." : "To this PC.",
                icon: "log-in", tone: d.how === "ssh" ? blue : plain, problem: false, group: "security",
                happened: d.name + " signed in" + (where.length > 0 ? " " + where : "") + (d.finger ? ", with a fingerprint," : "") + " at " + at(when) + ".",
                did: d.how === "ssh" ? "Let them in once the password or key was right."
                    : d.finger ? "Started their desktop once the fingerprint matched one saved for them." : "Started their desktop once the password was right.",
                todo: d.how === "ssh" ? "Nothing, if it was you. If it wasn't, change your password in Accounts, and turn SSH off in Privacy and security > Firewall."
                    : "Nothing, if it was you. If it wasn't, change your password in Accounts > Sign-in options."
            };
        }
        case "signout":
            return {
                title: d.name + " signed out",
                detail: d.since ? "After " + duration(d.since, when) + "." : "Their session ended.",
                icon: "log-out", tone: plain, problem: false, group: "security",
                happened: d.name + "'s session ended at " + at(when) + ", by signing out, restarting or shutting down.",
                did: "Closed their apps and desktop.",
                todo: "Nothing."
            };
        case "password": {
            const n = d.count || 1;
            const lastAt = new Date((d.last || 0) * 1000);
            const span = n > 1 && Format.isValid(lastAt) && Format.time(lastAt) !== at(when) ? " between " + at(when) + " and " + Format.time(lastAt) : " at " + at(when);
            const ssh = d.place === "ssh";
            return {
                title: ssh ? (n === 1 ? "Someone tried to sign in over SSH" : "Someone tried to sign in over SSH " + n + " times")
                    : d.place === "admin" ? (n === 1 ? "A wrong admin password was typed" : n + " wrong admin passwords were typed")
                    : (n === 1 ? "A wrong password was typed " : n + " wrong passwords were typed ") + passwordPlace(d.place),
                detail: ssh ? "As " + quoted(d.user) + (d.from ? ", from " + d.from : "") + ". It was turned away."
                    : "For " + d.name + ". " + (n === 1 ? "It was" : "Each was") + " turned away.",
                icon: "lock-round", tone: n >= 3 ? amber : plain, problem: false, group: "security",
                happened: ssh ? countText(n, "A wrong password was", "wrong passwords were") + " given over SSH for " + quoted(d.user) + (d.from ? ", from " + d.from : "") + span + "."
                    : countText(n, "A wrong password was", "wrong passwords were") + " typed for " + d.name + " " + passwordPlace(d.place) + span + ".",
                did: n === 1 ? "Turned it away." : "Turned each one away.",
                todo: ssh ? "If you don't use SSH, turn it off in Privacy and security > Firewall. If you do, make sure every account has a strong password."
                    : "Nothing, if it was you mistyping. If it wasn't you, change your password in Accounts > Sign-in options."
            };
        }
        case "firewall": {
            const who = firewallWho(d.kind);
            // The log's device is the network interface it came in on; people
            // know the sender by its address.
            const from = d.source;
            const many = (d.ports || []).length > 1;
            const n = d.count || 1;
            const what = many ? n + " tries on " + d.ports.length + " ports" : portName(d.port, d.proto) + (n > 1 ? ", " + n + " times" : "");
            return {
                title: (n > 1 ? "Connections from " : "A connection from ") + who + (n > 1 ? " were blocked" : " was blocked"),
                detail: what + ", from " + from + ".",
                icon: "shield-round", tone: blue, problem: false, group: "security",
                happened: who.charAt(0).toUpperCase() + who.slice(1) + " (" + d.source + ") tried to connect to this PC"
                    + (many ? " " + n + " times, on " + d.ports.length + " different ports," : " for " + portName(d.port, d.proto) + (n > 1 ? ", " + n + " times," : ""))
                    + (n > 1 ? " starting at " : " at ") + at(when) + ".",
                did: "Turned " + (n > 1 ? "every try" : "it") + " away at the firewall. Nothing on this PC was reached.",
                todo: "Nothing. If you were expecting it, allow it in Privacy and security > Firewall."
            };
        }
        case "updates": {
            const n = d.count || 0;
            return {
                title: d.auto ? "Security updates installed" : "Updates installed",
                detail: Format.plural(n, "update", "updates") + (d.auto ? ", installed automatically." : ".") + (d.restore ? " A restore point was saved first." : ""),
                icon: "refresh-cw", tone: green, problem: false, group: "updates",
                happened: Format.plural(n, "update was", "updates were") + " installed" + (d.auto ? " automatically, in the background" : "") + " at " + at(when) + ".",
                did: d.restore ? "Saved a restore point first, then installed them." : "Installed them.",
                todo: d.restore ? "Nothing. If something stopped working since, you can go back in System > Recovery." : "Nothing."
            };
        }
        case "update-failed":
            return {
                title: "Updates didn't finish",
                detail: "Installing them stopped with an error.",
                icon: "triangle-alert", tone: amber, problem: true, group: "updates",
                happened: "Installing updates stopped partway at " + at(when) + ", with the error below.",
                did: d.auto ? "Stopped, and will try again with the next automatic update." : "Stopped, and kept the error below.",
                todo: "Open Updates and choose Check for updates to try again. If it fails again, send a report."
            };
        case "install-failed":
            return {
                title: (d.app && d.app.length > 0 ? d.app : "An app") + " couldn't be installed",
                detail: "Installing it stopped with an error.",
                icon: "triangle-alert", tone: amber, problem: true, group: "apps",
                happened: "Installing " + (d.app && d.app.length > 0 ? d.app : (d.packages || []).join(", ")) + " stopped partway at " + at(when) + ", with the error below.",
                did: "Stopped, and kept the error below.",
                todo: "Try again from the Store. If it fails again, send a report."
            };
        case "restore-point": {
            // The label is what Recovery lists the point under.
            const what = d.label && d.label.length > 0 ? d.label
                : d.kind === "manual" ? "Made by you"
                : d.kind === "driver" ? "Before a driver was installed"
                : d.kind === "update" ? "Before updates" : "";
            const covers = d.summary && d.summary.length > 0 ? " It covers " + d.summary.charAt(0).toLowerCase() + d.summary.slice(1).replace(/\.$/, "") + "." : "";
            return {
                title: "A restore point was saved",
                detail: what.length > 0 ? what.replace(/\.$/, "") + "." : "Wild OS can go back to it.",
                icon: "history", tone: green, problem: false, group: "updates",
                happened: "A copy of Wild OS's own files was saved at " + at(when) + ", so this PC can go back to how it was then." + covers,
                did: "Kept it with the other restore points. Your own files aren't part of it and stay as they are.",
                todo: "Nothing. To go back to it, open System > Recovery."
            };
        }
        case "app-installed": {
            const name = d.app && d.app.length > 0 ? d.app : d.id;
            return {
                title: name + " was installed",
                detail: d.via === "flatpak" ? (d.remote === "flathub" ? "From Flathub, through the Store." : "From the Store.") : "From the Store.",
                icon: "status-ok", tone: green, problem: false, group: "apps",
                happened: name + " was installed at " + at(when) + ".",
                did: "Installed it and added it to Start.",
                todo: "Nothing. To remove it, open Apps > Installed apps."
            };
        }
        case "app-removed": {
            const name = d.app && d.app.length > 0 ? d.app : d.id;
            return {
                title: name + " was removed",
                detail: "It's no longer on this PC.",
                icon: "trash-round", tone: plain, problem: false, group: "apps",
                happened: name + " was removed at " + at(when) + ".",
                did: "Removed it from this PC and from Start.",
                todo: "Nothing. To get it back, find it in the Store."
            };
        }
        case "space-freed": {
            // Free up space (settings-free-space), by hand or by Tidy up
            // automatically when less than 10% of the disk was free.
            const order = ["bin", "restorePoints", "updates", "caches", "leftovers", "winmode", "downloads"];
            const labels = { bin: "the Recycle Bin", restorePoints: "old restore points", updates: "update files",
                caches: "app caches", leftovers: "leftovers from removed apps",
                winmode: "unused space inside Windows mode", downloads: "large files in Downloads" };
            const items = d.items || {};
            const parts = order.filter(k => items[k] > 0).map(k => labels[k]);
            // Every item named: there are seven at most.
            const what = parts.length <= 1 ? (parts[0] || "nothing")
                : parts.slice(0, -1).join(", ") + " and " + parts[parts.length - 1];
            const total = Object.keys(items).reduce((sum, k) => sum + (items[k] || 0), 0);
            const size = System.formatBytes(total);
            return {
                title: d.auto ? "Space was freed up automatically" : "Space was freed up",
                detail: size + " from " + what + ".",
                icon: "hard-drive", tone: green, problem: false, group: "other",
                happened: d.auto ? "Less than 10% of the disk was free at " + at(when) + ", so Wild OS cleared the items ticked in Free up space."
                    : "You freed up space at " + at(when) + ".",
                did: "Cleared " + what + ", " + size + " in all. Your documents, pictures and settings were not touched"
                    + (items.leftovers > 0 ? ", apart from the settings of apps you had removed." : "."),
                todo: d.auto ? "Nothing. To change what is cleared, or to turn this off, go to System > Storage > Free up space."
                    : "Nothing."
            };
        }
        case "apps-updated": {
            const names = d.apps || [];
            return {
                title: names.length === 1 ? (names[0] || "An app") + " was updated" : "Apps updated",
                detail: names.length === 1 ? "To its newest version." : listNames(names) + ".",
                icon: "refresh-cw", tone: green, problem: false, group: "updates",
                happened: (names.length === 1 ? (names[0] || "An app") + " was" : listNames(names) + " were") + " updated at " + at(when) + ".",
                did: "Updated " + (names.length === 1 ? "it" : "them") + " to the newest version.",
                todo: "Nothing."
            };
        }
        case "programs": {
            const parts = [];
            if (d.installed > 0) {
                parts.push(Format.plural(d.installed, "program", "programs") + " installed");
            }
            if (d.removed > 0) {
                parts.push(Format.plural(d.removed, "program", "programs") + " removed");
            }
            if (d.updated > 0) {
                parts.push(Format.plural(d.updated, "program", "programs") + " updated");
            }
            const line = parts.join(", ");
            return {
                title: "Programs were changed",
                detail: line.charAt(0).toUpperCase() + line.slice(1) + ", by an administrator.",
                icon: "app-window", tone: plain, problem: false, group: "apps",
                happened: line.charAt(0).toUpperCase() + line.slice(1) + " at " + at(when) + ", with apt, by someone with the admin password.",
                did: "Made the changes asked for.",
                todo: "Nothing, if it was you or someone you trust with the admin password."
            };
        }
        case "backup": {
            const drive = d.drive && d.drive.length > 0 ? d.drive : "the backup drive";
            if (d.result === "failed") {
                return {
                    title: "Backup didn't finish",
                    detail: d.reason && d.reason.length > 0 ? d.reason : "It stopped with an error.",
                    icon: "hard-drive", tone: amber, problem: true, group: "other",
                    happened: "A backup to " + drive + " stopped at " + at(when) + " before it finished.",
                    did: "Kept your earlier backups as they were.",
                    todo: "Open Backup to see what it needs, then choose Back up now."
                };
            }
            if (d.result === "stopped") {
                return {
                    title: "Backup was stopped",
                    detail: "It stopped before it finished.",
                    icon: "hard-drive", tone: plain, problem: false, group: "other",
                    happened: "A backup to " + drive + " was stopped at " + at(when) + " before it finished.",
                    did: "Kept your earlier backups as they were.",
                    todo: "Choose Back up now in Backup when it suits you."
                };
            }
            return {
                title: "Backup finished",
                detail: d.files > 0 ? Format.plural(d.files, "file", "files") + " copied to " + drive + "." : "Nothing had changed since the last one.",
                icon: "hard-drive", tone: green, problem: false, group: "other",
                happened: "Your files were backed up to " + drive + " at " + at(when) + "." + (d.warning && d.warning.length > 0 ? " " + d.warning : ""),
                did: d.files > 0 ? "Copied " + Format.plural(d.files, "new or changed file", "new or changed files") + " and kept the earlier versions." : "Checked every file. None had changed.",
                todo: "Nothing."
            };
        }
        case "startup":
            return {
                title: "This PC started",
                detail: d.seconds !== undefined ? "Started in " + Format.plural(d.seconds, "second", "seconds") + "." : "Started.",
                icon: "power-round", tone: plain, problem: false, group: "other",
                happened: "This PC started" + (d.seconds !== undefined ? " in " + Format.plural(d.seconds, "second", "seconds") : "")
                    + (d.waited ? ", not counting the " + Format.plural(d.waited, "second", "seconds") + " the disk password screen waited for you" : "")
                    + ". It was ready at " + at(when) + ".",
                did: "Unlocked the disk with your password and started Wild OS.",
                todo: "Nothing."
            };
        case "shutdown":
        case "restart": {
            const restart = e.kind === "restart";
            const by = d.cause === "lid" ? "when the lid was closed"
                : d.cause === "power" ? "with the power button"
                : d.cause === "battery" ? "because the battery was nearly flat"
                : d.cause === "updates" ? "to finish installing updates"
                : d.cause === "person" ? "by " + (d.user || "someone signed in")
                : d.cause === "system" ? "by Wild OS or an administrator" : "";
            return {
                title: restart ? "This PC restarted" : "This PC was shut down",
                detail: by.length > 0 ? by.charAt(0).toUpperCase() + by.slice(1) + "." : (restart ? "It restarted properly." : "It shut down properly."),
                icon: "power-round", tone: plain, problem: false, group: "other",
                happened: (restart ? "This PC restarted" : "This PC shut down") + (by.length > 0 ? " " + by : "") + " at " + at(when) + ", and went through a normal shutdown.",
                did: "Closed the apps that were open and finished writing everything to the disk before " + (restart ? "restarting." : "switching off."),
                todo: "Nothing."
            };
        }
        case "unclean": {
            const last = new Date((d.last || 0) * 1000);
            const lid = d.lid ? new Date(d.lid * 1000) : null;
            return {
                title: "This PC didn't shut down properly",
                detail: d.asleep ? "It was asleep when it stopped" + (lid ? ", with the lid closed." : ".")
                    : lid ? "The lid was closed at " + Format.time(lid) + " and it stayed on until it stopped."
                    : "It lost power or stopped responding.",
                icon: "triangle-alert", tone: amber, problem: true, group: "other",
                happened: "The last thing Wild OS recorded was " + dayAndTime(last) + ". After that it stopped without shutting down"
                    + (d.asleep ? ", while it was asleep" : lid ? ", with the lid closed since " + dayAndTime(lid) : "")
                    + ": the battery ran out, the power was cut, the power button was held down or it stopped responding.",
                did: "Started normally the next time. Anything open and not saved then may have lost its latest changes.",
                todo: d.asleep ? "A PC that is asleep still uses a little battery, and stops like this when it runs out. To have it switch off instead, set what closing the lid does to Shut down in Settings > System > Power and battery > More power settings."
                    : lid ? "Closing the lid didn't shut it down or put it to sleep. Check what closing the lid does in Settings > System > Power and battery > More power settings, and look for \"The lid was closed but the PC stayed on\" here."
                    : "If you didn't switch it off yourself and it keeps happening, send a report so it can be looked at."
            };
        }
        case "sleep": {
            const hib = d.kind === "hibernate";
            const why = d.reason === "lid" ? "when the lid was closed"
                : d.reason === "power" ? "with the power button"
                : d.reason === "person" ? "when " + (d.user || "someone") + " chose Sleep or left it idle"
                : d.reason === "system" ? "when Wild OS asked it to" : "";
            const woke = d.woke ? new Date(d.woke * 1000) : null;
            return {
                title: hib ? "This PC hibernated" : "This PC went to sleep",
                detail: woke ? "Woke up at " + Format.time(woke) + ", after " + duration(0, new Date(d.slept * 1000)) + "." : "It didn't wake up from this.",
                icon: "moon", tone: plain, problem: false, group: "other",
                happened: (hib ? "This PC hibernated" : "This PC went to sleep") + (why.length > 0 ? " " + why : "") + " at " + at(when) + "."
                    + (woke ? " It woke up " + dayAndTime(woke) + "." : " There is no record of it waking up: it ran out of battery or was switched off while asleep."),
                did: hib ? "Saved everything open to the disk and switched off." : "Kept everything open in memory and used as little power as it could.",
                todo: "Nothing."
            };
        }
        case "lid-on": {
            const apps = (d.apps || []).join(" and ");
            const actionWords = { "nothing": "do nothing", "lock": "lock the screen", "screen-off": "turn off the screen", "ask": "ask what to do",
                "shutdown": "shut down", "sleep": "go to sleep", "hibernate": "hibernate" };
            const where = d.profile === "Battery" ? " on battery" : d.profile === "AC" ? " when plugged in" : "";
            const page = "Settings > System > Power and battery > More power settings";
            const opened = d.opened ? new Date(d.opened * 1000) : null;
            const reason = d.reason === "app" ? "Kept on by " + apps + "."
                : d.reason === "setting" ? "Closing the lid is set to " + actionWords[d.action] + where + "."
                : d.reason === "screen" ? "Another screen was connected."
                : d.reason === "held-up" ? "It started to shut down, but an app held it up."
                : d.reason === "not-done" ? "It was set to " + (actionWords[d.action] || "act") + ", but didn't."
                : d.reason === "signin-setting" ? "At the sign-in screen, closing the lid is set to do nothing."
                : "Nothing recorded why.";
            return {
                title: "The lid was closed but the PC stayed on",
                detail: reason,
                icon: "computer", tone: amber, problem: true, group: "other",
                happened: "The lid was closed at " + at(when) + " and nothing happened in the minute after: the PC didn't shut down or go to sleep. "
                    + (d.reason === "app" ? apps + " asked Wild OS not to, while it worked."
                       : d.reason === "setting" ? "That is what closing the lid is set to do" + where + "."
                       : d.reason === "screen" ? "Another screen was connected, and closing the lid is set to do nothing then."
                       : d.reason === "held-up" ? "Closing the lid is set to shut down, and it started to: Wild OS began signing out. An open app held it up, usually by asking whether to save your work, and nobody could answer with the lid closed. Once an app has asked, the shutdown is called off, so the PC stays on until it is shut down another way."
                       : d.reason === "not-done" && d.action === "shutdown" ? "It was set to shut down, but the shutdown didn't happen. An app with unsaved work can stop it."
                       : d.reason === "not-done" ? "It was set to " + (actionWords[d.action] || "act") + ", but that didn't happen."
                       : d.reason === "signin-setting" ? "Nobody was signed in, and at the sign-in screen closing the lid is set to do nothing."
                       : "")
                    + (opened ? " The lid was opened again " + dayAndTime(opened) + "." : ""),
                did: "Kept running. A PC left on like this in a bag keeps using its battery and can get warm.",
                todo: d.reason === "app" ? "Close " + apps + ", or let it finish, before you close the lid."
                    : d.reason === "screen" ? "To make the lid act anyway, turn on Do this even when another screen is connected in " + page + "."
                    : d.reason === "held-up" || d.reason === "not-done" && d.action === "shutdown" ? "Save your work and close apps before you close the lid, or set the lid to Sleep in " + page + ", which keeps everything as it is."
                    : "To change what closing the lid does, open " + page + "."
            };
        }
        case "finger-unknown": {
            const n = d.count || 1;
            return {
                title: n === 1 ? "A fingerprint wasn't recognised" : n + " fingerprints weren't recognised",
                detail: "For " + d.name + ". " + (n === 1 ? "It was" : "Each was") + " turned away.",
                icon: "fingerprint", tone: n >= 3 ? amber : plain, problem: false, group: "security",
                happened: (n === 1 ? "A finger that didn't match any saved for " : n + " fingers that didn't match any saved for ") + d.name
                    + " " + (n === 1 ? "was" : "were") + " put on the reader at " + at(when) + ".",
                did: n === 1 ? "Didn't let it in." : "Didn't let any of them in.",
                todo: "Nothing, if it was you. If your finger often isn't recognised, add it again in Accounts > Sign-in options."
            };
        }
        case "unlock":
            return {
                title: d.name + " unlocked the PC",
                detail: "With a fingerprint.",
                icon: "fingerprint", tone: plain, problem: false, group: "security",
                happened: d.name + " unlocked the lock screen with a fingerprint at " + at(when) + ".",
                did: "Unlocked once the fingerprint matched one saved for them.",
                todo: "Nothing, if it was you. If it wasn't, remove the saved fingerprints in Accounts > Sign-in options."
            };
        case "admin-finger": {
            const n = d.count || 1;
            return {
                title: n === 1 ? "An admin task was approved with a fingerprint" : n + " admin tasks were approved with a fingerprint",
                detail: "By " + d.name + (d.by === "sudo" ? ", in Terminal." : "."),
                icon: "fingerprint", tone: blue, problem: false, group: "security",
                happened: d.name + " approved " + (n === 1 ? "a task that needs" : n + " tasks that need") + " the admin password with a fingerprint" + (d.by === "sudo" ? ", in Terminal," : "") + " at " + at(when) + ".",
                did: "Let " + (n === 1 ? "it" : "them") + " go ahead once the fingerprint matched.",
                todo: "Nothing, if it was you. To use the password instead, turn fingerprints off for admin tasks in Accounts > Sign-in options."
            };
        }
        case "disk-error": {
            const n = d.count || 1;
            const what = d.disk === "system" ? "The system disk" : d.disk === "usb" ? "A USB drive" : "A disk";
            return {
                title: what + " had trouble reading or writing",
                detail: Format.plural(n, "error", "errors") + ", on " + d.device + ".",
                icon: "hard-drive", tone: red, problem: true, group: "other",
                happened: what + " (" + d.device + ") couldn't read or write some of its data at " + at(when) + (n > 1 ? ", " + n + " times within the hour" : "") + ".",
                did: "Reported the error to the program that was reading or writing. The data it was after may be damaged.",
                todo: d.disk === "usb" ? "Copy what you need off it and use another drive. Choose Eject in Files before you unplug it."
                    : d.disk === "system" ? "Back up your files now with Backup. If it happens again, the disk may be failing."
                    : "Copy what you need off it. If it happens again, the disk may be failing."
            };
        }
        case "winmode-failed": {
            const resume = d.action === "resume";
            return {
                title: resume ? "Windows mode couldn't resume" : "Windows mode couldn't start",
                detail: "Windows apps couldn't open.",
                icon: "windowsapps", tone: amber, problem: true, group: "apps",
                happened: (resume ? "Windows mode was asked to carry on where it left off" : "Windows mode was asked to start Windows") + " at " + at(when) + ", and it couldn't.",
                did: "Left Windows as it was and said so on screen.",
                todo: "Try again from Windows mode on the taskbar. If it keeps happening, restart the PC or send a report."
            };
        }
        // From the camera and microphone lights on the taskbar: amber and
        // green, as the lights are.
        case "device-use": {
            const camera = d.device === "camera";
            const thing = camera ? "the camera" : "the microphone";
            const app = d.app && d.app.length > 0 ? d.app : "An app";
            const ended = typeof d.end === "number" ? new Date(d.end * 1000) : null;
            const span = ended ? "From " + at(when) + " to " + at(ended) + ", " + duration(when.getTime() / 1000, ended) + "."
                : "From " + at(when) + ".";
            return {
                title: app + " used " + thing,
                detail: span,
                icon: camera ? "camera" : "mic", tone: camera ? green : amber, problem: false, group: "security",
                happened: app + " started using " + thing + " at " + at(when)
                    + (ended ? " and stopped at " + at(ended) + ", after " + duration(when.getTime() / 1000, ended) + "." : "."),
                did: "Showed the " + (camera ? "camera" : "microphone") + " light on the taskbar while it was in use, and noted which app and when, nothing more.",
                todo: "Nothing, if you expected it. If not, click the light on the taskbar while it is on: Stop cuts the app off, and the switch there turns " + thing + " off for every app."
            };
        }
        case "setting": {
            const service ={ files: "File sharing", remote: "Remote Desktop", ssh: "SSH" };
            const levels = { standard: "Standard", strict: "Strict", maximum: "Maximum" };
            const port = (d.proto === "udp" ? "UDP port " : "Port ") + d.port;
            const title = d.what === "firewall" ? (d.on ? "The firewall was turned on" : "The firewall was turned off")
                : d.what === "trust" ? (d.network || "A network") + " was set to " + (d.trust === "public" ? "Public" : "Home")
                : d.what === "discovery" ? "Network discovery was turned " + (d.on ? "on" : "off")
                : d.what === "service" ? (service[d.service] || d.service) + " was turned " + (d.on ? "on" : "off")
                : d.what === "allow" ? (d.name || port) + " was allowed through the firewall"
                : d.what === "rule" ? (d.name || port) + (d.change === "removed" ? " was removed from the firewall" : " was turned " + d.change + " in the firewall")
                : d.what === "level" ? "Protection was set to " + (levels[d.level] || d.level)
                : "A security setting was changed";
            const weaker = (d.what === "firewall" && !d.on) || (d.what === "service" && d.on) || d.what === "allow"
                || (d.what === "level" && d.level === "standard");
            return {
                title: title,
                detail: (d.what === "allow" ? port + ", " + (d.scope === "anyone" ? "from anyone. " : d.scope === "device" ? "from " + d.address + " only. " : "from this network. ") : "")
                    + (d.user ? "By " + d.user + "." : "By Wild OS."),
                icon: "shield-round", tone: weaker ? amber : blue, problem: false, group: "security",
                happened: title + " at " + at(when) + (d.what === "level" && d.from ? ", from " + (levels[d.from] || d.from) : "") + (d.user ? ", by " + d.user : "") + ".",
                did: d.what === "level" ? "Changed the firewall, USB and other protections to match." : "Loaded the firewall's rules again to match.",
                todo: "Nothing, if it was you. To change it back, open Settings > Privacy and security" + (d.what === "level" ? "." : " > Firewall.")
            };
        }
        case "went-back": {
            const staged = d.how === "staged";
            return {
                title: staged ? "Going back to an earlier version was set up" : "This PC went back to an earlier version",
                detail: staged ? "It happens when the PC restarts." : (d.label ? d.label.replace(/\.$/, "") + "." : "To restore point " + d.point + ".")
                    + (d.how === "recovery" ? " From start-up recovery." : ""),
                icon: "history", tone: green, problem: false, group: "updates",
                happened: staged ? "Going back to restore point " + d.point + (d.label ? " (" + d.label + ")" : "") + " was set up at " + at(when) + ", to happen at the next start-up."
                    : "Wild OS's own files went back to restore point " + d.point + (d.label ? " (" + d.label + ")" : "") + " at " + at(when) + (d.how === "recovery" ? ", from start-up recovery" : "") + ".",
                did: staged ? "Saved a restore point of the version you're on first, so this can be undone." : "Put back the system files and apps of that restore point. Your own files, Wi-Fi and accounts stayed as they were.",
                todo: staged ? "Restart when you're ready." : "To undo it, go back to \"Before going back to an earlier version\" in System > Recovery."
            };
        }
        case "reset":
            return {
                title: "Wild OS was reset",
                detail: "Keeping your files. A restore point was saved first.",
                icon: "rotate-ccw", tone: green, problem: false, group: "updates",
                happened: "Reset Wild OS started at " + at(when) + ", from start-up recovery, keeping your files.",
                did: "Saved a restore point, then put Wild OS's own files back to the copy kept on this PC. Your files and accounts were kept.",
                todo: "Reinstall any apps you miss from the Store. If something is wrong, go back to \"Before resetting Wild OS\" in System > Recovery."
            };
        }
        return {
            title: "Something happened", detail: "", icon: "info", tone: plain, problem: false, group: "other",
            happened: "", did: "", todo: "Nothing."
        };
    }

    // The board's three columns: when, which program, what it said. In a
    // file or a report "Today" would go stale, so those get dates.
    function techText(e, dated) {
        const lines = e.tech || [];
        const width = Math.min(16, Math.max(4, ...lines.map(l => l.source.length))) + 3;
        return lines.map(l => {
            const day = Format.day(l.when);
            const name = dated ? Format.shortDate(l.when) : day === "today" ? "Today" : day === "yesterday" ? "Yesterday" : Format.shortDate(l.when);
            return name + " " + Qt.formatTime(l.when, "HH:mm:ss") + "   " + (l.source + " ".repeat(width)).slice(0, width) + l.text;
        }).join("\n");
    }

    // The whole event as text, for Copy, Save as a file and the report.
    function fullText(e, all) {
        const w = describe(e, all);
        return w.title + "\n" + Format.longDate(e.when) + ", " + Qt.formatTime(e.when, "HH:mm:ss") + "\n\n"
            + "What happened\n" + w.happened + "\n\n"
            + "What Wild OS did\n" + w.did + "\n\n"
            + "What you can do\n" + w.todo + "\n\n"
            + "Technical detail\n" + techText(e, true) + "\n";
    }
}
