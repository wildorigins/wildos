import json
import os
import tempfile
import unittest
from unittest import mock

from winmode import config, usb, virsh

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")

# Device descriptor (18 bytes) for a device whose class is in its
# interfaces, then a configuration descriptor and the interfaces given.
DEVICE = bytes([18, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, 0, 1, 1, 2, 3, 1])
CONFIG = bytes([9, 2, 0, 0, 1, 1, 0, 0x80, 50])


def interface(cls, sub=0, proto=0):
    return bytes([9, 4, 0, 0, 1, cls, sub, proto, 0])


def descriptors(*interfaces, device=DEVICE):
    return device + CONFIG + b"".join(interfaces)


class FakeSys:
    """A /sys/bus/usb/devices and /run/udev/data in a temporary folder."""

    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sysfs = os.path.join(self.tmp.name, "devices")
        self.udev = os.path.join(self.tmp.name, "udev")
        os.makedirs(self.sysfs)
        os.makedirs(self.udev)
        self.next_dev = 2

    def add(self, port, vendor="0529", product="0001", maker="", name="", serial="", authorized="1",
            removable="removable", desc=None, device_class="00", db=None):
        path = os.path.join(self.sysfs, port)
        os.makedirs(path)
        dev = self.next_dev
        self.next_dev += 1
        attrs = {"idVendor": vendor, "idProduct": product, "busnum": port.split("-")[0], "devnum": str(dev),
                 "authorized": authorized, "removable": removable, "bDeviceClass": device_class, "dev": f"189:{dev}"}
        if maker:
            attrs["manufacturer"] = maker
        if name:
            attrs["product"] = name
        if serial:
            attrs["serial"] = serial
        for key, value in attrs.items():
            with open(os.path.join(path, key), "w") as f:
                f.write(value + "\n")
        with open(os.path.join(path, "descriptors"), "wb") as f:
            f.write(desc if desc is not None else descriptors(interface(255)))
        if db:
            with open(os.path.join(self.udev, f"c189:{dev}"), "w") as f:
                f.write("".join(f"E:{k}={v}\n" for k, v in db.items()))
        return path

    def add_input(self, path, number, **props):
        d = os.path.join(path, path.rsplit("/", 1)[-1] + ":1.0", "0003:046D:C52B.0001", "input", f"input{number}")
        os.makedirs(d)
        with open(os.path.join(self.udev, f"+input:input{number}"), "w") as f:
            f.write("".join(f"E:{k}={v}\n" for k, v in props.items()))

    def add_disk(self, path, disk, partitions=()):
        d = os.path.join(path, path.rsplit("/", 1)[-1] + ":1.0", "host6", "target6:0:0", "6:0:0:0", "block", disk)
        os.makedirs(d)
        for p in partitions:
            os.makedirs(os.path.join(d, p))
            with open(os.path.join(d, p, "partition"), "w") as f:
                f.write("1\n")

    def scan(self, system=(), mounted=()):
        return usb.scan(self.sysfs, self.udev, (set(system), set(mounted)))

    def close(self):
        self.tmp.cleanup()


class Descriptors(unittest.TestCase):
    def test_classes_of_the_device_and_each_interface(self):
        raw = descriptors(interface(3, 1, 1), interface(3, 0, 0))
        self.assertEqual(usb.interface_classes(raw), [(0, 0, 0), (3, 1, 1), (3, 0, 0)])

    def test_truncated_descriptors_do_not_crash(self):
        self.assertEqual(usb.interface_classes(b""), [])
        self.assertEqual(usb.interface_classes(bytes([18, 1, 0])), [])
        self.assertEqual(usb.interface_classes(bytes([0, 0, 0, 0])), [])

    def test_boot_keyboard_and_mouse(self):
        self.assertEqual(usb.boot_input([(3, 1, 1)]), "keyboard")
        self.assertEqual(usb.boot_input([(3, 1, 2)]), "pointer")
        # A HID device that is not a boot keyboard or mouse (many dongles).
        self.assertIsNone(usb.boot_input([(3, 0, 0)]))

    def test_kinds(self):
        self.assertEqual(usb.kind_of([(7, 1, 2)]), "printer")
        self.assertEqual(usb.kind_of([(1, 1, 0), (3, 0, 0)]), "audio")
        self.assertEqual(usb.kind_of([(8, 6, 80)]), "storage")
        self.assertEqual(usb.kind_of([(255, 0, 0)]), "other")
        self.assertEqual(usb.kind_of([(3, 0, 0)]), "input")


class Names(unittest.TestCase):
    def test_maker_not_repeated(self):
        self.assertEqual(usb.friendly_name("SanDisk", "SanDisk Ultra"), "SanDisk Ultra")
        self.assertEqual(usb.friendly_name("Brother", "QL-700"), "Brother QL-700")

    def test_database_names_when_the_device_has_none(self):
        self.assertEqual(usb.friendly_name("", "", "Brother Industries, Ltd", "QL-700 Label Printer"),
                         "Brother QL-700 Label Printer")
        self.assertEqual(usb.friendly_name("", "", "SafeNet Inc.", ""), "SafeNet")

    def test_hostile_names_are_cleaned(self):
        name = usb.friendly_name("<b>Evil</b>", "Stick\x00\n\"; rm -rf /" + "x" * 100)
        self.assertNotIn("<", name)
        self.assertNotIn("\n", name)
        self.assertNotIn('"', name)
        self.assertLessEqual(len(name), 121)

    def test_nothing_at_all(self):
        self.assertEqual(usb.friendly_name("", ""), "")


class Scan(unittest.TestCase):
    def setUp(self):
        self.sys = FakeSys()

    def tearDown(self):
        self.sys.close()

    def by_port(self, devices):
        return {d["port"]: d for d in devices}

    def test_a_dongle_is_offered(self):
        self.sys.add("1-2", vendor="0529", product="0001", maker="SafeNet", name="Sentinel HL", serial="ABC 123")
        dev = self.sys.scan()[0]
        self.assertEqual(dev["name"], "SafeNet Sentinel HL")
        self.assertEqual(dev["problem"], "")
        self.assertEqual(dev["key"], "0529:0001:ABC_123")
        self.assertEqual(dev["alias"], "ua-wildos-usb-1-2")

    def test_no_serial_keys_by_model(self):
        self.sys.add("1-2", vendor="04F9", product="2042")
        self.assertEqual(self.sys.scan()[0]["key"], "04f9:2042")

    def test_hubs_root_hubs_interfaces_and_built_in_devices_are_left_out(self):
        self.sys.add("1-1", device_class="09")
        self.sys.add("1-3", desc=descriptors(interface(9)))
        self.sys.add("1-4", removable="fixed", name="Integrated Camera")
        os.makedirs(os.path.join(self.sys.sysfs, "usb1"))
        os.makedirs(os.path.join(self.sys.sysfs, "1-2:1.0"))
        self.assertEqual(self.sys.scan(), [])

    def test_keyboards_mice_and_touchpads_stay(self):
        kb = self.sys.add("1-2", desc=descriptors(interface(3, 0, 0)))
        self.sys.add_input(kb, 5, ID_INPUT="1", ID_INPUT_KEYBOARD="1")
        mouse = self.sys.add("1-3", desc=descriptors(interface(3, 0, 0)))
        self.sys.add_input(mouse, 6, ID_INPUT="1", ID_INPUT_MOUSE="1")
        pad = self.sys.add("1-4", desc=descriptors(interface(3, 0, 0)))
        self.sys.add_input(pad, 7, ID_INPUT="1", ID_INPUT_TOUCHPAD="1")
        # Volume knobs on an audio interface are keys, not a keyboard.
        audio = self.sys.add("1-5", desc=descriptors(interface(1, 1), interface(3, 0, 0)))
        self.sys.add_input(audio, 8, ID_INPUT="1", ID_INPUT_KEY="1")
        devs = self.by_port(self.sys.scan())
        self.assertEqual(devs["1-2"]["problem"], "keyboard")
        self.assertEqual(devs["1-3"]["problem"], "pointer")
        self.assertEqual(devs["1-4"]["problem"], "pointer")
        self.assertEqual(devs["1-5"]["problem"], "")
        self.assertEqual(devs["1-5"]["kind"], "audio")

    def test_a_blocked_keyboard_is_a_keyboard_not_something_to_allow(self):
        self.sys.add("1-2", authorized="0", desc=descriptors(interface(8, 6, 80), interface(3, 1, 1)))
        self.assertEqual(self.sys.scan()[0]["problem"], "keyboard")

    def test_blocked(self):
        self.sys.add("1-2", authorized="0")
        self.assertEqual(self.sys.scan()[0]["problem"], "blocked")

    def test_the_system_drive_and_mounted_drives(self):
        live = self.sys.add("2-1", desc=descriptors(interface(8, 6, 80)))
        self.sys.add_disk(live, "sdb", ["sdb1", "sdb2"])
        stick = self.sys.add("2-2", desc=descriptors(interface(8, 6, 80)))
        self.sys.add_disk(stick, "sdc", ["sdc1"])
        spare = self.sys.add("2-3", desc=descriptors(interface(8, 6, 80)))
        self.sys.add_disk(spare, "sdd", ["sdd1"])
        devs = self.by_port(self.sys.scan(system={"sdb", "sdb1"}, mounted={"sdb", "sdb1", "sdc1", "sdc"}))
        self.assertEqual(devs["2-1"]["problem"], "system")
        self.assertEqual(devs["2-2"]["problem"], "in-use")
        self.assertEqual(devs["2-3"]["problem"], "")
        self.assertEqual(devs["2-3"]["kind"], "storage")

    def test_garbage_ids_are_skipped(self):
        self.sys.add("1-2", vendor="zz'/>", product="0001")
        self.assertEqual(self.sys.scan(), [])


class Disks(unittest.TestCase):
    """disks_in_use against a fake /sys/class/block and /sys/dev/block: the
    root file system on an encrypted partition of nvme0n1, a live medium on
    sdb1, and a stick mounted from sdc1."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.block = os.path.join(t, "class-block")
        self.dev = os.path.join(t, "dev-block")
        devices = os.path.join(t, "devices")
        os.makedirs(self.block)
        os.makedirs(self.dev)
        layout = {"nvme0n1": ["nvme0n1p1", "nvme0n1p2"], "sdb": ["sdb1"], "sdc": ["sdc1"]}
        numbers = {"nvme0n1p1": "259:1", "nvme0n1p2": "259:2", "sdb1": "8:17", "sdc1": "8:33", "dm-0": "253:0"}
        for disk, parts in layout.items():
            os.makedirs(os.path.join(devices, disk))
            os.symlink(os.path.join(devices, disk), os.path.join(self.block, disk))
            for p in parts:
                os.makedirs(os.path.join(devices, disk, p))
                open(os.path.join(devices, disk, p, "partition"), "w").close()
                os.symlink(os.path.join(devices, disk, p), os.path.join(self.block, p))
        os.makedirs(os.path.join(devices, "dm-0", "slaves"))
        os.symlink(os.path.join(devices, "dm-0"), os.path.join(self.block, "dm-0"))
        os.symlink(os.path.join(devices, "nvme0n1", "nvme0n1p2"), os.path.join(devices, "dm-0", "slaves", "nvme0n1p2"))
        for name, number in numbers.items():
            os.symlink(os.path.join(self.block, name), os.path.join(self.dev, number))
        self.mountinfo = os.path.join(t, "mountinfo")
        with open(self.mountinfo, "w") as f:
            f.write("22 1 253:0 / / rw - ext4 /nonexistent/mapper/root rw\n"
                    "23 22 259:1 / /boot/efi rw - vfat /nonexistent/nvme0n1p1 rw\n"
                    "24 22 8:17 / /run/live/medium ro - iso9660 /nonexistent/sdb1 ro\n"
                    "25 22 8:33 / /media/alex/STICK rw - vfat /nonexistent/sdc1 rw\n"
                    "26 22 0:25 / /proc rw - proc proc rw\n")
        self.swaps = os.path.join(t, "swaps")
        with open(self.swaps, "w") as f:
            f.write("Filename Type Size Used Priority\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_system_and_mounted(self):
        system, mounted = usb.disks_in_use(self.mountinfo, self.swaps, self.block, self.dev)
        self.assertTrue({"nvme0n1", "nvme0n1p2", "dm-0", "sdb", "sdb1"} <= system)
        self.assertNotIn("sdc", system)
        self.assertTrue({"sdc", "sdc1"} <= mounted)


def dev(port, key, problem="", bus=1, device=None):
    device = device or int(port.split("-")[1]) + 10
    return {"port": port, "name": port, "kind": "other", "key": key, "vendor": "0529", "product": "0001",
            "bus": bus, "device": device, "alias": f"ua-wildos-usb-{bus}-{device}", "problem": problem}


class Plan(unittest.TestCase):
    def test_remembered_devices_go_while_windows_runs(self):
        devices = [dev("1-2", "a"), dev("1-3", "b")]
        attach, detach = usb.plan(devices, {"a"}, set(), True)
        self.assertEqual([d["port"] for d in attach], ["1-2"])
        self.assertEqual(detach, [])

    def test_nothing_goes_while_windows_is_off(self):
        attach, detach = usb.plan([dev("1-2", "a")], {"a"}, set(), False)
        self.assertEqual((attach, detach), ([], []))

    def test_switched_off_comes_back(self):
        d = dev("1-2", "a")
        attach, detach = usb.plan([d], set(), {d["alias"]}, True)
        self.assertEqual((attach, detach), ([], [d["alias"]]))

    def test_unplugged_devices_are_cleared(self):
        attach, detach = usb.plan([], {"a"}, {"ua-wildos-usb-1-9"}, True)
        self.assertEqual(detach, ["ua-wildos-usb-1-9"])

    def test_already_there_is_left_alone(self):
        d = dev("1-2", "a")
        self.assertEqual(usb.plan([d], {"a"}, {d["alias"]}, True), ([], []))

    def test_wild_os_devices_never_go(self):
        devices = [dev("1-2", "a", "keyboard"), dev("1-3", "a", "system"), dev("1-4", "a", "blocked"),
                   dev("1-5", "a", "in-use"), dev("1-6", "a", "pointer")]
        self.assertEqual(usb.plan(devices, {"a"}, set(), True), ([], []))

    def test_a_failed_attach_waits_for_a_new_plug_in(self):
        d = dev("1-2", "a")
        self.assertEqual(usb.plan([d], {"a"}, set(), True, {d["alias"]: "full"}), ([], []))

    def test_two_identical_devices_without_serials_both_go(self):
        devices = [dev("1-2", "a"), dev("1-3", "a")]
        attach, _ = usb.plan(devices, {"a"}, set(), True)
        self.assertEqual(len(attach), 2)


class Describe(unittest.TestCase):
    def test_states(self):
        devices = [dev("1-1", "w"), dev("1-2", "x"), dev("1-3", "y", "blocked"), dev("1-4", "z", "keyboard"),
                   dev("1-5", "v", "in-use"), dev("1-6", "f")]
        attached = {devices[0]["alias"]}
        failed = {devices[5]["alias"]: "full"}
        out = usb.describe(devices, {"w", "v", "f", "z"}, attached, True, failed)
        states = {d["port"]: (d["state"], d["use"], d["problem"]) for d in out["devices"]}
        self.assertTrue(out["running"])
        self.assertEqual(states["1-1"], ("windows", True, ""))
        self.assertEqual(states["1-2"], ("wildos", False, ""))
        self.assertEqual(states["1-3"], ("blocked", False, "blocked"))
        self.assertEqual(states["1-4"], ("refused", False, "keyboard"))
        self.assertEqual(states["1-5"], ("waiting", True, "in-use"))
        self.assertEqual(states["1-6"], ("waiting", True, "full"))
        for d in out["devices"]:
            self.assertNotIn("key", d)

    def test_waiting_for_windows(self):
        out = usb.describe([dev("1-2", "a")], {"a"}, set(), False, {})
        self.assertEqual(out["devices"][0]["state"], "waiting")
        self.assertFalse(out["running"])


class Libvirt(unittest.TestCase):
    def test_hostdev_xml_and_aliases(self):
        d = dev("1-2", "a")
        xml = usb.hostdev_xml(d)
        self.assertIn("<address bus='1' device='12'/>", xml)
        self.assertIn("<vendor id='0x0529'/>", xml)
        self.assertIn("<alias name='ua-wildos-usb-1-12'/>", xml)
        domain = (f"<domain><devices><input type='tablet' bus='usb'/>{xml}"
                  "<hostdev mode='subsystem' type='usb'><alias name='ua-mine'/></hostdev>"
                  "<hostdev mode='subsystem' type='pci'><alias name='ua-wildos-usb-0-0'/></hostdev>"
                  "</devices></domain>")
        self.assertEqual(usb.attached_aliases(domain), {"ua-wildos-usb-1-12"})
        self.assertEqual(usb.attached_aliases("not xml"), set())

    def test_errors(self):
        self.assertEqual(usb.attach_problem(virsh.VirshError("internal error: No free USB ports")), "full")
        self.assertEqual(usb.attach_problem(virsh.VirshError("Permission denied")), "failed")

    def test_template_leaves_room_for_devices(self):
        with open(os.path.join(DATA, "templates", "domain.xml.in"), encoding="utf-8") as f:
            self.assertIn("<controller type='usb' model='qemu-xhci' ports='8'/>", f.read())

    def test_policy_needs_an_administrator(self):
        with open(os.path.join(DATA, "org.wildos.winmode.policy"), encoding="utf-8") as f:
            text = f.read()
        action = text[text.index('<action id="org.wildos.winmode.usb">'):]
        self.assertIn("<allow_active>auth_admin_keep</allow_active>", action[:action.index("</action>")])


class FakeService:
    domain = "wildos-windows"

    def __init__(self):
        self.state = {}
        self.claims = None
        self.saving = False

    def claim(self, sender):
        return self.claims

    def sender_uid(self, sender):
        return 1000


class Manager(unittest.TestCase):
    """UsbManager with libvirt and the device scan mocked."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        patcher = mock.patch.object(config, "STATE_DIR", self.tmp.name)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self.tmp.cleanup)
        self.calls = []
        self.vm = "running"
        self.live = "<domain><devices/></domain>"
        self.fail_attach = None

        def run(*args, **kwargs):
            self.calls.append(args)
            if args[0] == "attach-device":
                if self.fail_attach:
                    raise virsh.VirshError(self.fail_attach)
                with open(args[2]) as f:
                    self.attached_xml = f.read()
            return ""

        for name, fn in (("run", run), ("state", lambda d: self.vm), ("dumpxml", lambda d, inactive=False: self.live)):
            p = mock.patch.object(usb.virsh, name, fn)
            p.start()
            self.addCleanup(p.stop)
        self.devices = [dev("1-2", "0529:0001:x"), dev("1-3", "04f9:2042")]
        p = mock.patch.object(usb, "scan", lambda *a, **k: [dict(d) for d in self.devices])
        p.start()
        self.addCleanup(p.stop)

    def test_attaches_remembered_devices_live(self):
        m = usb.UsbManager(FakeService())
        snapshot, failed = m.reconcile({"0529:0001:x"}, {}, False)
        self.assertEqual(self.calls[0][0], "attach-device")
        self.assertIn("--live", self.calls[0])
        self.assertNotIn("--config", self.calls[0])
        self.assertIn("ua-wildos-usb-1-12", self.attached_xml)
        states = {d["port"]: d["state"] for d in snapshot["devices"]}
        self.assertEqual(states, {"1-2": "windows", "1-3": "wildos"})
        self.assertEqual(failed, {})

    def test_detaches_by_alias(self):
        self.live = "<domain><devices>" + usb.hostdev_xml(self.devices[0]) + "</devices></domain>"
        m = usb.UsbManager(FakeService())
        snapshot, _ = m.reconcile(set(), {}, False)
        self.assertEqual(self.calls, [("detach-device-alias", "wildos-windows", "ua-wildos-usb-1-12", "--live")])
        self.assertEqual(snapshot["devices"][0]["state"], "wildos")

    def test_devices_come_back_before_windows_is_saved(self):
        self.live = "<domain><devices>" + usb.hostdev_xml(self.devices[0]) + "</devices></domain>"
        service = FakeService()
        m = usb.UsbManager(service)
        m.give_back_for_save()
        self.assertEqual(self.calls, [("detach-device-alias", "wildos-windows", "ua-wildos-usb-1-12", "--live")])
        # Nor given again while the save is being written.
        self.calls.clear()
        self.live = "<domain><devices/></domain>"
        service.saving = True
        m.reconcile({"0529:0001:x"}, {}, False)
        self.assertEqual(self.calls, [])

    def test_nothing_while_setting_up_or_off(self):
        m = usb.UsbManager(FakeService())
        m.reconcile({"0529:0001:x"}, {}, True)
        self.vm = "shut off"
        snapshot, _ = m.reconcile({"0529:0001:x"}, {}, False)
        self.assertEqual(self.calls, [])
        self.assertEqual(snapshot["devices"][0]["state"], "waiting")

    def test_a_full_controller_is_reported_and_not_retried(self):
        self.fail_attach = "internal error: No free USB ports"
        m = usb.UsbManager(FakeService())
        snapshot, failed = m.reconcile({"0529:0001:x"}, {}, False)
        self.assertEqual(failed, {"ua-wildos-usb-1-12": "full"})
        self.assertEqual(snapshot["devices"][0]["problem"], "full")
        self.calls.clear()
        m.reconcile({"0529:0001:x"}, failed, False)
        self.assertEqual(self.calls, [])

    def test_choices_are_remembered_across_restarts(self):
        m = usb.UsbManager(FakeService())
        answers = []
        with mock.patch.object(m, "request", lambda waiter=None: waiter({"devices": []})):
            m.choose("1-2", True, mock.Mock(done=answers.append))
        self.assertEqual(answers, [""])
        with open(usb.state_file()) as f:
            self.assertIn("0529:0001:x", json.load(f)["use"])
        self.assertIn("0529:0001:x", usb.UsbManager(FakeService()).remembered)
        with mock.patch.object(m, "request", lambda waiter=None: waiter({"devices": []})):
            m.choose("1-2", False, mock.Mock(done=answers.append))
        self.assertEqual(usb.UsbManager(FakeService()).remembered, {})

    def test_wild_os_devices_cannot_be_chosen(self):
        self.devices[0]["problem"] = "keyboard"
        m = usb.UsbManager(FakeService())
        answers = []
        m.choose("1-2", True, mock.Mock(done=answers.append))
        self.assertEqual(len(answers), 1)
        self.assertNotEqual(answers[0], "")
        self.assertEqual(m.remembered, {})

    def test_a_device_gone_meanwhile(self):
        m = usb.UsbManager(FakeService())
        answers = []
        m.choose("1-9", True, mock.Mock(done=answers.append))
        self.assertEqual(answers, ["That device isn't plugged in any more."])

    def test_bad_ports_and_other_owners_are_refused(self):
        service = FakeService()
        m = usb.UsbManager(service)
        self.assertIn("plugged in", m.m_set(":sender", "../../x", True, defer=None))
        service.claims = "Windows mode on this PC is set up for Alex."
        self.assertEqual(m.m_set(":sender", "1-2", True, defer=None), service.claims)


if __name__ == "__main__":
    unittest.main()
