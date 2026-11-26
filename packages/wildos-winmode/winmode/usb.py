"""USB devices handed to Windows (Settings > Windows apps > Windows mode).

A device that only has Windows drivers (a licence dongle, a label printer,
a scanner) goes to Windows live, as a libvirt USB host device, and comes
back when its switch goes off or Windows stops. The choice is remembered
here, per device, not in the domain's definition: a remembered device goes
to Windows whenever Windows runs and it is plugged in, and the definition
never names a device that may not be there at the next start.

Every device this module attaches carries a user alias (ua-wildos-usb-...),
so the live definition itself says which devices are Windows mode's, and
a restart of the service picks up where it was.

Devices are never offered when Wild OS needs them (keyboards, mice and
touchpads, the drive it runs from) and hubs are not shown. A device USB
protection (usbguard) holds back stays held back: it is shown as blocked,
with the same "Allow once" the shield offers, and nothing here changes
usbguard's decision.
"""

import json
import os
import re
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

from gi.repository import Gio, GLib

from . import config, virsh

SYSFS = "/sys/bus/usb/devices"
SYS_BLOCK = "/sys/class/block"
UDEV_DATA = "/run/udev/data"
POLKIT_ACTION = "org.wildos.winmode.usb"
ALIAS_PREFIX = "ua-wildos-usb-"
# Where Wild OS itself lives: a device holding any of these is never offered.
SYSTEM_MOUNTS = ("/", "/boot", "/boot/efi", "/efi", "/usr", "/var", "/home",
                 "/run/live/medium", "/run/initramfs/live", "/lib/live/mount/medium")
# A burst of udev events (a device and each of its interfaces, drivers
# binding) settles into one look.
SETTLE_MS = 700

# Interface classes, from the USB-IF's list.
HID, HUB, STORAGE, PRINTER, AUDIO, VIDEO, STILL_IMAGE = 3, 9, 8, 7, 1, 14, 6
COMMS, CDC_DATA, WIRELESS, SMART_CARD = 2, 10, 224, 11


def log(*parts):
    print("usb:", *parts, file=sys.stderr, flush=True)


def state_file():
    return os.path.join(config.STATE_DIR, "usb.json")


def read_attr(path, name):
    try:
        with open(os.path.join(path, name), encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


def clean(text, limit=60):
    """A name as the device or udev's database gives it, which a hostile
    device controls: printable, no markup, one line, not too long."""
    text = "".join(ch for ch in text if ch.isprintable() and ch not in "<>&\"'`\\")
    return " ".join(text.split())[:limit].strip()


COMPANY = re.compile(r"[ ,]+(inc|corp|corporation|co|company|ltd|limited|llc|gmbh|ag|sa|s\.a|bv|b\.v|plc|"
                     r"technology|technologies|electronics|international|industries|industrial|"
                     r"semiconductor|systems|computer)\.?$", re.I)


def short_company(name):
    """"Brother Industries, Ltd" reads as "Brother" in a list."""
    while True:
        shorter = COMPANY.sub("", name).rstrip(" ,.")
        if shorter == name or not shorter:
            return name
        name = shorter


def friendly_name(maker, product, db_vendor="", db_model=""):
    """What the device calls itself, or else what udev's copy of usb.ids
    calls it. "SanDisk SanDisk Ultra" reads badly, so the maker only goes
    in front when the product name does not already start with it."""
    maker, product = clean(maker), clean(product)
    if not product:
        maker, product = short_company(clean(db_vendor)), clean(db_model)
    maker = short_company(maker)
    if not product:
        return maker
    if not maker or product.lower().startswith(maker.lower().split()[0]):
        return product
    return f"{maker} {product}"


def interface_classes(descriptors):
    """(class, subclass, protocol) of the device and each interface, from
    the raw descriptors the kernel keeps in sysfs. These exist even while
    usbguard holds the device back."""
    found = []
    i = 0
    while i + 1 < len(descriptors) and descriptors[i] >= 2:
        length, kind = descriptors[i], descriptors[i + 1]
        if kind == 1 and i + 6 < len(descriptors):
            found.append((descriptors[i + 4], descriptors[i + 5], descriptors[i + 6]))
        elif kind == 4 and i + 7 < len(descriptors):
            found.append((descriptors[i + 5], descriptors[i + 6], descriptors[i + 7]))
        i += length
    return found


def kind_of(classes):
    """What to call the device: one word the page turns into an icon and a
    description."""
    codes = {c for c, _, _ in classes}
    for code, kind in ((PRINTER, "printer"), (AUDIO, "audio"), (VIDEO, "camera"), (STILL_IMAGE, "scanner"),
                       (STORAGE, "storage"), (SMART_CARD, "smartcard"), (WIRELESS, "wireless"),
                       (COMMS, "network"), (CDC_DATA, "network")):
        if code in codes:
            return kind
    if HID in codes:
        return "input"
    return "other"


def boot_input(classes):
    """keyboard or pointer from a HID boot interface, or None. Only used
    when udev has nothing to say: a blocked device has no input devices
    yet, and a "memory stick" with a keyboard inside is the classic attack,
    so it is never offered either."""
    for c, sub, proto in classes:
        if c == HID and sub == 1:
            if proto == 1:
                return "keyboard"
            if proto == 2:
                return "pointer"
    return None


def udev_properties(path):
    props = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("E:"):
                    key, _, value = line[2:].rstrip("\n").partition("=")
                    props[key] = value
    except OSError:
        pass
    return props


def input_role(dev_path, udev_data=UDEV_DATA):
    """keyboard or pointer when Wild OS takes typing or pointing from this
    device (udev's input_id says so for each input device under it), else
    None."""
    role = None
    for root, dirs, _ in os.walk(dev_path):
        if os.path.basename(root) != "input":
            continue
        for name in dirs:
            if not re.fullmatch(r"input\d+", name):
                continue
            props = udev_properties(os.path.join(udev_data, "+input:" + name))
            if props.get("ID_INPUT_KEYBOARD") == "1":
                return "keyboard"
            if props.get("ID_INPUT_MOUSE") == "1" or props.get("ID_INPUT_TOUCHPAD") == "1":
                role = "pointer"
        dirs[:] = []
    return role


def block_devices(dev_path):
    """The disks a USB device holds and their partitions: {"sdb", "sdb1"}."""
    names = set()
    for root, dirs, _ in os.walk(dev_path):
        if os.path.basename(root) == "block":
            for disk in dirs:
                names.add(disk)
                try:
                    entries = os.listdir(os.path.join(root, disk))
                except OSError:
                    entries = []
                names.update(e for e in entries if os.path.exists(os.path.join(root, disk, e, "partition")))
            dirs[:] = []
    return names


def expand_block(name, sys_block=SYS_BLOCK, seen=None):
    """A block device, the disk it is a partition of, and everything under
    it (an encrypted or LVM volume's slaves, down to the disks)."""
    seen = set() if seen is None else seen
    if not name or name in seen:
        return seen
    seen.add(name)
    path = os.path.join(sys_block, name)
    if os.path.exists(os.path.join(path, "partition")):
        seen.add(os.path.basename(os.path.dirname(os.path.realpath(path))))
    try:
        slaves = os.listdir(os.path.join(path, "slaves"))
    except OSError:
        slaves = []
    for slave in slaves:
        expand_block(slave, sys_block, seen)
    return seen


def block_name(source, majmin, sys_dev="/sys/dev/block"):
    """The kernel's name for a mount's device."""
    if source.startswith("/dev/"):
        try:
            rdev = os.stat(source).st_rdev
            majmin = f"{os.major(rdev)}:{os.minor(rdev)}"
        except OSError:
            pass
    if not majmin or majmin.startswith("0:"):
        return None
    path = os.path.join(sys_dev, majmin)
    return os.path.basename(os.path.realpath(path)) if os.path.exists(path) else None


def disks_in_use(mountinfo="/proc/self/mountinfo", swaps="/proc/swaps", sys_block=SYS_BLOCK,
                 sys_dev="/sys/dev/block"):
    """(system, mounted): the block devices Wild OS runs from (its own
    mounts and swap), and every one with something mounted from it."""
    system, mounted = set(), set()
    try:
        with open(mountinfo, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError:
        lines = []
    for line in lines:
        head, sep, tail = line.partition(" - ")
        fields, rest = head.split(), tail.split()
        if not sep or len(fields) < 5 or len(rest) < 2:
            continue
        name = block_name(rest[1], fields[2], sys_dev)
        if not name:
            continue
        names = expand_block(name, sys_block)
        mounted |= names
        if fields[4].replace("\\040", " ") in SYSTEM_MOUNTS:
            system |= names
    try:
        with open(swaps, encoding="utf-8") as f:
            for line in f.read().splitlines()[1:]:
                parts = line.split()
                if parts and parts[1] == "partition":
                    name = block_name(parts[0], "", sys_dev)
                    if name:
                        system |= expand_block(name, sys_block)
    except OSError:
        pass
    return system, mounted


def device_key(vendor, product, serial):
    """How a device is remembered: the same model plus its serial number,
    when it has one. Two identical devices without one share a choice."""
    key = f"{vendor}:{product}".lower()
    serial = clean(serial, 64).replace(" ", "_")
    return f"{key}:{serial}" if serial else key


def scan(sysfs=SYSFS, udev_data=UDEV_DATA, in_use=None):
    """The USB devices plugged in, as the Settings page lists them, before
    anything about Windows: port, name, kind, key, the libvirt address and
    alias, whether usbguard holds it back, and why Wild OS keeps it."""
    system, mounted = in_use if in_use is not None else disks_in_use()
    found = []
    try:
        entries = sorted(os.listdir(sysfs))
    except OSError:
        return found
    for port in entries:
        if ":" in port or port.startswith("usb"):
            continue
        path = os.path.join(sysfs, port)
        vendor, product = read_attr(path, "idVendor"), read_attr(path, "idProduct")
        bus, devnum = read_attr(path, "busnum"), read_attr(path, "devnum")
        if not re.fullmatch(r"[0-9a-fA-F]{4}", vendor) or not re.fullmatch(r"[0-9a-fA-F]{4}", product) \
                or not bus.isdigit() or not devnum.isdigit():
            continue
        try:
            with open(os.path.join(path, "descriptors"), "rb") as f:
                classes = interface_classes(f.read())
        except OSError:
            classes = []
        if read_attr(path, "bDeviceClass") == "09" or any(c == HUB for c, _, _ in classes):
            continue
        # Built in to the PC (its camera, fingerprint reader, Bluetooth):
        # not something anyone plugs in to hand to Windows.
        if read_attr(path, "removable") == "fixed":
            continue
        udev = udev_properties(os.path.join(udev_data, "c" + read_attr(path, "dev"))) if read_attr(path, "dev") else {}
        name = friendly_name(read_attr(path, "manufacturer"), read_attr(path, "product"),
                             udev.get("ID_VENDOR_FROM_DATABASE", ""), udev.get("ID_MODEL_FROM_DATABASE", ""))
        blocked = read_attr(path, "authorized") == "0"
        role = input_role(path, udev_data) or boot_input(classes)
        disks = block_devices(path)
        problem = ""
        if role:
            problem = role
        elif disks & system:
            problem = "system"
        elif blocked:
            problem = "blocked"
        elif disks & mounted:
            problem = "in-use"
        found.append({
            "port": port,
            "name": name,
            "kind": kind_of(classes),
            "key": device_key(vendor, product, read_attr(path, "serial")),
            "vendor": vendor.lower(),
            "product": product.lower(),
            "bus": int(bus),
            "device": int(devnum),
            "alias": f"{ALIAS_PREFIX}{int(bus)}-{int(devnum)}",
            "problem": problem,
        })
    return found


def hostdev_xml(dev):
    """The host device for one plugged-in device. The address picks it out
    among identical ones; vendor and product make libvirt refuse should a
    different device have taken that address since the scan."""
    return (f"<hostdev mode='subsystem' type='usb' managed='no'>"
            f"<source><vendor id='0x{dev['vendor']}'/><product id='0x{dev['product']}'/>"
            f"<address bus='{dev['bus']}' device='{dev['device']}'/></source>"
            f"<alias name='{dev['alias']}'/></hostdev>")


def attached_aliases(xml):
    """The USB host devices in a live definition that Windows mode put
    there, by alias. Any others (added by hand) are left alone."""
    try:
        root = ET.fromstring(xml)
    except ET.ParseError:
        return set()
    found = set()
    for hd in root.iter("hostdev"):
        alias = hd.find("alias")
        if hd.get("type") == "usb" and alias is not None and (alias.get("name") or "").startswith(ALIAS_PREFIX):
            found.add(alias.get("name"))
    return found


def plan(devices, remembered, attached, active, failed=()):
    """What to change: (devices to attach, aliases to detach). A device
    goes to Windows when it is remembered, Windows runs (paused counts)
    and nothing keeps it here; everything else Windows mode attached comes
    back, including a device since unplugged. One that failed to attach is
    not tried again until it is plugged in afresh or switched again."""
    want = {d["alias"] for d in devices
            if active and d["key"] in remembered and (d["problem"] == "" or d["alias"] in attached)}
    detach = sorted(a for a in attached if a not in want)
    attach = [d for d in devices if d["alias"] in want and d["alias"] not in attached and d["alias"] not in failed]
    return attach, detach


def describe(devices, remembered, attached, active, failed):
    """The list for Settings: each device's state is windows (Windows has
    it), waiting (it goes to Windows once Windows runs, or once its problem
    is gone), wildos, blocked, or refused (problem says why)."""
    out = []
    for d in devices:
        use = d["key"] in remembered
        problem = d["problem"]
        if d["alias"] in attached:
            state, problem = "windows", ""
        elif problem == "blocked":
            state = "blocked"
        elif problem in ("keyboard", "pointer", "system"):
            # Its switch is locked off, even when an identical model
            # without a serial number is remembered.
            state, use = "refused", False
        elif use and d["alias"] in failed:
            state, problem = "waiting", failed[d["alias"]]
        elif use:
            state = "waiting"
        else:
            state = "wildos"
        out.append({"port": d["port"], "name": d["name"], "kind": d["kind"], "use": use, "state": state,
                    "problem": problem})
    out.sort(key=lambda d: (d["name"].lower(), d["port"]))
    return {"running": bool(active), "devices": out}


def attach_problem(error):
    """A libvirt error as a problem code the page can put in words."""
    return "full" if "no free usb ports" in str(error).lower() else "failed"


class UsbManager:
    """Keeps Windows' USB devices in line with the remembered choices.
    Looks again whenever udev reports a USB change, Windows starts or
    stops, or a switch changes. libvirt calls run on a worker thread, one
    look at a time."""

    def __init__(self, service):
        self.service = service
        self.domain = service.domain
        saved = config.read_json(state_file(), {}) or {}
        self.remembered = saved.get("use", {}) if isinstance(saved.get("use"), dict) else {}
        self.failed = {}
        self.snapshot = None
        self.waiters = []
        self.running = False
        self.again = False
        self.settle = None
        self.monitor = None

    def start(self):
        self.watch_udev()
        self.request()

    def save(self):
        try:
            config.write_json(state_file(), {"use": self.remembered}, 0o600)
        except OSError as e:
            log(e)

    # udev.
    def watch_udev(self):
        try:
            self.monitor = subprocess.Popen(["udevadm", "monitor", "--udev", "--subsystem-match=usb"],
                                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except OSError as e:
            log("can't watch for devices:", e)
            return
        os.set_blocking(self.monitor.stdout.fileno(), False)

        def readable(fd, cond):
            try:
                data = os.read(fd, 65536)
            except BlockingIOError:
                return True
            if not data:
                GLib.timeout_add_seconds(5, lambda: (self.watch_udev(), False)[1])
                return False
            self.soon()
            return True

        GLib.io_add_watch(self.monitor.stdout.fileno(), GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP, readable)

    def soon(self):
        if self.settle is None:
            def fire():
                self.settle = None
                self.request()
                return False
            self.settle = GLib.timeout_add(SETTLE_MS, fire)

    def vm_changed(self, vm_state):
        """From the service's refresh, when libvirt reports a new state."""
        self.request()

    # One look at a time, on a worker thread.
    def request(self, waiter=None):
        if waiter:
            self.waiters.append(waiter)
        if self.running:
            self.again = True
            return
        self.running = True
        setting_up = bool(self.service.state.get("setting_up"))
        remembered = set(self.remembered)
        failed = dict(self.failed)
        threading.Thread(target=self.work, args=(remembered, failed, setting_up), daemon=True).start()

    def work(self, remembered, failed, setting_up):
        try:
            result = self.reconcile(remembered, failed, setting_up)
        except Exception as e:
            log("looking at devices:", e)
            result = None
        GLib.idle_add(lambda: (self.finished(result), False)[1])

    def reconcile(self, remembered, failed, setting_up):
        devices = scan()
        st = virsh.state(self.domain)
        # Setup owns the virtual PC until it finishes, and a Windows being
        # saved to disk has given its devices back (give_back_for_save).
        active = st in ("running", "paused") and not setting_up and not self.service.saving
        attached = set()
        if st in ("running", "paused"):
            try:
                attached = attached_aliases(virsh.dumpxml(self.domain))
            except virsh.VirshError as e:
                log(e)
        present = {d["alias"] for d in devices}
        failed = {a: why for a, why in failed.items() if a in present}
        attach, detach = plan(devices, remembered, attached, active, failed)
        for alias in detach:
            try:
                virsh.run("detach-device-alias", self.domain, alias, "--live", timeout=30)
                log("given back to Wild OS:", alias)
            except virsh.VirshError as e:
                if "no device found" not in str(e).lower():
                    log("couldn't give back", alias, e)
                    continue
            attached.discard(alias)
        for dev in attach:
            path = os.path.join(config.STATE_DIR, "usb-device.xml")
            try:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(hostdev_xml(dev))
                virsh.run("attach-device", self.domain, path, "--live", timeout=30)
                attached.add(dev["alias"])
                log(f"given to Windows: {dev['name']} ({dev['vendor']}:{dev['product']} at {dev['alias']})")
            except (OSError, virsh.VirshError) as e:
                failed[dev["alias"]] = attach_problem(e)
                log(f"couldn't give {dev['name']} to Windows:", e)
        return describe(devices, remembered, attached, active, failed), failed

    def give_back_for_save(self):
        """Before Windows is saved to disk: the devices it was given come
        back here first. A saved Windows names each one by the number the PC
        gave it when it was plugged in, which changes when it is plugged in
        again or the PC restarts, and libvirt then refused to load the save
        at all, so every app open in Windows was lost (issue 92). Windows
        gets them again as it comes back (vm_changed)."""
        try:
            attached = attached_aliases(virsh.dumpxml(self.domain))
        except virsh.VirshError as e:
            log(e)
            return
        for alias in sorted(attached):
            try:
                virsh.run("detach-device-alias", self.domain, alias, "--live", timeout=30)
                log("given back to Wild OS before saving Windows:", alias)
            except virsh.VirshError as e:
                log("couldn't give back", alias, e)

    def finished(self, result):
        self.running = False
        if result is not None:
            snapshot, failed = result
            self.failed = failed
            changed = snapshot != self.snapshot
            self.snapshot = snapshot
            if changed:
                self.service.export.emit("UsbChanged")
        waiters, self.waiters = self.waiters, []
        for waiter in waiters:
            waiter(self.snapshot)
        if self.again:
            self.again = False
            self.request()

    # D-Bus methods.
    def m_devices(self, sender, defer):
        if self.snapshot is not None and not self.running:
            return json.dumps(self.snapshot)
        self.request(lambda snap: defer.done(json.dumps(snap or {"running": False, "devices": []})))
        return defer

    def m_set(self, sender, port, use, defer):
        """Settings' "Use in Windows" switch. Giving a device to Windows
        needs an administrator (org.wildos.winmode.usb); taking it back
        never does. "" when done, else a sentence to show."""
        if not re.fullmatch(r"[0-9]+-[0-9]+(\.[0-9]+)*", port or ""):
            return "That device isn't plugged in any more."
        why = self.service.claim(sender)
        if why:
            return why
        if not use:
            self.choose(port, False, defer)
            return defer

        def allowed(ok):
            if ok:
                self.choose(port, True, defer)
            else:
                defer.done("Only an administrator can give devices to Windows.")

        self.check_admin(sender, allowed)
        return defer

    def choose(self, port, use, defer):
        devices = scan()
        dev = next((d for d in devices if d["port"] == port), None)
        if dev is None:
            defer.done("That device isn't plugged in any more.")
            return
        if use and dev["problem"] in ("keyboard", "pointer", "system", "blocked"):
            defer.done("Wild OS needs this device, so it can't go to Windows." if dev["problem"] != "blocked"
                       else "USB protection is blocking this device. Allow it first.")
            return
        if use:
            self.remembered[dev["key"]] = {"name": dev["name"], "since": int(time.time())}
        else:
            self.remembered.pop(dev["key"], None)
        self.failed.pop(dev["alias"], None)
        self.save()
        log(("remembered for Windows: " if use else "kept with Wild OS: ") + dev["name"])

        def answer(snapshot):
            mine = next((d for d in (snapshot or {}).get("devices", []) if d["port"] == port), None)
            problem = (mine or {}).get("problem")
            if use and problem == "full":
                defer.done("Windows already has as many devices as it can take. Switch one off first.")
            elif use and problem == "failed":
                defer.done("Windows couldn't take this device. Unplug it, plug it back in and try again.")
            else:
                defer.done("")

        self.request(answer)

    def check_admin(self, sender, reply):
        """Asks polkit, letting it show the password prompt."""
        uid = self.service.sender_uid(sender)
        if uid == 0:
            reply(True)
            return

        def got(conn, res):
            try:
                ok = bool(conn.call_finish(res).unpack()[0][0])
            except GLib.Error as e:
                log("polkit:", e.message)
                ok = False
            reply(ok)

        subject = ("system-bus-name", {"name": GLib.Variant("s", sender)})
        self.service.bus.call("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
                              "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization",
                              GLib.Variant("((sa{sv})sa{ss}us)", (subject, POLKIT_ACTION, {}, 1, "")),
                              GLib.VariantType("((bba{ss}))"), Gio.DBusCallFlags.NONE, 300000, None, got)
