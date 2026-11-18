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
//
// Each sentence is one string to translate, with its variations spelt out
// rather than pieced together, because other languages order the parts
// differently and agree words with what they describe.
QtObject {
    id: words

    readonly property color amber: UI.Style.warning
    readonly property color green: UI.Style.success
    // The board's red; on light, the Style's own deeper one.
    readonly property color red: UI.Style.dark ? "#ff8f8f" : UI.Style.danger
    readonly property color blue: UI.Style.accent
    readonly property color plain: UI.Style.ink

    function at(date) {
        return Format.time(date);
    }

    function dayAndTime(date) {
        return Format.dayAndTime(date);
    }

    function quoted(text) {
        return qsTr("“%1”").arg(text);
    }

    function capital(text) {
        return text.charAt(0).toUpperCase() + text.slice(1);
    }

    // Whole sentences, one space apart, leaving out the empty ones.
    function sentences(list) {
        return list.filter(s => s && s.length > 0).join(" ");
    }

    function both(a, b) {
        return qsTr("%1 and %2").arg(a).arg(b);
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
            return both(list[0], list[1]);
        }
        const more = list.length - 2 === 1 ? qsTr("1 more") : qsTr("%1 more").arg(list.length - 2);
        return qsTr("%1, %2 and %3").arg(list[0]).arg(list[1]).arg(more);
    }

    // How many times the same program stopped in the 30 days kept.
    function repeats(e, all) {
        const program = e.data.program || "";
        return all.filter(x => (x.kind === "crash" || x.kind === "service-crash") && (x.data.program || "") === program).length;
    }

    function repeatSentence(e, all) {
        const n = repeats(e, all);
        return n <= 1 ? qsTr("This is the first time in the last 30 days.") : qsTr("It has happened %1 times in the last 30 days.").arg(n);
    }

    function duration(fromSecs, to) {
        const minutes = Math.max(0, Math.round((to.getTime() / 1000 - fromSecs) / 60));
        if (minutes < 1) {
            return qsTr("less than a minute");
        }
        if (minutes < 60) {
            return Format.plural(minutes, qsTr("minute"), qsTr("minutes"));
        }
        const hours = Math.floor(minutes / 60);
        const rest = minutes % 60;
        const hoursText = Format.plural(hours, qsTr("hour"), qsTr("hours"));
        return rest > 0 ? qsTr("%1 %2").arg(hoursText).arg(Format.plural(rest, qsTr("minute"), qsTr("minutes"))) : hoursText;
    }

    function firewallWho(kind) {
        return kind === "router" ? qsTr("your router")
            : kind === "local" ? qsTr("another PC on this network")
            : kind === "internet" ? qsTr("the internet")
            : qsTr("another network");
    }

    function portLabel(port, proto) {
        return proto === "udp" ? qsTr("UDP port %1").arg(port) : qsTr("Port %1").arg(port);
    }

    function portName(port, proto) {
        const service = Firewall.serviceName(port, proto);
        return service.length > 0 ? service : portLabel(port, proto);
    }

    function passwordPlace(place) {
        return place === "signin" ? qsTr("at the sign-in screen")
            : place === "lock" ? qsTr("to unlock the screen")
            : place === "ssh" ? qsTr("over SSH")
            : qsTr("when an app asked for the admin password");
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
                title: app.length > 0 ? qsTr("%1 closed unexpectedly").arg(app) : qsTr("A program closed unexpectedly"),
                detail: app.length > 0 ? qsTr("It stopped suddenly. Open it again to carry on.") : qsTr("It was called %1.").arg(d.program),
                icon: "triangle-alert", tone: amber, problem: true, group: "apps",
                happened: sentences([qsTr("%1 stopped suddenly at %2.").arg(app.length > 0 ? app : d.program).arg(at(when)), repeatSentence(e, all)]),
                did: qsTr("Recorded which program stopped, how and when. Nothing from its memory was kept, and nothing was sent anywhere."),
                todo: qsTr("Nothing, unless it keeps happening. If it does, send a report so it can be fixed.")
            };
        }
        case "service-crash":
            return {
                title: qsTr("A part of Wild OS stopped unexpectedly"),
                detail: qsTr("%1, which runs in the background.").arg(d.program),
                icon: "triangle-alert", tone: amber, problem: true, group: "other",
                happened: sentences([qsTr("%1 stopped suddenly at %2. It runs in the background, so you may not have seen anything happen.").arg(d.program).arg(at(when)),
                    repeatSentence(e, all)]),
                did: qsTr("Recorded which program stopped, how and when. Nothing from its memory was kept, and nothing was sent anywhere."),
                todo: qsTr("If something stopped working at that time, restart the PC. If it keeps happening, send a report so it can be fixed.")
            };
        case "oom": {
            const name = d.app && d.app.length > 0 ? d.app : d.program;
            return {
                title: qsTr("%1 was closed to free up memory").arg(name),
                detail: qsTr("This PC ran out of memory."),
                icon: "triangle-alert", tone: amber, problem: true, group: "apps",
                happened: qsTr("This PC ran out of memory at %1, so %2 was closed to keep everything else running.").arg(at(when)).arg(name),
                did: qsTr("Closed %1 so the rest of the PC could carry on.").arg(name),
                todo: qsTr("Close apps and browser tabs you aren't using. If it keeps happening, this PC may need more memory for what you do.")
            };
        }
        case "drive-in": {
            const usb = d.kind === "usb";
            return {
                title: usb ? qsTr("A USB drive was connected") : qsTr("A drive was connected"),
                detail: qsTr("It's called %1.").arg(quoted(d.label)),
                icon: "security-key", tone: plain, problem: false, group: "security",
                happened: (usb ? qsTr("A USB drive called %1 was plugged in at %2 and made ready to use.") : qsTr("A drive called %1 was plugged in at %2 and made ready to use."))
                    .arg(quoted(d.label)).arg(at(when)),
                did: qsTr("Made it ready to use, so its files show in Files."),
                todo: qsTr("Nothing, if it's yours. If you don't know where a drive came from, don't open files on it.")
            };
        }
        case "drive-yanked": {
            const usb = d.kind === "usb";
            return {
                title: usb ? qsTr("A USB drive was removed while it was in use") : qsTr("A drive was removed while it was in use"),
                detail: qsTr("Anything being copied to %1 may not have finished.").arg(quoted(d.label)),
                icon: "security-key", tone: red, problem: true, group: "other",
                happened: qsTr("%1 was unplugged at %2 while it was still in use, before it was ejected.").arg(quoted(d.label)).arg(at(when)),
                did: qsTr("Tidied up after it, so it can be plugged in again."),
                todo: qsTr("Plug it back in and check that the files you last copied to it open. Next time, choose Eject in Files before you unplug it.")
            };
        }
        case "signin": {
            const where = d.how === "ssh" ? (d.from ? qsTr("from another computer, over SSH from %1").arg(d.from) : qsTr("from another computer, over SSH"))
                : d.how === "console" ? qsTr("at a text console")
                : d.how === "screen" ? qsTr("at the sign-in screen") : "";
            const happened = d.finger
                ? (where.length > 0 ? qsTr("%1 signed in %2, with a fingerprint, at %3.").arg(d.name).arg(where).arg(at(when))
                   : qsTr("%1 signed in, with a fingerprint, at %2.").arg(d.name).arg(at(when)))
                : (where.length > 0 ? qsTr("%1 signed in %2 at %3.").arg(d.name).arg(where).arg(at(when))
                   : qsTr("%1 signed in at %2.").arg(d.name).arg(at(when)));
            return {
                title: qsTr("%1 signed in").arg(d.name),
                detail: d.finger ? qsTr("With a fingerprint.") : d.how === "ssh" ? qsTr("From another computer, over SSH.") : d.how === "console" ? qsTr("At a text console.") : d.how === "screen" ? qsTr("At the sign-in screen.") : qsTr("To this PC."),
                icon: "log-in", tone: d.how === "ssh" ? blue : plain, problem: false, group: "security",
                happened: happened,
                did: d.how === "ssh" ? qsTr("Let them in once the password or key was right.")
                    : d.finger ? qsTr("Started their desktop once the fingerprint matched one saved for them.") : qsTr("Started their desktop once the password was right."),
                todo: d.how === "ssh" ? qsTr("Nothing, if it was you. If it wasn't, change your password in Accounts, and turn SSH off in Privacy and security > Firewall.")
                    : qsTr("Nothing, if it was you. If it wasn't, change your password in Accounts > Sign-in options.")
            };
        }
        case "signout":
            return {
                title: qsTr("%1 signed out").arg(d.name),
                detail: d.since ? qsTr("After %1.").arg(duration(d.since, when)) : qsTr("Their session ended."),
                icon: "log-out", tone: plain, problem: false, group: "security",
                happened: qsTr("%1's session ended at %2, by signing out, restarting or shutting down.").arg(d.name).arg(at(when)),
                did: qsTr("Closed their apps and desktop."),
                todo: qsTr("Nothing.")
            };
        case "password": {
            const n = d.count || 1;
            const lastAt = new Date((d.last || 0) * 1000);
            const span = n > 1 && Format.isValid(lastAt) && Format.time(lastAt) !== at(when) ? qsTr("between %1 and %2").arg(at(when)).arg(Format.time(lastAt))
                : qsTr("at %1").arg(at(when));
            const ssh = d.place === "ssh";
            let happened;
            if (ssh) {
                happened = n === 1
                    ? (d.from ? qsTr("A wrong password was given over SSH for %1, from %2 %3.").arg(quoted(d.user)).arg(d.from).arg(span)
                       : qsTr("A wrong password was given over SSH for %1 %2.").arg(quoted(d.user)).arg(span))
                    : (d.from ? qsTr("%1 wrong passwords were given over SSH for %2, from %3 %4.").arg(n).arg(quoted(d.user)).arg(d.from).arg(span)
                       : qsTr("%1 wrong passwords were given over SSH for %2 %3.").arg(n).arg(quoted(d.user)).arg(span));
            } else {
                happened = n === 1 ? qsTr("A wrong password was typed for %1 %2 %3.").arg(d.name).arg(passwordPlace(d.place)).arg(span)
                    : qsTr("%1 wrong passwords were typed for %2 %3 %4.").arg(n).arg(d.name).arg(passwordPlace(d.place)).arg(span);
            }
            return {
                title: ssh ? (n === 1 ? qsTr("Someone tried to sign in over SSH") : qsTr("Someone tried to sign in over SSH %1 times").arg(n))
                    : d.place === "admin" ? (n === 1 ? qsTr("A wrong admin password was typed") : qsTr("%1 wrong admin passwords were typed").arg(n))
                    : (n === 1 ? qsTr("A wrong password was typed %1").arg(passwordPlace(d.place)) : qsTr("%1 wrong passwords were typed %2").arg(n).arg(passwordPlace(d.place))),
                detail: ssh ? (d.from ? qsTr("As %1, from %2. It was turned away.").arg(quoted(d.user)).arg(d.from) : qsTr("As %1. It was turned away.").arg(quoted(d.user)))
                    : (n === 1 ? qsTr("For %1. It was turned away.") : qsTr("For %1. Each was turned away.")).arg(d.name),
                icon: "lock-round", tone: n >= 3 ? amber : plain, problem: false, group: "security",
                happened: happened,
                did: n === 1 ? qsTr("Turned it away.") : qsTr("Turned each one away."),
                todo: ssh ? qsTr("If you don't use SSH, turn it off in Privacy and security > Firewall. If you do, make sure every account has a strong password.")
                    : qsTr("Nothing, if it was you mistyping. If it wasn't you, change your password in Accounts > Sign-in options.")
            };
        }
        case "firewall": {
            const who = firewallWho(d.kind);
            // The log's device is the network interface it came in on; people
            // know the sender by its address.
            const from = d.source;
            const many = (d.ports || []).length > 1;
            const n = d.count || 1;
            const what = many ? qsTr("%1 tries on %2 ports").arg(n).arg(d.ports.length)
                : n > 1 ? qsTr("%1, %2 times").arg(portName(d.port, d.proto)).arg(n) : portName(d.port, d.proto);
            const happened = many
                ? (n > 1 ? qsTr("%1 (%2) tried to connect to this PC %3 times, on %4 different ports, starting at %5.")
                   : qsTr("%1 (%2) tried to connect to this PC %3 times, on %4 different ports, at %5."))
                    .arg(capital(who)).arg(d.source).arg(n).arg(d.ports.length).arg(at(when))
                : n > 1 ? qsTr("%1 (%2) tried to connect to this PC for %3, %4 times, starting at %5.")
                    .arg(capital(who)).arg(d.source).arg(portName(d.port, d.proto)).arg(n).arg(at(when))
                : qsTr("%1 (%2) tried to connect to this PC for %3 at %4.").arg(capital(who)).arg(d.source).arg(portName(d.port, d.proto)).arg(at(when));
            return {
                title: n > 1 ? qsTr("Connections from %1 were blocked").arg(who) : qsTr("A connection from %1 was blocked").arg(who),
                detail: qsTr("%1, from %2.").arg(what).arg(from),
                icon: "shield-round", tone: blue, problem: false, group: "security",
                happened: happened,
                did: n > 1 ? qsTr("Turned every try away at the firewall. Nothing on this PC was reached.") : qsTr("Turned it away at the firewall. Nothing on this PC was reached."),
                todo: qsTr("Nothing. If you were expecting it, allow it in Privacy and security > Firewall.")
            };
        }
        case "updates": {
            const n = d.count || 0;
            const count = d.auto ? (n === 1 ? qsTr("1 update, installed automatically.") : qsTr("%1 updates, installed automatically.").arg(n))
                : (n === 1 ? qsTr("1 update.") : qsTr("%1 updates.").arg(n));
            const happened = d.auto ? (n === 1 ? qsTr("1 update was installed automatically, in the background at %1.").arg(at(when))
                    : qsTr("%1 updates were installed automatically, in the background at %2.").arg(n).arg(at(when)))
                : (n === 1 ? qsTr("1 update was installed at %1.").arg(at(when)) : qsTr("%1 updates were installed at %2.").arg(n).arg(at(when)));
            return {
                title: d.auto ? qsTr("Security updates installed") : qsTr("Updates installed"),
                detail: sentences([count, d.restore ? qsTr("A restore point was saved first.") : ""]),
                icon: "refresh-cw", tone: green, problem: false, group: "updates",
                happened: happened,
                did: d.restore ? qsTr("Saved a restore point first, then installed them.") : qsTr("Installed them."),
                todo: d.restore ? qsTr("Nothing. If something stopped working since, you can go back in System > Recovery.") : qsTr("Nothing.")
            };
        }
        case "update-failed":
            return {
                title: qsTr("Updates didn't finish"),
                detail: qsTr("Installing them stopped with an error."),
                icon: "triangle-alert", tone: amber, problem: true, group: "updates",
                happened: qsTr("Installing updates stopped partway at %1, with the error below.").arg(at(when)),
                did: d.auto ? qsTr("Stopped, and will try again with the next automatic update.") : qsTr("Stopped, and kept the error below."),
                todo: qsTr("Open Updates and choose Check for updates to try again. If it fails again, send a report.")
            };
        case "install-failed": {
            const app = d.app && d.app.length > 0 ? d.app : "";
            return {
                title: app.length > 0 ? qsTr("%1 couldn't be installed").arg(app) : qsTr("An app couldn't be installed"),
                detail: qsTr("Installing it stopped with an error."),
                icon: "triangle-alert", tone: amber, problem: true, group: "apps",
                happened: qsTr("Installing %1 stopped partway at %2, with the error below.").arg(app.length > 0 ? app : (d.packages || []).join(", ")).arg(at(when)),
                did: qsTr("Stopped, and kept the error below."),
                todo: qsTr("Try again from the Store. If it fails again, send a report.")
            };
        }
        case "restore-point": {
            // The label is what Recovery lists the point under.
            const what = d.label && d.label.length > 0 ? d.label
                : d.kind === "manual" ? qsTr("Made by you")
                : d.kind === "driver" ? qsTr("Before a driver was installed")
                : d.kind === "update" ? qsTr("Before updates") : "";
            const covers = d.summary && d.summary.length > 0 ? qsTr("It covers %1.").arg(d.summary.charAt(0).toLowerCase() + d.summary.slice(1).replace(/\.$/, "")) : "";
            return {
                title: qsTr("A restore point was saved"),
                detail: what.length > 0 ? what.replace(/\.$/, "") + "." : qsTr("Wild OS can go back to it."),
                icon: "history", tone: green, problem: false, group: "updates",
                happened: sentences([qsTr("A copy of Wild OS's own files was saved at %1, so this PC can go back to how it was then.").arg(at(when)), covers]),
                did: qsTr("Kept it with the other restore points. Your own files aren't part of it and stay as they are."),
                todo: qsTr("Nothing. To go back to it, open System > Recovery.")
            };
        }
        case "app-installed": {
            const name = d.app && d.app.length > 0 ? d.app : d.id;
            return {
                title: qsTr("%1 was installed").arg(name),
                detail: d.via === "flatpak" && d.remote === "flathub" ? qsTr("From Flathub, through the Store.") : qsTr("From the Store."),
                icon: "status-ok", tone: green, problem: false, group: "apps",
                happened: qsTr("%1 was installed at %2.").arg(name).arg(at(when)),
                did: qsTr("Installed it and added it to Start."),
                todo: qsTr("Nothing. To remove it, open Apps > Installed apps.")
            };
        }
        case "app-removed": {
            const name = d.app && d.app.length > 0 ? d.app : d.id;
            return {
                title: qsTr("%1 was removed").arg(name),
                detail: qsTr("It's no longer on this PC."),
                icon: "trash-round", tone: plain, problem: false, group: "apps",
                happened: qsTr("%1 was removed at %2.").arg(name).arg(at(when)),
                did: qsTr("Removed it from this PC and from Start."),
                todo: qsTr("Nothing. To get it back, find it in the Store.")
            };
        }
        case "space-freed": {
            // Free up space (settings-free-space), by hand or by Tidy up
            // automatically when less than 10% of the disk was free.
            const order = ["bin", "restorePoints", "updates", "caches", "leftovers", "winmode", "downloads"];
            const labels = { bin: qsTr("the Recycle Bin"), restorePoints: qsTr("old restore points"), updates: qsTr("update files"),
                caches: qsTr("app caches"), leftovers: qsTr("leftovers from removed apps"),
                winmode: qsTr("unused space inside Windows mode"), downloads: qsTr("large files in Downloads") };
            const items = d.items || {};
            const parts = order.filter(k => items[k] > 0).map(k => labels[k]);
            // Every item named: there are seven at most.
            const what = parts.length <= 1 ? (parts[0] || qsTr("nothing"))
                : both(parts.slice(0, -1).join(", "), parts[parts.length - 1]);
            const total = Object.keys(items).reduce((sum, k) => sum + (items[k] || 0), 0);
            const size = System.formatBytes(total);
            return {
                title: d.auto ? qsTr("Space was freed up automatically") : qsTr("Space was freed up"),
                detail: qsTr("%1 from %2.").arg(size).arg(what),
                icon: "hard-drive", tone: green, problem: false, group: "other",
                happened: d.auto ? qsTr("Less than 10% of the disk was free at %1, so Wild OS cleared the items ticked in Free up space.").arg(at(when))
                    : qsTr("You freed up space at %1.").arg(at(when)),
                did: (items.leftovers > 0 ? qsTr("Cleared %1, %2 in all. Your documents, pictures and settings were not touched, apart from the settings of apps you had removed.")
                      : qsTr("Cleared %1, %2 in all. Your documents, pictures and settings were not touched.")).arg(what).arg(size),
                todo: d.auto ? qsTr("Nothing. To change what is cleared, or to turn this off, go to System > Storage > Free up space.")
                    : qsTr("Nothing.")
            };
        }
        case "apps-updated": {
            const names = d.apps || [];
            const one = names.length === 1;
            const name = one ? names[0] || "" : "";
            return {
                title: one ? (name.length > 0 ? qsTr("%1 was updated").arg(name) : qsTr("An app was updated")) : qsTr("Apps updated"),
                detail: one ? qsTr("To its newest version.") : listNames(names) + ".",
                icon: "refresh-cw", tone: green, problem: false, group: "updates",
                happened: one ? (name.length > 0 ? qsTr("%1 was updated at %2.").arg(name).arg(at(when)) : qsTr("An app was updated at %1.").arg(at(when)))
                    : qsTr("%1 were updated at %2.").arg(listNames(names)).arg(at(when)),
                did: one ? qsTr("Updated it to the newest version.") : qsTr("Updated them to the newest version."),
                todo: qsTr("Nothing.")
            };
        }
        case "programs": {
            const parts = [];
            if (d.installed > 0) {
                parts.push(d.installed === 1 ? qsTr("1 program installed") : qsTr("%1 programs installed").arg(d.installed));
            }
            if (d.removed > 0) {
                parts.push(d.removed === 1 ? qsTr("1 program removed") : qsTr("%1 programs removed").arg(d.removed));
            }
            if (d.updated > 0) {
                parts.push(d.updated === 1 ? qsTr("1 program updated") : qsTr("%1 programs updated").arg(d.updated));
            }
            const line = capital(parts.join(", "));
            return {
                title: qsTr("Programs were changed"),
                detail: qsTr("%1, by an administrator.").arg(line),
                icon: "app-window", tone: plain, problem: false, group: "apps",
                happened: qsTr("%1 at %2, with apt, by someone with the admin password.").arg(line).arg(at(when)),
                did: qsTr("Made the changes asked for."),
                todo: qsTr("Nothing, if it was you or someone you trust with the admin password.")
            };
        }
        case "backup": {
            const drive = d.drive && d.drive.length > 0 ? d.drive : qsTr("the backup drive");
            if (d.result === "failed") {
                return {
                    title: qsTr("Backup didn't finish"),
                    detail: d.reason && d.reason.length > 0 ? d.reason : qsTr("It stopped with an error."),
                    icon: "hard-drive", tone: amber, problem: true, group: "other",
                    happened: qsTr("A backup to %1 stopped at %2 before it finished.").arg(drive).arg(at(when)),
                    did: qsTr("Kept your earlier backups as they were."),
                    todo: qsTr("Open Backup to see what it needs, then choose Back up now.")
                };
            }
            if (d.result === "stopped") {
                return {
                    title: qsTr("Backup was stopped"),
                    detail: qsTr("It stopped before it finished."),
                    icon: "hard-drive", tone: plain, problem: false, group: "other",
                    happened: qsTr("A backup to %1 was stopped at %2 before it finished.").arg(drive).arg(at(when)),
                    did: qsTr("Kept your earlier backups as they were."),
                    todo: qsTr("Choose Back up now in Backup when it suits you.")
                };
            }
            return {
                title: qsTr("Backup finished"),
                detail: d.files > 0 ? (d.files === 1 ? qsTr("1 file copied to %1.").arg(drive) : qsTr("%1 files copied to %2.").arg(d.files).arg(drive))
                    : qsTr("Nothing had changed since the last one."),
                icon: "hard-drive", tone: green, problem: false, group: "other",
                happened: sentences([qsTr("Your files were backed up to %1 at %2.").arg(drive).arg(at(when)), d.warning || ""]),
                did: d.files > 0 ? (d.files === 1 ? qsTr("Copied 1 new or changed file and kept the earlier versions.")
                        : qsTr("Copied %1 new or changed files and kept the earlier versions.").arg(d.files))
                    : qsTr("Checked every file. None had changed."),
                todo: qsTr("Nothing.")
            };
        }
        case "startup": {
            const took = d.seconds !== undefined ? Format.plural(d.seconds, qsTr("second"), qsTr("seconds")) : "";
            const waited = d.waited ? Format.plural(d.waited, qsTr("second"), qsTr("seconds")) : "";
            const ready = took.length > 0
                ? (waited.length > 0 ? qsTr("This PC started in %1, not counting the %2 the disk password screen waited for you. It was ready at %3.").arg(took).arg(waited).arg(at(when))
                   : qsTr("This PC started in %1. It was ready at %2.").arg(took).arg(at(when)))
                : (waited.length > 0 ? qsTr("This PC started, not counting the %1 the disk password screen waited for you. It was ready at %2.").arg(waited).arg(at(when))
                   : qsTr("This PC started. It was ready at %1.").arg(at(when)));
            return {
                title: qsTr("This PC started"),
                detail: took.length > 0 ? qsTr("Started in %1.").arg(took) : qsTr("Started."),
                icon: "power-round", tone: plain, problem: false, group: "other",
                happened: ready,
                did: qsTr("Unlocked the disk with your password and started Wild OS."),
                todo: qsTr("Nothing.")
            };
        }
        case "shutdown":
        case "restart": {
            const restart = e.kind === "restart";
            const by = d.cause === "lid" ? qsTr("when the lid was closed")
                : d.cause === "power" ? qsTr("with the power button")
                : d.cause === "battery" ? qsTr("because the battery was nearly flat")
                : d.cause === "updates" ? qsTr("to finish installing updates")
                : d.cause === "person" ? qsTr("by %1").arg(d.user || qsTr("someone signed in"))
                : d.cause === "system" ? qsTr("by Wild OS or an administrator") : "";
            const happened = restart
                ? (by.length > 0 ? qsTr("This PC restarted %1 at %2, and went through a normal shutdown.").arg(by).arg(at(when))
                   : qsTr("This PC restarted at %1, and went through a normal shutdown.").arg(at(when)))
                : (by.length > 0 ? qsTr("This PC shut down %1 at %2, and went through a normal shutdown.").arg(by).arg(at(when))
                   : qsTr("This PC shut down at %1, and went through a normal shutdown.").arg(at(when)));
            return {
                title: restart ? qsTr("This PC restarted") : qsTr("This PC was shut down"),
                detail: by.length > 0 ? capital(by) + "." : (restart ? qsTr("It restarted properly.") : qsTr("It shut down properly.")),
                icon: "power-round", tone: plain, problem: false, group: "other",
                happened: happened,
                did: restart ? qsTr("Closed the apps that were open and finished writing everything to the disk before restarting.")
                    : qsTr("Closed the apps that were open and finished writing everything to the disk before switching off."),
                todo: qsTr("Nothing.")
            };
        }
        case "unclean": {
            const last = new Date((d.last || 0) * 1000);
            const lid = d.lid ? new Date(d.lid * 1000) : null;
            return {
                title: qsTr("This PC didn't shut down properly"),
                detail: d.asleep ? (lid ? qsTr("It was asleep when it stopped, with the lid closed.") : qsTr("It was asleep when it stopped."))
                    : lid ? qsTr("The lid was closed at %1 and it stayed on until it stopped.").arg(Format.time(lid))
                    : qsTr("It lost power or stopped responding."),
                icon: "triangle-alert", tone: amber, problem: true, group: "other",
                happened: d.asleep ? qsTr("The last thing Wild OS recorded was %1. After that it stopped without shutting down, while it was asleep: the battery ran out, the power was cut, the power button was held down or it stopped responding.").arg(dayAndTime(last))
                    : lid ? qsTr("The last thing Wild OS recorded was %1. After that it stopped without shutting down, with the lid closed since %2: the battery ran out, the power was cut, the power button was held down or it stopped responding.").arg(dayAndTime(last)).arg(dayAndTime(lid))
                    : qsTr("The last thing Wild OS recorded was %1. After that it stopped without shutting down: the battery ran out, the power was cut, the power button was held down or it stopped responding.").arg(dayAndTime(last)),
                did: qsTr("Started normally the next time. Anything open and not saved then may have lost its latest changes."),
                todo: d.asleep ? qsTr("A PC that is asleep still uses a little battery, and stops like this when it runs out. To have it switch off instead, set what closing the lid does to Shut down in Settings > System > Power and battery > More power settings.")
                    : lid ? qsTr("Closing the lid didn't shut it down or put it to sleep. Check what closing the lid does in Settings > System > Power and battery > More power settings, and look for \"The lid was closed but the PC stayed on\" here.")
                    : qsTr("If you didn't switch it off yourself and it keeps happening, send a report so it can be looked at.")
            };
        }
        case "sleep": {
            const hib = d.kind === "hibernate";
            const why = d.reason === "lid" ? qsTr("when the lid was closed")
                : d.reason === "power" ? qsTr("with the power button")
                : d.reason === "person" ? qsTr("when %1 chose Sleep or left it idle").arg(d.user || qsTr("someone"))
                : d.reason === "system" ? qsTr("when Wild OS asked it to") : "";
            const woke = d.woke ? new Date(d.woke * 1000) : null;
            const went = hib
                ? (why.length > 0 ? qsTr("This PC hibernated %1 at %2.").arg(why).arg(at(when)) : qsTr("This PC hibernated at %1.").arg(at(when)))
                : (why.length > 0 ? qsTr("This PC went to sleep %1 at %2.").arg(why).arg(at(when)) : qsTr("This PC went to sleep at %1.").arg(at(when)));
            return {
                title: hib ? qsTr("This PC hibernated") : qsTr("This PC went to sleep"),
                detail: woke ? qsTr("Woke up at %1, after %2.").arg(Format.time(woke)).arg(duration(0, new Date(d.slept * 1000))) : qsTr("It didn't wake up from this."),
                icon: "moon", tone: plain, problem: false, group: "other",
                happened: sentences([went, woke ? qsTr("It woke up %1.").arg(dayAndTime(woke))
                    : qsTr("There is no record of it waking up: it ran out of battery or was switched off while asleep.")]),
                did: hib ? qsTr("Saved everything open to the disk and switched off.") : qsTr("Kept everything open in memory and used as little power as it could."),
                todo: qsTr("Nothing.")
            };
        }
        case "lid-on": {
            const apps = (d.apps || []).reduce((list, app) => list.length > 0 ? both(list, app) : app, "");
            const actionWords = { "nothing": qsTr("do nothing"), "lock": qsTr("lock the screen"), "screen-off": qsTr("turn off the screen"), "ask": qsTr("ask what to do"),
                "shutdown": qsTr("shut down"), "sleep": qsTr("go to sleep"), "hibernate": qsTr("hibernate") };
            const action = actionWords[d.action] || qsTr("act");
            const page = qsTr("Settings > System > Power and battery > More power settings");
            const opened = d.opened ? new Date(d.opened * 1000) : null;
            const setTo = d.profile === "Battery" ? qsTr("Closing the lid is set to %1 on battery.").arg(actionWords[d.action])
                : d.profile === "AC" ? qsTr("Closing the lid is set to %1 when plugged in.").arg(actionWords[d.action])
                : qsTr("Closing the lid is set to %1.").arg(actionWords[d.action]);
            const reason = d.reason === "app" ? qsTr("Kept on by %1.").arg(apps)
                : d.reason === "setting" ? setTo
                : d.reason === "screen" ? qsTr("Another screen was connected.")
                : d.reason === "held-up" ? qsTr("It started to shut down, but an app held it up.")
                : d.reason === "not-done" ? qsTr("It was set to %1, but didn't.").arg(action)
                : d.reason === "signin-setting" ? qsTr("At the sign-in screen, closing the lid is set to do nothing.")
                : qsTr("Nothing recorded why.");
            const because = d.reason === "app" ? qsTr("%1 asked Wild OS not to, while it worked.").arg(apps)
                : d.reason === "setting" ? (d.profile === "Battery" ? qsTr("That is what closing the lid is set to do on battery.")
                    : d.profile === "AC" ? qsTr("That is what closing the lid is set to do when plugged in.")
                    : qsTr("That is what closing the lid is set to do."))
                : d.reason === "screen" ? qsTr("Another screen was connected, and closing the lid is set to do nothing then.")
                : d.reason === "held-up" ? qsTr("Closing the lid is set to shut down, and it started to: Wild OS began signing out. An open app held it up, usually by asking whether to save your work, and nobody could answer with the lid closed. Once an app has asked, the shutdown is called off, so the PC stays on until it is shut down another way.")
                : d.reason === "not-done" && d.action === "shutdown" ? qsTr("It was set to shut down, but the shutdown didn't happen. An app with unsaved work can stop it.")
                : d.reason === "not-done" ? qsTr("It was set to %1, but that didn't happen.").arg(action)
                : d.reason === "signin-setting" ? qsTr("Nobody was signed in, and at the sign-in screen closing the lid is set to do nothing.")
                : "";
            return {
                title: qsTr("The lid was closed but the PC stayed on"),
                detail: reason,
                icon: "computer", tone: amber, problem: true, group: "other",
                happened: sentences([qsTr("The lid was closed at %1 and nothing happened in the minute after: the PC didn't shut down or go to sleep.").arg(at(when)),
                    because, opened ? qsTr("The lid was opened again %1.").arg(dayAndTime(opened)) : ""]),
                did: qsTr("Kept running. A PC left on like this in a bag keeps using its battery and can get warm."),
                todo: d.reason === "app" ? qsTr("Close %1, or let it finish, before you close the lid.").arg(apps)
                    : d.reason === "screen" ? qsTr("To make the lid act anyway, turn on Do this even when another screen is connected in %1.").arg(page)
                    : d.reason === "held-up" || d.reason === "not-done" && d.action === "shutdown" ? qsTr("Save your work and close apps before you close the lid, or set the lid to Sleep in %1, which keeps everything as it is.").arg(page)
                    : qsTr("To change what closing the lid does, open %1.").arg(page)
            };
        }
        case "finger-unknown": {
            const n = d.count || 1;
            return {
                title: n === 1 ? qsTr("A fingerprint wasn't recognised") : qsTr("%1 fingerprints weren't recognised").arg(n),
                detail: (n === 1 ? qsTr("For %1. It was turned away.") : qsTr("For %1. Each was turned away.")).arg(d.name),
                icon: "fingerprint", tone: n >= 3 ? amber : plain, problem: false, group: "security",
                happened: n === 1 ? qsTr("A finger that didn't match any saved for %1 was put on the reader at %2.").arg(d.name).arg(at(when))
                    : qsTr("%1 fingers that didn't match any saved for %2 were put on the reader at %3.").arg(n).arg(d.name).arg(at(when)),
                did: n === 1 ? qsTr("Didn't let it in.") : qsTr("Didn't let any of them in."),
                todo: qsTr("Nothing, if it was you. If your finger often isn't recognised, add it again in Accounts > Sign-in options.")
            };
        }
        case "unlock":
            return {
                title: qsTr("%1 unlocked the PC").arg(d.name),
                detail: qsTr("With a fingerprint."),
                icon: "fingerprint", tone: plain, problem: false, group: "security",
                happened: qsTr("%1 unlocked the lock screen with a fingerprint at %2.").arg(d.name).arg(at(when)),
                did: qsTr("Unlocked once the fingerprint matched one saved for them."),
                todo: qsTr("Nothing, if it was you. If it wasn't, remove the saved fingerprints in Accounts > Sign-in options.")
            };
        case "admin-finger": {
            const n = d.count || 1;
            const sudo = d.by === "sudo";
            const happened = n === 1
                ? (sudo ? qsTr("%1 approved a task that needs the admin password with a fingerprint, in Terminal, at %2.")
                   : qsTr("%1 approved a task that needs the admin password with a fingerprint at %2.")).arg(d.name).arg(at(when))
                : (sudo ? qsTr("%1 approved %2 tasks that need the admin password with a fingerprint, in Terminal, at %3.")
                   : qsTr("%1 approved %2 tasks that need the admin password with a fingerprint at %3.")).arg(d.name).arg(n).arg(at(when));
            return {
                title: n === 1 ? qsTr("An admin task was approved with a fingerprint") : qsTr("%1 admin tasks were approved with a fingerprint").arg(n),
                detail: (sudo ? qsTr("By %1, in Terminal.") : qsTr("By %1.")).arg(d.name),
                icon: "fingerprint", tone: blue, problem: false, group: "security",
                happened: happened,
                did: n === 1 ? qsTr("Let it go ahead once the fingerprint matched.") : qsTr("Let them go ahead once the fingerprint matched."),
                todo: qsTr("Nothing, if it was you. To use the password instead, turn fingerprints off for admin tasks in Accounts > Sign-in options.")
            };
        }
        case "disk-error": {
            const n = d.count || 1;
            const what = d.disk === "system" ? qsTr("The system disk") : d.disk === "usb" ? qsTr("A USB drive") : qsTr("A disk");
            return {
                title: qsTr("%1 had trouble reading or writing").arg(what),
                detail: qsTr("%1, on %2.").arg(Format.plural(n, qsTr("error"), qsTr("errors"))).arg(d.device),
                icon: "hard-drive", tone: red, problem: true, group: "other",
                happened: n > 1 ? qsTr("%1 (%2) couldn't read or write some of its data at %3, %4 times within the hour.").arg(what).arg(d.device).arg(at(when)).arg(n)
                    : qsTr("%1 (%2) couldn't read or write some of its data at %3.").arg(what).arg(d.device).arg(at(when)),
                did: qsTr("Reported the error to the program that was reading or writing. The data it was after may be damaged."),
                todo: d.disk === "usb" ? qsTr("Copy what you need off it and use another drive. Choose Eject in Files before you unplug it.")
                    : d.disk === "system" ? qsTr("Back up your files now with Backup. If it happens again, the disk may be failing.")
                    : qsTr("Copy what you need off it. If it happens again, the disk may be failing.")
            };
        }
        case "winmode-failed": {
            const resume = d.action === "resume";
            return {
                title: resume ? qsTr("Windows mode couldn't resume") : qsTr("Windows mode couldn't start"),
                detail: qsTr("Windows apps couldn't open."),
                icon: "windowsapps", tone: amber, problem: true, group: "apps",
                happened: (resume ? qsTr("Windows mode was asked to carry on where it left off at %1, and it couldn't.")
                    : qsTr("Windows mode was asked to start Windows at %1, and it couldn't.")).arg(at(when)),
                did: qsTr("Left Windows as it was and said so on screen."),
                todo: qsTr("Try again from Windows mode on the taskbar. If it keeps happening, restart the PC or send a report.")
            };
        }
        // From the camera and microphone lights on the taskbar: amber and
        // green, as the lights are.
        case "device-use": {
            const camera = d.device === "camera";
            const thing = camera ? qsTr("the camera") : qsTr("the microphone");
            const app = d.app && d.app.length > 0 ? d.app : qsTr("An app");
            const ended = typeof d.end === "number" ? new Date(d.end * 1000) : null;
            const span = ended ? qsTr("From %1 to %2, %3.").arg(at(when)).arg(at(ended)).arg(duration(when.getTime() / 1000, ended))
                : qsTr("From %1.").arg(at(when));
            return {
                title: qsTr("%1 used %2").arg(app).arg(thing),
                detail: span,
                icon: camera ? "camera" : "mic", tone: camera ? green : amber, problem: false, group: "security",
                happened: ended ? qsTr("%1 started using %2 at %3 and stopped at %4, after %5.").arg(app).arg(thing).arg(at(when)).arg(at(ended)).arg(duration(when.getTime() / 1000, ended))
                    : qsTr("%1 started using %2 at %3.").arg(app).arg(thing).arg(at(when)),
                did: camera ? qsTr("Showed the camera light on the taskbar while it was in use, and noted which app and when, nothing more.")
                    : qsTr("Showed the microphone light on the taskbar while it was in use, and noted which app and when, nothing more."),
                todo: qsTr("Nothing, if you expected it. If not, click the light on the taskbar while it is on: Stop cuts the app off, and the switch there turns %1 off for every app.").arg(thing)
            };
        }
        case "setting": {
            const service = { files: qsTr("File sharing"), remote: qsTr("Remote Desktop"), ssh: "SSH" };
            const levels = { standard: qsTr("Standard"), strict: qsTr("Strict"), maximum: qsTr("Maximum") };
            const port = portLabel(d.port, d.proto);
            const network = d.network || qsTr("A network");
            const ruleName = d.name || port;
            const title = d.what === "firewall" ? (d.on ? qsTr("The firewall was turned on") : qsTr("The firewall was turned off"))
                : d.what === "trust" ? (d.trust === "public" ? qsTr("%1 was set to Public") : qsTr("%1 was set to Home")).arg(network)
                : d.what === "discovery" ? (d.on ? qsTr("Network discovery was turned on") : qsTr("Network discovery was turned off"))
                : d.what === "service" ? (d.on ? qsTr("%1 was turned on") : qsTr("%1 was turned off")).arg(service[d.service] || d.service)
                : d.what === "allow" ? qsTr("%1 was allowed through the firewall").arg(ruleName)
                : d.what === "rule" ? (d.change === "removed" ? qsTr("%1 was removed from the firewall").arg(ruleName)
                    : d.change === "on" ? qsTr("%1 was turned on in the firewall").arg(ruleName)
                    : d.change === "off" ? qsTr("%1 was turned off in the firewall").arg(ruleName)
                    : qsTr("%1 was turned %2 in the firewall").arg(ruleName).arg(d.change))
                : d.what === "level" ? qsTr("Protection was set to %1").arg(levels[d.level] || d.level)
                : qsTr("A security setting was changed");
            const weaker = (d.what === "firewall" && !d.on) || (d.what === "service" && d.on) || d.what === "allow"
                || (d.what === "level" && d.level === "standard");
            const scope = d.what !== "allow" ? ""
                : d.scope === "anyone" ? qsTr("%1, from anyone.").arg(port)
                : d.scope === "device" ? qsTr("%1, from %2 only.").arg(port).arg(d.address)
                : qsTr("%1, from this network.").arg(port);
            const from = d.what === "level" && d.from ? levels[d.from] || d.from : "";
            const happened = from.length > 0
                ? (d.user ? qsTr("%1 at %2, from %3, by %4.").arg(title).arg(at(when)).arg(from).arg(d.user) : qsTr("%1 at %2, from %3.").arg(title).arg(at(when)).arg(from))
                : (d.user ? qsTr("%1 at %2, by %3.").arg(title).arg(at(when)).arg(d.user) : qsTr("%1 at %2.").arg(title).arg(at(when)));
            return {
                title: title,
                detail: sentences([scope, d.user ? qsTr("By %1.").arg(d.user) : qsTr("By Wild OS.")]),
                icon: "shield-round", tone: weaker ? amber : blue, problem: false, group: "security",
                happened: happened,
                did: d.what === "level" ? qsTr("Changed the firewall, USB and other protections to match.") : qsTr("Loaded the firewall's rules again to match."),
                todo: d.what === "level" ? qsTr("Nothing, if it was you. To change it back, open Settings > Privacy and security.")
                    : qsTr("Nothing, if it was you. To change it back, open Settings > Privacy and security > Firewall.")
            };
        }
        case "went-back": {
            const staged = d.how === "staged";
            const recovery = d.how === "recovery";
            const label = d.label || "";
            let happened;
            if (staged) {
                happened = label.length > 0 ? qsTr("Going back to restore point %1 (%2) was set up at %3, to happen at the next start-up.").arg(d.point).arg(label).arg(at(when))
                    : qsTr("Going back to restore point %1 was set up at %2, to happen at the next start-up.").arg(d.point).arg(at(when));
            } else if (label.length > 0) {
                happened = (recovery ? qsTr("Wild OS's own files went back to restore point %1 (%2) at %3, from start-up recovery.")
                    : qsTr("Wild OS's own files went back to restore point %1 (%2) at %3.")).arg(d.point).arg(label).arg(at(when));
            } else {
                happened = (recovery ? qsTr("Wild OS's own files went back to restore point %1 at %2, from start-up recovery.")
                    : qsTr("Wild OS's own files went back to restore point %1 at %2.")).arg(d.point).arg(at(when));
            }
            return {
                title: staged ? qsTr("Going back to an earlier version was set up") : qsTr("This PC went back to an earlier version"),
                detail: staged ? qsTr("It happens when the PC restarts.")
                    : sentences([label.length > 0 ? label.replace(/\.$/, "") + "." : qsTr("To restore point %1.").arg(d.point),
                        recovery ? qsTr("From start-up recovery.") : ""]),
                icon: "history", tone: green, problem: false, group: "updates",
                happened: happened,
                did: staged ? qsTr("Saved a restore point of the version you're on first, so this can be undone.") : qsTr("Put back the system files and apps of that restore point. Your own files, Wi-Fi and accounts stayed as they were."),
                // The restore point's label is written by restore-points,
                // in English, so it is quoted as Recovery lists it.
                todo: staged ? qsTr("Restart when you're ready.") : qsTr("To undo it, go back to \"Before going back to an earlier version\" in System > Recovery.")
            };
        }
        case "reset":
            return {
                title: qsTr("Wild OS was reset"),
                detail: qsTr("Keeping your files. A restore point was saved first."),
                icon: "rotate-ccw", tone: green, problem: false, group: "updates",
                happened: qsTr("Reset Wild OS started at %1, from start-up recovery, keeping your files.").arg(at(when)),
                did: qsTr("Saved a restore point, then put Wild OS's own files back to the copy kept on this PC. Your files and accounts were kept."),
                todo: qsTr("Reinstall any apps you miss from the Store. If something is wrong, go back to \"Before resetting Wild OS\" in System > Recovery.")
            };
        }
        return {
            title: qsTr("Something happened"), detail: "", icon: "info", tone: plain, problem: false, group: "other",
            happened: "", did: "", todo: qsTr("Nothing.")
        };
    }

    // The board's three columns: when, which program, what it said. In a
    // file or a report "Today" would go stale, so those get dates.
    function techText(e, dated) {
        const lines = e.tech || [];
        const width = Math.min(16, Math.max(4, ...lines.map(l => l.source.length))) + 3;
        return lines.map(l => {
            const day = Format.day(l.when);
            const name = dated ? Format.shortDate(l.when) : day === "today" ? qsTr("Today") : day === "yesterday" ? qsTr("Yesterday") : Format.shortDate(l.when);
            return name + " " + Qt.formatTime(l.when, "HH:mm:ss") + "   " + (l.source + " ".repeat(width)).slice(0, width) + l.text;
        }).join("\n");
    }

    // The whole event as text, for Copy, Save as a file and the report.
    function fullText(e, all) {
        const w = describe(e, all);
        return w.title + "\n" + qsTr("%1, %2").arg(Format.longDate(e.when)).arg(Qt.formatTime(e.when, "HH:mm:ss")) + "\n\n"
            + qsTr("What happened") + "\n" + w.happened + "\n\n"
            + qsTr("What Wild OS did") + "\n" + w.did + "\n\n"
            + qsTr("What you can do") + "\n" + w.todo + "\n\n"
            + qsTr("Technical detail") + "\n" + techText(e, true) + "\n";
    }
}
