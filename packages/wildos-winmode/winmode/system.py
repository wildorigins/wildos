"""winmoded: the Windows mode system service (brief section 3).

A small root service that owns libvirt, the disk image and the guest's
channels. It runs the lifecycle state machine and idle policy (5.6), keeps
the guest's clock right after a pause or restore, and relays wild-guest's
window list. Sleep and shutdown handling (logind's delay inhibitor, board
9.9) are here too. The per-user agent (agent.py) reaches it on the
system bus; polkit lets the active local user in (org.wildos.winmode.use).
"""

import hashlib
import json
import os
import pwd
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time

from gi.repository import Gio, GLib

from . import activation, config, dbusutil, documents, firewall, guestlink, hostinfo, lifecycle, paths, qga, setup, usb, virsh

XML = """
<node>
  <interface name="org.wildos.WindowsMode1.Host">
    <method name="Status"><arg type="s" direction="out"/></method>
    <method name="Wake"><arg type="s" direction="out"/></method>
    <method name="Prestart"><arg type="s" direction="out"/></method>
    <method name="Idle">
      <arg name="mode" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Power">
      <arg name="action" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Documents"><arg type="s" direction="out"/></method>
    <method name="Launch">
      <arg name="path" type="s" direction="in"/>
      <arg name="args" type="as" direction="in"/>
      <arg name="cwd" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Windows"><arg type="s" direction="out"/></method>
    <method name="Window">
      <arg name="hwnd" type="t" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Apps">
      <arg name="icons" type="b" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Uninstall">
      <arg name="path" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="SetTheme">
      <arg name="theme" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="SetOption">
      <arg name="key" type="s" direction="in"/>
      <arg name="value" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="SetWarm">
      <arg name="warm" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="ConnectionChanged">
      <arg name="open" type="b" direction="in"/>
    </method>
    <method name="Credentials"><arg type="s" direction="out"/></method>
    <method name="InstallGuestHelper"><arg type="s" direction="out"/></method>
    <method name="SetupStart">
      <arg name="request" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="SetupControl">
      <arg name="action" type="s" direction="in"/>
      <arg name="argument" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Firmware"><arg type="s" direction="out"/></method>
    <method name="SetProductKey">
      <arg name="key" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="ActivationStart"><arg type="s" direction="out"/></method>
    <method name="ActivationCheck"><arg type="s" direction="out"/></method>
    <method name="ActivationEnd"><arg type="s" direction="out"/></method>
    <method name="EndProgram">
      <arg name="pid" type="u" direction="in"/>
      <arg name="image" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="GuestRequest">
      <arg name="message" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Remove"><arg type="s" direction="out"/></method>
    <method name="UsbDevices"><arg type="s" direction="out"/></method>
    <method name="SetUsbDevice">
      <arg name="port" type="s" direction="in"/>
      <arg name="use" type="b" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <signal name="Changed"/>
    <signal name="WindowsChanged"><arg name="windows" type="s"/></signal>
    <signal name="UsbChanged"/>
  </interface>
</node>
"""

LAUNCH_HOLD = "launch"
# A product key going in: Windows has to stay running until it is done.
LICENCE_HOLD = "licence"
# How long to wait before trying again to restart Windows for a new memory
# limit when the last try didn't finish.
MEMORY_RETRY = 600
PRESTART_HOLD = "prestart"
# How long Windows started ahead of time stays up for the agent to sign in
# to it (measured: about 6 seconds just after a boot).
PRESTART_SIGN_IN_SECONDS = 45
# From the moment Windows runs until wild-guest's first window list, the
# window count is unknown, not zero: an app may well be open.
WINDOW_LIST_HOLD = "window list"
# A wild-guest that never reports must not keep Windows awake for good.
WINDOW_LIST_WAIT = 600
# Resume, Start, Restart and Shut down from the taskbar's panel, and keep
# (saved to disk as Wild OS shuts down, signs out or restarts, board 9.9).
POWER_HOLD = "power"
POWER_ACTIONS = ("resume", "start", "restart", "shutdown", "keep")
# How long a clean Windows shutdown may take before Windows is switched
# off instead, which is reported.
SHUTDOWN_WAIT = 90
# The share of logind's InhibitDelayMaxSec (60 s, data/logind) that saving
# or shutting Windows down may use when the PC itself shuts down.
PC_SHUTDOWN_BUDGET = 50
# Windows is saved rather than shut down as the PC shuts down, so it only
# restarts when restart_windows does it: on mains, with no app open, once
# an update waits for a restart or a week after the last one. Asked at
# most this often, as asking takes a few seconds of the guest agent.
UPDATE_CHECK_EVERY = 3600
UPDATE_RESTART_DAYS = 7
# A restart that installs updates can take a while to shut down.
UPDATE_RESTART_WAIT = 900
NEEDS_RESTART_SCRIPT = (
    r"$p = (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired') -or "
    r"(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending'); "
    r"$d = [int][math]::Floor(((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalDays); "
    r'"$p $d"')
# Removing Windows mode needs an administrator every time.
REMOVE_ACTION = "org.wildos.winmode.remove"
# What a removal keeps of the service's own state: choices about this PC
# rather than about the Windows that was on it.
KEPT_ON_REMOVE = ("file_sharing",)


def log(*parts):
    print(*parts, file=sys.stderr, flush=True)


def end_program_script(pid, image):
    """PowerShell that ends process `pid` if it is still `image` in a
    person's session; None for anything that cannot be named safely."""
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return None
    if pid <= 4 or not image or len(image) > 260 or any(ch < " " for ch in image):
        return None
    # PowerShell takes the curly single quotes as quotes too.
    quoted = image
    for q in ("'", "‘", "’", "‚", "‛"):
        quoted = quoted.replace(q, q + q)
    return (f"$p = Get-Process -Id {pid} -ErrorAction SilentlyContinue; "
            f"if ($p -and $p.SessionId -ne 0 -and $p.Path -ieq '{quoted}') "
            f"{{ Stop-Process -Id {pid} -Force; 'ended' }} else {{ 'not running' }}")


def random_password():
    # The old builder's alphabet, without look-alike characters, and a fixed
    # tail that meets Windows' complexity rule.
    alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789"
    return "".join(secrets.choice(alphabet) for _ in range(24)) + "a7Q"


def app_windows(windows):
    """The windows that count as a Windows app being open: visible and not
    a tool window. Owned windows (dialogs) count too, since a dialog left
    open is still work in progress."""
    return [w for w in windows if isinstance(w, dict) and w.get("visible", True) and not w.get("tool")]


def has_virtiofs(xml):
    """Whether a domain definition carries a virtiofs shared folder."""
    return bool(re.search(r"<filesystem\b[^>]*>(?:(?!</filesystem>).)*<driver\b[^>]*type=['\"]virtiofs['\"]", xml, re.S))


def drop_virtiofs(xml):
    """The definition without its virtiofs shared folders, and without the
    shared memory backing that only virtiofs needed (it kept Windows'
    memory from going back to Wild OS)."""
    xml = re.sub(r"\s*<filesystem\b(?:(?!</filesystem>).)*<driver\b[^>]*type=['\"]virtiofs['\"](?:(?!</filesystem>).)*</filesystem>",
                 "", xml, flags=re.S)
    return re.sub(r"\s*<memoryBacking>(?:(?!</memoryBacking>).)*<access mode=['\"]shared['\"]/>(?:(?!</memoryBacking>).)*</memoryBacking>",
                  "", xml, flags=re.S)


# Windows' processor share while it boots ahead of time (the default is 100).
BACKGROUND_CPU_WEIGHT = 20
# Windows' share of memory follows what it uses (trim_memory), in KiB. An
# idle Windows uses 1.2 to 1.5 GB; measured on Debian 13's libvirt, it barely
# pages at what it uses plus 512 MB, and every MB the balloon takes comes
# back to Wild OS. The old 1.5 GB headroom and 3 GB floor held about 1 GB
# more than that for nothing.
TRIM_HEADROOM_KIB = 512 * 1024
TRIM_FLOOR_KIB = 1536 * 1024
TRIM_STEP_KIB = 256 * 1024
TRIM_AFTER = 60
# How long a pause or save waits for the balloon to reach its last trim:
# the less Windows holds, the less a save writes and a restore reads.
TRIM_SETTLE = 15
# Guest scripts brought up to date in an existing Windows when wild-guest
# connects (guest_upkeep), in this order, each by the $version line inside it.
UPKEEP_SCRIPTS = ("opengl", "tune")
TUNE_DIR = r"C:\Program Files\Wild OS"


class Service:
    def __init__(self, bus):
        self.bus = bus
        self.cfg = config.load()
        self.domain = self.cfg["domain"]
        saved = config.read_json(config.state_file(), {}) or {}
        self.state = saved if isinstance(saved, dict) else {}
        self.state.setdefault("when_idle", self.cfg["when_idle"])
        self.memory = virsh.defined_memory(self.domain)
        self.last_wake = 0.0
        self.life = lifecycle.Lifecycle(self.cfg["idle_seconds"])
        self.windows = {}
        self.error = None
        self.busy_action = None
        # A save to disk under way, which winmoded's own stop waits for.
        self.saving = False
        # The live Windows still carries the old virtiofs share (save_ok).
        self.old_share = False
        # logind's delay lock (hold_shutdown) and whether the PC is on its
        # way down.
        self.inhibitor = None
        self.pc_shutting_down = False
        self.memory_tried = -MEMORY_RETRY
        self.update_checked = -UPDATE_CHECK_EVERY
        self.guest_update_tried = -3600.0
        self.idle_timer = None
        self.window_list_timer = None
        self.upkeep_running = False
        self.auth_cache = {}
        # Taken from the main loop (claim) and from wake's worker thread.
        self.bind_lock = threading.Lock()
        self.link = guestlink.GuestLink(self.guest_event, self.guest_state)
        self.usb = usb.UsbManager(self)
        self.export = dbusutil.Exported(bus, config.OBJECT_PATH, XML, config.HOST_INTERFACE, {
            "Status": (self.m_status, "s", False),
            "Wake": (self.m_wake, "s", True),
            "Prestart": (self.m_prestart, "s", True),
            "Idle": (self.m_idle, "s", True),
            "Power": (self.m_power, "s", True),
            "Documents": (self.m_documents, "s", True),
            "Launch": (self.m_launch, "s", True),
            "Windows": (self.m_windows, "s", True),
            "Window": (self.m_window, "s", True),
            "Apps": (self.m_apps, "s", True),
            "Uninstall": (self.m_uninstall, "s", True),
            "SetTheme": (self.m_set_theme, "s", True),
            "SetOption": (self.m_set_option, "s", True),
            "SetWarm": (self.m_set_warm, "s", True),
            "ConnectionChanged": (self.m_connection, None, True),
            "Credentials": (self.m_credentials, "s", True),
            "InstallGuestHelper": (self.m_install_guest, "s", True),
            "GuestRequest": (self.m_guest_request, "s", True),
            "EndProgram": (self.m_end_program, "s", True),
            "SetupStart": (self.m_setup_start, "s", True),
            "SetupControl": (self.m_setup_control, "s", True),
            "Firmware": (self.m_firmware, "s", True),
            "SetProductKey": (self.m_set_product_key, "s", True),
            "ActivationStart": (self.m_activation_start, "s", True),
            "ActivationCheck": (self.m_activation_check, "s", True),
            "ActivationEnd": (self.m_activation_end, "s", True),
            "Remove": (self.m_remove, "s", True),
            "UsbDevices": (self.usb.m_devices, "s", True),
            "SetUsbDevice": (self.usb.m_set, "s", True),
        }, authorize=self.authorize)
        self.power = hostinfo.Power(bus, self.host_changed)
        self.pressure = hostinfo.PressureWatch(self.host_changed)
        self.watch_libvirt()
        if virsh.state(self.domain) != "":
            ok, why = firewall.apply(self.cfg, self.state.get("file_sharing", False))
            if not ok:
                log("firewall:", why)
        self.setup = setup.Setup(self)
        self.activation = activation.Activation(self, random_password)
        self.refresh()
        self.setup_changed()
        self.setup.resume_after_restart()
        self.usb.start()
        GLib.timeout_add_seconds(30, self.slow_check)
        bus.signal_subscribe("org.freedesktop.login1", "org.freedesktop.login1.Manager", "PrepareForShutdown",
                             "/org/freedesktop/login1", None, Gio.DBusSignalFlags.NONE, self.preparing_shutdown)

    def preparing_shutdown(self, conn, sender, path, iface, signal, params):
        self.pc_shutting_down = bool(params.unpack()[0])
        if self.pc_shutting_down:
            def work():
                try:
                    self.shut_down_windows()
                finally:
                    GLib.idle_add(lambda: (self.hold_shutdown(False), False)[1])
            threading.Thread(target=work, name="shutdown", daemon=True).start()
        else:
            # Shutting down was called off: held again while Windows runs.
            self.refresh()

    def hold_shutdown(self, on):
        """logind's delay lock while Windows runs: the PC's shutdown waits
        for Windows to be saved or shut down (up to InhibitDelayMaxSec, 60 s
        with Wild OS's logind drop-in) instead of libvirt-guests switching
        it off part way through a save."""
        if on and self.inhibitor is None and not self.pc_shutting_down:
            try:
                reply, fds = self.bus.call_with_unix_fd_list_sync(
                    "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Inhibit",
                    GLib.Variant("(ssss)", ("shutdown", "Windows mode", "Keeping Windows apps' work", "delay")),
                    GLib.VariantType("(h)"), Gio.DBusCallFlags.NONE, 5000, None, None)
                self.inhibitor = fds.get(reply.unpack()[0])
            except GLib.Error as e:
                log("couldn't hold the PC's shutdown for Windows:", e.message)
        elif not on and self.inhibitor is not None:
            os.close(self.inhibitor)
            self.inhibitor = None

    def shut_down_windows(self):
        """The PC is shutting down, restarting or updating. With Windows apps
        open the person was asked as the session ended (board 9.9, the
        agent), unless the PC was shut down some other way, so the safe
        choice is taken here: Windows saved to disk as it is, and the next
        sign-in reopens it. With nothing open it is saved to disk too, so
        the first app after the PC starts again loads it back in seconds
        instead of waiting a minute or two for Windows to boot (measured on
        an XPS 13: 104 s). Windows still restarts for its updates, in the
        background (restart_windows). It shuts down cleanly only when a new
        memory limit or a fresh sign-in needs it. A save or shutdown the
        person chose is let finish first. A setup in progress is left to
        libvirt-guests, so setup carries on after the restart as it was."""
        if self.setup.thread and self.setup.thread.is_alive():
            return
        deadline = time.monotonic() + PC_SHUTDOWN_BUDGET
        while (self.busy_action or self.saving) and time.monotonic() < deadline:
            time.sleep(0.2)
        state = virsh.state(self.domain)
        if state not in ("running", "paused"):
            return
        if self.all_windows() and self.save_ok():
            log("the PC is shutting down with Windows apps open: saving Windows to disk so nothing is lost")
            try:
                self.do_save()
                self.state["kept"] = {"at": int(time.time())}
                self.save_state()
                return
            except virsh.VirshError as e:
                log("saving Windows as the PC shuts down:", e)
        elif self.save_ok() and not self.restart_wanted():
            log("the PC is shutting down: saving Windows to disk, so its first app afterwards is quick")
            try:
                self.do_save()
                return
            except virsh.VirshError as e:
                log("saving Windows as the PC shuts down:", e)
        log("the PC is shutting down: shutting Windows down")
        self.stop_windows(max(5, deadline - time.monotonic()))

    def stop_windows(self, wait=SHUTDOWN_WAIT):
        """Shuts Windows down cleanly, as its own Shut down would: through the
        guest agent, which closes apps without asking (Wild OS asked first),
        or the power button when the agent can't be reached. A saved
        Windows is loaded back first, so it shuts down properly rather than
        losing the save as a power cut would. Switched off if it hasn't
        finished in `wait` seconds; returns True then, so it can be said."""
        st = virsh.state(self.domain)
        if st in ("", "shut off"):
            return False
        if st == "saved":
            self.wake()
        elif st == "paused":
            virsh.resume(self.domain)
        deadline = time.monotonic() + wait
        try:
            # libvirt answers only once Windows has switched itself off, so
            # running out of time here means Windows is still shutting down.
            virsh.run("shutdown", self.domain, "--mode", "agent", timeout=max(5, int(wait)))
        except virsh.VirshError as e:
            if "took longer" not in str(e) and virsh.state(self.domain) == "running":
                log("shutting Windows down through its agent:", e)
                virsh.run("shutdown", self.domain, "--mode", "acpi", check=False, timeout=20)
        while time.monotonic() < deadline:
            if virsh.state(self.domain) in ("shut off", ""):
                return False
            time.sleep(0.5)
        log(f"Windows didn't shut down in {round(wait)} s: switching it off")
        virsh.run("destroy", self.domain, check=False, timeout=15)
        return True

    def save_state(self):
        try:
            config.write_json(config.state_file(), self.state, 0o600)
        except OSError as e:
            log("state:", e)

    def changed(self):
        self.export.emit("Changed")

    def notify(self):
        """From the setup worker's thread: setup's progress changed."""
        GLib.idle_add(self.setup_changed)

    def setup_changed(self):
        # Windows is never paused or saved while it is being set up.
        running = bool((self.state.get("setup") or {}).get("running"))
        self.life.hold(time.monotonic(), "setup", running)
        self.schedule_idle()
        self.changed()
        return False

    def new_guest_password(self):
        """The Windows account's first password, for the answer file. Nobody
        keeps it: the person's agent sets a new one through the guest agent
        the first time it connects (Credentials) and keeps that one."""
        return random_password()

    # Authorisation: root always; anyone else through polkit, which lets
    # the active local user in without a password.
    def authorize(self, sender, reply):
        cached = self.auth_cache.get(sender)
        if cached and cached[1] > time.monotonic():
            reply(cached[0])
            return

        def got_uid(conn, res):
            try:
                uid = conn.call_finish(res).unpack()[0]
            except GLib.Error:
                reply(False)
                return
            if uid == 0:
                self.auth_cache[sender] = (True, time.monotonic() + 60)
                reply(True)
                return
            subject = ("system-bus-name", {"name": GLib.Variant("s", sender)})
            self.bus.call("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
                          "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization",
                          GLib.Variant("((sa{sv})sa{ss}us)", (subject, config.POLKIT_ACTION, {}, 1, "")),
                          GLib.VariantType("((bba{ss}))"), Gio.DBusCallFlags.NONE, 60000, None, got_polkit)

        def got_polkit(conn, res):
            try:
                ok = bool(conn.call_finish(res).unpack()[0][0])
            except GLib.Error as e:
                log("polkit:", e.message)
                ok = False
            self.auth_cache[sender] = (ok, time.monotonic() + 60)
            reply(ok)

        self.bus.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixUser",
                      GLib.Variant("(s)", (sender,)), GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 5000, None, got_uid)

    def sender_uid(self, sender):
        try:
            return self.bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                      "GetConnectionUnixUser", GLib.Variant("(s)", (sender,)), GLib.VariantType("(u)"),
                                      Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]
        except GLib.Error:
            return None

    # One person's Windows mode. Windows Pro gives its account one remote
    # session and a new connection takes it over, and the shared drive holds
    # one person's folders, so the first person to use it owns it. Root (the
    # command line, setup) is let through.
    def claim(self, sender):
        """None if the caller may use Windows mode, else why not."""
        uid = self.sender_uid(sender)
        if uid is None:
            return "Windows mode couldn't tell who is asking."
        if uid == 0:
            return None
        owner = self.state.get("owner_uid")
        if owner is None:
            # Nothing to own until Windows mode is set up.
            if virsh.state(self.domain) == "":
                return None
            self.state["owner_uid"] = uid
            self.save_state()
            log(f"Windows mode now belongs to uid {uid}")
            return None
        if owner != uid:
            try:
                name = pwd.getpwuid(owner).pw_gecos.split(",")[0] or pwd.getpwuid(owner).pw_name
            except KeyError:
                name = "another account"
            return f"Windows mode on this PC is set up for {name}."
        return None

    def runtime_installed(self):
        """From setup's thread, once the virtual PC package is in: until then
        there was no virsh to watch."""
        GLib.idle_add(lambda: (self.events is None and self.watch_libvirt(), False)[1])

    # What libvirt and logind say.
    def watch_libvirt(self):
        """virsh event streams lifecycle events, so state changes arrive as
        they happen with nothing polling."""
        try:
            # No --all: virsh refuses it alongside --event, so the watcher used
            # to exit at once and only launches and wakes ever saw a change.
            self.events = subprocess.Popen(["virsh", "-q", "-c", virsh.URI, "event", "--loop", "--event", "lifecycle"],
                                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except OSError:
            self.events = None
            return
        os.set_blocking(self.events.stdout.fileno(), False)

        def readable(fd, cond):
            try:
                data = os.read(fd, 4096)
            except BlockingIOError:
                return True
            if not data:
                GLib.timeout_add_seconds(5, lambda: (self.watch_libvirt(), False)[1])
                return False
            self.refresh()
            return True

        GLib.io_add_watch(self.events.stdout.fileno(), GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP, readable)

    def host_changed(self):
        since = self.state.get("since") or {}
        paused_for = time.time() - since["at"] if since.get("state") == "paused" and since.get("at") else None
        action = self.life.reconsider_paused(self.state["when_idle"], self.power.on_battery, *hostinfo.meminfo(),
                                             hostinfo.under_pressure(), paused_for)
        # A wake or anything else holding Windows awake comes first.
        if action == lifecycle.SAVE and self.save_ok() and not self.busy_action and not self.life.busy_reasons:
            log("paused long enough, or the host needs the memory or is on battery: saving the paused guest to disk")
            self.run_idle(lifecycle.SAVE)

    def slow_check(self):
        """Every 30 s: the Automatic policy's inputs, the guest channel, and
        the window list, read afresh in case an update was lost (the guest
        drops what it cannot send while the channel reconnects)."""
        self.host_changed()
        st = virsh.state(self.domain)
        if st != self.life.vm_state and not self.busy_action:
            # A lifecycle event that never came.
            self.refresh()
        if self.restart_wanted() and not self.busy_action:
            if st == "shut off":
                # Off already: the next start takes the new limit.
                self.state["memory_pending"] = False
                self.state["restart_pending"] = False
                self.save_state()
                self.changed()
            elif st in ("paused", "saved") and time.monotonic() - self.memory_tried > MEMORY_RETRY:
                # A shutdown that timed out (Windows busy updating, say) is
                # tried again, as nothing else would bring it round.
                self.restart_for_memory()
        if st == "running":
            self.trim_memory()
            self.resume_activation_cleanup()
            if not self.link.connected:
                self.connect_guest()
            else:
                self.link.request({"type": "windows"}, self.guest_event)
        return True

    def trim_memory(self):
        """Windows keeps the memory it was given even when it isn't using
        it (free page reporting only works with Linux guests), which left a
        16 GB PC with 8 GB tied up and the desktop in swap. So while no
        Windows app is open, every 30 s the balloon is set to what Windows
        uses plus headroom, and Windows goes into its pause holding only
        that. Never while an app is open: Windows only grows into what the
        balloon hands back, a step every 30 s, so whatever the limit, a
        trimmed Windows worked like a 4 GB PC. The first app window and
        every launch (wake) give it everything back."""
        if self.busy_action or self.life.busy() or time.monotonic() - self.last_wake < TRIM_AFTER:
            return
        self.set_trim()

    def set_trim(self):
        """Sets the balloon to what Windows uses plus headroom and returns
        that target in KiB, or None when Windows' figures are not in."""
        try:
            stats = virsh.memory_stats(self.domain)
        except virsh.VirshError:
            return None
        actual, usable = stats.get("actual"), stats.get("usable")
        ceiling = virsh.defined_memory(self.domain, inactive=False) // 1024
        if not actual or usable is None or not ceiling:
            return None
        used = actual - usable
        target = max(TRIM_FLOOR_KIB, min(ceiling, used + TRIM_HEADROOM_KIB))
        # Small moves cost more than they save; growing is never held back.
        if target < actual - TRIM_STEP_KIB or target > actual:
            try:
                virsh.set_memory(self.domain, target)
            except virsh.VirshError as e:
                log("memory:", e)
                return None
        return target

    def settle_trim(self):
        """Before a pause or a save: one last trim, then a few seconds for
        the balloon to get there. Gives up at once if an app is opened."""
        target = self.set_trim()
        if target is None:
            return
        deadline = time.monotonic() + TRIM_SETTLE
        while time.monotonic() < deadline and not self.life.busy():
            try:
                if virsh.memory_stats(self.domain).get("actual", 0) <= target + TRIM_STEP_KIB:
                    return
            except virsh.VirshError:
                return
            time.sleep(1)

    def match_cpus(self):
        """Windows gets every processor thread, up to 8, when setup defines it
        (setup.define). Turning hyper-threading on or off in Settings changes
        how many the host has, so a cold start brings the guest in line. A
        saved Windows keeps its count until it next starts from scratch."""
        want = max(2, min(8, os.cpu_count() or 4))
        try:
            xml = virsh.dumpxml(self.domain, inactive=True)
        except virsh.VirshError as e:
            log("processor count:", e)
            return
        m = re.search(r"<vcpu\b[^>]*>(\d+)</vcpu>", xml)
        if not m or int(m.group(1)) == want:
            return
        xml = re.sub(r"(<vcpu\b[^>]*>)\d+(</vcpu>)", rf"\g<1>{want}\g<2>", xml, count=1)
        xml = re.sub(r"(<topology\b[^>]*\bcores=')\d+(')", rf"\g<1>{want}\g<2>", xml, count=1)
        path = os.path.join(config.STATE_DIR, "domain.xml")
        with open(path, "w") as f:
            f.write(xml)
        virsh.run("define", path)
        log(f"Windows now gets {want} processor threads (was {m.group(1)})")

    def full_memory(self):
        """Everything Windows started with, back from the balloon. Not the
        definition's limit: a new one waiting for Windows' next start is
        more than libvirt lets the running Windows have, and asking for it
        gave nothing back at all."""
        limit = virsh.defined_memory(self.domain, inactive=False)
        if limit:
            try:
                virsh.set_memory(self.domain, limit // 1024)
            except virsh.VirshError as e:
                log("memory:", e)

    # State.
    def refresh(self):
        st = virsh.state(self.domain)
        prev = self.life.vm_state
        now = time.monotonic()
        self.life.update(now, vm_state=st)
        if st != prev:
            self.note_since(prev, st)
            if st in ("running", "paused") and prev not in ("running", "paused"):
                try:
                    self.old_share = has_virtiofs(virsh.dumpxml(self.domain))
                except virsh.VirshError:
                    self.old_share = False
        self.hold_shutdown(st in ("running", "paused"))
        if st == "running":
            if prev != "running":
                self.await_window_list(True)
            self.connect_guest()
        elif prev == "running" and st != "running":
            self.await_window_list(False)
            self.link.close()
            if st != "paused":
                self.set_windows({})
        self.schedule_idle()
        if st != prev:
            self.usb.vm_changed(st)
            self.changed()

    def note_since(self, prev, st):
        """When Windows went into the state it is in, for the panel's "Paused
        for 12 minutes" and "Shut down 2 hours ago". A service that has only
        just started cannot know, unless the state is still the one it
        recorded before (Windows shut down as the PC did, say)."""
        since = self.state.get("since") or {}
        if prev == "":
            if since.get("state") == st:
                return
            self.state["since"] = {"state": st, "at": None}
        else:
            self.state["since"] = {"state": st, "at": int(time.time())}
        # Work kept for the next sign-in is gone once Windows is anything
        # but saved: reopened, or shut down by hand.
        if st != "saved" and prev == "saved":
            self.state.pop("kept", None)
        self.save_state()

    def note_timing(self, result):
        """How long the last start from off and restore from disk took, to
        Remote Desktop answering: what the panel says the next one takes."""
        came_from = result.get("from") if isinstance(result, dict) else None
        key = {"stopped": "start", "shut off": "start", "saved": "restore"}.get(came_from)
        if key and isinstance(result.get("seconds"), (int, float)):
            self.state["timings"] = dict(self.state.get("timings") or {}, **{key: round(result["seconds"], 1)})

    def await_window_list(self, waiting):
        if self.window_list_timer:
            GLib.source_remove(self.window_list_timer)
            self.window_list_timer = None
        if waiting:
            def gave_up():
                self.window_list_timer = None
                log(f"wild-guest sent no window list in {WINDOW_LIST_WAIT} s; going by what is known")
                self.await_window_list(False)
                return False
            self.window_list_timer = GLib.timeout_add_seconds(WINDOW_LIST_WAIT, gave_up)
        self.life.hold(time.monotonic(), WINDOW_LIST_HOLD, waiting)
        self.schedule_idle()

    def connect_guest(self):
        if self.link.sock is not None:
            if not self.link.connected:
                self.link.request({"type": "hello"}, self.link._hello, timeout=5)
            return
        path = virsh.channel_path(self.domain, "org.wildos.guest.0")
        if path:
            self.link.connect(path)

    def guest_state(self, connected):
        log("wild-guest", "connected" if connected else "disconnected",
            (self.link.hello or {}).get("version", "") if connected else "")
        if connected:
            edition = (self.link.hello or {}).get("edition")
            if isinstance(edition, str) and edition != self.state.get("edition"):
                self.state["edition"] = edition[:40]
                self.save_state()
            theme = self.state.get("theme")
            if theme:
                self.link.send(dict(theme, type="theme"))
            warm = self.state.get("warm")
            if warm:
                self.link.send(dict(warm, type="warm"))
            self.link.request({"type": "windows"}, self.guest_event)
            self.resume_activation_cleanup()
            self.guest_upkeep()
        self.changed()

    def guest_upkeep(self):
        """Guest scripts newer than the ones this Windows has had run in it
        when wild-guest connects, in the background, one after another.

        tune.ps1 keeps Windows lean. Only a Windows that setup gave it to
        (state["tune"] set by Setup.finish) gets newer versions this way; a
        Windows set up before it existed is left as it is, because the
        clean-up removes apps and features its owner may already be using.

        A Windows set up before guest/opengl.ps1 existed (or whose setup
        could not download Mesa) gets it the first time wild-guest connects,
        in the background. Programs only see it from Windows' next sign-in:
        GALLIUM_DRIVER reaches new sign-ins only, and apps start from the
        session's own environment. Without it Mesa picks its Direct3D 12
        driver, which crashes MagicaVoxel just like OpenGL 1.1 did. The PC's
        next restart was meant to bring that sign-in, but this runs as
        Windows starts, so the download often finished after the session
        had signed in, and every app stayed on the old environment until
        the restart after that. Windows now shuts down cleanly the next
        time no Windows app is open, as for a new memory limit."""
        if self.state.get("setting_up") or self.upkeep_running:
            return
        for name in UPKEEP_SCRIPTS:
            if name == "tune" and not self.state.get("tune"):
                continue
            try:
                with open(os.path.join(config.DATA, "guest", name + ".ps1"), encoding="utf-8") as f:
                    script = f.read()
            except OSError as e:
                log(f"{name}:", e)
                continue
            m = re.search(r"^\$version = '([^']+)'", script, re.M)
            if m and self.state.get(name) != m.group(1):
                self.run_upkeep(name, script, m.group(1))
                return

    def run_upkeep(self, name, script, version):
        """One upkeep script, in the background. tune.ps1 has to stay inside
        Windows (its start-up task runs it at every boot), so it is copied
        in and run from there; opengl.ps1 runs once and is gone."""
        self.upkeep_running = True
        self.life.hold(time.monotonic(), "upkeep", True)

        def work():
            if name == "tune":
                qga.powershell(self.domain, f"New-Item -ItemType Directory -Force '{TUNE_DIR}' | Out-Null", 60)
                qga.write_file(self.domain, TUNE_DIR + r"\tune.ps1", script.encode("utf-8"))
                # Removing Windows capabilities alone took ten minutes once.
                code, out, err = qga.powershell(self.domain, f"& '{TUNE_DIR}\\tune.ps1' -Install", 1800)
            else:
                code, out, err = qga.powershell(self.domain, script, 600)
            return code, out.strip(), err.strip()

        def finished(result):
            self.upkeep_running = False
            self.life.hold(time.monotonic(), "upkeep", False)
            self.schedule_idle()
            if isinstance(result, Exception) or result[0] != 0:
                # Tried again when wild-guest next connects.
                log(f"{name}: not done:", result if isinstance(result, Exception) else (result[1] + result[2])[-300:])
                return
            # The script's own last line; PowerShell's warnings go to the error
            # stream and must not be mistaken for it.
            last = result[1].splitlines()[-1] if result[1] else "done"
            log(f"{name}:", last, version)
            self.state[name] = version
            if name == "opengl" and last.strip() == "installed":
                self.state["restart_pending"] = True
                if virsh.state(self.domain) in ("paused", "saved") and not self.busy_action:
                    self.restart_for_memory()
            self.save_state()
            self.guest_upkeep()

        dbusutil.in_thread(work, done=finished)

    def guest_event(self, msg):
        t = msg.get("type")
        if t == "windows":
            session = msg.get("session")
            ws = msg.get("windows")
            if isinstance(ws, list):
                windows = dict(self.windows)
                if session is None:
                    windows = {}
                else:
                    windows[session] = ws
                self.set_windows(windows)
                if WINDOW_LIST_HOLD in self.life.busy_reasons:
                    self.await_window_list(False)
        elif t == "session" and msg.get("state") == "down":
            windows = dict(self.windows)
            windows.pop(msg.get("session"), None)
            self.set_windows(windows)

    def all_windows(self):
        return [w for ws in self.windows.values() for w in app_windows(ws)]

    def set_windows(self, windows):
        before = self.all_windows()
        self.windows = windows
        after = self.all_windows()
        self.life.update(time.monotonic(), windows=len(after))
        if after and not before:
            # An app opened from inside Windows (the Windows desktop, or a
            # program starting another) never went through wake.
            self.full_memory()
        self.schedule_idle()
        if before != after:
            self.export.emit("WindowsChanged", "(s)", json.dumps(after))
            self.changed()

    def schedule_idle(self):
        if self.idle_timer:
            GLib.source_remove(self.idle_timer)
            self.idle_timer = None
        d = self.life.deadline()
        if d is None:
            return
        wait = max(0, d - time.monotonic())
        self.idle_timer = GLib.timeout_add(int(wait * 1000) + 50, self.idle_fired)

    def restart_wanted(self):
        """A new memory limit, or Windows' sign-in needing to start afresh
        (opengl), both waiting for a clean shutdown while no app is open."""
        return bool(self.state.get("memory_pending") or self.state.get("restart_pending"))

    def restart_for_memory(self):
        """Shuts Windows down cleanly so the next app starts it with the new
        memory limit, or a fresh sign-in. Only when no Windows app is open."""
        self.busy_action = "shutdown"
        self.memory_tried = time.monotonic()

        def work():
            st = virsh.state(self.domain)
            if st == "paused":
                virsh.run("resume", self.domain)
            elif st == "saved":
                # Loaded back first, so Windows shuts down properly rather
                # than losing the save as a power cut would.
                self.wake()
            elif st == "shut off":
                return True
            virsh.run("shutdown", self.domain)
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                if virsh.state(self.domain) == "shut off":
                    return True
                time.sleep(2)
            return False

        def finished(result):
            self.busy_action = None
            if result is True:
                self.state["memory_pending"] = False
                self.state["restart_pending"] = False
                log("Windows shut down for its new memory limit or a fresh sign-in")
            else:
                log("Windows didn't shut down for its new memory limit or a fresh sign-in:", result)
            self.save_state()
            self.refresh()

        dbusutil.in_thread(work, done=finished)

    def idle_fired(self):
        self.idle_timer = None
        if self.life.due(time.monotonic()) and not self.busy_action and self.restart_wanted():
            log(f"no Windows app open for {self.cfg['idle_seconds']} s: shutting down for the new memory limit or a fresh sign-in")
            self.restart_for_memory()
            return False
        if (self.life.due(time.monotonic()) and not self.busy_action and not self.upkeep_running
                and time.monotonic() - self.guest_update_tried >= 3600
                and virsh.state(self.domain) == "running" and self.guest_outdated()):
            log(f"no Windows app open for {self.cfg['idle_seconds']} s: updating wild-guest inside Windows first")
            self.update_guest()
            return False
        if self.life.due(time.monotonic()) and not self.busy_action:
            action = lifecycle.idle_action(self.state["when_idle"], self.power.on_battery, *hostinfo.meminfo(),
                                           hostinfo.under_pressure())
            log(f"no Windows app open for {self.cfg['idle_seconds']} s: {action}")
            self.run_idle(action)
        return False

    def run_idle(self, action, done=None):
        # A Windows still carrying the old virtiofs share would not load
        # back from disk (that is what failed on real hardware), so until
        # its next cold start takes the share out it is paused instead.
        if action == lifecycle.SAVE and not self.save_ok():
            action = lifecycle.PAUSE
            if virsh.state(self.domain) == "paused":
                return
        self.busy_action = action
        # Said at once: the agent ends its connection as a save starts, or
        # the connection would reconnect to the restored Windows by itself
        # and Windows would start its first app again.
        self.changed()

        def work():
            t0 = time.monotonic()
            if (virsh.state(self.domain) == "running" and not self.power.on_battery
                    and time.monotonic() - self.update_checked >= UPDATE_CHECK_EVERY):
                self.update_checked = time.monotonic()
                why = self.windows_needs_restart()
                if why:
                    return ("restart", why)
            if virsh.state(self.domain) == "running":
                self.settle_trim()
            if action == lifecycle.PAUSE:
                virsh.suspend(self.domain)
            else:
                self.do_save()
            return time.monotonic() - t0

        def finished(result):
            self.busy_action = None
            if isinstance(result, tuple):
                self.restart_windows(result[1])
                if done:
                    done(result)
                return
            if isinstance(result, Exception):
                self.error = str(result)
                log(f"{action} failed:", result)
            else:
                self.error = None
                self.state["last"] = {"action": action, "seconds": round(result, 2), "at": int(time.time())}
                self.save_state()
            self.refresh()
            self.changed()
            if done:
                done(result)

        dbusutil.in_thread(work, done=finished)

    def windows_needs_restart(self):
        """Why Windows should restart now, or None: an update waiting for a
        restart, or UPDATE_RESTART_DAYS since the last one. Windows is
        saved rather than shut down as the PC shuts down, so without this
        it would never restart to finish its updates."""
        try:
            code, out, _ = qga.powershell(self.domain, NEEDS_RESTART_SCRIPT, 30)
            pending, days = out.strip().splitlines()[-1].split()
            days = int(days)
        except (virsh.VirshError, ValueError, IndexError, TimeoutError) as e:
            log("couldn't ask Windows whether it needs a restart:", e)
            return None
        if pending == "True":
            return "an update is waiting for a restart"
        if days >= UPDATE_RESTART_DAYS:
            return f"it last restarted {days} days ago"
        return None

    def restart_windows(self, why):
        """Restarts Windows while no app is open and the PC is on mains:
        a clean shutdown, which finishes installing any update, then a
        start in the background like the one at sign-in, so the next app
        still finds it ready. An app opened meanwhile waits for it, as it
        would for a start from off."""
        log(f"no Windows app open: restarting Windows in the background, as {why}")
        self.busy_action = "restart"
        # Not idle while it boots and signs in (see m_prestart).
        self.life.hold(time.monotonic(), PRESTART_HOLD, True)
        self.changed()

        def work():
            virsh.run("shutdown", self.domain, check=False)
            deadline = time.monotonic() + UPDATE_RESTART_WAIT
            while virsh.state(self.domain) != "shut off":
                if time.monotonic() > deadline:
                    raise RuntimeError("Windows didn't shut down to restart")
                time.sleep(2)
            try:
                return self.wake(background=True)
            finally:
                self.set_cpu_weight(100)

        def finished(result):
            self.busy_action = None
            if isinstance(result, Exception):
                log("restarting Windows:", result)
            else:
                log(f"Windows restarted in {result.get('seconds')} s")

            def release():
                self.life.hold(time.monotonic(), PRESTART_HOLD, False)
                self.schedule_idle()
                return False
            GLib.timeout_add_seconds(PRESTART_SIGN_IN_SECONDS, release)
            self.refresh()
            self.changed()

        dbusutil.in_thread(work, done=finished)

    def save_ok(self):
        """Whether Windows can go to disk now: set up, and not carrying the
        virtiofs share of Windows modes set up before 2026-11-08, which
        drop_old_share takes out at the next cold start."""
        return virsh.state(self.domain) != "" and not self.old_share

    def do_save(self):
        """virsh managedsave. Windows is paused first, so the save writes its
        memory once instead of chasing pages it keeps changing, and nothing
        it does after the decision (an app saving, an update installing)
        is half done in the image. --running brings it back running.

        A shutdown that stops winmoded meanwhile waits for this (main())."""
        self.saving = True
        try:
            if virsh.state(self.domain) == "running":
                virsh.suspend(self.domain)
            virsh.run("managedsave", self.domain, "--running", timeout=300)
        finally:
            self.saving = False

    def drop_old_share(self):
        """Takes the virtiofs shared folder out of the definition of a Windows
        set up before the folders moved to the RemoteApp connection. With it
        attached a save to disk would not load back. Only from switched
        off: the running Windows keeps the definition it started with."""
        try:
            xml = virsh.dumpxml(self.domain, inactive=True)
        except virsh.VirshError as e:
            log("old shared folder:", e)
            return
        if not has_virtiofs(xml):
            return
        path = os.path.join(config.STATE_DIR, "domain.xml")
        with open(path, "w") as f:
            f.write(drop_virtiofs(xml))
        virsh.run("define", path)
        self.old_share = False
        log("took the old virtiofs shared folder out of Windows; saving to disk works from now on")

    def reopen_guest_channel(self):
        """After a restore the guest channel has a new host end, which
        wild-guest only notices when told (its service control code 128)."""
        try:
            qga.run(self.domain, r"C:\Windows\System32\sc.exe", ["control", "WildGuest", "128"], 10)
        except (virsh.VirshError, ValueError, KeyError, TimeoutError) as e:
            log("telling wild-guest to reopen its channel:", e)
        GLib.idle_add(lambda: (self.link.close(), self.connect_guest(), False)[2])

    def wait_rdp(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                with socket.create_connection((self.cfg["guest_address"], self.cfg["rdp_port"]), timeout=1):
                    return True
            except OSError:
                time.sleep(0.2)
        return False

    def set_cpu_weight(self, weight):
        """Windows' share of the processor when it competes with the desktop
        (cgroup weight; 100 is everyone's default)."""
        virsh.run("schedinfo", self.domain, "--live", f"cpu_shares={weight}", check=False, timeout=10)

    def wake(self, background=False):
        """Gets the guest running and ready for a RemoteApp connection:
        resume, restore or boot, then the clock, the guest agent and
        Remote Desktop (brief 5.6, Resume). In the background (started ahead
        of time) it boots with a small share of the processor, so the
        desktop that has just started is not slowed by it."""
        t0 = time.monotonic()
        self.last_wake = t0
        st = virsh.state(self.domain)
        came_from = {"running": "running", "paused": "paused", "saved": "saved", "shut off": "stopped"}.get(st, st)
        if st == "":
            raise RuntimeError("Windows mode isn't set up on this PC")
        if st == "paused":
            virsh.resume(self.domain)
        elif st in ("saved", "shut off"):
            firewall.apply(self.cfg, self.state.get("file_sharing", False))
            if st == "shut off":
                self.match_cpus()
                self.drop_old_share()
            try:
                virsh.start(self.domain)
                if background:
                    self.set_cpu_weight(BACKGROUND_CPU_WEIGHT)
            except virsh.VirshError as e:
                if st != "saved":
                    raise
                # A save that will not load back must not keep Windows from
                # starting at all: it starts afresh, as after a power cut,
                # and only what was open in it is lost.
                log("the saved Windows would not load; starting it afresh:", e)
                virsh.run("managedsave-remove", self.domain, check=False)
                virsh.start(self.domain)
                st, came_from = "shut off", "stopped"
                self.state["restore_failed_at"] = int(time.time())
                self.save_state()
        elif st != "running":
            raise RuntimeError(f"Windows is {st}")
        # Where a wake's time goes, in seconds from its start, for
        # launch-stats.
        stages = {"started": round(time.monotonic() - t0, 2)}
        # An app is about to start: all the memory first, which it keeps
        # until no app is open (trim_memory). A restored Windows comes back
        # with the balloon it was saved with.
        if st in ("running", "paused", "saved"):
            self.full_memory()
        resumed = time.monotonic() - t0
        if st != "running":
            if not qga.wait(self.domain, 30 if st != "shut off" else 240):
                raise RuntimeError("Windows didn't respond after starting")
            stages["guest_agent"] = round(time.monotonic() - t0, 2)
            try:
                qga.set_time(self.domain)
            except (virsh.VirshError, ValueError, KeyError) as e:
                log("guest-set-time:", e)
            if st == "saved":
                self.reopen_guest_channel()
        if not self.wait_rdp(30 if st != "shut off" else 240):
            raise RuntimeError("Windows isn't accepting the connection")
        stages["remote_desktop"] = round(time.monotonic() - t0, 2)
        return {"ok": True, "from": came_from, "seconds": round(time.monotonic() - t0, 2), "resume_seconds": round(resumed, 2),
                "stages": stages}

    def qemu_usage(self):
        """QEMU's CPU time and memory, for Task Manager: a root process that
        the person's own tools may not be allowed to see in /proc."""
        try:
            with open(f"/run/libvirt/qemu/{self.domain}.pid") as f:
                pid = int(f.read().strip())
            with open(f"/proc/{pid}/stat") as f:
                fields = f.read().rsplit(")", 1)[1].split()
            ticks = os.sysconf("SC_CLK_TCK")
            cpu = (int(fields[11]) + int(fields[12])) / ticks
            rss = 0
            with open(f"/proc/{pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        rss = int(line.split()[1]) * 1024
            # QEMU names each virtual processor's thread "CPU n/KVM", so
            # their count is the processors Windows has now, which a saved
            # Windows can hold at an older number than match_cpus sets.
            vcpus = 0
            for task in os.listdir(f"/proc/{pid}/task"):
                try:
                    with open(f"/proc/{pid}/task/{task}/comm") as f:
                        vcpus += f.read().startswith("CPU ")
                except OSError:
                    pass
            return {"pid": pid, "cpu_seconds": round(cpu, 2), "rss_bytes": rss, "vcpus": vcpus}
        except (OSError, ValueError, IndexError):
            return None

    # D-Bus methods.
    def status(self):
        # libvirt's events keep this current; asking virsh on every call
        # would start a process each time Task Manager refreshes.
        st = self.life.vm_state or virsh.state(self.domain)
        d = self.life.deadline()
        return {
            "state": lifecycle.state_from_vm(st, self.state.get("setting_up", False), self.error if st in ("", "crashed") else None),
            "vm": st,
            "windows": self.all_windows(),
            "connection": self.life.connection,
            "busy": sorted(self.life.busy_reasons),
            "idle_in": None if d is None else max(0, round(d - time.monotonic(), 1)),
            "when_idle": self.state["when_idle"],
            "on_battery": self.power.on_battery,
            "guest": {"connected": self.link.connected, "version": (self.link.hello or {}).get("version"),
                      "sessions": (self.link.hello or {}).get("sessions")},
            "edition": self.state.get("edition"),
            "last": self.state.get("last"),
            # {state, at}: since when Windows has been as it is (note_since).
            "since": self.state.get("since"),
            # {start, restore}: seconds the last of each took (note_timing).
            "timings": self.state.get("timings") or {},
            # Saved as Wild OS shut down with apps open, for the next sign-in
            # to reopen.
            "kept": bool(self.state.get("kept")) and st == "saved",
            "action": self.busy_action,
            "save_to_disk": self.save_ok(),
            "memory_pending": bool(self.state.get("memory_pending")),
            "error": self.error,
            "computer_name": self.cfg["computer_name"] or self.state.get("computer_name", ""),
            "account": self.cfg["account"],
            "guest_address": self.cfg["guest_address"],
            "rdp_port": self.cfg["rdp_port"],
            "drive": self.cfg["drive"],
            "memory_bytes": self.memory,
            "disk_bytes": paths.used_bytes(setup.DISK),
            "idle_seconds": self.cfg["idle_seconds"],
            "qemu": self.qemu_usage() if st in ("running", "paused") else None,
            "share_root": self.cfg["share_root"],
            "folders": self.cfg["folders"],
            "setup": self.setup.status(),
            "activation": self.activation.status(),
        }

    def m_status(self, sender):
        return json.dumps(self.status())

    def m_wake(self, sender, defer):
        if self.state.get("setting_up"):
            # Setup owns the virtual PC until it finishes; a Windows app
            # opened meanwhile would otherwise get "couldn't start".
            return json.dumps({"ok": False, "error": "setting up",
                               "message": "Windows mode is still being set up. Windows apps open once it's finished."})
        why = self.claim(sender)
        if why:
            return json.dumps({"ok": False, "error": why, "message": why})
        now = time.monotonic()
        self.life.hold(now, LAUNCH_HOLD, True)
        self.schedule_idle()

        def finished(result):
            # The agent opens its connection next; the hold stays a little
            # longer so the guest is not paused in between.
            GLib.timeout_add_seconds(20, lambda: (self.release_launch(), False)[1])
            self.refresh()
            if isinstance(result, Exception):
                defer.done(json.dumps({"ok": False, "error": str(result)}))
            else:
                self.state["last"] = {"action": "wake", "seconds": result["seconds"], "from": result["from"], "at": int(time.time())}
                self.note_timing(result)
                self.save_state()
                defer.done(json.dumps(result))

        def work():
            # A pause or save already under way finishes first.
            while self.busy_action:
                time.sleep(0.1)
            # Someone is waiting now: a boot started ahead of time gets the
            # whole processor again.
            if virsh.state(self.domain) == "running":
                self.set_cpu_weight(100)
            return self.wake()

        dbusutil.in_thread(work, done=finished)
        return defer

    def prestart_blocker(self):
        """Why Windows shouldn't be started ahead of time now, or None. Only
        on mains, with room for it to stay paused in memory afterwards
        (where the Automatic idle policy would otherwise save it straight
        to disk), and only from switched off or saved."""
        st = virsh.state(self.domain)
        if st not in ("shut off", "saved"):
            return f"it is {st or 'not set up'}"
        if self.state.get("setting_up"):
            return "it is being set up"
        if self.state["when_idle"] == "save":
            return "it is set to go to disk when idle"
        if self.power.on_battery:
            return "the PC is on battery"
        if hostinfo.under_pressure():
            return "memory is under pressure"
        available, total = hostinfo.meminfo()
        if total and (available - self.memory) / total < lifecycle.MEMORY_FREE_SHARE:
            return "there isn't enough free memory"
        if self.busy_action:
            return "something else is under way"
        return None

    def m_prestart(self, sender, defer):
        """Called by the session agent once sign-in has settled, so the first
        Windows app opens from memory instead of a cold start. The guest
        then idles into the paused state like any other."""
        why = self.claim(sender)
        if why:
            return json.dumps({"ok": False, "error": why})
        blocker = self.prestart_blocker()
        if blocker:
            log("not starting Windows ahead of time:", blocker)
            return json.dumps({"ok": False, "skipped": blocker})
        log("starting Windows ahead of time")
        # Not idle while it boots, nor while the agent signs in to it after:
        # paused halfway through signing in, Windows did not answer when it
        # was woken next.
        self.life.hold(time.monotonic(), PRESTART_HOLD, True)

        def release():
            self.life.hold(time.monotonic(), PRESTART_HOLD, False)
            self.schedule_idle()
            return False

        def finished(result):
            GLib.timeout_add_seconds(PRESTART_SIGN_IN_SECONDS, release)
            self.refresh()
            self.schedule_idle()
            if isinstance(result, Exception):
                defer.done(json.dumps({"ok": False, "error": str(result)}))
            else:
                defer.done(json.dumps(result))

        def work():
            try:
                return self.wake(background=True)
            finally:
                self.set_cpu_weight(100)

        dbusutil.in_thread(work, done=finished)
        return defer

    def release_launch(self):
        self.life.hold(time.monotonic(), LAUNCH_HOLD, False)
        self.schedule_idle()

    def m_idle(self, sender, mode, defer):
        """Pause or save now: Pause now and Save to disk in the taskbar's
        Windows mode panel, and the command line."""
        if mode not in (lifecycle.PAUSE, lifecycle.SAVE):
            return json.dumps({"ok": False, "error": "mode is pause or save"})
        st = virsh.state(self.domain)
        if self.busy_action:
            return json.dumps({"ok": False, "error": "busy", "message": "Windows mode is busy. Try again in a moment."})
        if mode == lifecycle.SAVE and not self.save_ok():
            return json.dumps({"ok": False, "error": "old shared folder",
                               "message": "Windows mode can save to disk once Windows has restarted. Restart it from here first."})
        # A paused Windows can still go to disk, which frees its memory.
        if st != "running" and not (st == "paused" and mode == lifecycle.SAVE):
            return json.dumps({"ok": False, "error": "not running"})
        self.run_idle(mode, done=lambda r: defer.done(json.dumps(
            {"ok": False, "error": str(r)} if isinstance(r, Exception) else {"ok": True, "seconds": round(r, 2)})))
        return defer

    def m_power(self, sender, action, defer):
        """The panel's Resume, Start, Restart and Shut down, and keep: saved
        to disk because Wild OS is shutting down or signing out with
        Windows apps open, which the next sign-in reopens. Answers
        {ok, from, seconds, forced (switched off after SHUTDOWN_WAIT)} or
        {ok: false, error, message}. The agent asks about unsaved work
        before Restart and Shut down; this does what it is told."""
        if action in (lifecycle.PAUSE, lifecycle.SAVE):
            return self.m_idle(sender, action, defer)
        if action not in POWER_ACTIONS:
            return json.dumps({"ok": False, "error": "unknown action"})
        if self.state.get("setting_up"):
            return json.dumps({"ok": False, "error": "setting up", "message": "Windows mode is still being set up."})
        why = self.claim(sender)
        if why:
            return json.dumps({"ok": False, "error": why, "message": why})
        if action == "keep" and not self.save_ok() and virsh.state(self.domain) in ("running", "paused"):
            return json.dumps({"ok": False, "error": "old shared folder"})
        self.life.hold(time.monotonic(), POWER_HOLD, True)
        self.schedule_idle()

        def work():
            # A pause or save already under way finishes first.
            while self.busy_action:
                time.sleep(0.1)
            self.busy_action = action
            GLib.idle_add(lambda: (self.changed(), False)[1])
            t0 = time.monotonic()
            came_from = virsh.state(self.domain)
            out = {"ok": True, "from": came_from}
            if action in ("resume", "start"):
                out.update(self.wake())
            elif action == "keep":
                if came_from in ("running", "paused"):
                    self.do_save()
                self.state["kept"] = {"at": int(time.time())}
                self.save_state()
            else:
                out["forced"] = self.stop_windows()
                if action == "restart":
                    # From off it is a cold start, so a new memory limit or
                    # processor count comes with it.
                    self.state["memory_pending"] = False
                    self.state["restart_pending"] = False
                    self.wake()
            out["seconds"] = round(time.monotonic() - t0, 2)
            return out

        def finished(result):
            self.busy_action = None
            self.life.hold(time.monotonic(), POWER_HOLD, False)
            if action in ("start", "restart") and not isinstance(result, Exception):
                # Signed in to by the agent next, as after a start ahead of
                # time; paused halfway through signing in, Windows did not
                # answer when it was woken.
                self.life.hold(time.monotonic(), PRESTART_HOLD, True)
                GLib.timeout_add_seconds(PRESTART_SIGN_IN_SECONDS, lambda: (
                    self.life.hold(time.monotonic(), PRESTART_HOLD, False), self.schedule_idle(), False)[2])
            if isinstance(result, Exception):
                log(f"{action} failed:", result)
                reply = {"ok": False, "error": str(result)}
            else:
                self.state["last"] = {"action": action, "seconds": result["seconds"], "at": int(time.time()),
                                      "forced": bool(result.get("forced"))}
                if result.get("forced"):
                    log(f"{action}: Windows was switched off after it didn't shut down in {SHUTDOWN_WAIT} s")
                self.save_state()
                if action in ("resume", "start"):
                    self.note_timing(result)
                reply = result
            self.refresh()
            self.schedule_idle()
            self.changed()
            defer.done(json.dumps(reply))

        dbusutil.in_thread(work, done=finished)
        return defer

    def m_documents(self, sender, defer):
        """What is open in Windows, for the question before it shuts down:
        wild-guest's Office documents (with Office's own Saved state) and
        window list. A wild-guest from before "documents" answers with an
        error, and the window list alone is used."""
        if not self.link.connected or virsh.state(self.domain) != "running":
            return json.dumps({"office": None, "windows": self.all_windows()})

        def got(reply):
            if reply.get("type") == "documents":
                defer.done(json.dumps({"office": reply.get("office"), "windows": reply.get("windows") or self.all_windows()}))
            else:
                defer.done(json.dumps({"office": None, "windows": self.all_windows()}))
        self.link.request({"type": "documents"}, got, timeout=12)
        return defer

    def guest_request(self, msg, defer, timeout=15):
        if not self.link.connected:
            self.connect_guest()
            return json.dumps({"type": "error", "error": "not-connected"})
        self.link.request(msg, lambda reply: defer.done(json.dumps(reply)), timeout=timeout)
        return defer

    def m_launch(self, sender, path, args, cwd, defer):
        msg = {"type": "launch", "path": path, "args": list(args)}
        if cwd:
            msg["cwd"] = cwd
        return self.guest_request(msg, defer)

    def m_windows(self, sender):
        return json.dumps(self.all_windows())

    def m_window(self, sender, hwnd, defer):
        return self.guest_request({"type": "window", "hwnd": hwnd}, defer, 5)

    def m_apps(self, sender, icons, defer):
        return self.guest_request({"type": "apps", "icons": bool(icons)}, defer, 120)

    def m_uninstall(self, sender, path, defer):
        """Removes a Store app in Windows, or finds another program's own
        uninstaller for the agent to open (wild-guest's Uninstall.cs)."""
        return self.guest_request({"type": "uninstall", "path": path}, defer, 200)

    def m_set_theme(self, sender, theme_json):
        try:
            theme = json.loads(theme_json)
            theme = {"dark": bool(theme.get("dark")), "accent": str(theme.get("accent", ""))[:7]}
        except (ValueError, AttributeError):
            return "That theme couldn't be read."
        if theme == self.state.get("theme"):
            return ""
        self.state["theme"] = theme
        self.save_state()
        if self.link.connected:
            self.link.send(dict(theme, type="theme"))
        return ""

    def m_set_warm(self, sender, warm_json):
        """Which apps wild-guest keeps warm (brief 5.5a), from the agent:
        {"apps": [{"id", "path"}], "limit": n}."""
        try:
            warm = json.loads(warm_json)
            apps = [{"id": str(a.get("id", ""))[:40], "path": str(a["path"])[:400]} for a in warm.get("apps", [])[:8]]
            warm = {"apps": apps, "limit": max(0, min(int(warm.get("limit", 0)), 6))}
        except (ValueError, AttributeError, KeyError, TypeError):
            return "That list couldn't be read."
        if warm != self.state.get("warm"):
            self.state["warm"] = warm
            self.save_state()
            if self.link.connected:
                self.link.send(dict(warm, type="warm"))
        return ""

    def m_set_option(self, sender, key, value):
        if key == "memoryGb":
            return self.set_memory(value)
        if key == "whenIdle":
            key = "when_idle"
        allowed = config.OPTION_KEYS.get(key)
        if not allowed or value not in allowed:
            return "That setting isn't available."
        self.state[key] = value
        self.save_state()
        self.changed()
        return ""

    def set_memory(self, value):
        """Settings' memory limit. The virtual PC's definition changes now,
        and Windows only gets it by starting from off: a paused Windows (no
        app open) is shut down now, a running one when it next goes idle,
        instead of being paused."""
        try:
            gb = int(value)
        except ValueError:
            return "That setting isn't available."
        host = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
        if gb < 4 or gb > 32 or (gb > 4 and gb * 1024 ** 3 > host * 0.75):
            return "That's more memory than this PC can spare."
        if virsh.state(self.domain) != "":
            kib = str(gb * 1024 * 1024)
            try:
                virsh.run("setmaxmem", self.domain, kib, "--config")
                virsh.run("setmem", self.domain, kib, "--config")
            except virsh.VirshError as e:
                log("memory:", e)
                return "Windows mode couldn't change its memory. Try again."
            self.memory = virsh.defined_memory(self.domain)
            st = virsh.state(self.domain)
            # A saved Windows loads back with the memory it was saved with,
            # whatever the definition now says, so it needs the same clean
            # restart as a running one.
            if st in ("running", "paused", "saved"):
                self.state["memory_pending"] = True
                if st in ("paused", "saved") and not self.busy_action:
                    self.restart_for_memory()
        rec = self.state.get("setup") or {}
        self.state["setup"] = dict(rec, options=dict(rec.get("options") or {}, memoryGb=gb))
        self.save_state()
        self.changed()
        return ""

    def m_connection(self, sender, is_open):
        self.life.update(time.monotonic(), connection=bool(is_open))
        self.schedule_idle()
        self.changed()

    def m_credentials(self, sender, defer):
        """A new password for the Windows account, set through the guest
        agent and handed to the person's agent, which keeps it in their
        keyring (brief section 4). Nothing keeps a copy here."""
        why = self.claim(sender)
        if why:
            defer.error(why)
            return defer
        account = self.cfg["account"]

        def work():
            if virsh.state(self.domain) != "running":
                self.wake()
            pw = random_password()
            qga.set_password(self.domain, account, pw)
            # A PC set up by the earlier proof script keeps its copy in step, so
            # that still connects.
            legacy = os.path.join(config.STATE_DIR, "secrets", "guest-password")
            if os.path.exists(legacy):
                with open(legacy, "w") as f:
                    f.write(pw)
            return pw

        def finished(result):
            if isinstance(result, Exception):
                defer.error(f"couldn't set the Windows password: {result}")
            else:
                defer.done(json.dumps({"account": account, "password": result,
                                       "domain": self.cfg["computer_name"] or self.state.get("computer_name", "")}))

        dbusutil.in_thread(work, done=finished)
        return defer

    def m_setup_start(self, sender, request, defer):
        """Sets up Windows mode for whoever asked, who then owns it."""
        try:
            req = json.loads(request)
            if not isinstance(req, dict):
                raise ValueError
        except ValueError:
            return "That request couldn't be read."
        uid = self.sender_uid(sender)
        if uid is None:
            return "Windows mode couldn't tell who is asking."
        owner = self.state.get("owner_uid")
        if uid != 0 and owner is not None and owner != uid:
            return self.claim(sender) or "Windows mode on this PC is set up for another account."
        if uid != 0:
            self.state["owner_uid"] = uid
            self.save_state()

        def work():
            return self.setup.start(req, uid)

        dbusutil.in_thread(work, done=lambda r: defer.done(f"Windows mode couldn't start setting up. ({r})"
                                                         if isinstance(r, Exception) else r))
        return defer

    def m_setup_control(self, sender, action, argument, defer):
        """pause, resume, cancel, or iso with a file's path."""
        actions = {"pause": lambda: self.setup.pause() or "", "resume": lambda: self.setup.resume() or "",
                   "cancel": lambda: self.setup.cancel() or "", "iso": lambda: self.setup.choose_iso(argument)}
        if action not in actions:
            return "That isn't something setup can do."
        dbusutil.in_thread(actions[action], done=lambda r: defer.done(str(r) if isinstance(r, Exception) else r))
        return defer

    def m_set_product_key(self, sender, key, defer):
        """Settings' Add a key (board 9.8): installs a Windows product key,
        or this PC's own built-in one when the key is "firmware", and
        activates. "" when Windows is activated, or why not. Windows is
        usually paused when nobody has an app open, and the key can only go
        in while it runs, so it is woken first and kept awake until done."""
        why = self.claim(sender)
        if why:
            return why
        self.life.hold(time.monotonic(), LICENCE_HOLD, True)
        self.schedule_idle()

        def work():
            while self.busy_action:
                time.sleep(0.1)
            try:
                self.wake()
            except (RuntimeError, virsh.VirshError) as e:
                log("waking Windows for a product key:", e)
                return "Windows mode couldn't start Windows to add the key. Try again, or restart the PC."
            return self.setup.set_key(key)

        def finished(result):
            self.life.hold(time.monotonic(), LICENCE_HOLD, False)
            self.schedule_idle()
            self.refresh()
            defer.done(str(result) if isinstance(result, Exception) else result)

        dbusutil.in_thread(work, done=finished)
        return defer

    def m_activation_start(self, sender, defer):
        """Settings' Use a licence on my Microsoft account (activation.py):
        Windows woken, the account for it made and the policy lifted.
        {account, password, domain} for the desktop, or {message}."""
        why = self.claim(sender)
        if why:
            return json.dumps({"message": why})
        if self.activation.ending:
            return json.dumps({"message": "The last activation is still being tidied up. Try again in a minute."})
        self.life.hold(time.monotonic(), activation.HOLD, True)
        self.schedule_idle()

        def work():
            while self.busy_action:
                time.sleep(0.1)
            try:
                self.wake()
            except (RuntimeError, virsh.VirshError) as e:
                log("waking Windows for activation:", e)
                return {"message": "Windows mode couldn't start Windows. Try again, or restart the PC."}
            return self.activation.start()

        def finished(result):
            if isinstance(result, Exception):
                log("activation:", result)
                result = {"message": "Windows mode couldn't add the account for activating. Try again, or restart the PC."}
            if "password" in result:
                result["domain"] = self.cfg["computer_name"] or self.state.get("computer_name", "")
            else:
                self.life.hold(time.monotonic(), activation.HOLD, False)
                self.schedule_idle()
            self.refresh()
            self.changed()
            defer.done(json.dumps(result))

        dbusutil.in_thread(work, done=finished)
        return defer

    def m_activation_check(self, sender, defer):
        why = self.claim(sender)
        if why:
            return json.dumps({"activated": False, "active": False})
        dbusutil.in_thread(self.activation.check, done=lambda r: (self.changed(), defer.done(json.dumps({
            "activated": r is True, "active": bool(self.activation.rec)})))[1])
        return defer

    def m_activation_end(self, sender, defer):
        """Done or Cancel, the desktop closing, or Windows saying it is
        activated: "" once the account is gone and the policy is back."""
        why = self.claim(sender)
        if why:
            return why
        self.end_activation(defer.done)
        return defer

    def end_activation(self, done=None):
        def work():
            while self.activation.ending:
                time.sleep(0.2)
            return self.activation.end()

        def finished(result):
            if isinstance(result, Exception):
                log("activation clean-up:", result)
                result = "Windows mode couldn't remove the account for activating yet. It tries again by itself."
            if not self.activation.rec:
                self.life.hold(time.monotonic(), activation.HOLD, False)
                self.schedule_idle()
            self.changed()
            if done:
                done(result)

        dbusutil.in_thread(work, done=finished)

    def resume_activation_cleanup(self):
        """An activation this run of the service didn't start (Wild OS or
        Windows mode restarted part way), or one past its hour: the account
        goes and the policy comes back as soon as Windows runs."""
        if self.activation.needs_cleanup() and not self.busy_action:
            log("finishing the clean-up after activating with a Microsoft account")
            self.end_activation()

    def m_firmware(self, sender, defer):
        """Board 9.1's licence row: whether this PC's firmware has a key and
        Windows is still on its disks. The key itself never leaves here."""
        dbusutil.in_thread(setup.firmware, done=lambda r: defer.done(
            json.dumps({"error": str(r)} if isinstance(r, Exception) else r)))
        return defer

    def m_end_program(self, sender, pid, image, defer):
        """Ends a Windows program that did not close when its window was
        closed on the desktop. Only that process, only if it still runs the
        program the window said it belonged to, and only in a person's
        session, never one of Windows' own (session 0)."""
        why = self.claim(sender)
        if why:
            defer.error(why)
            return defer
        script = end_program_script(pid, image)
        if not script:
            return "not a program that can be ended"

        def work():
            code, out, err = qga.powershell(self.domain, script, timeout=30)
            return (out or err).strip()

        def finished(result):
            if isinstance(result, Exception):
                log("ending a program that did not close:", result)
                defer.done(f"couldn't end it: {result}")
            else:
                log(f"ending process {pid} ({image}) that did not close:", result)
                defer.done(result)

        dbusutil.in_thread(work, done=finished)
        return defer

    def m_guest_request(self, sender, message, defer):
        """Any wild-guest request, answered with its reply: for tests and
        troubleshooting."""
        try:
            msg = json.loads(message)
        except ValueError:
            return json.dumps({"type": "error", "error": "not JSON"})
        if not isinstance(msg, dict) or not isinstance(msg.get("type"), str):
            return json.dumps({"type": "error", "error": "not a request"})
        return self.guest_request(msg, defer, 30)

    def m_remove(self, sender, defer):
        """Settings' Remove Windows mode, through the person's agent: after an
        administrator's password, Windows, its disk, its saved memory and
        firmware settings, the downloads and secrets setup kept, and the
        record of it all are deleted, and Windows mode is back to not set
        up. The shared folders are the person's own and are not touched.
        "" when done, else a sentence to show."""
        why = self.claim(sender)
        if why:
            return why

        def allowed(ok):
            if not ok:
                defer.done("Windows mode wasn't removed. It needs an administrator's password.")
                return
            log("removing Windows mode")
            self.life.hold(time.monotonic(), "remove", True)
            dbusutil.in_thread(self.remove_windows, done=finished)

        def finished(result):
            self.life.hold(time.monotonic(), "remove", False)
            self.refresh()
            self.setup_changed()
            if isinstance(result, Exception):
                log("removing Windows mode:", repr(result))
                defer.done("Windows mode couldn't be removed completely. Restart the PC, then try again.")
                return
            defer.done(result)

        self.check_admin(sender, REMOVE_ACTION, allowed)
        return defer

    def remove_windows(self):
        """From a worker thread. Whatever Windows was doing is ended, not
        saved: it is about to be deleted."""
        deadline = time.monotonic() + 120
        while (self.busy_action or self.saving or self.upkeep_running) and time.monotonic() < deadline:
            time.sleep(0.2)
        try:
            disks = [d for d in virsh.disk_files(self.domain) if d.startswith(config.STATE_DIR + os.sep)]
        except virsh.VirshError:
            disks = []
        # Stops a setup under way and deletes the virtual PC with its saved
        # memory and firmware settings, its disk and setup's secrets.
        self.setup.cancel()
        if virsh.state(self.domain) != "":
            # A definition with snapshots refuses a plain undefine.
            virsh.run("destroy", self.domain, check=False)
            virsh.run("undefine", self.domain, "--nvram", "--managed-save", "--snapshots-metadata", check=False)
        if virsh.state(self.domain) != "":
            return "Windows mode couldn't delete Windows. Restart the PC, then try again."
        for path in disks + [os.path.join(config.STATE_DIR, name) for name in ("domain.xml", "usb-device.xml")]:
            try:
                os.remove(path)
            except OSError:
                pass
        for folder in (setup.CACHE, setup.SECRETS):
            shutil.rmtree(folder, ignore_errors=True)
        self.usb.remembered = {}
        self.usb.save()
        self.windows = {}
        self.error = None
        self.memory = 0
        kept = {k: self.state[k] for k in KEPT_ON_REMOVE if k in self.state}
        self.state.clear()
        self.state.update(kept, when_idle=self.cfg["when_idle"])
        self.save_state()
        log("Windows mode was removed")
        return ""

    def check_admin(self, sender, action, reply):
        """Asks polkit for `action`, letting it show the password prompt."""
        uid = self.sender_uid(sender)
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
        self.bus.call("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
                      "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization",
                      GLib.Variant("((sa{sv})sa{ss}us)", (subject, action, {}, 1, "")),
                      GLib.VariantType("((bba{ss}))"), Gio.DBusCallFlags.NONE, 300000, None, got)

    def guest_exe_digest(self):
        try:
            with open(self.cfg["guest_exe"], "rb") as f:
                return hashlib.sha256(f.read()).hexdigest()[:16]
        except OSError:
            return None

    def guest_outdated(self):
        """Whether the wild-guest inside Windows is older than this
        package's. Setup and an install record which one went in; a Windows
        from before this was recorded gets the package's once."""
        digest = self.guest_exe_digest()
        return bool(digest) and self.state.get("guest_exe") != digest

    def update_guest(self):
        """Puts this package's wild-guest into Windows, only while no app is
        open (from idle_fired): the service and the session helper restart,
        and so does the connection, which a new app opens again. Before
        this, a wild-guest fix reached only new setups."""
        self.upkeep_running = True
        self.guest_update_tried = time.monotonic()
        self.life.hold(time.monotonic(), "upkeep", True)
        digest = self.guest_exe_digest()

        def work():
            with open(self.cfg["guest_exe"], "rb") as f:
                data = f.read()
            target = r"C:\Program Files\Wild OS\wild-guest.exe"
            qga.write_file(self.domain, target + ".new", data)
            code, out, err = qga.powershell(self.domain, INSTALL_GUEST_PS, 90)
            return code, (out + err).strip()[-300:]

        def finished(result):
            self.upkeep_running = False
            self.life.hold(time.monotonic(), "upkeep", False)
            if isinstance(result, Exception) or result[0] != 0:
                # Tried again an hour on (guest_update_tried).
                log("wild-guest: not updated:", result if isinstance(result, Exception) else result[1])
            else:
                log("wild-guest: updated to", digest)
                self.state["guest_exe"] = digest
                self.save_state()
            self.schedule_idle()

        dbusutil.in_thread(work, done=finished)

    def m_install_guest(self, sender, defer):
        """Puts this package's wild-guest into Windows and restarts it: the
        setup payload carries it for new installs, this updates one that
        exists."""
        exe = self.cfg["guest_exe"]
        self.life.hold(time.monotonic(), "install", True)

        def work():
            with open(exe, "rb") as f:
                data = f.read()
            if virsh.state(self.domain) != "running":
                self.wake()
            target = r"C:\Program Files\Wild OS\wild-guest.exe"
            qga.powershell(self.domain, "New-Item -ItemType Directory -Force 'C:\\Program Files\\Wild OS' | Out-Null", 60)
            qga.write_file(self.domain, target + ".new", data)
            code, out, err = qga.powershell(self.domain, INSTALL_GUEST_PS, 90)
            if code == 0:
                self.state["guest_exe"] = hashlib.sha256(data).hexdigest()[:16]
                self.save_state()
            return {"ok": code == 0, "output": (out + err).strip()[-2000:]}

        def finished(result):
            self.life.hold(time.monotonic(), "install", False)
            self.schedule_idle()
            defer.done(json.dumps({"ok": False, "error": str(result)} if isinstance(result, Exception) else result))

        dbusutil.in_thread(work, done=finished)
        return defer


# The same registration setup.ps1 does, for a guest that already exists.
INSTALL_GUEST_PS = r"""
$ErrorActionPreference = 'Stop'
$dir = 'C:\Program Files\Wild OS'
# A running program can be renamed but not replaced, and the service
# restarts itself if stopped, so the old copy is moved aside instead.
Get-ChildItem "$dir\wild-guest.*.old" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
if (Test-Path "$dir\wild-guest.exe") { Rename-Item -Force "$dir\wild-guest.exe" ("wild-guest.{0}.old" -f (Get-Date -Format yyyyMMddHHmmss)) }
Rename-Item -Force "$dir\wild-guest.exe.new" 'wild-guest.exe'
if (-not (Get-Service WildGuest -ErrorAction SilentlyContinue)) {
    New-Service -Name WildGuest -DisplayName 'Wild OS guest helper' -BinaryPathName "`"$dir\wild-guest.exe`" --service" -StartupType Automatic | Out-Null
    sc.exe failure WildGuest reset= 60 actions= restart/2000/restart/5000/restart/30000 | Out-Null
}
Get-Process wild-guest -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Start-Service WildGuest -ErrorAction SilentlyContinue
(Get-Service WildGuest).Status.ToString()
"""


def main():
    loop = GLib.MainLoop()
    bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    # The object exists before the name does: D-Bus delivers the call that
    # started this service as soon as the name is taken, and it must find it.
    service = Service(bus)

    def lost(conn, name):
        log(f"winmoded: lost {name}")
        loop.quit()

    Gio.bus_own_name_on_connection(bus, config.BUS_NAME, Gio.BusNameOwnerFlags.NONE,
                                   lambda conn, name: log(f"winmoded: serving {name}"), lost)
    GLib.unix_signal_add(GLib.PRIORITY_HIGH, 15, loop.quit)
    loop.run()
    # Stopped as the PC shuts down while Windows is being saved: half a save
    # would not load back, so it is let finish (systemd allows 120 s and,
    # with KillMode=mixed, leaves virsh running meanwhile).
    deadline = time.monotonic() + 110
    while service.saving and time.monotonic() < deadline:
        time.sleep(0.2)
    # Held until here, so it lives as long as the loop.
    del service
    return 0
