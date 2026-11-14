"""The Windows mode agent: one per person, in their session (brief section 3).

It owns what belongs to the person rather than the PC: the one RemoteApp
connection and its password (kept in their keyring), taskbar grouping of
the windows it brings, the Start entries and file types for Windows apps,
and pushing their light or dark look to Windows. Everything that touches
the guest itself goes through the system service (system.py).

Served on the session bus as org.wildos.WindowsMode1, the interface
Settings already reads (wildos-settings, windowsmode.h): Status is one JSON
object, Changed says to read it again, and calls that can fail answer with
a sentence to show, or nothing.
"""

import json
import os
import shutil
import signal
import subprocess
import sys
import time

from gi.repository import Gio, GLib

from . import config, dbusutil, documents, launchers, msdl, paths, rdp, theme, xsmp

XML = """
<node>
  <interface name="org.wildos.WindowsMode1">
    <method name="Status"><arg type="s" direction="out"/></method>
    <method name="Launch">
      <arg name="app" type="s" direction="in"/>
      <arg name="files" type="as" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="LaunchProgram">
      <arg name="path" type="s" direction="in"/>
      <arg name="args" type="as" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="UninstallApp">
      <arg name="app" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="RunInstaller">
      <arg name="path" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="OutsideSharedFolders">
      <arg name="files" type="as" direction="in"/>
      <arg type="as" direction="out"/>
    </method>
    <method name="CopyIntoDocuments">
      <arg name="files" type="as" direction="in"/>
      <arg type="as" direction="out"/>
    </method>
    <method name="Power">
      <arg name="action" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Documents"><arg type="s" direction="out"/></method>
    <method name="RefreshApps"><arg type="s" direction="out"/></method>
    <method name="SetOption">
      <arg name="key" type="s" direction="in"/>
      <arg name="value" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="SetProductKey">
      <arg name="key" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="CheckUpdates"><arg type="s" direction="out"/></method>
    <method name="OpenDesktop"><arg type="s" direction="out"/></method>
    <method name="ActivateWithAccount"><arg type="s" direction="out"/></method>
    <method name="EndActivation"><arg type="s" direction="out"/></method>
    <method name="ActivationPassword"><arg type="s" direction="out"/></method>
    <method name="Repair"><arg type="s" direction="out"/></method>
    <method name="Remove"><arg type="s" direction="out"/></method>
    <method name="StartSetup">
      <arg name="request" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="PauseSetup"><arg type="s" direction="out"/></method>
    <method name="ResumeSetup"><arg type="s" direction="out"/></method>
    <method name="CancelSetup"><arg type="s" direction="out"/></method>
    <method name="ChooseIso">
      <arg name="path" type="s" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="OpenDownloadPage"><arg type="s" direction="out"/></method>
    <method name="Firmware"><arg type="s" direction="out"/></method>
    <method name="ShowSetupProgress"/>
    <signal name="Changed"/>
  </interface>
</node>
"""

NOT_YET = "This isn't available yet."
REMOVING = "Windows mode is being removed."
ACTIVATING = ("Windows is open for activation. Finish in the Windows desktop, or press Cancel in Settings, "
              "then try again.")
# How often Windows is asked whether it is activated while the person works
# through Microsoft's troubleshooter.
ACTIVATION_CHECK_SECONDS = 10
# How long a launch waits for a new connection's session to be up.
SESSION_WAIT = 90
# Seconds after sign-in before Windows is started ahead of time.
PRESTART_DELAY = 45
# Seconds after the agent starts before Windows apps kept as Wild OS shut
# down are brought back.
KEPT_DELAY = 5
# The question before Windows (or Wild OS) shuts down waits for the person;
# the system dialog takes the safe choice itself after 60 seconds.
ASK_TIMEOUT = 600
# Windows' own restart and shutdown can take minutes with updates.
POWER_TIMEOUT = 900
# Keeping Windows apps' work as the session ends: a save that takes longer
# carries on, but the session's end no longer waits for it.
LEAVE_SAVE_TIMEOUT = 75
# A launch is watched every 1.5 s for up to two minutes; its program gone
# on three polls in a row (4.5 s, longer than any launcher takes to hand
# over) with no new window means it ended before it opened.
LAUNCH_WATCH_MS = 1500
LAUNCH_WATCH_POLLS = 80
LAUNCH_GONE_POLLS = 3
SECRET_ATTRS = ["service", "org.wildos.WindowsMode1", "account"]
# The Office apps a setup with Office brings, as boards 9.13 and 9.14 name
# them, by their Start entries' ids.
OFFICE_APPS = [("word", "Word"), ("excel", "Excel"), ("powerpoint", "PowerPoint"), ("outlook", "Outlook")]


class Answer:
    """Stands in for a D-Bus caller's Deferred when the agent asks itself:
    then(value) gets the answer, on the main loop."""

    def __init__(self, then):
        self.then = then

    def done(self, value):
        GLib.idle_add(lambda: (self.then(value), False)[1])

    def error(self, message):
        self.done(message)


def log(*parts):
    print(*parts, file=sys.stderr, flush=True)


def split_arguments(text):
    """A Start menu shortcut's arguments as a list, Windows-style: quotes
    group, backslashes stay."""
    out, cur, quoted, any_char = [], [], False, False
    for ch in text or "":
        if ch == '"':
            quoted, any_char = not quoted, True
        elif ch.isspace() and not quoted:
            if cur or any_char:
                out.append("".join(cur))
            cur, any_char = [], False
        else:
            cur.append(ch)
    if cur or any_char:
        out.append("".join(cur))
    return out


def wake_key(came_from, via):
    return f"{came_from}/{via}"


# Until this PC has timed each path itself: a cold start of Windows and its
# sign-in, restoring from disk, and opening the connection to a Windows that
# is already awake.
DEFAULT_WAKE = {
    # Measured on an XPS 13 9310: 82 s to Remote Desktop, 108 s to Word.
    "stopped/new connection": 90,
    "saved/new connection": 15,
    "paused/new connection": 8,
    "running/new connection": 8,
}


def slow_reason(lifecycle, name):
    """Why opening an app is taking longer (board 9.13), from what the agent
    knows: how Windows was when the app was opened. Empty when nothing is
    known, and wildos-rdp says it plainly in its own words."""
    if lifecycle in ("Stopped", "NotInstalled", None):
        return (f"Windows is starting up from off, which takes a minute or two and only happens now and then. "
                f"{name} will open as soon as it's ready.")
    if lifecycle == "Saved":
        return f"Windows mode is being brought back from where it was saved. {name} will open as soon as it's ready."
    return ""


def launch_state(rec):
    """The target a launch is held to (brief 5.5a): warm or cold when Windows
    was paused or running, saved or stopped otherwise."""
    came_from = rec.get("from")
    if came_from in ("saved", "stopped"):
        return came_from
    return ("warm" if rec.get("warm") else "cold") + "-" + (came_from or "running")


class Keyring:
    """The Windows account's password in the person's keyring (KWallet
    serves the Secret Service on Wild OS), through libsecret's secret-tool.
    Without a keyring it is only kept in memory, and a new one is set the
    next time the agent starts."""

    def __init__(self):
        self.memory = {}
        self.tool = shutil.which("secret-tool")

    def usable(self):
        """Only a keyring that already exists. Asking KWallet for one that
        doesn't makes it ask the person to create one (the Blowfish or GPG
        dialog), which happens after an automatic first sign-in, when
        pam_kwallet had no password to make it with. Without one the
        password stays in memory and a new one is set next time."""
        if not self.tool:
            return False
        data = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
        return os.path.exists(os.path.join(data, "kwalletd", "kdewallet.kwl"))

    def get(self, account):
        if account in self.memory:
            return self.memory[account]
        if self.usable():
            try:
                p = subprocess.run([self.tool, "lookup", *SECRET_ATTRS, account], capture_output=True, text=True, timeout=10)
                if p.returncode == 0 and p.stdout:
                    self.memory[account] = p.stdout.rstrip("\n")
                    return self.memory[account]
            except (OSError, subprocess.TimeoutExpired):
                pass
        return None

    def put(self, account, password):
        self.memory[account] = password
        if self.usable():
            try:
                subprocess.run([self.tool, "store", "--label", "Windows mode", *SECRET_ATTRS, account],
                               input=password, text=True, capture_output=True, timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                pass

    def forget(self, account):
        self.memory.pop(account, None)
        if self.usable():
            try:
                subprocess.run([self.tool, "clear", *SECRET_ATTRS, account], capture_output=True, timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                pass


class Agent:
    def __init__(self, session, system):
        self.session = session
        self.system_bus = system
        self.options = config.read_json(config.user_config_file(), {}) or {}
        self.options.setdefault("open_files", True)
        self.registry = launchers.load_registry()
        self.keyring = Keyring()
        self.host = {}
        self.windows = []
        self.queue = []
        # The sign-in-only connection after Windows starts ahead of time.
        self.preparing = None
        self.waking = False
        self.removing = False
        self.launches = {}
        self.last_launch = None
        self.last_app = None
        self.refreshed = False
        # Installers handed over by RunInstaller, by program name: once their
        # windows have all closed, Windows' Start menu is read again so the
        # new app gets its Start entry.
        self.installers = {}
        # An installer was run since the Start menu was last read. Some hand
        # over to a second program and close, so their own windows never
        # show; the menu is then read once every Windows app has closed.
        self.installed_since_refresh = False
        self.history = config.read_json(os.path.join(config.user_data_dir(), "history.json"), {}) or {}
        self.wake_times = config.read_json(os.path.join(config.user_data_dir(), "wake-times.json"), {}) or {}
        # Times "Show progress" was asked for (the setup notification, or
        # Start's "Setting up" button): the taskbar's Windows mode item opens
        # its panel each time this goes up.
        self.progress_requests = 0
        # Notifications with buttons, by id: what each button does.
        self.notification_actions = {}
        session.signal_subscribe("org.freedesktop.Notifications", "org.freedesktop.Notifications", None,
                                 "/org/freedesktop/Notifications", None, Gio.DBusSignalFlags.NONE, self.notification_signal)
        # The windows already open when the "Starting Windows" card went up.
        self.starting = None
        self.starting_timer = None
        self.conn = rdp.Connection(self.connection_changed, self.auth_failed,
                                   os.path.join(config.user_data_dir(), "connection.log"))
        # Anything installed from the whole Windows desktop gets its Start
        # entry once the desktop closes.
        self.desktop = rdp.Connection(self.desktop_changed, self.auth_failed,
                                      os.path.join(config.user_data_dir(), "desktop.log"), desktop=True)
        # Activating with a Microsoft account (issue 56): the desktop signed
        # in to the account made for it, what stage it is at (starting, open
        # or ending) and how the last one went.
        self.activation_desktop = rdp.Connection(self.activation_desktop_changed, lambda: None,
                                                 os.path.join(config.user_data_dir(), "activation.log"), desktop=True)
        self.activation = None
        self.activation_account = None
        self.activation_result = None
        self.activation_timer = None
        self.activation_tidied = False
        # wildos-rdp's windows for apps being opened, by token, and its
        # interface on the session bus.
        self.loading = {}
        self.remote = rdp.RemoteApp(session, on_window=self.remote_window, on_loading=self.remote_loading,
                                    on_details=self.remote_details, on_close_stalled=self.remote_close_stalled,
                                    on_retry=self.remote_retry)
        # Launches still waiting for their first window, watched for a
        # program that ends before it shows one.
        self.watched = []
        # Those whose program ended, for Try again.
        self.ended_launches = []
        self.export = dbusutil.Exported(session, config.OBJECT_PATH, XML, config.AGENT_INTERFACE, {
            "Status": (self.m_status, "s", False),
            "Launch": (self.m_launch, "s", False),
            "LaunchProgram": (self.m_launch_program, "s", False),
            "RunInstaller": (self.m_run_installer, "s", False),
            "UninstallApp": (self.m_uninstall_app, "s", False),
            "OutsideSharedFolders": (self.m_outside, "as", False),
            "CopyIntoDocuments": (self.m_copy, "as", False),
            "RefreshApps": (self.m_refresh, "s", False),
            "Power": (self.m_power, "s", False),
            "Documents": (self.m_documents, "s", False),
            "SetOption": (self.m_set_option, "s", False),
            "SetProductKey": (self.m_set_product_key, "s", False),
            "CheckUpdates": (lambda sender: NOT_YET, "s", False),
            "OpenDesktop": (self.m_open_desktop, "s", False),
            "ActivateWithAccount": (self.m_activate_with_account, "s", False),
            "EndActivation": (self.m_end_activation, "s", False),
            "ActivationPassword": (self.m_activation_password, "s", False),
            "Repair": (lambda sender: NOT_YET, "s", False),
            "Remove": (self.m_remove, "s", False),
            "StartSetup": (self.m_start_setup, "s", False),
            "PauseSetup": (lambda sender, defer: self.setup_control("pause", "", defer), "s", False),
            "ResumeSetup": (lambda sender, defer: self.setup_control("resume", "", defer), "s", False),
            "CancelSetup": (lambda sender, defer: self.setup_control("cancel", "", defer), "s", False),
            "ChooseIso": (lambda sender, path, defer: self.setup_control("iso", path, defer), "s", False),
            "OpenDownloadPage": (self.m_open_download_page, "s", False),
            "Firmware": (self.m_firmware, "s", False),
            "ShowSetupProgress": (self.m_show_setup_progress, None, False),
        })
        system.signal_subscribe(config.BUS_NAME, config.HOST_INTERFACE, None, config.OBJECT_PATH, None,
                                Gio.DBusSignalFlags.NONE, self.host_signal)
        self.tagger = None
        if os.environ.get("DISPLAY"):
            try:
                from . import windows
                self.tagger = windows.Tagger(self.lookup_window, self.window_tagged, self.rail_seen)
            except Exception as e:
                log("window grouping is off:", e)
        # Asked before the first call is answered (none is until this
        # returns), so a just-started agent never reports "not set up" on a PC
        # that is.
        self.refresh_host_now()
        self.watch_theme()
        GLib.timeout_add_seconds(5, lambda: (self.push_warm(), False)[1])
        # Shortly after sign-in, once the desktop and its startup apps have
        # settled, Windows is started ahead of time (with a small share of the
        # processor) and signed in to, so the first Windows app opens from
        # memory into a session that is ready. The service declines on battery,
        # without enough free memory, or when Windows is already up.
        GLib.timeout_add_seconds(PRESTART_DELAY, lambda: (self.prestart(), False)[1])
        # Work kept as Wild OS last shut down (board 9.9) comes back a moment
        # after sign-in, once the desktop is up.
        GLib.timeout_add_seconds(KEPT_DELAY, lambda: (self.reopen_kept(), False)[1])
        # Asked as the session ends, like an app with unsaved work.
        self.leaving = xsmp.LeaveWatcher(log)
        self.session_end = xsmp.Client(session, self.windows_open, self.ask_before_leaving, log)

    def prestart(self):
        if self.host.get("kept") or self.removing or self.activation:
            return
        def done(reply):
            if isinstance(reply, GLib.Error):
                log("starting Windows ahead of time:", reply.message)
                return
            try:
                result = json.loads(reply or "{}")
            except ValueError:
                return
            if result.get("ok"):
                self.prepare_session()
        self.call("Prestart", done=done, timeout=600)

    def prepare_session(self):
        """Windows was started ahead of time: it is signed in to as well, and
        the session left disconnected, so the first app takes it over in a
        couple of seconds instead of waiting for Windows to sign in and start
        its shell (about three seconds more, measured). Only while no
        connection is open or opening."""
        if self.conn.running or self.preparing is not None or self.activation:
            return

        def go(creds):
            if not creds.get("password") or self.conn.running:
                return
            args = rdp.prepare_arguments(self.host.get("guest_address"), self.host.get("rdp_port", 3389), creds["account"],
                                         creds.get("domain", ""), drive=self.drive_folder())

            def ended(ok):
                self.preparing = None
                log("session prepared" if ok else "preparing the session didn't finish")
            try:
                self.preparing = rdp.prepare_session(args, creds["password"], os.path.join(config.user_data_dir(), "prepare.log"),
                                                     ended, native=self.conn.native_client)
            except OSError as e:
                log("couldn't prepare the session:", e)
        if self.host.get("guest_address"):
            self.credentials(go)
        else:
            self.refresh_host(lambda: self.credentials(go) if self.host.get("guest_address") else None)

    def end_preparing(self):
        """A launch needs the session now: the sign-in-only connection ends
        (the session stays, disconnected) so it cannot hold it."""
        if self.preparing is not None and self.preparing.poll() is None:
            try:
                os.killpg(self.preparing.pid, signal.SIGTERM)
            except OSError:
                pass

    # The system service.
    def call(self, method, args=None, signature=None, done=None, timeout=600):
        params = GLib.Variant(signature, args) if signature else None

        def finished(conn, res):
            try:
                value = conn.call_finish(res).unpack()
                value = value[0] if value else None
            except GLib.Error as e:
                value = GLib.Error(e.message)
            if done:
                done(value)

        self.system_bus.call(config.BUS_NAME, config.OBJECT_PATH, config.HOST_INTERFACE, method, params, None,
                             Gio.DBusCallFlags.NONE, timeout * 1000, None, finished)

    def call_json(self, method, args=None, signature=None, done=None, timeout=600):
        def parsed(value):
            if isinstance(value, GLib.Error):
                # The service didn't answer at all (not there, crashed or
                # too slow), as against answering with an error.
                done({"ok": False, "error": value.message, "unanswered": True})
                return
            try:
                done(json.loads(value) if isinstance(value, str) else {})
            except ValueError:
                done({"ok": False, "error": "unreadable answer"})
        self.call(method, args, signature, parsed if done else None, timeout)

    def host_signal(self, conn, sender, path, iface, signal, params):
        if signal == "WindowsChanged":
            try:
                self.set_windows(json.loads(params.unpack()[0]))
            except ValueError:
                pass
        self.refresh_host()

    def refresh_host_now(self):
        try:
            value = self.system_bus.call_sync(config.BUS_NAME, config.OBJECT_PATH, config.HOST_INTERFACE, "Status", None,
                                              GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE, 10000, None).unpack()[0]
            st = json.loads(value)
        except (GLib.Error, ValueError) as e:
            log("Windows mode's service didn't answer:", e)
            self.refresh_host()
            return
        if isinstance(st, dict) and "state" in st:
            self.host = st
            if isinstance(st.get("windows"), list):
                self.windows = st["windows"]

    def refresh_host(self, then=None):
        def got(st):
            if isinstance(st, dict) and "state" in st:
                was = (self.host.get("setup") or {}).get("running")
                self.host = st
                if was and not (st.get("setup") or {}).get("running") and st.get("state") != "NotInstalled":
                    self.setup_finished()
                if isinstance(st.get("windows"), list):
                    self.set_windows(st["windows"])
                self.end_connection_if_gone(st)
                if st.get("activation") and not self.activation and not self.activation_tidied:
                    self.activation_tidied = True
                    # Started before this agent did (it restarted part way):
                    # nothing here can finish it, so it is tidied up.
                    log("an activation from before this agent started is being ended")
                    self.finish_activation("restarted")
            self.changed()
            if then:
                then()
        self.call_json("Status", done=got, timeout=10)

    def end_connection_if_gone(self, st):
        """A connection to a Windows that is being saved to disk, or is off,
        ends here. Left to itself it reconnects once Windows is back, and
        Remote Desktop then starts the connection's first app again: a
        second copy of whatever was opened first. Windows' own windows come
        back through a new connection (reopen_windows)."""
        if st.get("action") in ("save", "keep", "shutdown", "restart") or st.get("vm") == "saved":
            if self.conn.open and not self.waking:
                log("Windows is going to disk or off: ending the connection")
                self.conn.close()

    def changed(self):
        self.export.emit("Changed")

    # Windows and the connection.
    def set_windows(self, ws):
        self.windows = ws
        if self.tagger:
            self.tagger.scan()
        now = time.monotonic()
        # Any new window but the RemoteApp shell's own means Windows is up and
        # the app (or the installer, which has no app of its own) is showing.
        if self.starting is not None and any(w.get("hwnd") not in self.starting and self.app_for(w) != "" for w in ws):
            self.hide_starting()
        stems = {launchers.exe_stem(w.get("image")) for w in ws}
        for stem, seen in list(self.installers.items()):
            if stem in stems:
                self.installers[stem] = True
            elif seen:
                del self.installers[stem]
                GLib.timeout_add_seconds(5, lambda: (self.m_refresh(None), False)[1])
        if self.installed_since_refresh and not any(self.app_for(w) != "" for w in ws):
            self.installed_since_refresh = False
            GLib.timeout_add_seconds(5, lambda: (self.m_refresh(None), False)[1])
        # The guest lists windows once a second, so a window it lists may
        # well be on screen already: the screen's own time is used when the
        # window has been seen there.
        for w in ws:
            seen = self.tagger.seen_at_handle(w.get("hwnd")) if self.tagger else None
            self.window_appeared(self.app_for(w), w.get("hwnd"), seen or now, "window on screen" if seen else "window listed")
        self.reopen_if_needed()

    # Per-launch timing (brief 5.5a). Each launch records, in seconds from
    # the click: connection_ready (Windows awake and the session able to
    # take it), process_started (wild-guest started or showed the app) and
    # first_window (its first new window on screen), with the state Windows
    # was in and whether the app was warm. One JSON object per line in
    # launches.jsonl; "wildos-winmode launch-stats" reads the percentiles.
    def start_timing(self, app_id, started, info):
        known = {w.get("hwnd") for w in self.windows if self.app_for(w) == app_id}
        self.launches[app_id] = {"t0": started, "known": known, "info": dict(info)}

    def mark(self, app_id, name, value=None, now=None):
        rec = self.launches.get(app_id)
        if rec and name not in rec["info"]:
            rec["info"][name] = value if value is not None else round((now or time.monotonic()) - rec["t0"], 3)

    def window_appeared(self, app_id, handle, now, how):
        rec = self.launches.get(app_id)
        if rec and handle not in rec["known"]:
            del self.launches[app_id]
            self.record_launch(app_id, rec, now, how)

    def connection_seen(self, now):
        """The first window of a new connection (RemoteApp's own) means the
        session is up."""
        for app_id, rec in self.launches.items():
            if rec["info"].get("via") == "new connection":
                self.mark(app_id, "connection_ready", now=now)

    def count_launch(self, app_id):
        if app_id and app_id in self.registry:
            self.last_app = app_id
            self.history[app_id] = self.history.get(app_id, 0) + 1
            try:
                config.write_json(os.path.join(config.user_data_dir(), "history.json"), self.history)
            except OSError:
                pass
            self.push_warm()

    def push_warm(self):
        """The apps to keep warm: Office's first when installed, then the
        most launched, as many as Windows mode's memory allows (about one
        for each 2 GB, at most four)."""
        office = ["word", "excel", "powerpoint", "outlook"]
        ids = sorted(self.registry, key=lambda i: (i not in office, -self.history.get(i, 0), office.index(i) if i in office else 0))
        memory = self.host.get("memory_bytes") or 4 * 1024 ** 3
        limit = max(1, min(4, int(memory // (2 * 1024 ** 3))))
        apps = [{"id": i, "path": self.registry[i]["target"]} for i in ids[:limit]]
        self.call("SetWarm", (json.dumps({"apps": apps, "limit": limit}),), "(s)")
        return False

    def record_launch(self, app_id, rec, now, how):
        info = rec["info"]
        info.setdefault("connection_ready", info.get("wake_seconds"))
        out = dict(info, app=app_id, first_window=round(now - rec["t0"], 3), seen_by=how,
                   at=time.strftime("%Y-%m-%dT%H:%M:%S"))
        out["state"] = launch_state(out)
        self.last_launch = out
        self.hide_starting()
        self.wake_times[wake_key(info.get("from") or "running", info.get("via"))] = out["first_window"]
        try:
            config.write_json(os.path.join(config.user_data_dir(), "wake-times.json"), self.wake_times)
        except OSError:
            pass
        log("launch:", json.dumps(out))
        try:
            os.makedirs(config.user_data_dir(), exist_ok=True)
            with open(os.path.join(config.user_data_dir(), "launches.jsonl"), "a", encoding="utf-8") as f:
                f.write(json.dumps(out) + "\n")
        except OSError:
            pass
        self.changed()

    def app_for(self, window):
        if not isinstance(window, dict):
            return None
        by_aumid = launchers.app_id_for_aumid(window.get("aumid"), self.registry)
        if by_aumid:
            return by_aumid
        image = window.get("image")
        if not image:
            return None
        return "" if launchers.is_shell(image) else launchers.app_id_for_image(image, self.registry)

    def lookup_window(self, handle, done):
        for w in self.windows:
            if w.get("hwnd") == handle:
                done(self.app_for(w))
                return

        def got(reply):
            done(self.app_for(reply.get("window") if isinstance(reply, dict) else None))

        self.call_json("Window", (handle,), "(t)", got, timeout=10)

    def rail_seen(self, seen_at):
        self.connection_seen(seen_at)

    def window_tagged(self, xid, handle, app_id, seen_at):
        self.window_appeared(app_id, handle, seen_at or time.monotonic(), "window on screen")

    def desktop_changed(self, is_open):
        if not is_open:
            GLib.timeout_add_seconds(5, lambda: (self.m_refresh(None), False)[1])

    def connection_changed(self, is_open):
        log("connection", "open" if is_open else "closed")
        self.call("ConnectionChanged", (is_open,), "(b)")
        self.changed()

    def auth_failed(self):
        log("Windows refused the stored password; a new one is set next time")
        dbusutil.in_thread(self.keyring.forget, self.host.get("account", "wild"))

    def guest_paths(self, files):
        mapping = paths.folder_map(self.host.get("folders", []), self.host.get("share_root", ""))
        drive = self.host.get("drive", "Z")
        return [paths.to_guest(f, mapping, drive) for f in files], mapping

    def launch(self, path, args, name, app_id, done):
        """Starts a Windows program. Windows is woken first (resumed,
        restored or started; immediate when it is running). With the
        connection open the program starts inside the session through
        wild-guest; otherwise the connection opens with it.

        With wildos-rdp the app's window is on screen at once, with the
        loading W, and becomes the app when Windows shows it (boards 9.11 to
        9.13); the "Starting Windows" card is then not needed."""
        if self.removing:
            done(REMOVING)
            return
        if self.activation:
            done(ACTIVATING)
            return
        started = time.monotonic()
        self.end_preparing()
        window = self.open_app_window(app_id, name, path)
        if not window:
            self.show_starting(name, app_id)
        finish = done

        def done(message):
            if message:
                self.hide_starting()
                self.close_app_window(window)
            finish(message)

        self.queue.append((path, args, name, app_id, started, done))
        self.wake_then_launch()
        if window:
            self.watch_launch(window, path, args, name, app_id)

    def watch_launch(self, window, path, args, name, app_id):
        """A program that ends before it shows a window (it crashed, or gave
        up) would leave its loading window saying it is still opening. Once
        the program has been seen running in Windows (or wild-guest says it
        started it), its being gone for a few seconds with no new window
        anywhere ends the wait: the loading window says it closed before it
        opened, and offers Try again."""
        w = {"window": window, "path": path, "args": args, "name": name, "app_id": app_id,
             "since_ms": int(time.time() * 1000), "seen": False, "gone": 0, "polls": 0,
             "windows": {x.get("hwnd") for x in self.windows}}
        self.watched.append(w)

        def poll():
            if w not in self.watched:
                return False
            w["polls"] += 1
            new_window = any(x.get("hwnd") not in w["windows"] for x in self.windows)
            if window["closed"] or new_window or w["polls"] > LAUNCH_WATCH_POLLS:
                self.watched.remove(w)
                return False

            def answered(reply):
                if w not in self.watched or not isinstance(reply, dict) or reply.get("type") != "running":
                    return
                if reply.get("running"):
                    w["seen"], w["gone"] = True, 0
                    return
                # Started and already gone before it was ever seen running
                # (wild-guest notes every start).
                if reply.get("started"):
                    w["seen"] = True
                if w["seen"]:
                    w["gone"] += 1
                if w["gone"] >= LAUNCH_GONE_POLLS:
                    self.watched.remove(w)
                    self.ended_launches = self.ended_launches[-4:] + [w]
                    log(name, "ended before it showed a window")
                    self.remote.update(window["token"], "ended")
            self.call_json("GuestRequest", (json.dumps({"type": "running", "path": path, "since": w["since_ms"]}),), "(s)",
                           answered, timeout=10)
            return True
        GLib.timeout_add(LAUNCH_WATCH_MS, poll)

    def remote_retry(self, token):
        """Try again on a loading window whose program ended: the same launch,
        in a new loading window."""
        for w in list(self.ended_launches):
            if w["window"]["token"] == token or (token and not w["window"]["token"]):
                self.ended_launches.remove(w)
                self.remote.cancel(token)
                self.launch(w["path"], w["args"], w["name"], w["app_id"], lambda message: None)
                return

    def wake_then_launch(self):
        """Wakes Windows, then starts every queued launch: inside the
        session when the connection is open, or with a new connection."""
        if self.waking:
            return
        self.waking = True

        def woke(reply):
            self.waking = False
            pending, self.queue = self.queue, []
            if not reply.get("ok"):
                for item in pending:
                    item[5](reply.get("message") or "Windows mode couldn't start. Try again, or restart the PC.")
                log("wake failed:", reply.get("error"))
                return
            if self.conn.open:
                for item in pending:
                    self.launch_in_session(*item, wake=reply)
                return
            self.queue = pending
            # Where Windows is comes from the service; an agent that started
            # before it could answer asks again first, rather than connect
            # to nowhere.
            def connect():
                self.credentials(lambda creds: self.open_connection(creds, reply))
            if self.host.get("guest_address"):
                connect()
            else:
                self.refresh_host(connect)

        self.call_json("Wake", done=woke)

    def open_app_window(self, app_id, name, program=""):
        """wildos-rdp's window for an app being opened: through the running
        client when the connection is open, or by starting the client now
        when it is not, so the window is there while Windows wakes. Returns
        what close_app_window() needs."""
        known = app_id in self.registry
        desktop_id = launchers.PREFIX + app_id if known else ""
        icon = launchers.PREFIX + app_id if known else ""
        window = {"token": 0, "spawned": False, "closed": False, "app": app_id}
        if self.conn.open:
            # Board 9.12: an app that opens within half a second of the click
            # (Windows awake, the app warm) opens without the loader.
            warm = self.host.get("state") == "Running"

            def got(token):
                window["token"] = token or 0
                if window["closed"]:
                    self.remote.cancel(window["token"])
                elif window["token"]:
                    self.loading[window["token"]] = window
            self.remote.prepare(desktop_id, name, icon, program, 500 if warm else 0, got)
        elif not self.conn.running:
            reason = slow_reason(self.host.get("state"), name)
            window["spawned"] = self.conn.spawn(desktop_id, name, icon, reason, program)
        return window

    def close_app_window(self, window):
        if not window or window["closed"]:
            return
        window["closed"] = True
        if window["spawned"]:
            self.conn.abandon()
        self.remote.cancel(window["token"])

    def app_window_stage(self, app_id, stage):
        """News for the app's window: "almost" once its process is up."""
        for token, window in list(self.loading.items()):
            if window["app"] == app_id and not window["closed"]:
                self.remote.update(token, stage)

    def remote_window(self, window_id, title):
        """A window from wildos-rdp: which app it is decides its taskbar
        group, and its arrival is the launch's time to window."""
        now = time.monotonic()
        self.connection_seen(now)

        def found(app_id):
            if app_id:
                self.remote.set_window_app(window_id, launchers.PREFIX + app_id)
                self.window_appeared(app_id, window_id, now, "window on screen")
        self.lookup_window(window_id, found)

    def remote_loading(self, token, reason, window_id):
        window = self.loading.pop(token, None)
        if window and reason == "cancelled":
            # Cancel on board 9.13: the person stopped waiting. Windows keeps
            # going; the app simply opens without its loader if it still comes.
            window["closed"] = True
            log("launch cancelled from its window:", window["app"])

    def remote_close_stalled(self, window_id, pid, program):
        """A window closed on the desktop that Windows did not close, with
        nothing unsaved in it (wildos-rdp only asks then): its program is
        ended in the background, so nothing frozen is left behind."""
        log("window", hex(window_id), "did not close; ending", program, pid)
        self.call("EndProgram", (pid, program), "(us)", lambda r: log("end program:", r), timeout=60)

    def remote_details(self, token):
        try:
            subprocess.Popen(["wildos-settings", "--page", "windowsmode"], start_new_session=True,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError as e:
            log("Settings couldn't open:", e)

    def show_starting(self, name, app_id):
        """The card that says Windows is starting (wildos-system-dialogs),
        whenever the app will wait more than a moment for Windows. The
        estimate is the last time the same path took on this PC."""
        lifecycle = self.host.get("state")
        if lifecycle == "Running" and self.conn.open:
            return
        came_from = {"Running": "running", "Paused": "paused", "Saved": "saved"}.get(lifecycle, "stopped")
        key = wake_key(came_from, "session" if self.conn.open else "new connection")
        seconds = self.wake_times.get(key) or DEFAULT_WAKE.get(key, 1)
        if seconds < 3 or self.starting is not None:
            return
        self.starting = {w.get("hwnd") for w in self.windows}
        icon = launchers.PREFIX + app_id if app_id in self.registry else ""
        self.dialogs("ShowWindowsStarting", GLib.Variant("(ssib)", (name, icon, int(round(seconds)), came_from == "stopped")))
        # Never left up by a launch that opens no window of its own.
        self.starting_timer = GLib.timeout_add_seconds(int(seconds * 3) + 60, lambda: (self.hide_starting(), False)[1])

    def hide_starting(self):
        if self.starting is None:
            return
        self.starting = None
        if self.starting_timer:
            GLib.source_remove(self.starting_timer)
            self.starting_timer = None
        self.dialogs("HideWindowsStarting", None)

    def dialogs(self, method, args):
        self.session.call("org.wildos.SystemDialogs", "/Dialogs", "org.wildos.SystemDialogs", method, args, None,
                          Gio.DBusCallFlags.NO_AUTO_START, 2000, None, None, None)

    def launch_in_session(self, path, args, name, app_id, started, done, wake):
        self.start_timing(app_id, started, {"from": wake.get("from"), "wake_seconds": wake.get("seconds"), "via": "session"})

        # A connection that has only just opened is still signing in, and the
        # session helper starts after that: a cold sign-in can take a good
        # while, so the launch waits for the session up to a time limit.
        deadline = time.monotonic() + SESSION_WAIT

        def attempt():
            if not self.conn.running:
                # The connection ended while this launch waited for its session
                # (the last app was closed just as this one was opened, and
                # Windows signed the session out): it opens again, with this
                # app, rather than wait for a session nothing will bring.
                log("the connection ended before", name, "could open; opening it again")
                self.queue.append((path, args, name, app_id, started, done))
                self.wake_then_launch()
                return

            def got(reply):
                if reply.get("type") == "launched":
                    self.mark(app_id, "process_started")
                    self.app_window_stage(app_id, "almost")
                    self.mark(app_id, "warm", bool(reply.get("warm")))
                    done("")
                elif reply.get("error") in ("no-session", "not-connected", "timeout") and time.monotonic() < deadline:
                    GLib.timeout_add(250, lambda: (attempt(), False)[1])
                else:
                    log("launch failed:", reply)
                    done("That Windows app didn't open. Try again.")
            self.call_json("Launch", (path, args, ""), "(sass)", got, timeout=20)
        attempt()

    def drive_folder(self):
        try:
            return paths.drive_folder(self.host.get("folders") or config.load()["folders"], config.user_data_dir())
        except OSError as e:
            log("shared folders:", e)
            return None

    def m_set_product_key(self, sender, key, defer):
        def done(reply):
            self.refresh_host()
            defer.done(reply if isinstance(reply, str) else "Windows mode's background service didn't answer.")
        # Windows may have to start from off before the key can go in.
        self.call("SetProductKey", (key,), "(s)", done, timeout=840)
        return defer

    def m_open_desktop(self, sender, defer):
        """The whole Windows desktop, for things only it can do (Windows'
        own settings, an installer that needs it). The app connection
        closes first: the desktop takes the one session over."""
        def woke(reply):
            if not reply.get("ok"):
                defer.done(reply.get("message") or "Windows mode couldn't start. Try again, or restart the PC.")
                return

            def signed(creds):
                if not creds.get("password"):
                    defer.done("Windows mode couldn't sign in to Windows. Restart the PC, then try again.")
                    return
                self.conn.stop_now()
                self.end_preparing()
                # Asked again while one is still running: either its window
                # closed and FreeRDP never exited (seen after Windows showed
                # an error), which used to leave the button doing nothing
                # until the PC restarted, or it is open behind other windows.
                # Either way a new one takes the session over in front.
                if self.desktop.running:
                    log("the Windows desktop's last connection was still running; starting a new one")
                    self.desktop.stop_now()
                args = rdp.desktop_arguments(self.host.get("guest_address"), self.host.get("rdp_port", 3389),
                                             creds["account"], creds.get("domain", ""), drive=self.drive_folder())
                try:
                    self.desktop.start(args, creds["password"])
                except OSError as e:
                    defer.done(f"The Windows desktop couldn't open. ({e.strerror})")
                    return
                defer.done("")
            self.credentials(signed)

        if self.removing:
            return REMOVING
        if self.activation:
            return ACTIVATING
        self.call_json("Wake", done=woke)
        return defer

    def m_activate_with_account(self, sender, defer):
        """Settings' Use a licence on my Microsoft account: the service makes
        a second Windows account just for this (activation.py), and the whole
        desktop opens signed in to it for Microsoft's troubleshooter. The
        app connection and any Windows desktop close first, since Windows
        shows one session at a time; their apps keep running and come back
        afterwards."""
        if self.removing:
            return REMOVING
        if self.activation:
            return ""
        self.activation = "starting"
        self.activation_result = None
        self.changed()

        def started(reply):
            if not reply.get("password"):
                self.activation = None
                if reply.get("activated"):
                    self.activation_result = {"activated": True, "already": True, "message": ""}
                    self.refresh_host()
                    defer.done("")
                    return
                self.changed()
                defer.done(reply.get("message") or "Windows mode's background service didn't answer. Restart the PC, then try again.")
                return
            account = reply["account"]
            self.activation_account = account
            self.keyring.memory[account] = reply["password"]
            dbusutil.in_thread(self.keyring.put, account, reply["password"])
            self.conn.stop_now()
            self.end_preparing()
            self.desktop.stop_now()
            if self.activation_desktop.running:
                self.activation_desktop.stop_now()
            args = rdp.desktop_arguments(self.host.get("guest_address"), self.host.get("rdp_port", 3389), account,
                                         reply.get("domain", ""), title="Activate Windows")
            self.activation = "open"
            try:
                self.activation_desktop.start(args, reply["password"])
            except OSError as e:
                self.finish_activation("failed")
                defer.done(f"The Windows desktop couldn't open. ({e.strerror})")
                return
            self.activation_timer = GLib.timeout_add_seconds(ACTIVATION_CHECK_SECONDS, self.check_activation)
            self.refresh_host()
            defer.done("")

        # Windows may have to start from off, then the account is made.
        self.call_json("ActivationStart", done=started, timeout=840)
        return defer

    def check_activation(self):
        if self.activation != "open":
            self.activation_timer = None
            return False

        def got(reply):
            if self.activation != "open":
                return
            if reply.get("activated"):
                log("Windows says it is activated")
                self.finish_activation("activated")
            elif reply.get("active") is False:
                # The service ended it itself: its hour was up.
                self.finish_activation("expired")
        self.call_json("ActivationCheck", done=got, timeout=90)
        return True

    def activation_desktop_changed(self, is_open):
        if not is_open and self.activation == "open":
            self.finish_activation("closed")

    def m_end_activation(self, sender, defer):
        """Done or Cancel in Settings."""
        if self.activation in (None, "starting"):
            return ""
        self.finish_activation("done", defer.done)
        return defer

    def m_activation_password(self, sender):
        """For the one time Windows asks for the account's current password
        (turning it into a Microsoft account), and only while it exists."""
        if self.activation != "open" or not self.activation_account:
            return ""
        return self.keyring.memory.get(self.activation_account, "")

    def finish_activation(self, why, done=None):
        """The desktop closes and the service signs the account out, deletes
        it and its profile and protects Windows mode's own account again.
        Windows is asked once more first, so Done pressed straight after
        the troubleshooter still counts."""
        if self.activation == "ending":
            if done:
                done("")
            return
        self.activation = "ending"
        if self.activation_timer:
            GLib.source_remove(self.activation_timer)
            self.activation_timer = None
        self.activation_desktop.close()
        self.changed()
        account = self.activation_account

        def ended(reply, activated):
            message = reply if isinstance(reply, str) else \
                "Windows mode's background service didn't answer. It removes the extra account by itself when it next starts."
            self.activation = None
            self.activation_account = None
            if account:
                dbusutil.in_thread(self.keyring.forget, account)
            self.activation_result = {"activated": activated, "message": message}
            log("activation ended:", why, "activated" if activated else "not activated", message)
            self.refresh_host()
            self.reopen_windows(within=60)
            if done:
                done("")

        def checked(reply):
            activated = bool(reply.get("activated")) or why == "activated"
            self.call("ActivationEnd", done=lambda r: ended(r, activated), timeout=420)

        if why in ("activated", "restarted", "failed"):
            checked({"activated": why == "activated"})
        else:
            self.call_json("ActivationCheck", done=checked, timeout=90)

    def credentials(self, done):
        """The account and its password: from the keyring (secret-tool, which
        KWallet can keep waiting for seconds, so off the main loop) or else
        from the service."""
        account = self.host.get("account", "wild")
        self.trust_current_windows()

        def looked_up(pw):
            if isinstance(pw, str) and pw:
                done({"account": account, "password": pw, "domain": self.host.get("computer_name", "")})
                return

            def got(reply):
                if reply.get("password"):
                    dbusutil.in_thread(self.keyring.put, reply["account"], reply["password"])
                done(reply)
            self.call_json("Credentials", done=got, timeout=120)
        if account in self.keyring.memory:
            looked_up(self.keyring.memory[account])
        else:
            dbusutil.in_thread(self.keyring.get, account, done=looked_up)

    def trust_current_windows(self):
        """Each setup makes a new Windows with a certificate of its own, which
        FreeRDP would refuse as changed. The certificate trusted for the
        last Windows is forgotten once per setup, by the time it finished,
        before the first connection to the new one."""
        finished = (self.host.get("setup") or {}).get("finished")
        if not finished or not self.host.get("guest_address"):
            return
        marker = os.path.join(config.user_data_dir(), "certificate-for.json")
        if (config.read_json(marker, {}) or {}).get("finished") == finished:
            return
        if rdp.forget_certificate(self.host["guest_address"], self.host.get("rdp_port", 3389)):
            log("forgot the certificate of the Windows set up before this one")
        try:
            config.write_json(marker, {"finished": finished})
        except OSError as e:
            log("certificate:", e)

    def m_remove(self, sender, defer):
        """Settings' Remove Windows mode: Windows and everything in it go,
        with its Start entries, file types and saved sign-in, and Windows
        mode is back to not set up, ready to set up again. The service asks
        for an administrator's password and does the deleting; documents
        are in the person's own folders, which Windows only borrowed."""
        if self.removing:
            return REMOVING
        self.removing = True
        self.changed()
        self.queue = []
        self.conn.close()
        self.desktop.close()
        self.end_preparing()
        self.hide_starting()

        def removed(reply):
            if reply is None or isinstance(reply, GLib.Error):
                self.removing = False
                self.refresh_host()
                defer.done("Windows mode's background service didn't answer. Restart the PC, then try again.")
                return
            if reply:
                self.removing = False
                self.refresh_host()
                defer.done(reply)
                return
            self.forget_windows()
            self.removing = False
            self.refresh_host(lambda: defer.done(""))
        # The administrator's password is asked for first, and may take a while.
        self.call("Remove", None, None, removed, timeout=1800)
        return defer

    def forget_windows(self):
        """What this person's side kept about the Windows that was removed."""
        account = self.host.get("account", "wild")
        try:
            launchers.remove_all()
        except OSError as e:
            log("Start entries:", e)
        self.registry = {}
        self.windows = []
        self.launches = {}
        self.last_launch = None
        self.last_app = None
        self.history = {}
        self.wake_times = {}
        self.installers = {}
        self.installed_since_refresh = False
        self.refreshed = False
        for name in ("history.json", "wake-times.json", "launches.jsonl", "certificate-for.json"):
            try:
                os.remove(os.path.join(config.user_data_dir(), name))
            except OSError:
                pass
        if self.host.get("guest_address"):
            rdp.forget_certificate(self.host["guest_address"], self.host.get("rdp_port", 3389))
        dbusutil.in_thread(self.keyring.forget, account)
        log("Windows was removed; Windows mode is ready to set up again")

    def open_connection(self, creds, wake):
        pending, self.queue = self.queue, []
        self.waking = False
        if not pending:
            return
        if not creds.get("password"):
            for item in pending:
                item[5]("Windows mode couldn't sign in to Windows. Restart the PC, then try again.")
            return
        path, args, name, app_id, started, done = pending[0]
        info = {"from": wake.get("from"), "wake_seconds": wake.get("seconds"), "via": "new connection"}
        if isinstance(wake.get("stages"), dict):
            info["wake_stages"] = wake["stages"]
        self.start_timing(app_id, started, info)
        self.mark(app_id, "connection_started")
        rdp_args = rdp.arguments(self.host.get("guest_address"), self.host.get("rdp_port", 3389), creds["account"], creds.get("domain", ""),
                                 name, path, args, drive=self.drive_folder())
        try:
            self.conn.start(rdp_args, creds["password"])
        except OSError as e:
            done(f"Windows mode couldn't open {name}. ({e.strerror})")
            return
        done("")
        for item in pending[1:]:
            self.launch_in_session(*item, wake=wake)
        if not self.refreshed:
            self.refreshed = True
            GLib.timeout_add_seconds(30, lambda: (self.m_refresh(None), False)[1])

    # D-Bus methods.
    def status(self):
        h = self.host
        setup = h.get("setup") or {}
        state = {"NotInstalled": "none", "SettingUp": "setup", "Error": "failed"}.get(h.get("state"), "ready" if h else "")
        if setup.get("running"):
            state = "failed" if setup.get("failed") else "setup"
        elif setup and setup.get("stage") != "done" and state == "ready":
            # A virtual PC is there but setup never finished with it (setup
            # was stopped part way): not ready, and the wizard offers to
            # start over.
            state = "failed"
        open_ids = {self.app_for(w) for w in self.windows}
        options = dict(setup.get("options") or {})
        # In Settings' words (board 9.8).
        options["whenIdle"] = {"keep": "memory", "save": "disk"}.get(h.get("when_idle"), h.get("when_idle"))
        return {
            "state": state,
            # Board 9.5's progress while setting up (setup.py).
            "stages": setup.get("stages") or [],
            "percent": setup.get("percent", 0),
            "left": setup.get("left", ""),
            "paused": bool(setup.get("paused")),
            "problem": setup.get("problem"),
            "notice": setup.get("notice"),
            "licence": setup.get("licence") or {},
            "options": options,
            "apps": [{"id": i, "name": a["name"], "icon": launchers.PREFIX + i, "open": i in open_ids}
                        for i, a in sorted(self.registry.items(), key=lambda kv: kv[1]["name"].lower())],
            "lifecycle": h.get("state"),
            "diskBytes": h.get("disk_bytes") or 0,
            "memoryPending": bool(h.get("memory_pending")),
            "vm": h.get("vm"),
            "connection": self.conn.open,
            "windows": len(self.windows),
            "openFiles": bool(self.options.get("open_files")),
            "idleIn": h.get("idle_in"),
            "lastLaunch": self.last_launch,
            "lastHostAction": h.get("last"),
            # For the taskbar's Windows mode panel.
            "starting": self.waking or self.starting is not None,
            "openApps": launchers.open_windows(self.windows, self.registry),
            "lastApp": self.app_summary(launchers.last_app(self.registry, self.last_app, self.history)),
            "progressRequests": self.progress_requests,
            "removing": self.removing,
            # Settings' Microsoft account activation: starting, open, ending
            # or "", and how the last one went ({activated, message}).
            "activation": {"state": self.activation or "", "result": self.activation_result},
        }

    def app_summary(self, app_id):
        app = self.registry.get(app_id)
        return {"id": app_id, "name": app["name"]} if app else None

    def m_status(self, sender):
        return json.dumps(self.status())

    def m_launch(self, sender, app_id, files, defer):
        app = self.registry.get(app_id)
        if not app:
            return "That Windows app isn't installed any more."
        self.count_launch(app_id)
        guest, _ = self.guest_paths(files)
        if any(g is None for g in guest):
            return "Windows apps can only open files in your shared folders. Move it to Documents, then try again."
        args = split_arguments(app.get("arguments", "")) + guest
        self.launch(app["target"], args, app["name"], app_id, defer.done)
        return defer

    def m_launch_program(self, sender, path, args, defer):
        app_id = launchers.app_id_for_image(path, self.registry)
        self.launch(path, list(args), launchers.exe_stem(path) or "Windows app", app_id, defer.done)
        return defer

    def m_uninstall_app(self, sender, app_id, defer):
        """Settings' Uninstall for a Windows app. A Store app is removed
        inside Windows at once; any other program's own uninstaller opens
        in a window like an installer, and its Start entry goes once that
        window closes. The answer is JSON: "removed" (gone already),
        "opened" (its uninstaller is on screen) or a message."""
        def answer(state, message=""):
            defer.done(json.dumps({"state": state, "message": message}))

        if self.removing:
            return json.dumps({"state": "failed", "message": REMOVING})
        app = self.registry.get(app_id)
        if not app:
            return json.dumps({"state": "removed", "message": ""})
        name = app.get("name") or "That app"
        deadline = time.monotonic() + SESSION_WAIT

        def found(reply):
            kind = reply.get("type")
            if kind == "uninstalled" or reply.get("error") == "not-installed":
                self.m_refresh(None)
                answer("removed")
            elif kind == "uninstaller" and reply.get("program"):
                program = reply["program"]
                self.installers[launchers.exe_stem(program)] = False
                self.installed_since_refresh = True
                self.launch(program, split_arguments(reply.get("arguments", "")), name, "installer",
                            lambda message: answer("failed", message) if message else answer("opened"))
            elif reply.get("error") in ("not-connected", "timeout") and time.monotonic() < deadline:
                # Windows has only just started: wild-guest connects soon.
                GLib.timeout_add_seconds(2, lambda: (ask(), False)[1])
            elif reply.get("error") == "no-uninstaller":
                answer("failed", f"Windows doesn't list an uninstaller for {name}. Open the Windows desktop from Windows mode "
                       "in Settings and remove it in Windows' own Settings.")
            else:
                log("uninstall failed:", reply)
                answer("failed", f"{name} couldn't be uninstalled. Try again.")

        def ask():
            self.call_json("Uninstall", (app["target"],), "(s)", found, timeout=210)

        def woke(reply):
            if not reply.get("ok"):
                answer("failed", reply.get("message") or "Windows mode couldn't start. Try again, or restart the PC.")
                return
            ask()

        self.call_json("Wake", done=woke)
        return defer

    def m_run_installer(self, sender, host_path, defer):
        """App Bridge and wild-run hand Windows installers here: the file is
        copied into the shared Downloads folder unless Windows can already
        see it, then run in Windows mode."""
        if not os.path.isfile(host_path):
            return "That file isn't there any more."
        guest, mapping = self.guest_paths([host_path])
        if guest[0] is None:
            downloads = mapping.get("Downloads")
            if not downloads:
                return "Windows mode can't see your Downloads folder. Check the shared folders in Settings."
            try:
                copy = paths.copy_into(host_path, downloads)
            except OSError:
                return "The installer couldn't be copied to Downloads. Check there's space on this PC."
            guest = [paths.to_guest(copy, mapping, self.host.get("drive", "Z"))]
        name = os.path.basename(host_path)
        self.installers["msiexec" if name.lower().endswith(".msi") else launchers.exe_stem(name)] = False
        self.installed_since_refresh = True
        if name.lower().endswith(".msi"):
            self.launch(r"C:\Windows\System32\msiexec.exe", ["/i", guest[0]], name, "installer", defer.done)
        else:
            self.launch(guest[0], [], name, "installer", defer.done)
        return defer

    def m_outside(self, sender, files):
        guest, _ = self.guest_paths(files)
        return [f for f, g in zip(files, guest) if g is None]

    def m_copy(self, sender, files):
        _, mapping = self.guest_paths(files)
        docs = mapping.get("Documents")
        out = []
        for f in files:
            try:
                out.append(paths.copy_into(f, docs) if docs else "")
            except OSError:
                out.append("")
        return out

    def m_refresh(self, sender, defer=None):
        def got(reply):
            apps = reply.get("apps")
            if not isinstance(apps, list):
                msg = "Windows mode couldn't list its apps. Try again when Windows mode is running."
            else:
                self.registry = launchers.write_all(apps)
                launchers.set_defaults(self.registry, bool(self.options.get("open_files")))
                self.push_warm()
                msg = ""
                self.changed()
            if defer:
                defer.done(msg)
        self.call_json("Apps", (True,), "(b)", got, timeout=180)
        return defer

    # Setting up (boards 9.1 to 9.7). The wizard asks here; the system
    # service does the work and owns Windows mode afterwards.
    def m_start_setup(self, sender, request, defer):
        try:
            req = json.loads(request)
        except ValueError:
            return "That request couldn't be read."
        self.setup_open_files = bool((req.get("options") or {}).get("openFiles", True))

        def started(reply):
            # From first run, which goes straight to the desktop (board 9.11):
            # the notification says what is happening and where to follow it.
            if reply == "" and req.get("background"):
                office = bool((req.get("options") or {}).get("office"))
                self.refresh_host(lambda: self.notify_setting_up(office))
            else:
                self.refresh_host()
            defer.done(reply if isinstance(reply, str) else "Windows mode's background service didn't answer.")
        self.call("SetupStart", (json.dumps(req),), "(s)", started, timeout=120)
        return defer

    def setup_control(self, action, argument, defer):
        def done(reply):
            # Read again before answering, so whatever asks next (the wizard
            # after Start over) sees the state the action left. Asked without
            # waiting, like every call to the service: the agent never stops
            # answering while the service is busy.
            self.refresh_host(lambda: defer.done(reply if isinstance(reply, str)
                                                 else "Windows mode's background service didn't answer."))
        self.call("SetupControl", (action, argument), "(ss)", done, timeout=900)
        return defer

    def m_open_download_page(self, sender):
        try:
            subprocess.Popen(["xdg-open", msdl.PAGE], start_new_session=True,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            return "A web browser couldn't be opened."
        return ""

    def m_firmware(self, sender, defer):
        self.call("Firmware", None, None, lambda r: defer.done(r if isinstance(r, str) else "{}"), timeout=120)
        return defer

    # Pause, Save to disk, Resume, Start, Restart and Shut down (the taskbar's
    # Windows mode panel), and the question before Windows or Wild OS shuts
    # down with work open (board 9.9).
    def m_power(self, sender, action, defer):
        """"" when done, or a sentence to show. Restart and Shut down ask
        first when an app has unsaved changes, or might (an app whose state
        Windows can't tell): Go back leaves everything as it is; for Shut
        down, Save to disk instead keeps the work and frees the memory."""
        if action not in ("pause", "save", "resume", "start", "restart", "shutdown"):
            return "That isn't something Windows mode can do."

        def answered(choice):
            if choice == "back":
                defer.done("")
            elif choice == "error":
                defer.done("Windows mode couldn't check for unsaved work, so Windows was left as it is.")
            else:
                self.power(lambda message: defer.done(message), "save" if choice == "keep" else action)

        def checked(docs):
            if not documents.needs_asking(docs):
                self.power(defer.done, action)
            else:
                self.ask("windows-" + action, docs, answered)

        if action in ("restart", "shutdown"):
            self.documents_now(checked)
        else:
            self.power(defer.done, action)
        return defer

    def power(self, done, action):
        failed = {"pause": "Windows couldn't be paused.", "save": "Windows couldn't be saved to disk.",
                  "resume": "Windows couldn't be resumed.", "start": "Windows couldn't start.",
                  "restart": "Windows couldn't restart.", "shutdown": "Windows couldn't shut down."}

        def finished(reply):
            self.refresh_host()
            if not reply.get("ok"):
                log(action, "failed:", reply.get("error"))
                done(reply.get("message") or failed.get(action, "That didn't work.") + " Try again, or restart the PC.")
                return
            if action in ("resume", "start", "restart"):
                self.reopen_windows()
            if action in ("start", "restart"):
                self.prepare_session()
            if reply.get("forced"):
                done("Windows didn't finish shutting down, so Windows mode switched it off.")
                return
            done("")

        # The connection ends first: it would otherwise reconnect to the
        # restored or restarted Windows by itself (end_connection_if_gone).
        # So does the whole desktop's, and one stuck after its window closed
        # goes with it, so a restart of Windows mode clears it.
        if action in ("save", "restart", "shutdown"):
            self.conn.close()
            self.desktop.close()
            self.end_preparing()
        self.call_json("Power", (action,), "(s)", finished, timeout=POWER_TIMEOUT)

    def reopen_windows(self, within=90):
        """Windows' own windows back on the desktop after a resume: through
        the open connection if it lived through the pause, or through a new
        one that asks for no app. Waits for wild-guest's window list, which
        follows a restore by a few seconds."""
        self.reopen_until = time.monotonic() + within
        self.reopen_if_needed()

    def reopen_if_needed(self):
        if time.monotonic() > getattr(self, "reopen_until", 0) or self.conn.running or self.waking:
            return
        if not any(self.app_for(w) != "" for w in self.windows):
            return
        self.reopen_until = 0

        def go(creds):
            if not creds.get("password") or self.conn.running:
                return
            args = rdp.resume_arguments(self.host.get("guest_address"), self.host.get("rdp_port", 3389), creds["account"],
                                        creds.get("domain", ""), drive=self.drive_folder())
            try:
                self.conn.start(args, creds["password"])
                log("Windows apps open in Windows are back on the desktop")
            except OSError as e:
                log("couldn't bring Windows apps back:", e)
        self.credentials(go)

    def reopen_kept(self):
        """Windows apps kept as Wild OS last shut down, signed out or
        restarted: Windows is restored and its windows come back."""
        if not self.host.get("kept") or self.waking:
            return
        log("bringing back the Windows apps kept when Wild OS last shut down")
        self.power(lambda message: message and log("reopening kept work:", message), "resume")

    def windows_open(self):
        """Whether the session's end should ask (board 9.9): any Windows app
        open that could hold work."""
        return bool(documents.documents(self.windows, None, self.app_name))

    def app_name(self, image):
        app = self.registry.get(launchers.app_id_for_image(image, self.registry) or "")
        return app["name"] if app else None

    def documents_now(self, done, timeout=20, unanswered=None):
        """What is open in Windows, with what is unsaved (documents.py).
        unanswered(), when given, runs instead when the service doesn't
        answer in time."""
        def got(reply):
            if unanswered and reply.get("unanswered"):
                unanswered()
                return
            windows = reply.get("windows") if isinstance(reply.get("windows"), list) else self.windows
            done(documents.documents(windows, reply.get("office"), self.app_name))
        self.call_json("Documents", done=got, timeout=timeout)

    def m_documents(self, sender, defer):
        self.documents_now(lambda docs: defer.done(json.dumps(docs)))
        return defer

    def ask(self, action, docs, answered):
        """Board 9.9 through wildos-dialogs; answered(keep, discard, back,
        or error when it couldn't be asked)."""
        payload = json.dumps([{"app": d["app"], "document": d["document"], "unsaved": d["unsaved"]} for d in docs])

        def finished(conn, res):
            try:
                choice = conn.call_finish(res).unpack()[0]
            except GLib.Error as e:
                log("the question about open Windows apps couldn't be asked:", e.message)
                choice = "error"
            answered(choice)

        self.session.call("org.wildos.SystemDialogs", "/Dialogs", "org.wildos.SystemDialogs", "AskWindowsShutdown",
                          GLib.Variant("(ss)", (action, payload)), GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE,
                          ASK_TIMEOUT * 1000, None, finished)

    def ask_before_leaving(self, finish):
        """Wild OS is shutting down, restarting or signing out with Windows
        apps open: board 9.9. Keep my work saves Windows to disk as it is
        (the next sign-in brings it back), Close without saving shuts it
        down, Go back calls the session's end off. finish(cancel)."""
        action = self.leaving.current()
        words = {"restart": "restart", "signout": "sign out"}.get(action, "shut down")

        def kept(reply):
            if reply.get("ok"):
                finish(False)
                return
            if reply.get("unanswered"):
                # Never hold a sign-out for a service that doesn't answer;
                # one still saving carries on, and the PC's own shutdown
                # waits for it.
                log("Windows mode's service didn't answer in time; letting the session end:", reply.get("error"))
                finish(False)
                return
            log("keeping Windows apps' work failed:", reply.get("error"))
            self.notify(f"Windows mode couldn't keep your Windows apps, so Wild OS didn't {words}. "
                        "Save your work in them, then try again.")
            finish(True)
            self.power(lambda message: None, "resume")

        def answered(choice):
            if choice == "back":
                finish(True)
            elif choice == "discard":
                self.conn.close()
                self.call_json("Power", ("shutdown",), "(s)", lambda reply: None, timeout=POWER_TIMEOUT)
                finish(False)
            else:
                # Keep, the default after 60 seconds, and the safe choice
                # when the question couldn't be asked at all.
                self.conn.close()
                self.call_json("Power", ("keep",), "(s)", kept, timeout=LEAVE_SAVE_TIMEOUT)

        def checked(docs):
            if docs:
                self.ask(action, docs, answered)
            else:
                finish(False)

        def no_service():
            # Nothing can be kept without it, so nothing is promised.
            log("Windows mode's service didn't answer; not asking about Windows apps")
            finish(False)
        self.documents_now(checked, timeout=8, unanswered=no_service)

    def m_show_setup_progress(self, sender):
        self.progress_requests += 1
        self.changed()

    def notify_actions(self, title, body, actions, icon="wildos-windows-app"):
        """A notification with buttons: actions is [(key, words, then)], the
        first button left of the second, as Plasma lays them out."""
        flat = [part for key, words, _ in actions for part in (key, words)]

        def sent(conn, res):
            try:
                nid = conn.call_finish(res).unpack()[0]
            except GLib.Error as e:
                log("notification:", e.message)
                return
            self.notification_actions[nid] = {key: then for key, _, then in actions}
            log("told:", title)

        self.session.call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
                          GLib.Variant("(susssasa{sv}i)", ("Windows mode", 0, icon, title, body, flat,
                                                          {"desktop-entry": GLib.Variant("s", "org.wildos.winmode")}, -1)),
                          GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 5000, None, sent)

    def notification_signal(self, conn, sender, path, iface, signal, params):
        if signal == "ActionInvoked":
            nid, key = params.unpack()
            then = self.notification_actions.pop(nid, {}).get(key)
            if then:
                then()
        elif signal == "NotificationClosed":
            self.notification_actions.pop(params.unpack()[0], None)

    def notify_setting_up(self, office):
        """Board 9.11. The time is the service's own estimate, which is
        longer than the boards' 25 minutes until setups have been timed."""
        left = str((self.host.get("setup") or {}).get("left") or "")
        when = ("in " + left[0].lower() + left[1:].removesuffix(" left")) if left.startswith("About ") else "soon"
        what = "Word, Excel, PowerPoint and Outlook" if office else "Windows apps"
        self.notify_actions("Setting up Windows mode",
                            f"Carry on using Wild OS. {what} will be ready {when}. Follow along from Windows mode on the taskbar.",
                            [("show-progress", "Show progress", lambda: self.m_show_setup_progress(None)),
                             ("ok", "OK", lambda: None)])

    def notify_ready(self, office):
        """Board 9.14: once, when everything is installed."""
        def show_start():
            self.session.call("org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell", "activateLauncherMenu",
                              None, None, Gio.DBusCallFlags.NONE, 5000, None, None)

        def open_word():
            self.m_launch(None, "word", [], Answer(lambda message: message and self.notify(message)))

        apps = [(i, n) for i, n in OFFICE_APPS if i in self.registry] if office else []
        if apps:
            # The board's row of app icons waits on the notification popup,
            # which shows no pictures in the text.
            names = [n for _, n in apps]
            listed = names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]
            body = (f"{listed} {'is' if len(names) == 1 else 'are'} in Start. "
                    f"Open {'it' if len(names) == 1 else 'one'} and sign in once with your Microsoft account.")
        else:
            body = "Windows apps you install appear in Start."
        actions = [("show-start", "Show in Start", show_start)]
        if any(i == "word" for i, _ in apps):
            actions.append(("open-word", "Open Word", open_word))
        self.notify_actions("Windows mode is ready", body, actions)

    def notify(self, text):
        self.session.call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
                          GLib.Variant("(susssasa{sv}i)", ("Windows mode", 0, "wildos-windows-app", "Windows mode", text, [], {}, -1)),
                          None, Gio.DBusCallFlags.NONE, 5000, None, None)

    def setup_finished(self):
        """Windows is ready: its apps get Start entries, files open in them
        if the person chose that in setup, and one notification says so. The
        choice is read back from the setup record, so it holds when the
        agent was started again (a restart) part way through setup."""
        options = (self.host.get("setup") or {}).get("options") or {}
        self.options["open_files"] = bool(options.get("openFiles", getattr(self, "setup_open_files", self.options.get("open_files", True))))
        try:
            config.write_json(config.user_config_file(), self.options)
        except OSError:
            pass

        def refreshed(message):
            self.notify_ready(bool(options.get("office")))
        GLib.timeout_add_seconds(5, lambda: (self.m_refresh(None, Answer(refreshed)), False)[1])

    def m_set_option(self, sender, key, value, defer):
        if key == "openFiles":
            self.options["open_files"] = value in ("true", "1", "yes")
            config.write_json(config.user_config_file(), self.options)
            launchers.set_defaults(self.registry, self.options["open_files"])
            self.changed()
            return ""
        if key == "whenIdle":
            mapped = {"memory": "keep", "disk": "save"}.get(value, value)
            self.call("SetOption", ("when_idle", mapped), "(ss)",
                      lambda r: defer.done(r if isinstance(r, str) else "That setting couldn't be changed."))
            return defer
        if key == "memoryGb":
            # The system service changes the virtual PC itself.
            self.call("SetOption", ("memoryGb", value), "(ss)",
                      lambda r: defer.done(r if isinstance(r, str) else "That setting couldn't be changed."))
            return defer
        return "That setting isn't available."

    # Theme.
    def watch_theme(self):
        self.theme_timer = None
        f = Gio.File.new_for_path(theme.kdeglobals_path())
        try:
            self.theme_monitor = f.monitor_file(Gio.FileMonitorFlags.WATCH_MOVES, None)
            self.theme_monitor.connect("changed", lambda *a: self.theme_soon())
        except GLib.Error:
            self.theme_monitor = None
        self.push_theme()

    def theme_soon(self):
        if self.theme_timer:
            GLib.source_remove(self.theme_timer)
        self.theme_timer = GLib.timeout_add(800, self.push_theme)

    def push_theme(self):
        self.theme_timer = None
        self.call("SetTheme", (json.dumps(theme.read()),), "(s)")
        return False


def main():
    loop = GLib.MainLoop()
    session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    system = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    # The object exists before the name does: D-Bus delivers the call that
    # started the agent as soon as the name is taken, and it must find it.
    agent = Agent(session, system)
    Gio.bus_own_name_on_connection(session, config.BUS_NAME, Gio.BusNameOwnerFlags.NONE,
                                   lambda conn, name: log(f"wildos-winmode agent: serving {name}"),
                                   lambda conn, name: loop.quit())
    GLib.unix_signal_add(GLib.PRIORITY_HIGH, 15, loop.quit)
    loop.run()
    # FreeRDP runs in a process group of its own; left behind, it would keep
    # its windows on screen and hold up systemd stopping the agent.
    agent.conn.stop_now()
    agent.desktop.stop_now()
    agent.activation_desktop.stop_now()
    # Held until here, so it lives as long as the loop.
    del agent
    return 0
