"""Setting up Windows mode: everything from nothing to Windows ready, in the
system service (brief 5.1 to 5.4, boards 9.5 and 9.7).

The steps were first proven on a real PC on 7 November 2026: the
licence, Windows from Microsoft, the drivers, the answer file and setup
payload, the isolated network, the virtual PC, the install with setup.ps1,
activation and Microsoft 365. They run in a worker thread and report
progress in the stages board 9.5 shows:

  media     download Windows and the parts it needs, checked
  install   Windows Setup, until Windows answers its agent
  activate  slmgr /ato with the licence chosen
  tune      setup.ps1's own steps inside Windows, until it reports ready
  office    Microsoft 365, if chosen

Every step is resumable. Progress lives in the service's state file; after
a restart (of the service, or of the PC: wildos-winmode-setup-resume.service
wakes the service at boot while a setup is under way) the worker carries on
from the step it was on. Downloads continue where they stopped.
"""

import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

from . import config, firewall, isocheck, msdl, qga, virsh

CACHE = os.path.join(config.STATE_DIR, "cache")
WINDOWS_ISO = os.path.join(CACHE, "windows.iso")
VIRTIO = os.path.join(CACHE, "virtio-win.iso")
PAYLOAD = os.path.join(CACHE, "payload.iso")
SECRETS = os.path.join(config.STATE_DIR, "secrets")
DISK = os.path.join(config.STATE_DIR, "windows.qcow2")
# Present while a setup is under way, for the unit that resumes it at boot.
MARKER = os.path.join(config.STATE_DIR, "setting-up")
TEMPLATES = os.path.join(config.DATA, "templates")

STAGES = ["media", "install", "activate", "tune", "office"]
# How much of setup each stage is, for the one progress bar, and about how
# long each takes on a typical PC, for "About N minutes left".
WEIGHTS = {"media": 25, "install": 45, "activate": 5, "tune": 10, "office": 15}
MINUTES = {"media": 15, "install": 25, "activate": 1, "tune": 10, "office": 15}

# A Windows 11 disk holds about this much once Setup has copied it.
INSTALLED_BYTES = 16 * 1000 ** 3
INSTALL_LIMIT = 2 * 3600
# Windows Setup has written this much within a few minutes of starting; a
# disk still under it after BOOT_WAIT seconds means it never started.
BOOT_WRITTEN = 200 * 1000 ** 2
BOOT_WAIT = 300
BOOT_TRIES = 3
# How long setup.ps1 has inside Windows, from the start of the tune stage.
TUNE_LIMIT = 2 * 3600
# How long Windows has to answer its agent once started for a later stage.
AGENT_WAIT = 600
# The task a retry of the tune stage runs setup.ps1 from again.
RERUN_TASK = "Wild OS setup again"
# setup.ps1's steps, in order (guest/setup.ps1), for the "tune" stage.
GUEST_STEPS = ["guest-tools", "shared-folder", "wild-guest", "remote-desktop", "account", "tuning", "updates",
               "known-folders", "office-policies", "startup-trim", "opengl", "theme", "ready-task"]

TOOL_AGENT = "wildos-winmode (+https://wildos.io)"
VIRTIO_BASE = "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads"
# Microsoft's published generic installation key for Pro: installs Pro but
# never activates. Every setup installs with it, so Windows is always Pro,
# the edition with the Remote Desktop host Windows mode needs. The person's
# own key, from the firmware or typed in, goes in at the activate stage,
# where Windows turns down one for Home, instead of an hour of setup ending
# on an edition that can't work.
GENERIC_PRO_KEY = "VK7JG-NPHTM-C97JM-9MPGT-3V66T"
ACTIVATION_KEY = "activation-key"
KEY_ALPHABET = "BCDFGHJKMNPQRTVWXY2346789"
GUEST_MAC = "52:54:00:57:4f:01"
OFFICE_PRODUCTS = {"personal": "O365HomePremRetail", "family": "O365HomePremRetail",
                   "business": "O365BusinessRetail", "work": "O365ProPlusRetail",
                   # A one-off purchase: the key goes in when Word first opens,
                   # and Home and Business 2024 is the retail edition with
                   # Outlook that most keys are sold for.
                   "key": "HomeBusiness2024Retail"}
# Setup runs on whatever the PC isn't using (boards 9.5 and 9.12): the
# worker thread and the virtual PC get the smallest share of the processor
# and the disk while it lasts, so the desktop always comes first.
SPARE_CPU_WEIGHT = 1
SPARE_IO_WEIGHT = 10
WORD = r"C:\Program Files\Microsoft Office\root\Office16\WINWORD.EXE"


def log(*parts):
    print("setup:", *parts, flush=True)


def is_iso(path):
    """A real disc image: big enough, with ISO 9660's signature where it
    belongs (a web page saved in its place has neither)."""
    try:
        if os.path.getsize(path) < 50 * 1000 ** 2:
            return False
        with open(path, "rb") as f:
            f.seek(0x8001)
            return f.read(5) == b"CD001"
    except OSError:
        return False


def runtime_installed():
    p = subprocess.run(["dpkg-query", "-W", "-f=${db:Status-Status}", "wildos-winmode-runtime"],
                       capture_output=True, text=True)
    return p.stdout.strip() == "installed"


class Stopped(Exception):
    """The worker was asked to stop (cancel, or the service is going)."""


def apt_locked(p):
    """An apt-get run that stopped because another one, or Settings'
    updates, held the package lock."""
    out = (p.stderr or "") + (p.stdout or "")
    return "Could not get lock" in out or "Unable to acquire" in out or "is held by process" in out


# How long a download may sit without a byte before it counts as stopped.
READ_TIMEOUT = 20
# While offline: how often to look again, and how often to try the server
# itself in case NetworkManager's own check is what fails (a network that
# blocks it).
OFFLINE_POLL = 3
OFFLINE_PROBE = 30
OFFLINE_TEXT = "Waiting for the internet. Setup carries on by itself as soon as you're back online."
# The units apt-get runs in, outside Windows mode's own service: one that
# installs, which is always let finish, and one that only refreshes the
# package lists, which can be stopped at any point.
APT_UNIT = "wildos-winmode-runtime-install"
APT_REFRESH_UNIT = "wildos-winmode-apt-refresh"
APT_LOG = "/run/wildos-winmode-apt.log"


def online():
    """False when NetworkManager says this PC has no working way to the
    internet: no connection, a sign-in page or limited. When there is no
    NetworkManager to ask, or it doesn't know, the download decides."""
    try:
        from gi.repository import Gio, GLib
        bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        reply = bus.call_sync("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                              "org.freedesktop.DBus.Properties", "Get",
                              GLib.Variant("(ss)", ("org.freedesktop.NetworkManager", "Connectivity")),
                              GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 3000, None)
        # 1 none, 2 portal, 3 limited, 4 full, 0 unknown
        return reply.unpack()[0] not in (1, 2, 3)
    except Exception:  # noqa: BLE001 (no NetworkManager, or no D-Bus: can't tell)
        return True


def reachable(host, port=443, timeout=5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def apt_command(args, unit=APT_UNIT):
    """apt-get (or dpkg) in a unit of its own. Run as a child of the
    service, it was killed with it when the service stopped part way (Task
    Manager, or an update of Windows mode), and dpkg killed mid-install
    leaves every later install refusing to run, Settings' updates included,
    until someone runs dpkg --configure -a."""
    if shutil.which("systemd-run") and os.path.isdir("/run/systemd/system"):
        return ["systemd-run", "--quiet", "--wait", "--collect", "--service-type=exec", f"--unit={unit}",
                "--setenv=DEBIAN_FRONTEND=noninteractive", "--setenv=LC_ALL=C.UTF-8",
                f"--property=StandardOutput=truncate:{APT_LOG}", f"--property=StandardError=append:{APT_LOG}",
                "--property=Nice=10", "--"] + args
    return args


def dpkg_interrupted(p):
    out = (p.stderr or "") + (p.stdout or "")
    return "dpkg was interrupted" in out


def apt_progress(line):
    """A line apt writes to APT::Status-Fd as what setup shows: the part
    being downloaded or installed and how far through it is, or None."""
    kind, _, rest = line.partition(":")
    if kind not in ("dlstatus", "pmstatus"):
        return None
    try:
        pct = int(float(rest.split(":")[1]))
    except (IndexError, ValueError):
        return None
    verb = "Downloading" if kind == "dlstatus" else "Installing"
    return f"{verb} the parts Windows mode runs on, {max(0, min(100, pct))}%"


def latest_apt_progress(text):
    """The last progress line in apt's output so far, as apt_progress
    shows it, or None."""
    for line in reversed(text.splitlines()):
        shown = apt_progress(line)
        if shown:
            return shown
    return None


class Problem(Exception):
    """A step failed in a way the person can act on. kind is what board 9.7
    shows: download, space, virtualisation or failed. retry is False when
    only the person can put it right (a licence that can't be read, their
    own file gone), so running the step again by itself would not help."""

    def __init__(self, kind, text, retry=True):
        super().__init__(text)
        self.kind = kind
        self.text = text
        self.retry = retry


class Redo(Exception):
    """Something an earlier stage made has gone (the virtual PC's disk, say):
    that stage runs again before this one carries on. Not a failure."""

    def __init__(self, stage, why):
        super().__init__(why)
        self.stage = stage


# Automatic recovery. A stage that fails is run again once by itself; a
# second failure in a row purges what that stage made and goes back to the
# stage before it. If the stages after going back fail twice more, setup
# stops and shows the problem with Try again and Start over, as it always
# did. The person never has to press anything for the first three tries.
RETRY_NOTICE = "Setup hit a problem and is trying that step again."
BACK_NOTICE = "Setup hit the same problem twice, so it's going back a step and doing that again."
# A short wait before trying again, so a cause that clears up by itself (a
# busy disk, libvirt restarting after an update) has time to.
RETRY_WAIT = 10
# Times an earlier stage may be redone in one run because its result went
# missing, before that counts as a failure.
REDO_LIMIT = 3


def previous_stage(stage, stages=STAGES):
    """The stage before this one, or the first stage itself."""
    i = stages.index(stage) if stage in stages else 0
    return stages[max(0, i - 1)]


def next_move(recovery, stage):
    """What to do after `stage` failed, from the recovery record so far:
    ("retry" | "back" | "stop", the new record). Pure, for the tests."""
    r = dict(recovery or {})
    if r.get("back"):
        # Already went back once: one more try, then stop.
        r["backFails"] = r.get("backFails", 0) + 1
        return ("stop" if r["backFails"] >= 2 else "retry"), r
    fails = r.get("fails", 0) + 1 if r.get("stage") == stage else 1
    if fails < 2:
        return "retry", {"stage": stage, "fails": fails}
    return "back", {"stage": stage, "fails": fails, "back": previous_stage(stage), "backFails": 0}


def recovered(recovery, stage):
    """The recovery record once `stage` has finished: cleared when that was
    the stage which failed, so a later stage starts with its own tries."""
    r = recovery or {}
    return None if r.get("stage") == stage else (r or None)


def size_text(n):
    return f"{n / 1000 ** 3:.1f} GB" if n >= 1000 ** 3 else f"{max(1, round(n / 1000 ** 2))} MB"


# The licence (brief 2.1).
def key_shape_ok(key):
    return bool(re.fullmatch(rf"([{KEY_ALPHABET}N]{{5}}-){{4}}[{KEY_ALPHABET}N]{{5}}", key or "")) and key.count("N") <= 1


# Why slmgr /ato failed, by the error code it prints (the text around it
# follows Windows' language, the code does not). Only a missing connection
# clears up by itself; every other answer needs something from the person.
EDITION_ERRORS = {"C004F069", "C004F050", "C004F015", "C004F210"}
ACTIVATION_ERRORS = [
    ({"C004C008", "C004C770", "C004C771", "803FA071"},
     "Microsoft says this key is already in use on another PC, or on an earlier Windows mode. Microsoft may let you move it: "
     "open the Windows desktop, then Settings, Activation, and choose Troubleshoot."),
    ({"C004C003", "C004C060", "C004C4A2", "C004E028"},
     "Microsoft won't activate with this key. It may be blocked or not genuine. Check where it came from."),
    ({"C004F074", "8007232B", "C004F039", "C004F038"},
     "This is a volume licence key that activates through an organisation's own server, which Windows mode can't reach."),
    ({"803FA067", "C004F213", "C004C020"},
     "Microsoft found no licence for this PC with this key. Check the key is for Windows 11 Pro."),
]
OFFLINE_ERRORS = {"80072EE7", "80072EFD", "80072EE2", "80072F8F", "8007267C", "80072F78", "C004C001"}
NOT_ONLINE = "The key is in, but Windows couldn't reach Microsoft to activate. It tries again by itself when it's online."


def activation_reason(output, firmware=False):
    """What to tell someone whose key went in but did not activate, from
    slmgr /ato's output. None when it was only the connection, which Windows
    retries by itself."""
    codes = [c.upper() for c in re.findall(r"0x([0-9A-Fa-f]{8})", output or "")]
    for code in codes:
        if code in OFFLINE_ERRORS:
            return None
        if code in EDITION_ERRORS:
            return ("This PC's licence is for another edition of Windows, usually Home, which Windows mode can't use."
                    if firmware else
                    "Microsoft says this key is for another edition of Windows, usually Home. Windows mode runs "
                    "Windows 11 Pro, so it needs a Windows 11 Pro key.")
        for known, text in ACTIVATION_ERRORS:
            if code in known:
                return text
    if codes:
        return f"Windows didn't activate. Microsoft's answer was 0x{codes[0]}; check the key, or try again later."
    return None


def parse_msdm(table):
    """The product key in an ACPI MSDM table: the 36 byte header, then
    version 1, reserved, data type 1, reserved, data length 29, and the key.
    None when the table isn't a valid one."""
    if len(table) < 85 or table[:4] != b"MSDM":
        return None
    length = int.from_bytes(table[4:8], "little")
    if length < 85 or length > len(table) or sum(table[:length]) % 256 != 0:
        return None
    field = lambda off: int.from_bytes(table[off:off + 4], "little")  # noqa: E731
    if field(36) != 1 or field(44) != 1 or field(52) != 29:
        return None
    key = table[56:85].decode("ascii", "replace")
    return key if key_shape_ok(key) else None


def firmware():
    """What the PC's firmware and disks say about its Windows licence, for
    board 9.1: whether there is a key, and whether Windows is still
    installed (its edition, from its registry, when hivexget can read it).
    The key itself stays here."""
    out = {"msdm": False, "windows": False, "edition": "", "description": ""}
    try:
        with open("/sys/firmware/acpi/tables/MSDM", "rb") as f:
            out["msdm"] = parse_msdm(f.read()) is not None
    except OSError:
        pass
    if os.path.exists("/boot/efi/EFI/Microsoft/Boot/bootmgfw.efi"):
        out["windows"] = True
    if shutil.which("hivexget"):
        try:
            disks = subprocess.run(["lsblk", "-nrpo", "NAME,FSTYPE"], capture_output=True, text=True, timeout=10).stdout
        except (OSError, subprocess.SubprocessError):
            disks = ""
        for line in disks.splitlines():
            parts = line.split()
            if len(parts) != 2 or parts[1] != "ntfs" or out["edition"]:
                continue
            _read_windows(parts[0], out)
    return out


def _read_windows(dev, out):
    # The kernel's own ntfs3 driver, read-only, never through FUSE, in a
    # private folder that goes away again.
    where = subprocess.run(["findmnt", "-nro", "TARGET", "-S", dev], capture_output=True, text=True).stdout.strip()
    tmp = None
    if not where:
        tmp = where = f"/run/wildos-winmode-probe-{os.getpid()}"
        os.makedirs(tmp, exist_ok=True)
        if subprocess.run(["mount", "-t", "ntfs3", "-o", "ro,nodev,nosuid,noexec", dev, tmp], capture_output=True).returncode != 0:
            os.rmdir(tmp)
            return
    try:
        hive = os.path.join(where, "Windows/System32/config/SOFTWARE")
        if os.path.isfile(hive):
            q = lambda key, value: subprocess.run(["hivexget", hive, key, value], capture_output=True, text=True,  # noqa: E731
                                                  timeout=30).stdout.strip()
            edition = q(r"\Microsoft\Windows NT\CurrentVersion", "EditionID")
            if edition:
                out.update(windows=True, edition=edition,
                           description=q(r"\Microsoft\Windows NT\CurrentVersion\SoftwareProtectionPlatform",
                                         "OA3xOriginalProductKeyDescription"))
    finally:
        if tmp:
            subprocess.run(["umount", tmp], capture_output=True)
            os.rmdir(tmp)


# This PC's language, keyboard and time zone in Windows' terms (brief 5.4).
INPUT_LOCALES = {
    "gb": "0809:00000809", "us": "0409:00000409", "ie": "1809:00001809", "de": "0407:00000407",
    "fr": "040c:0000040c", "es": "0c0a:0000040a", "it": "0410:00000410", "nl": "0413:00020409",
    "pt": "0816:00000816", "br": "0416:00000416", "se": "041d:0000041d", "no": "0414:00000414",
    "dk": "0406:00000406", "fi": "040b:0000040b", "pl": "0415:00000415", "ch": "0807:00000807",
}
TIME_ZONES = {
    "GMT Standard Time": ["Europe/London", "Europe/Dublin", "Europe/Lisbon"],
    "Romance Standard Time": ["Europe/Paris", "Europe/Brussels", "Europe/Madrid"],
    "W. Europe Standard Time": ["Europe/Berlin", "Europe/Amsterdam", "Europe/Rome", "Europe/Stockholm",
                                "Europe/Zurich", "Europe/Oslo", "Europe/Vienna"],
    "Central European Standard Time": ["Europe/Warsaw", "Europe/Prague", "Europe/Budapest"],
    "GTB Standard Time": ["Europe/Athens", "Europe/Bucharest"],
    "FLE Standard Time": ["Europe/Helsinki", "Europe/Kiev", "Europe/Kyiv", "Europe/Riga", "Europe/Tallinn",
                          "Europe/Vilnius"],
    "Eastern Standard Time": ["America/New_York", "America/Toronto", "America/Detroit"],
    "Central Standard Time": ["America/Chicago"],
    "Mountain Standard Time": ["America/Denver"],
    "Pacific Standard Time": ["America/Los_Angeles", "America/Vancouver"],
    "AUS Eastern Standard Time": ["Australia/Sydney", "Australia/Melbourne"],
    "New Zealand Standard Time": ["Pacific/Auckland"],
    "Tokyo Standard Time": ["Asia/Tokyo"],
    "India Standard Time": ["Asia/Kolkata"],
    "Singapore Standard Time": ["Asia/Singapore"],
    "Arabian Standard Time": ["Asia/Dubai"],
    "South Africa Standard Time": ["Africa/Johannesburg"],
}


def read_kv(path):
    out = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                k, sep, v = line.strip().partition("=")
                if sep:
                    out[k.strip()] = v.strip().strip('"')
    except OSError:
        pass
    return out


def host_locale():
    lang = read_kv("/etc/default/locale").get("LANG") or read_kv("/etc/locale.conf").get("LANG") or ""
    tag = lang.split(".")[0].replace("_", "-")
    if not re.fullmatch(r"[a-z]{2,3}-[A-Z]{2}", tag):
        tag = "en-GB"
    xkb = read_kv("/etc/default/keyboard").get("XKBLAYOUT", "").split(",")[0]
    keyboard = INPUT_LOCALES.get(xkb) or ("0409:00000409" if tag == "en-US" else "0809:00000809")
    try:
        tz = subprocess.run(["timedatectl", "show", "-p", "Timezone", "--value"], capture_output=True, text=True,
                            timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        tz = ""
    windows_tz = next((w for w, names in TIME_ZONES.items() if tz in names), "UTC")
    return tag, keyboard, windows_tz


def xml_escape(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;").replace("'", "&apos;"))


def render(name, values):
    with open(os.path.join(TEMPLATES, name), encoding="utf-8") as f:
        text = f.read()
    for k, v in values.items():
        text = text.replace(f"@{k}@", str(v))
    left = sorted(set(re.findall(r"@[A-Z_]+@", text)))
    if left:
        raise RuntimeError(f"template {name} has unfilled fields: {' '.join(left)}")
    return text


# The languages Microsoft 365 installs in, as the Office Deployment Tool
# names them. An ID it doesn't know stops the whole install, so anything else
# falls back to Windows' own display language.
OFFICE_LANGUAGES = {
    "ar-sa", "bg-bg", "cs-cz", "da-dk", "de-de", "el-gr", "en-gb", "en-us", "es-es", "es-mx", "et-ee",
    "fi-fi", "fr-ca", "fr-fr", "he-il", "hr-hr", "hu-hu", "id-id", "it-it", "ja-jp", "ko-kr", "lt-lt",
    "lv-lv", "nb-no", "nl-nl", "pl-pl", "pt-br", "pt-pt", "ro-ro", "ru-ru", "sk-sk", "sl-si", "sr-latn-rs",
    "sv-se", "th-th", "tr-tr", "uk-ua", "vi-vn", "zh-cn", "zh-tw",
}


def office_config(plan, lang):
    """Microsoft 365 in the host's language, with its spelling and grammar
    tools, rather than the US English MatchOS alone gave on a UK PC."""
    product = OFFICE_PRODUCTS.get(plan, "O365HomePremRetail")
    excluded = ["Access", "Bing", "Groove", "Lync", "OneDrive", "OutlookForWindows", "Publisher", "Teams"]
    office_lang = lang.lower() if lang.lower() in OFFICE_LANGUAGES else ""
    languages = (f'      <Language ID="{office_lang}"/>\n' if office_lang else "") + '      <Language ID="MatchOS"/>\n'
    proofing = ('    <Product ID="ProofingTools">\n'
                f'      <Language ID="{office_lang}"/>\n'
                "    </Product>\n") if office_lang else ""
    return ("<Configuration>\n"
            '  <Add OfficeClientEdition="64" Channel="Current">\n'
            f'    <Product ID="{product}">\n'
            + languages
            + "".join(f'      <ExcludeApp ID="{a}"/>\n' for a in excluded)
            + "    </Product>\n"
            + proofing
            + "  </Add>\n"
            '  <Updates Enabled="FALSE"/>\n'
            '  <Display Level="None" AcceptEULA="TRUE"/>\n'
            '  <Property Name="AUTOACTIVATE" Value="0"/>\n'
            '  <Property Name="FORCEAPPSHUTDOWN" Value="TRUE"/>\n'
            "</Configuration>\n")


def stage_title(stage, licence, own=False):
    source = licence.get("source")
    return {
        "media": "Copy Windows 11 from your file" if own else "Download Windows 11 from Microsoft",
        "install": "Install Windows",
        "activate": ("Activate with your product key" if source == "key"
                     else "Activation" if source == "none" else "Activate with this PC's licence"),
        "tune": "Tune for speed and privacy",
        "office": "Install Microsoft 365",
    }[stage]


def stage_detail(stage, licence, own=False):
    if stage == "media":
        # Only a download can be matched to Microsoft's signature; a file
        # someone brings is only known to hold Windows 11 Pro.
        return "Checked that it's Windows 11 Pro" if own else "About 6 GB, checked against Microsoft's signature"
    if stage == "install":
        return "Automatic, no questions to answer"
    if stage == "activate":
        if licence.get("source") == "none":
            return "Skipped, as you chose. Add a key any time in Settings."
        return {"home": "Windows 11 Home", "pro": "Windows 11 Pro"}.get(licence.get("edition"), "Windows 11")
    if stage == "tune":
        return "Removes adverts, tracking and apps you won't use"
    return "Word, Excel, PowerPoint, Outlook, OneNote"


class Setup:
    """One setup at a time, in a worker thread. service is the system
    service: its state dict (saved with save_state), cfg, and changed()."""

    def __init__(self, service):
        self.svc = service
        self.cfg = service.cfg
        self.lock = threading.Lock()
        self.thread = None
        self.stop = threading.Event()
        self.resume_event = threading.Event()
        self.speed = 0.0

    # The record in the state file.
    @property
    def rec(self):
        return self.svc.state.get("setup") or {}

    def cancelled(self):
        """Start over has removed the record: whatever the worker was still
        finishing must not write a new one."""
        return self.stop.is_set() and "setup" not in self.svc.state

    def save(self, **changes):
        with self.lock:
            if self.cancelled():
                return
            rec = dict(self.rec)
            rec.update(changes)
            self.svc.state["setup"] = rec
            self.svc.state["setting_up"] = bool(rec.get("running"))
            self.svc.save_state()
        self.svc.notify()

    def set_stage(self, stage, state, percent=None, detail=None):
        with self.lock:
            if self.cancelled():
                return
            rec = dict(self.rec)
            stages = dict(rec.get("stages") or {})
            s = dict(stages.get(stage) or {})
            s["state"] = state
            if percent is not None:
                s["percent"] = max(0, min(100, int(percent)))
            if detail is not None:
                s["detail"] = detail
            stages[stage] = s
            rec["stages"] = stages
            if state == "active":
                rec["stage"] = stage
            self.svc.state["setup"] = rec
            self.svc.save_state()
        self.svc.notify()

    def done(self, step):
        return step in (self.rec.get("done") or [])

    def mark(self, step):
        self.save(done=sorted(set(self.rec.get("done") or []) | {step}))

    # For D-Bus.
    def status(self):
        rec = self.rec
        if not rec:
            return None
        licence = rec.get("licence") or {}
        own = bool(rec.get("iso"))
        ids = [s for s in STAGES if s != "office" or (rec.get("options") or {}).get("office")]
        stages, total, done_weight, minutes_left = [], 0, 0.0, 0.0
        for sid in ids:
            s = (rec.get("stages") or {}).get(sid) or {}
            state = s.get("state", "pending")
            pct = 100 if state == "done" else s.get("percent", 0)
            stages.append({"id": sid, "title": stage_title(sid, licence, own),
                           "detail": s.get("detail") or stage_detail(sid, licence, own), "state": state, "percent": pct})
            total += WEIGHTS[sid]
            done_weight += WEIGHTS[sid] * pct / 100.0
            minutes_left += MINUTES[sid] * (1 - pct / 100.0)
        if self.speed > 0 and rec.get("stage") == "media":
            remaining = (rec.get("download") or {}).get("remaining", 0)
            minutes_left += remaining / self.speed / 60 - MINUTES["media"] * (1 - ((rec.get("stages") or {}).get("media") or {}).get("percent", 0) / 100.0)
        minutes = max(1, round(minutes_left))
        return {
            "running": bool(rec.get("running")),
            "stage": rec.get("stage"),
            "stages": stages,
            "percent": round(100 * done_weight / total) if total else 0,
            "left": "About a minute left" if minutes <= 1 else f"About {minutes} minutes left",
            "paused": bool(rec.get("paused")),
            "problem": rec.get("problem"),
            # Setup is putting a problem right by itself: a line to show, with
            # no button to press.
            "notice": (rec.get("notice") or {}).get("text"),
            "licence": {"source": licence.get("source"), "edition": licence.get("edition"),
                        "activated": licence.get("activated"), "keyEnd": licence.get("last5") if licence.get("source") == "key" else None},
            "options": rec.get("options") or {},
            "ownIso": own,
            "failed": rec.get("failed"),
            "background": bool(rec.get("background")),
            "finished": rec.get("finished"),
            "terms": rec.get("terms"),
        }

    # Control.
    def start(self, request, owner_uid):
        """Starts setting up from the wizard's request: {licence: {source:
        firmware|key|none, key}, options: {...}}. "" or why not."""
        if self.thread and self.thread.is_alive() and self.stop.is_set():
            # Start over, straight after: the last worker may still be
            # finishing a step it can't cut short (installing the runtime).
            self.thread.join(90)
            if self.thread.is_alive():
                return "The last setup is still stopping. Try again in a minute."
        if self.thread and self.thread.is_alive():
            return "Windows mode is already being set up."
        if self.rec.get("running"):
            # Trying again after a problem: carry on from the step it was on,
            # keeping everything done so far, with the options as chosen this
            # time (the virtual PC is defined again from them).
            o = request.get("options") if isinstance(request.get("options"), dict) else {}
            options = dict(self.rec.get("options") or {})
            if o.get("memoryGb"):
                options["memoryGb"] = max(4, min(int(o.get("memoryGb") or 4), 32))
            for k in ("office", "openFiles"):
                if k in o:
                    options[k] = bool(o[k])
            # A file chosen again after the last one couldn't be copied.
            if request.get("iso") and request.get("iso") != (self.rec.get("iso") or {}).get("path"):
                own, why = check_own_iso(request.get("iso"), self.rec.get("owner", owner_uid))
                if why:
                    return why
                forget_windows_iso(own=True)
                self.save(iso=own, done=[d for d in self.rec.get("done") or [] if d != "windows-iso"])
            # Pressed by the person: the automatic tries start again too.
            self.save(failed=None, problem=None, paused=False, options=options, recovery=None, notice=None,
                      **self.fresh_clocks())
            self.launch()
            return ""
        if virsh.state(self.cfg["domain"]) != "":
            return "Windows mode is set up on this PC already."
        # The answer file skips Windows' own licence screen, and the Office
        # Deployment Tool's, so the terms are accepted in Wild OS instead and
        # kept with the setup they were accepted for.
        terms = request.get("terms") if isinstance(request.get("terms"), dict) else {}
        o = request.get("options") or {}
        if terms.get("windows") is not True or (bool(o.get("office")) and terms.get("office") is not True):
            return "Accept Microsoft's licence terms first."
        lic = request.get("licence") or {}
        source = lic.get("source")
        if source not in ("firmware", "key", "none"):
            return "That licence choice isn't one Windows mode knows."
        key = (lic.get("key") or "").strip().upper()
        if source == "key" and not key_shape_ok(key):
            return "That doesn't look like a Windows product key. Check it and try again."
        options = {
            "office": bool(o.get("office")),
            "officePlan": o.get("officePlan") if o.get("officePlan") in OFFICE_PRODUCTS else "personal",
            "openFiles": bool(o.get("openFiles", True)),
            "whenIdle": o.get("whenIdle") if o.get("whenIdle") in ("automatic", "memory", "disk") else "automatic",
            "memoryGb": max(4, min(int(o.get("memoryGb") or 4), 32)),
        }
        own = None
        if request.get("iso"):
            own, why = check_own_iso(request.get("iso"), owner_uid)
            if why:
                return why
        forget_windows_iso(own=bool(own))
        os.makedirs(SECRETS, mode=0o700, exist_ok=True)
        os.chmod(SECRETS, 0o700)
        if source == "key":
            with open(os.path.join(SECRETS, ACTIVATION_KEY), "w") as f:
                f.write(key + "\n")
            os.chmod(os.path.join(SECRETS, ACTIVATION_KEY), 0o600)
        self.svc.state["setup"] = {}
        accepted = {"windows": True, "office": options["office"], "at": int(time.time())}
        self.save(running=True, started=int(time.time()), owner=owner_uid, options=options,
                  licence={"source": source, "edition": lic.get("edition") or ""}, iso=own, terms=accepted,
                  background=bool(request.get("background")),
                  stages={}, done=[], problem=None, failed=None, paused=False)
        self.svc.state["when_idle"] = {"memory": "keep", "disk": "save"}.get(options["whenIdle"], "automatic")
        self.svc.save_state()
        self.launch()
        return ""

    def launch(self):
        with open(MARKER, "w") as f:
            f.write(str(int(time.time())))
        self.stop.clear()
        self.thread = threading.Thread(target=self.run, name="setup", daemon=True)
        self.thread.start()

    def fresh_clocks(self):
        """The install step's clocks, started again for a new try. Kept from
        the first try, they ran on through failures and through the PC being
        off, so once INSTALL_LIMIT (or BOOT_WAIT with BOOT_TRIES used up) had
        passed, every Try again and every restart failed at once. Nothing to
        reset before the install has begun or once it is done."""
        clocks = {"tune_started": None} if self.rec.get("tune_started") else {}
        if self.done("install") or not self.rec.get("install_started"):
            return clocks
        now = int(time.time())
        return dict(clocks, install_started=now, booted_at=now, boot_tries=1)

    def resume_after_restart(self):
        if self.rec.get("running") and not (self.thread and self.thread.is_alive()):
            log("carrying on with the setup that was under way")
            clocks = self.fresh_clocks()
            if self.rec.get("failed"):
                # It had stopped on a problem; a restart is a fresh start for
                # the automatic tries, as the problem card advises.
                clocks.update(recovery=None, notice=None)
            if clocks:
                self.save(**clocks)
            self.launch()

    def pause(self):
        self.save(paused=True)

    def resume(self):
        self.save(paused=False, problem=None)
        self.resume_event.set()
        if self.rec.get("running") and not (self.thread and self.thread.is_alive()):
            self.launch()

    def cancel(self):
        """Stops setup and removes what it made, apart from the downloads,
        so trying again doesn't fetch them twice."""
        self.stop.set()
        self.resume_event.set()
        if self.thread and self.thread.is_alive():
            self.thread.join(30)
        domain = self.cfg["domain"]
        if virsh.state(domain) != "":
            virsh.run("destroy", domain, check=False)
            virsh.run("undefine", domain, "--nvram", "--managed-save", check=False)
        # The copy of someone's own file isn't a download to keep: they
        # still have the file.
        if self.rec.get("iso"):
            forget_windows_iso(own=True)
        for path in (DISK, os.path.join(CACHE, "payload.iso"), os.path.join(SECRETS, "key"),
                     os.path.join(SECRETS, ACTIVATION_KEY), MARKER):
            try:
                os.remove(path)
            except OSError:
                pass
        self.svc.state.pop("setup", None)
        self.svc.state["setting_up"] = False
        self.svc.save_state()
        # The service keeps the virtual PC's last state to answer status
        # quickly, and an undefine sends nothing that clears it, so it still
        # said "shut off" and the wizard showed Start over as set up.
        self.svc.refresh()
        self.svc.notify()

    def choose_iso(self, path):
        """A Windows 11 ISO downloaded in a browser, when Microsoft refused
        the automated download (board 9.7): kept if its SHA-256 is one
        Microsoft lists."""
        try:
            st = os.stat(path)
        except OSError:
            return "That file isn't there any more."
        if not os.path.isfile(path) or st.st_size < 3 * 1000 ** 3:
            return "That isn't a Windows 11 ISO."
        os.makedirs(CACHE, exist_ok=True)
        target = os.path.join(CACHE, "windows.iso")
        try:
            sha = hashlib.sha256()
            with open(path, "rb") as src, open(target + ".part", "wb") as dst:
                for block in iter(lambda: src.read(4 * 1024 * 1024), b""):
                    sha.update(block)
                    dst.write(block)
            digest = sha.hexdigest()
            match = next((name for name, s in msdl.checksums() if s == digest), None)
        except (OSError, urllib.error.URLError, msdl.Changed) as e:
            return f"That file couldn't be checked. ({e})"
        if not match:
            os.remove(target + ".part")
            return "That file isn't a Windows 11 ISO from Microsoft, so it wasn't used."
        os.replace(target + ".part", target)
        with open(os.path.join(CACHE, "windows-iso.json"), "w") as f:
            json.dump({"url": "", "file": os.path.basename(path), "sha256": digest, "language": match, "fetched": 0}, f)
        self.resume()
        return ""

    # The worker.
    def run(self):
        # A PC that goes to sleep while idle mid-setup pauses the virtual PC
        # at whatever it was doing, and a missed moment (the key that starts
        # Windows Setup) never comes back. Closing the lid still sleeps it.
        try:
            inhibitor = subprocess.Popen(["systemd-inhibit", "--what=idle:sleep", "--mode=block", "--who=Windows mode",
                                          "--why=Setting up Windows", "sleep", "infinity"],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            inhibitor = None
        spare_capacity()
        try:
            self.work()
        finally:
            if inhibitor:
                inhibitor.terminate()

    def work(self):
        try:
            self.attempts()
        except Stopped:
            # Asked to stop while waiting to try again.
            log("stopped")

    def attempts(self):
        redone = 0
        while True:
            try:
                self.save(problem=None, failed=None)
                self.stages()
                return
            except Stopped:
                log("stopped")
                return
            except Redo as r:
                redone += 1
                log(f"redoing {r.stage}: {r}")
                if redone <= REDO_LIMIT:
                    self.purge(r.stage)
                    self.undo_from(r.stage)
                    continue
                if self.recover(self.rec.get("stage") or "media", r):
                    redone = 0
                    continue
                self.fail_with(r)
                return
            except Problem as p:
                log("problem:", p.kind, p.text)
                if p.kind == "download":
                    if not online():
                        # Every step that downloads carries on from where it
                        # stopped, so going round again once back online is
                        # the same as resuming.
                        self.wait_online()
                        continue
                    self.save(problem={"kind": "download", "text": p.text}, paused=True)
                    return
                if p.kind == "space":
                    # Board 9.7: free some space and carry on; nothing failed.
                    self.save(problem={"kind": "space", "text": p.text}, paused=True)
                    return
                if p.retry and self.recover(self.rec.get("stage") or "media", p):
                    continue
                self.fail_with(p)
                return
            except Exception as e:  # noqa: BLE001 (any failure is shown, never swallowed)
                log("failed:", repr(e))
                if self.recover(self.rec.get("stage") or "media", e):
                    continue
                self.fail_with(e)
                return

    def stages(self):
        self.step_media()
        self.step_install()
        self.step_activate()
        self.step_tune()
        if (self.rec.get("options") or {}).get("office"):
            self.step_office()
        self.finish()

    def fail_with(self, e):
        """Stops on a problem, shown with Try again and Start over."""
        stage = self.rec.get("stage") or "media"
        self.set_stage(stage, "failed")
        if isinstance(e, Problem):
            self.save(problem={"kind": e.kind, "text": e.text}, failed=e.text, notice=None)
            return
        text = "Windows mode couldn't finish setting up. Try again, and if it happens twice, restart the PC first."
        # The unexpected error itself goes on the card too, small, so a
        # failure can be reported without digging through the log.
        self.save(problem={"kind": "failed", "text": text, "detail": f"{type(e).__name__}: {e}"[:300]},
                  failed=f"{text} ({e})", notice=None)

    def recover(self, stage, error):
        """After `stage` failed: sets up the next automatic try and returns
        True, or returns False when setup should stop and show the problem."""
        move, rec = next_move(self.rec.get("recovery"), stage)
        if move == "stop":
            log(f"{stage} failed again after going back a step; stopping")
            self.save(recovery=rec)
            return False
        if move == "retry":
            log(f"{stage} failed ({error}); trying it again")
            self.save(recovery=rec, notice={"text": RETRY_NOTICE, "stage": stage}, **self.fresh_clocks())
            if stage == "install" and not self.rec.get("copied_at"):
                # Windows Setup hadn't copied Windows yet, so it starts again
                # from the installer: on the half written disk the installer
                # is never asked for, and Setup wipes the disk anyway.
                self.purge("install")
            if stage == "tune":
                self.save(rerun_guest=True)
        else:
            back = rec["back"]
            log(f"{stage} failed twice ({error}); clearing what it made and going back to {back}")
            self.save(recovery=rec, notice={"text": BACK_NOTICE, "stage": stage})
            self.purge(stage)
            self.undo_from(back)
        self.set_stage(stage, "active")
        self.wait(RETRY_WAIT)
        return True

    def wait(self, seconds):
        if self.stop.wait(seconds):
            raise Stopped()

    def wait_online(self, host=None):
        """Shows board 9.7's Download paused card and waits until the PC is
        back online, then returns for the step to carry on where it stopped.
        Pause and Start over work meanwhile; Try again looks at once."""
        log("offline; waiting for the connection to come back")
        problem = self.offline_problem()
        probed = time.monotonic()
        while True:
            if self.rec.get("problem") != problem and not self.rec.get("paused"):
                self.save(problem=problem)
            self.resume_event.wait(OFFLINE_POLL)
            self.resume_event.clear()
            self.check()
            if online():
                break
            if host and time.monotonic() - probed >= OFFLINE_PROBE:
                probed = time.monotonic()
                if reachable(host):
                    break
        log("back online")
        if (self.rec.get("problem") or {}).get("offline"):
            self.save(problem=None)

    def stage_done(self, stage):
        """A stage finished: marked done, and any automatic recovery for it
        is over."""
        rec = recovered(self.rec.get("recovery"), stage)
        changes = {"recovery": rec}
        if not rec:
            changes["notice"] = None
        self.save(**changes)
        if stage != "media":
            self.mark(stage)

    def unmark(self, *steps):
        self.save(done=[d for d in self.rec.get("done") or [] if d not in steps])

    # The done flags behind each stage, for running it again.
    STAGE_FLAGS = {"media": ("licence", "windows-iso"), "install": ("install",), "activate": ("activate",),
                   "tune": ("tune",), "office": ("office",)}

    def undo_from(self, stage):
        """Runs `stage` and every stage after it again. Their checks find
        what is still good, so only what is missing is made again."""
        later = STAGES[STAGES.index(stage):]
        self.unmark(*[f for s in later for f in self.STAGE_FLAGS[s]])
        stages = dict(self.rec.get("stages") or {})
        for s in later:
            if s in stages:
                stages[s] = dict(stages[s], state="pending", percent=0)
        self.save(stages=stages)

    def purge(self, stage):
        """Deletes what `stage` made, so running it again starts it clean.
        Only Windows mode's own files and virtual PC: never the shared
        folders, the person's own Windows 11 file, or the key they typed."""
        domain = self.cfg["domain"]
        if stage == "media":
            # Partial downloads go. A finished Windows download is checked
            # against Microsoft's signature again rather than fetched again,
            # and deleted if it fails that.
            for name in ("windows.iso.part", "virtio-win.iso.part"):
                _remove(os.path.join(CACHE, name))
            if not is_iso(os.path.join(CACHE, "virtio-win.iso")):
                _remove(os.path.join(CACHE, "virtio-win.iso"))
            for name in ("key", "msdm.bin"):
                _remove(os.path.join(SECRETS, name))
        elif stage == "install":
            if virsh.state(domain) != "":
                virsh.run("destroy", domain, check=False)
                virsh.run("undefine", domain, "--nvram", "--managed-save", check=False)
            for path in (DISK, os.path.join(CACHE, "payload.iso"), os.path.join(config.STATE_DIR, "domain.xml")):
                _remove(path)
            self.save(install_started=None, booted_at=None, boot_tries=None, copied_at=None)
        elif stage == "activate":
            lic = dict(self.rec.get("licence") or {})
            lic.pop("activated", None)
            lic.pop("refused", None)
            self.save(licence=lic)
        elif stage == "tune":
            self.save(tune_started=None, rerun_guest=True)
            self.guest_cleanup(r"Remove-Item 'C:\ProgramData\WildOS\stage.json', 'C:\ProgramData\WildOS\ready.json' "
                               r"-Force -ErrorAction SilentlyContinue")
        elif stage == "office":
            self.guest_cleanup(r"Remove-Item 'C:\ProgramData\WildOS\odt' -Recurse -Force -ErrorAction SilentlyContinue")

    def guest_cleanup(self, script):
        """Best effort: Windows may not be running to clear up after itself."""
        try:
            if virsh.state(self.cfg["domain"]) == "running":
                qga.powershell(self.cfg["domain"], script, 60)
        except (virsh.VirshError, KeyError, TimeoutError) as e:
            log("clearing up inside Windows:", e)

    def check(self):
        if self.stop.is_set():
            raise Stopped()
        while self.rec.get("paused") and not self.stop.is_set():
            self.resume_event.wait(5)
            self.resume_event.clear()
        if self.stop.is_set():
            raise Stopped()

    # media
    def step_media(self):
        self.set_stage("media", "active")
        os.makedirs(CACHE, exist_ok=True)
        self.check_space()
        self.runtime()
        self.check()
        self.prepare_licence()
        self.windows_iso()
        self.check()
        if self.needs()[VIRTIO]:
            self.set_stage("media", "active", 92, "Getting the drivers Windows needs")
            self.virtio_iso()
        self.set_stage("media", "done", 100, "")
        self.stage_done("media")

    def check_space(self):
        memory = (self.rec.get("options") or {}).get("memoryGb", 4)
        need = (30 + memory) * 1000 ** 3
        if self.needs()[WINDOWS_ISO] and not os.path.exists(WINDOWS_ISO):
            # Someone's own file is copied whole, and some are bigger than
            # Microsoft's download.
            need += max(9 * 1000 ** 3, int((self.rec.get("iso") or {}).get("bytes") or 0))
        if os.path.exists(DISK):
            need -= os.stat(DISK).st_blocks * 512
        free = shutil.disk_usage(config.STATE_DIR).free
        if free < need:
            raise Problem("space", f"Windows mode needs {size_text(need)} free and this PC has {size_text(free)}.")

    def runtime(self):
        """The virtual PC (QEMU, libvirt, wildos-rdp) is its own package, which
        the Wild OS image leaves out so that people who never set up Windows
        mode never download it."""
        if runtime_installed():
            return
        # No percentage until apt can give a real one: refreshing the package
        # lists has none, and over Tor it takes minutes.
        self.set_stage("media", "active", 0, "Getting ready to download the parts Windows mode runs on")
        env = dict(os.environ, DEBIAN_FRONTEND="noninteractive", LC_ALL="C.UTF-8")
        # Waits out an update that is already installing rather than failing.
        apt = ["apt-get", "-o", "DPkg::Lock::Timeout=900", "-o", "Dpkg::Options::=--force-confold"]
        # One left running by a service that was stopped part way: a refresh
        # is stopped and done again, an install is let finish rather than
        # started twice.
        subprocess.run(["systemctl", "stop", APT_REFRESH_UNIT], capture_output=True)
        while subprocess.run(["systemctl", "is-active", "--quiet", APT_UNIT]).returncode == 0:
            self.wait(5)
        # The lock timeout covers installing, not refreshing the package
        # lists: while Settings checks for updates the refresh fails at once,
        # and installing from a fresh install's old lists asks for versions
        # the repository has since replaced. So the refresh waits its turn.
        deadline = time.monotonic() + 900
        while True:
            p = self.apt(apt + ["update", "-qq"], env, 900, refresh=True)
            if p.returncode != 0 and not online():
                raise Problem("download", "Windows mode couldn't download the parts it runs on.")
            if p.returncode == 0 or not apt_locked(p) or time.monotonic() > deadline:
                break
            self.wait(10)
        if p.returncode != 0:
            log("apt-get update:", (p.stderr or p.stdout).strip()[-500:])
        self.check()
        install = apt + ["install", "-y", "-qq", "--no-install-recommends", "wildos-winmode-runtime"]
        p = self.apt(install, env, 3600, progress=True)
        if p.returncode != 0 and dpkg_interrupted(p):
            # An install cut off part way, by anything: finishing it is what
            # apt itself asks for, and nothing installs until it's done.
            log("dpkg was interrupted; finishing what it was doing")
            self.apt(["dpkg", "--configure", "-a", "--force-confold"], env, 1800)
            p = self.apt(install, env, 3600, progress=True)
        if p.returncode != 0:
            log("apt-get:", (p.stderr or p.stdout).strip()[-500:])
            if apt_locked(p):
                raise Problem("download", "Wild OS is installing updates. Try again when they've finished.")
            if dpkg_interrupted(p):
                raise Problem("failed", "An earlier software install on this PC was left half done, and Windows mode "
                                        "couldn't finish it. Send it with Report a problem so we can put it right.",
                              retry=False)
            raise Problem("download", "Windows mode couldn't download the parts it runs on.")
        self.svc.runtime_installed()

    def apt(self, args, env, timeout, refresh=False, progress=False):
        """apt-get or dpkg through apt_command, with its output read back
        from the log the unit writes. apt can take minutes to give up on a
        connection, so the network is watched meanwhile: offline shows the
        Download paused card at once, and a refresh of the package lists
        (refresh, which changes nothing installed) is stopped so setup
        waits for the connection instead. An install is let finish.

        With progress, apt's own figures for downloading and installing are
        read from the log as it goes and shown in the stage's line."""
        name = APT_REFRESH_UNIT if refresh else APT_UNIT
        if progress:
            args = args + ["-o", "APT::Status-Fd=1"]
        cmd = apt_command(args, name)
        unit = cmd[0] == "systemd-run"
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + timeout
        shown = False
        last = None
        while True:
            try:
                out, err = proc.communicate(timeout=OFFLINE_POLL)
                break
            except subprocess.TimeoutExpired:
                pass
            if progress and unit:
                now = latest_apt_progress(_read(APT_LOG))
                if now and now != last:
                    last = now
                    self.set_stage("media", "active", 0, now)
            offline = not online()
            if offline != shown:
                shown = offline
                self.save(problem=self.offline_problem() if offline else None)
            ended = time.monotonic() > deadline or (refresh and (offline or self.stop.is_set()))
            if ended:
                if unit:
                    subprocess.run(["systemctl", "stop", name], capture_output=True)
                proc.kill()
                out, err = proc.communicate()
                break
        if shown:
            self.save(problem=None)
        p = subprocess.CompletedProcess(cmd, proc.returncode, out, err)
        if unit:
            p.stdout = _read(APT_LOG)
        if progress:
            p.stdout = "\n".join(line for line in (p.stdout or "").splitlines() if apt_progress(line) is None)
        return p

    def offline_problem(self):
        received = (self.rec.get("download") or {}).get("received", 0)
        return {"kind": "download", "offline": True, "received": received, "text": OFFLINE_TEXT}

    def licence_ready(self):
        """What the install and activate stages read from the licence step:
        the key for the answer file, and the firmware table's copy."""
        if not os.path.exists(os.path.join(SECRETS, "key")):
            return False
        return (self.rec.get("licence") or {}).get("source") != "firmware" or os.path.exists(os.path.join(SECRETS, "msdm.bin"))

    def prepare_licence(self):
        if self.done("licence") and self.licence_ready():
            return
        os.makedirs(SECRETS, mode=0o700, exist_ok=True)
        lic = dict(self.rec.get("licence") or {})
        msdm_copy = os.path.join(SECRETS, "msdm.bin")
        key = None
        if lic.get("source") == "firmware":
            try:
                with open("/sys/firmware/acpi/tables/MSDM", "rb") as f:
                    table = f.read()
            except OSError:
                table = b""
            key = parse_msdm(table)
            if not key:
                raise Problem("failed", "This PC's licence couldn't be read. Go back and add a product key instead.",
                              retry=False)
            # QEMU reads the table from a root-only copy (brief 5.3).
            with open(msdm_copy, "wb") as f:
                f.write(table)
            group = next((g for g in ("libvirt-qemu", "kvm") if _gid(g) is not None), None)
            if group:
                os.chown(msdm_copy, 0, _gid(group))
                os.chown(SECRETS, 0, _gid(group))
                os.chmod(SECRETS, 0o710)
            os.chmod(msdm_copy, 0o640)
        elif lic.get("source") == "key":
            try:
                with open(os.path.join(SECRETS, ACTIVATION_KEY)) as f:
                    key = f.read().strip()
            except OSError:
                # Already put into Windows and forgotten, as it should be,
                # when this runs again after going back a step.
                if not lic.get("last5"):
                    raise Problem("failed", "The product key couldn't be read. Start over and type it again.",
                                  retry=False) from None
        else:
            key = GENERIC_PRO_KEY
        with open(os.path.join(SECRETS, "key"), "w") as f:
            f.write(GENERIC_PRO_KEY + "\n")
        os.chmod(os.path.join(SECRETS, "key"), 0o600)
        if key:
            lic["last5"] = key[-5:]
        self.save(licence=lic)
        self.mark("licence")

    def windows_iso(self):
        iso = os.path.join(CACHE, "windows.iso")
        meta_path = os.path.join(CACHE, "windows-iso.json")
        if not self.needs()[WINDOWS_ISO]:
            # Windows has copied itself to its disk: the installer isn't
            # read again, so a missing one isn't fetched again.
            return
        if self.done("windows-iso") and is_iso(iso):
            return
        if self.rec.get("iso"):
            if is_iso(iso) and (_read_json(meta_path) or {}).get("source") == "own":
                # Checked again after going back a step, without needing
                # the person's drive to be connected still.
                try:
                    isocheck.check(iso)
                    self.mark("windows-iso")
                    return
                except isocheck.Unusable:
                    _remove(iso)
            self.copy_own_iso(iso, meta_path)
            return
        lang, _, _ = host_locale()
        for attempt in range(2):
            meta = _read_json(meta_path)
            if not os.path.exists(iso):
                # Microsoft's links last 24 hours; a fresh one is asked for
                # when the stored one is older than 20.
                if not meta or time.time() - meta.get("fetched", 0) > 72000:
                    self.set_stage("media", "active", None, "Asking Microsoft for Windows 11")
                    try:
                        fresh = msdl.fetch(lang)
                    except msdl.Refused as e:
                        raise Problem("download", f"Microsoft didn't allow the download from here. ({e})") from e
                    except (msdl.Changed, OSError) as e:
                        raise Problem("download", f"Microsoft's download page didn't answer as expected. ({e})") from e
                    if meta and meta.get("file") != fresh["file"]:
                        _remove(iso + ".part")
                    meta = fresh
                    with open(meta_path, "w") as f:
                        json.dump(meta, f)
                try:
                    self.download(meta["url"], iso, "media", 0, 88, "Windows 11")
                except urllib.error.HTTPError as e:
                    if e.code in (403, 404, 410) and attempt == 0:
                        meta["fetched"] = 0
                        with open(meta_path, "w") as f:
                            json.dump(meta, f)
                        continue
                    raise Problem("download", f"The download stopped. ({e.code})") from e
            self.set_stage("media", "active", 89, "Checking it against Microsoft's signature")
            digest = _sha256(iso, self.check)
            if digest == (meta or {}).get("sha256"):
                self.mark("windows-iso")
                return
            log("SHA-256 doesn't match Microsoft's; deleting it")
            _remove(iso)
            if meta:
                meta["fetched"] = 0
                with open(meta_path, "w") as f:
                    json.dump(meta, f)
        raise Problem("download", "The Windows download didn't match Microsoft's signature twice, so it was deleted.")

    def copy_own_iso(self, iso, meta_path):
        """Copies the person's own Windows 11 file into the cache, where the
        virtual PC reads it like a download. Used where it is, libvirt would
        give the file to its own account and never give it back (it leaves
        a read-only disc's owner as it set it), a home folder or USB drive
        may be out of the virtual PC's reach, and the file could go before
        the install, which restarts, is done with it. cat runs as the person
        who chose the file, so the service only copies what they can read."""
        own = self.rec["iso"]
        part = iso + ".part"
        _remove(part)
        total = int(own.get("bytes") or 0)
        label = "Copying Windows 11 from your file"
        self.set_stage("media", "active", 1, label)
        missing = "Your Windows 11 file couldn't be read. Check the drive it's on is connected, then try again."
        try:
            proc = subprocess.Popen(["cat", "--", own["path"]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                    **as_user(self.rec.get("owner")))
        except OSError as e:
            raise Problem("failed", missing, retry=False) from e
        got, last = 0, time.monotonic()
        try:
            with open(part, "wb") as out:
                while True:
                    self.check()
                    block = proc.stdout.read(4 * 1024 * 1024)
                    if not block:
                        break
                    out.write(block)
                    got += len(block)
                    if time.monotonic() - last >= 2:
                        last = time.monotonic()
                        pct = 1 + 84 * got / total if total else 1
                        self.set_stage("media", "active", pct, f"{label}: {size_text(got)} of {size_text(total)}")
        except BaseException:
            proc.kill()
            proc.wait()
            _remove(part)
            raise
        finally:
            proc.stdout.close()
        if proc.wait() != 0 or got != total:
            _remove(part)
            raise Problem("failed", missing, retry=False)
        os.replace(part, iso)
        # Checked again on the copy, which only this service can change, in
        # case the file changed after the wizard looked at it.
        self.set_stage("media", "active", 86, "Checking it's Windows 11 Pro")
        try:
            found = isocheck.check(iso)
        except isocheck.Unusable as e:
            _remove(iso)
            raise Problem("failed", e.text, retry=False) from e
        with open(meta_path, "w") as f:
            json.dump({"source": "own", "file": own.get("file", ""), "build": found["build"], "fetched": 0}, f)
        self.mark("windows-iso")

    def download(self, url, target, stage, start, end, label, agent=None):
        """HTTPS download with resume (Range), pause and progress, into
        target.part and then target."""
        part = target + ".part"
        tries = 0
        while True:
            self.check()
            have = os.path.getsize(part) if os.path.exists(part) else 0
            req = urllib.request.Request(url, headers={"User-Agent": agent or msdl.AGENT})
            if have:
                req.add_header("Range", f"bytes={have}-")
            try:
                with urllib.request.urlopen(req, timeout=READ_TIMEOUT) as r:
                    if have and r.status != 206:
                        have = 0
                    total = have + int(r.headers.get("Content-Length") or 0)
                    with open(part, "ab" if have else "wb") as f:
                        last_t, last_n, got = time.monotonic(), have, have
                        while True:
                            if self.stop.is_set() or self.rec.get("paused"):
                                break
                            block = r.read(1024 * 1024)
                            if not block:
                                break
                            f.write(block)
                            got += len(block)
                            now = time.monotonic()
                            if now - last_t >= 2:
                                self.speed = (got - last_n) / (now - last_t)
                                last_t, last_n = now, got
                                pct = start + (end - start) * got / total if total else start
                                self.save(download={"received": got, "total": total, "remaining": max(0, total - got)})
                                self.set_stage(stage, "active", pct,
                                               f"{label}: {size_text(got)} of {size_text(total)}" if total else label)
                    if self.stop.is_set() or self.rec.get("paused"):
                        self.check()
                        continue
                    if total and os.path.getsize(part) < total:
                        raise OSError("the connection ended early")
                os.replace(part, target)
                self.speed = 0.0
                return
            except urllib.error.HTTPError as e:
                if e.code == 416:
                    _remove(part)
                    continue
                raise
            except (urllib.error.URLError, OSError, socket.timeout) as e:
                log("download:", e)
                if not online():
                    self.wait_online(urllib.parse.urlsplit(url).hostname)
                    tries = 0
                    continue
                tries += 1
                if tries >= 6:
                    raise Problem("download", "The download kept stopping, although this PC is online. "
                                              "The server may be busy.") from e
                self.wait(min(60, 5 * tries))

    def virtio_iso(self):
        iso = os.path.join(CACHE, "virtio-win.iso")
        if is_iso(iso):
            return
        _remove(iso)
        # The stable link redirects to the versioned file over plain HTTP; it
        # is followed by hand so every byte comes over HTTPS.
        url = f"{VIRTIO_BASE}/stable-virtio/virtio-win.iso"
        for _ in range(3):
            loc = _redirect(url)
            if not loc:
                break
            url = loc.replace("http://", "https://", 1)
        if not url.startswith("https://fedorapeople.org/groups/virt/virtio-win/"):
            raise Problem("download", "The driver download moved somewhere unexpected.")
        # fedorapeople.org shows browsers a "not a bot" page in place of the
        # file, so this download says what it is rather than posing as one.
        self.download(url, iso, "media", 92, 96, "Drivers", agent=TOOL_AGENT)
        if not is_iso(iso):
            _remove(iso)
            raise Problem("download", "The drivers download came back as something other than the drivers. Try again in a while.")

    # install
    def step_install(self):
        if self.done("install"):
            return
        self.set_stage("install", "active", 0)
        domain = self.cfg["domain"]
        started = self.rec.get("install_started")
        if started and not self.rec.get("copied_at") and virsh.state(domain) in ("", "shut off", "saved"):
            # Stopped before Windows had copied itself (the PC restarted, or
            # this is a try again): the half written disk can't boot and the
            # installer is only asked for on a new install, so start one.
            log("Windows hadn't finished copying; starting its install again")
            self.purge("install")
            started = None
        if not started:
            # A new install gets a payload and disk made for it, never ones
            # an earlier Windows mode left behind (Windows Setup wipes that
            # disk anyway, and the check for Setup having started reads it).
            _remove(PAYLOAD)
            if virsh.state(domain) not in ("running", "paused"):
                _remove(DISK)
        st = self.prepare_start(domain)
        self.check()
        if st in ("shut off", "saved"):
            firewall.apply(self.cfg, False)
            virsh.start(domain)
            if not started:
                self.press_to_boot(domain)
        # Each start, including the one after a restart of the PC, makes a
        # fresh cgroup at the default weights.
        self.weigh(domain, spare=True)
        if not started:
            started = int(time.time())
            self.save(install_started=started, boot_tries=1, booted_at=started)
        while True:
            self.check()
            if time.time() - started > INSTALL_LIMIT:
                raise Problem("failed", "Windows didn't finish installing. Try again, and if it happens twice, restart the PC first.")
            st = virsh.state(domain)
            if st == "shut off":
                raise Problem("failed", "Windows switched itself off while installing. Try again.")
            if st == "paused":
                virsh.resume(domain)
            if qga.ping(domain, 3):
                break
            try:
                used = os.stat(DISK).st_blocks * 512
            except OSError:
                used = 0
            # Windows Setup writes to the disk within a minute or two of
            # starting. A disk still empty after five means the virtual PC
            # never started it: the key for "Press any key to boot from CD
            # or DVD" was missed (the PC slept or was slow to get there).
            # Start the virtual PC again and press it again.
            tries = self.rec.get("boot_tries", 1)
            if used < BOOT_WRITTEN and time.time() - self.rec.get("booted_at", started) > BOOT_WAIT:
                if tries >= BOOT_TRIES:
                    raise Problem("failed", "Windows Setup didn't start. Try again, and if it happens twice, restart the PC first.")
                log("Windows Setup hasn't started; starting the virtual PC again")
                virsh.run("reset", domain)
                self.press_to_boot(domain)
                self.save(boot_tries=tries + 1, booted_at=int(time.time()))
                continue
            # The disk shows the copying; after it, Windows spends ten minutes
            # or so on its first start writing little, so the bar keeps
            # creeping on the clock rather than sitting still.
            if used < INSTALLED_BYTES:
                pct = used * 70 // INSTALLED_BYTES
            else:
                if not self.rec.get("copied_at"):
                    self.save(copied_at=int(time.time()))
                pct = 70 + min(25, (time.time() - self.rec["copied_at"]) / 60 * 2.5)
            self.set_stage("install", "active", int(pct))
            time.sleep(15)
        self.set_stage("install", "done", 100)
        self.stage_done("install")

    @staticmethod
    def weigh(domain, spare):
        """The virtual PC's share of the processor and disk: the least while
        setup runs, the default (100) once Windows is someone's to use."""
        cpu, io = (SPARE_CPU_WEIGHT, SPARE_IO_WEIGHT) if spare else (100, 100)
        virsh.run("schedinfo", domain, "--live", f"cpu_shares={cpu}", check=False, timeout=10)
        virsh.run("blkiotune", domain, "--live", "--weight", str(io), check=False, timeout=10)

    def press_to_boot(self, domain):
        """OVMF asks "Press any key to boot from CD or DVD" for a few seconds
        after it starts; Enter for half a minute covers a slow start. Only a
        boot meant for the installer gets it, so Windows' own restarts go to
        the disk."""
        for _ in range(30):
            virsh.run("send-key", domain, "KEY_ENTER", check=False)
            time.sleep(1)

    # What each stage needs, checked before the virtual PC starts.
    def needs(self):
        """The setup discs the virtual PC still reads: Windows' installer
        until Windows has copied itself to its disk, the drivers until
        setup.ps1 has installed them, and the setup payload (answer file,
        setup.ps1, office.xml) until setup finishes."""
        return {WINDOWS_ISO: not self.done("install") and not self.rec.get("copied_at"),
                VIRTIO: not self.done("tune"),
                PAYLOAD: True}

    def prepare_start(self, domain):
        """Makes sure everything the virtual PC's definition points at is
        there before it starts, and returns its state. What setup still
        needs is made again: the payload here, the downloads by the media
        stage. A disc it no longer needs is left out of the definition
        instead. A virtual PC that is already running has what it needs."""
        if self.done("install") and not os.path.exists(DISK):
            raise Redo("install", "the virtual PC's disk is missing")
        st = virsh.state(domain)
        if st in ("running", "paused"):
            return st
        needs = self.needs()
        if any(needed and not is_iso(path) for path, needed in needs.items() if path != PAYLOAD):
            log("a download the virtual PC needs is missing; fetching it again")
            stage = self.rec.get("stage")
            self.step_media()
            if stage and stage != "media":
                self.set_stage(stage, "active")
        if not os.path.exists(PAYLOAD):
            if self.rec.get("install_started"):
                log("the setup payload is missing; making it again")
            self.prepare_licence()
            self.payload()
        self.network()
        if st == "saved" and any(not os.path.exists(path) for path in needs):
            # Its saved memory refers to a disc that has gone.
            virsh.run("managedsave-remove", domain, check=False)
            st = "shut off"
        if st in ("", "shut off"):
            # Defined afresh every time, so a fix to the definition (the
            # licence table, a disc that went) reaches a virtual PC an
            # earlier try made.
            self.define()
            st = "shut off"
        return st

    def ensure_running(self, domain):
        """For the stages that work inside Windows: it is running and its
        agent answers."""
        st = self.prepare_start(domain)
        if st in ("shut off", "saved"):
            firewall.apply(self.cfg, False)
            virsh.start(domain)
            self.weigh(domain, spare=True)
        elif st == "paused":
            virsh.resume(domain)
        deadline = time.time() + AGENT_WAIT
        while not qga.ping(domain, 3):
            self.check()
            if time.time() > deadline:
                raise Problem("failed", "Windows didn't start. Try again, and if it happens twice, restart the PC first.")
            time.sleep(5)

    def payload(self):
        lang, keyboard, tz = host_locale()
        pw = self.svc.new_guest_password()
        with open(os.path.join(SECRETS, "key")) as f:
            key = f.read().strip()
        host = socket.gethostname().split(".")[0].upper()
        computer = (re.sub(r"[^A-Z0-9-]", "", host)[:13] or "WILDOS") + "-W"
        self.svc.state["computer_name"] = computer
        self.svc.save_state()
        work = os.path.join(CACHE, "payload")
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(os.path.join(work, "wildos"), mode=0o700)
        with open(os.path.join(work, "autounattend.xml"), "w", encoding="utf-8") as f:
            f.write(render("autounattend.xml.in", {
                "LANG": lang, "INPUT_LOCALE": keyboard, "TIMEZONE": xml_escape(tz), "COMPUTER_NAME": computer,
                "ACCOUNT": self.cfg["account"], "PASSWORD": xml_escape(pw), "PRODUCT_KEY": key, "FULL_NAME": "Wild OS"}))
        guest = os.path.join(config.DATA, "guest")
        shutil.copy(os.path.join(guest, "setup.ps1"), os.path.join(work, "wildos", "setup.ps1"))
        shutil.copy(os.path.join(guest, "opengl.ps1"), os.path.join(work, "wildos", "opengl.ps1"))
        shutil.copy(os.path.join(guest, "tune.ps1"), os.path.join(work, "wildos", "tune.ps1"))
        shutil.copy(self.cfg["guest_exe"], os.path.join(work, "wildos", "wild-guest.exe"))
        plan = (self.rec.get("options") or {}).get("officePlan", "personal")
        with open(os.path.join(work, "wildos", "office.xml"), "w", encoding="utf-8") as f:
            f.write(office_config(plan, lang))
        with open(os.path.join(work, "wildos", "settings.json"), "w", encoding="utf-8") as f:
            json.dump({"host": self.cfg["host_address"], "guest": self.cfg["guest_address"],
                       "account": self.cfg["account"], "folders": self.cfg["folders"], "drive": self.cfg["drive"],
                       "transport": "rdp"}, f)
        out = PAYLOAD
        # Windows Setup looks for autounattend.xml at the root of every drive.
        p = subprocess.run(["xorriso", "-as", "mkisofs", "-quiet", "-J", "-joliet-long", "-r", "-V", "WILDOS_SETUP",
                            "-o", out + ".new", work], capture_output=True, text=True)
        # The answer file holds the guest password; only the ISO keeps it.
        shutil.rmtree(work, ignore_errors=True)
        if p.returncode != 0:
            raise RuntimeError(f"xorriso: {p.stderr.strip()[-300:]}")
        os.replace(out + ".new", out)
        os.chmod(out, 0o600)

    def network(self):
        name = self.cfg["network"]
        if virsh.run("net-info", name, check=False, timeout=10) == "":
            path = os.path.join(config.STATE_DIR, "network.xml")
            with open(path, "w") as f:
                f.write(render("network.xml.in", {"NAME": name, "BRIDGE": self.cfg["bridge"], "GW": self.cfg["host_address"],
                                                  "GUEST_IP": self.cfg["guest_address"], "GUEST_MAC": GUEST_MAC}))
            virsh.run("net-define", path)
        virsh.run("net-autostart", name, check=False)
        if "yes" not in re.sub(r"\s+", " ", virsh.run("net-info", name, check=False)).split("Active: ")[-1][:4]:
            virsh.run("net-start", name)
        ok, why = firewall.apply(self.cfg, False)
        if not ok:
            raise RuntimeError(f"firewall: {why}")

    def define(self):
        if not os.path.exists(DISK):
            # Metadata allocated up front saves the guest a lookup on every
            # first write (brief 5.5a); the data stays sparse.
            subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", "-o", "preallocation=metadata", DISK, "64G"],
                           check=True, capture_output=True)
        # Every core: Windows is paused whenever no Windows app is open, so
        # its share costs the desktop nothing then, and app starts inside it
        # are bound by processor (2 of 4 cores made Word slow to open).
        cpus = max(2, min(8, os.cpu_count() or 4))
        with open("/proc/cpuinfo") as f:
            cpu_off = "svm" if re.search(r"\bsvm\b", f.read()) else "vmx"
        vendor = _read("/sys/class/dmi/id/sys_vendor") or "Wild OS"
        product = _read("/sys/class/dmi/id/product_name") or "Windows mode"
        msdm = os.path.join(SECRETS, "msdm.bin")
        # libvirt attaches the licence table itself, so it also lets QEMU read
        # it (file ownership and AppArmor). Passed on QEMU's own command
        # line, as it was, QEMU was refused the file.
        acpi = f"<acpi><table type='msdm'>{xml_escape(msdm)}</table></acpi>" if os.path.exists(msdm) else ""
        memory_mb = 1024 * (self.rec.get("options") or {}).get("memoryGb", 4)
        os.makedirs(self.cfg["share_root"], exist_ok=True)
        xml = render("domain.xml.in", {
            "NAME": self.cfg["domain"], "MEMORY_MB": memory_mb, "CPUS": cpus, "CPU_OFF": cpu_off, "DISK": DISK,
            "WINDOWS_ISO": WINDOWS_ISO, "VIRTIO_ISO": VIRTIO, "PAYLOAD_ISO": PAYLOAD, "NET": self.cfg["network"], "GUEST_MAC": GUEST_MAC,
            "SHARE": self.cfg["share_root"], "VIRTIOFSD_BINARY": "", "SMBIOS_MANUFACTURER": xml_escape(vendor),
            "SMBIOS_PRODUCT": xml_escape(product), "ACPI_TABLES": acpi})
        for path, needed in self.needs().items():
            if not needed and not os.path.exists(path):
                # An empty drive: the disc has gone and isn't needed now.
                xml = re.sub(r"\s*<source file='" + re.escape(path) + r"'/>", "", xml)
        if virsh.state(self.cfg["domain"]) != "":
            # Defining it again: libvirt only updates a domain whose UUID
            # matches, and refuses a second one under the same name.
            uuid = virsh.run("domuuid", self.cfg["domain"]).strip()
            xml = xml.replace("</name>", f"</name>\n  <uuid>{uuid}</uuid>", 1)
        path = os.path.join(config.STATE_DIR, "domain.xml")
        with open(path, "w") as f:
            f.write(xml)
        os.chmod(path, 0o600)
        virsh.run("define", path)
        self.svc.memory = virsh.defined_memory(self.cfg["domain"])

    # activate
    def step_activate(self):
        if self.done("activate"):
            return
        lic = dict(self.rec.get("licence") or {})
        if lic.get("source") == "none":
            self.set_stage("activate", "done", 100)
            self.stage_done("activate")
            return
        self.set_stage("activate", "active", 10)
        domain = self.cfg["domain"]
        self.ensure_running(domain)
        key = self.activation_key(lic)
        taken = None
        if not key and lic.get("taken"):
            # Put in on an earlier try and forgotten since: only activating
            # is left to do.
            taken = True
        elif not key:
            raise Problem("failed", "This PC's licence couldn't be read. Go back and add a product key instead.",
                          retry=False)
        for _ in range(0 if taken else 3):
            self.check()
            try:
                _, out, _ = qga.powershell(domain, (
                    f"try {{ Get-CimInstance SoftwareLicensingService | Invoke-CimMethod -MethodName InstallProductKey "
                    f"-Arguments @{{ProductKey='{key}'}} -ErrorAction Stop | Out-Null; 'taken' }} catch {{ 'refused' }}"), 120)
                last = out.strip().splitlines()[-1] if out.strip() else ""
                taken = True if last == "taken" else False if last == "refused" else None
            except (virsh.VirshError, KeyError, TimeoutError, IndexError) as e:
                log("product key:", e)
            if taken is not None:
                break
            time.sleep(10)
        if taken:
            lic["taken"] = True
            self.save(licence=lic)
        if taken is False:
            # Not a Windows 11 Pro key, most often one for Home. Windows
            # stays Pro, unactivated, and works the same.
            lic.update(activated=False, refused=True)
            self.save(licence=lic)
            self.forget_activation_key()
            self.set_stage("activate", "done", 100,
                           ("This PC's licence is for another edition of Windows, usually Home, which Windows mode can't use. "
                            if lic.get("source") == "firmware" else
                            "Windows didn't accept that key. It may be for Home, which Windows mode can't use. ")
                           + "Windows works without activating; add a Windows 11 Pro key any time in Settings.")
            self.stage_done("activate")
            return
        code = None
        answer = ""
        # The network may come up a moment after the agent does.
        for _ in range(6):
            self.check()
            try:
                _, answer, _ = qga.powershell(domain, r"cscript //nologo $env:SystemRoot\System32\slmgr.vbs /ato 2>&1 | Out-String", 180)
                _, out, _ = qga.powershell(domain, (
                    "(Get-CimInstance SoftwareLicensingProduct -Filter \"ApplicationID='55c92734-d682-4d71-983e-d6ec3f16059f'"
                    " and PartialProductKey is not null\" | Select-Object -First 1).LicenseStatus"), 60)
                code = int(out.strip().splitlines()[-1]) if out.strip() else None
            except (virsh.VirshError, ValueError, KeyError, TimeoutError, IndexError) as e:
                log("activation:", e)
            if code == 1 or activation_reason(answer):
                # Asking again won't change an answer other than "offline".
                break
            time.sleep(30)
        lic["activated"] = code == 1
        self.save(licence=lic)
        if taken:
            self.forget_activation_key()
        # Not activating is not a failure: Windows works, and a key can be
        # added in Settings (board 9.8).
        reason = None if code == 1 else activation_reason(answer, lic.get("source") == "firmware")
        if code != 1:
            log("activation:", " ".join((answer or "").split())[-300:])
        self.set_stage("activate", "done", 100,
                       None if code == 1 else
                       (reason + " " if reason else "Not activated yet. ")
                       + "Windows works without activating; add a Windows 11 Pro key any time in Settings.")
        self.stage_done("activate")

    def activation_key(self, lic):
        if lic.get("source") == "firmware":
            try:
                with open(os.path.join(SECRETS, "msdm.bin"), "rb") as f:
                    return parse_msdm(f.read())
            except OSError:
                return None
        try:
            with open(os.path.join(SECRETS, ACTIVATION_KEY)) as f:
                return f.read().strip() or None
        except OSError:
            return None

    def forget_activation_key(self):
        try:
            os.remove(os.path.join(SECRETS, ACTIVATION_KEY))
        except OSError:
            pass

    def set_key(self, key):
        """For Settings, any time after setup. The key goes into Windows over
        the guest agent and is never written to disk here."""
        domain = self.cfg["domain"]
        if virsh.state(domain) != "running":
            return "Open a Windows app first, so Windows is running, then try again."
        source = "key"
        if key == "firmware":
            try:
                with open("/sys/firmware/acpi/tables/MSDM", "rb") as f:
                    key = parse_msdm(f.read())
            except OSError:
                key = None
            if not key:
                return "This PC has no Windows licence built in."
            source = "firmware"
        key = key.strip().upper()
        if not key_shape_ok(key):
            return "That doesn't look like a Windows product key. Check it and try again."
        try:
            _, out, _ = qga.powershell(domain, (
                f"$s = Get-CimInstance SoftwareLicensingService; try {{ $s | Invoke-CimMethod -MethodName InstallProductKey "
                f"-Arguments @{{ProductKey='{key}'}} -ErrorAction Stop | Out-Null }} catch {{ 'refused'; exit }}; "
                "$s | Invoke-CimMethod -MethodName RefreshLicenseStatus | Out-Null; "
                r"cscript //nologo $env:SystemRoot\System32\slmgr.vbs /ato 2>&1 | Out-String; "
                "(Get-CimInstance SoftwareLicensingProduct -Filter \"ApplicationID='55c92734-d682-4d71-983e-d6ec3f16059f'"
                " and PartialProductKey is not null\" | Select-Object -First 1).LicenseStatus"), 240)
            if out.strip().endswith("refused"):
                return "Windows didn't accept that key. Check it, and that it's for Windows 11 Pro: Windows mode can't use a Home key."
            code = int(out.strip().splitlines()[-1]) if out.strip() else None
            if code != 1:
                log("activation:", " ".join(out.split())[-300:])
        except (virsh.VirshError, ValueError, KeyError, TimeoutError, IndexError) as e:
            log("product key:", e)
            return "Windows didn't take that key. Check it, and that this PC is online, then try again."
        lic = dict(self.rec.get("licence") or {})
        lic.update(source=source, last5=key[-5:], activated=code == 1)
        self.save(licence=lic)
        return "" if code == 1 else activation_reason(out, source == "firmware") or NOT_ONLINE

    # tune
    def step_tune(self):
        if self.done("tune"):
            return
        self.set_stage("tune", "active", 0)
        domain = self.cfg["domain"]
        self.ensure_running(domain)
        if self.rec.get("rerun_guest"):
            self.rerun_guest_setup(domain)
        started = self.rec.get("tune_started")
        if not started:
            started = int(time.time())
            self.save(tune_started=started)
        while True:
            self.check()
            if time.time() - started > TUNE_LIMIT:
                raise Problem("failed", "Windows didn't finish getting ready. Try again.")
            if virsh.state(domain) in ("shut off", "saved"):
                self.prepare_start(domain)
                virsh.start(domain)
            ready = self._guest_json(r"C:\ProgramData\WildOS\ready.json")
            if ready:
                lic = dict(self.rec.get("licence") or {})
                edition = str(ready.get("edition", ""))
                lic["edition"] = "home" if edition.startswith("Core") else "pro" if edition else lic.get("edition", "")
                self.save(licence=lic)
                if lic["edition"] == "home":
                    # Windows 11 Home has no Remote Desktop host (brief 2.2).
                    raise Problem("home", "This licence is for Windows 11 Home, which can't show Windows apps on Wild OS.",
                                  retry=False)
                break
            stage = self._guest_json(r"C:\ProgramData\WildOS\stage.json") or {}
            if stage.get("state") == "failed":
                raise Problem("failed", "Windows couldn't finish getting ready. Try again.")
            name = stage.get("name")
            if name in GUEST_STEPS:
                self.set_stage("tune", "active", 100 * GUEST_STEPS.index(name) // len(GUEST_STEPS))
            time.sleep(10)
        self.guest_cleanup(f"Unregister-ScheduledTask -TaskName '{RERUN_TASK}' -Confirm:$false -ErrorAction SilentlyContinue")
        self.set_stage("tune", "done", 100)
        self.stage_done("tune")

    def rerun_guest_setup(self, domain):
        """setup.ps1 runs once, at Windows' first sign-in. To run it again
        after it failed, the account gets a new password, which only this
        task is given (the person's agent sets its own the first time it
        connects, as it does after every setup), and a task runs it as that
        account, so its per-user steps reach the right profile. Its steps
        that passed are skipped."""
        password = self.svc.new_guest_password()
        account = self.cfg["account"]
        script = rf"""$ErrorActionPreference = 'Stop'
$cd = Get-PSDrive -PSProvider FileSystem | Where-Object {{ Test-Path "$($_.Root)wildos\setup.ps1" }} | Select-Object -First 1
if (-not $cd) {{ throw 'the setup disc is missing' }}
Remove-Item 'C:\ProgramData\WildOS\stage.json', 'C:\ProgramData\WildOS\ready.json' -Force -ErrorAction SilentlyContinue
$pw = '{password}'
Set-LocalUser -Name '{account}' -Password (ConvertTo-SecureString $pw -AsPlainText -Force)
$a = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$($cd.Root)wildos\setup.ps1`""
Register-ScheduledTask -TaskName '{RERUN_TASK}' -Action $a -User '{account}' -Password $pw -RunLevel Highest -Force | Out-Null
Start-ScheduledTask -TaskName '{RERUN_TASK}'
'started'
"""
        code, out, err = qga.powershell(domain, script, 120)
        if code != 0 or "started" not in out:
            raise RuntimeError(f"running setup.ps1 again: {(err or out).strip()[-300:]}")
        log("running setup.ps1 inside Windows again")
        self.save(rerun_guest=False)

    def _guest_json(self, path):
        try:
            code, out, _ = qga.powershell(self.cfg["domain"],
                                          f"if (Test-Path '{path}') {{ Get-Content -Raw '{path}' }} else {{ exit 3 }}", 20)
            return json.loads(out) if code == 0 and out.strip() else None
        except (virsh.VirshError, ValueError, KeyError, TimeoutError):
            return None

    # office
    def step_office(self):
        if self.done("office"):
            return
        self.set_stage("office", "active", 5)
        domain = self.cfg["domain"]
        self.ensure_running(domain)
        check = f"Test-Path '{WORD}'"
        _, have, _ = qga.powershell(domain, check, 30)
        if have.strip() != "True":
            script = r"""$ErrorActionPreference = 'Stop'
$d = 'C:\ProgramData\WildOS\odt'
New-Item -ItemType Directory -Force $d | Out-Null
$cd = Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path "$($_.Root)wildos\office.xml" } | Select-Object -First 1
Copy-Item "$($cd.Root)wildos\office.xml" "$d\office.xml" -Force
Invoke-WebRequest -UseBasicParsing 'https://officecdn.microsoft.com/pr/wsus/setup.exe' -OutFile "$d\setup.exe"
$sig = Get-AuthenticodeSignature "$d\setup.exe"
if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notlike '*O=Microsoft Corporation*') { throw "setup.exe signature: $($sig.Status)" }
$p = Start-Process "$d\setup.exe" -ArgumentList '/configure', "$d\office.xml" -WorkingDirectory $d -PassThru
"pid=$($p.Id)"
"""
            code, out, err = qga.powershell(domain, script, 600)
            if code != 0:
                raise Problem("failed", "Microsoft 365 couldn't be downloaded. Add it later from Settings.")
            t0 = time.time()
            while True:
                self.check()
                _, have, _ = qga.powershell(domain, check + "; (Get-Process setup -ErrorAction SilentlyContinue).Count", 30)
                lines = have.split()
                if lines and lines[0] == "True" and (len(lines) < 2 or lines[1] == "0"):
                    break
                if time.time() - t0 > 5400:
                    raise Problem("failed", "Microsoft 365 didn't finish installing. Add it later from Settings.")
                if len(lines) > 1 and lines[1] == "0" and lines[0] != "True":
                    raise Problem("failed", "Microsoft 365 didn't install. Add it later from Settings.")
                self.set_stage("office", "active", min(95, 5 + (time.time() - t0) * 90 / (MINUTES["office"] * 60)))
                time.sleep(15)
        self.set_stage("office", "done", 100)
        self.stage_done("office")

    def finish(self):
        domain = self.cfg["domain"]
        # The installer, the drivers disc and the payload (which holds the
        # first password) are only for setup (brief section 4).
        for dev in ("sda", "sdb", "sdc"):
            virsh.run("change-media", domain, dev, "--eject", "--live", "--config", check=False)
        for name in ("windows.iso", "windows.iso.part", "payload.iso"):
            _remove(os.path.join(CACHE, name))
        if self.rec.get("iso"):
            _remove(os.path.join(CACHE, "windows-iso.json"))
        _remove(MARKER)
        self.save(recovery=None, notice=None, rerun_guest=False)
        # Upkeep keeps tune.ps1 current only in a Windows that has it, which
        # a setup started before it shipped does not.
        try:
            code, out, _ = qga.powershell(domain, r"(Get-Content -Raw 'C:\ProgramData\WildOS\tune.json' | ConvertFrom-Json).version", 30)
            if code == 0 and out.strip():
                self.svc.state["tune"] = out.strip().splitlines()[-1]
                self.svc.save_state()
        except (virsh.VirshError, ValueError, KeyError, TimeoutError) as e:
            log("tune:", e)
        # The payload carried this package's wild-guest (guest_outdated).
        digest = self.svc.guest_exe_digest()
        if digest:
            self.svc.state["guest_exe"] = digest
            self.svc.save_state()
        self.weigh(domain, spare=False)
        self.save(running=False, stage="done", problem=None, finished=int(time.time()))
        log("Windows mode is ready")


def spare_capacity():
    """Puts the calling thread, and the downloads, copies and checksums it
    does, behind everything else: the lowest processor priority and the
    idle disk class. Both are per thread on Linux, so the service's own
    D-Bus thread stays as quick as ever."""
    tid = threading.get_native_id()
    try:
        os.setpriority(os.PRIO_PROCESS, tid, 19)
    except OSError as e:
        log("lowering the setup thread's priority:", e)
    # ioprio_set(IOPRIO_WHO_PROCESS, tid, IOPRIO_CLASS_IDLE << 13); the
    # programs it starts (xorriso, qemu-img) inherit both.
    try:
        import ctypes
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.syscall(251, 1, tid, 3 << 13) != 0:
            log("the setup thread's disk class:", os.strerror(ctypes.get_errno()))
    except (OSError, AttributeError) as e:
        log("the setup thread's disk class:", e)


def as_user(uid):
    """Popen arguments that run a program as the account that asked, so it
    reads only the files that account can. Nothing to change when the
    service isn't root (the tests) or root itself asked."""
    if not uid or os.geteuid() != 0:
        return {}
    import pwd
    pw = pwd.getpwuid(uid)
    return {"user": uid, "group": pw.pw_gid, "extra_groups": os.getgrouplist(pw.pw_name, pw.pw_gid), "cwd": "/"}


def check_own_iso(path, uid):
    """(what the file is, "") for a Windows 11 Pro ISO someone already has,
    or (None, why not). The check runs as them: the service is root, and
    must not read, or say anything about, a file they couldn't open."""
    if not isinstance(path, str) or not path.startswith("/") or "\0" in path or len(path) > 4096:
        return None, "That file couldn't be found."
    try:
        p = subprocess.run([sys.executable, "-I", isocheck.__file__, path], capture_output=True, text=True, timeout=120,
                           env={"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8"}, **as_user(uid))
        found = json.loads(p.stdout)
    except (OSError, subprocess.SubprocessError, ValueError, KeyError) as e:
        log("checking an own ISO:", e)
        found = None
    if not isinstance(found, dict):
        return None, "Your Windows 11 file couldn't be checked. Try again."
    if not found.get("ok"):
        return None, str(found.get("text") or isocheck.UNREADABLE)
    return {"path": path, "file": str(found.get("file") or os.path.basename(path)), "bytes": int(found.get("bytes") or 0),
            "name": str(found.get("name") or ""), "build": int(found.get("build") or 0)}, ""


def forget_windows_iso(own):
    """Clears the cached Windows ISO a new setup won't use: any, when it is
    to copy someone's own file; only an earlier own file's copy, when it is
    to download, so a download that was kept isn't fetched twice."""
    meta = _read_json(os.path.join(CACHE, "windows-iso.json")) or {}
    if own or meta.get("source") == "own":
        for name in ("windows.iso", "windows.iso.part", "windows-iso.json"):
            _remove(os.path.join(CACHE, name))


def _gid(group):
    import grp
    try:
        return grp.getgrnam(group).gr_gid
    except KeyError:
        return None


def _redirect(url):
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": msdl.AGENT})

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None

    try:
        urllib.request.build_opener(NoRedirect).open(req, timeout=30)
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            return e.headers.get("Location")
    except urllib.error.URLError:
        pass
    return None


def _sha256(path, check=None):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for i, block in enumerate(iter(lambda: f.read(8 * 1024 * 1024), b"")):
            h.update(block)
            if check and i % 32 == 0:
                check()
    return h.hexdigest()


def _read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


def _read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _remove(path):
    try:
        os.remove(path)
    except OSError:
        pass
