"""The one RemoteApp connection (brief 5.5, phase 1 risk 10.4).

Windows Pro gives each account one remote session, and a second connection
takes it over, so all Windows apps share one FreeRDP 3 connection. It is
opened with the first app; every later app is started inside the session
by wild-guest. Its first program is wild-guest's "--start", which starts
the app and ends, because FreeRDP's /app option splits on commas and
cannot carry a file name that has one.

The client is Wild OS's own, wildos-rdp: native Wayland windows at the
screen's own scale, and the app's window on screen with the loading W the
moment the app is opened (boards 9.11 to 9.13). The whole Windows desktop
opens in it too (--desktop). It takes FreeRDP's arguments, one per line on
standard input with the password last.
"""

import base64
import json
import os
import signal
import subprocess

from gi.repository import Gio, GLib

from . import config

NATIVE_CLIENT = "/usr/libexec/wildos/wildos-rdp"
REMOTE_APP_BUS = ("org.wildos.RemoteApp", "/org/wildos/RemoteApp", "org.wildos.RemoteApp")
GUEST_EXE = r"C:\Program Files\Wild OS\wild-guest.exe"
AUTH_FAILED = ("LOGON_FAILURE", "AUTHENTICATION_FAILED", "ERRCONNECT_LOGON_FAILURE", "STATUS_LOGON_FAILURE",
               "ERRCONNECT_PASSWORD")


def start_argument(path, args, cwd=None):
    msg = {"path": path, "args": list(args)}
    if cwd:
        msg["cwd"] = cwd
    if path:
        # A connection that reconnects by itself sends its first program
        # again; wild-guest starts each "once" a single time, so an app
        # closed long ago doesn't open again after a pause.
        msg["once"] = os.urandom(8).hex()
    return base64.b64encode(json.dumps(msg, separators=(",", ":")).encode()).decode()


def plain(text):
    """Text that can only ever be one FreeRDP argument: one per line on
    standard input, so a line break in a file name would start a new one
    (/drive:all, say), and /app splits on commas."""
    return "".join(" " if ch < " " or ch in ",\x7f" else ch for ch in str(text))


def display_scale():
    """The scale Plasma draws X11 apps at, so Windows lays its apps out at
    the same size as the rest of the desktop. KWin scales XWayland itself
    and leaves the factor in kwinrc's [Xwayland] Scale."""
    import configparser
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        cfg.read(os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "kwinrc"))
        return max(1.0, min(5.0, float(cfg.get("Xwayland", "Scale", fallback="1"))))
    except (ValueError, configparser.Error):
        return 1.0


def scale_arguments(scale):
    """Windows' scale for the session (any whole percentage FreeRDP takes,
    100 to 500), and the nearest of the three device scales RDP has."""
    device = 180 if scale >= 1.6 else 140 if scale >= 1.2 else 100
    return [f"/scale-desktop:{round(scale * 100)}", f"/scale-device:{device}"]


def arguments(host, port, account, domain, name, path, args, cwd=None, drive=None):
    """FreeRDP's arguments, without the password, which goes after them on
    standard input. Every argument goes on standard input (/args-from:stdin),
    so nothing secret is on a command line. The path and arguments travel
    base64 encoded, so only the name, account and domain need cleaning."""
    app = f"program:{GUEST_EXE},name:{plain(name)},cmd:--start {start_argument(path, args, cwd)}"
    out = [f"/v:{host}:{port}", f"/u:{plain(account)}"]
    if domain:
        out.append(f"/d:{plain(domain)}")
    out += [
        # The guest's certificate is self-signed and only reached on the
        # isolated network: trust it the first time, refuse it if it changes.
        "/cert:tofu",
        "/sec:nla",
        # Kerberos cannot work without a domain controller; trying it first
        # costs time and logs errors (phase 1).
        "/auth-pkg-list:!kerberos",
        f"/app:{app}",
        "+clipboard",
        "/sound:sys:pulse",
        "+auto-reconnect",
        *drive_arguments(drive),
        *scale_arguments(display_scale()),
        "/log-level:WARN",
    ]
    return out


def prepare_arguments(host, port, account, domain, drive=None):
    """A connection that only signs in (wildos-rdp's --prepare-session): the
    session starts, then is left disconnected for the first app to take
    over. It asks Windows for no app, and never reconnects on its own, so it
    cannot take the session back from the connection that opens an app."""
    out = arguments(host, port, account, domain, "Windows mode", GUEST_EXE, [], drive=drive)
    return [a for a in out if a != "+auto-reconnect"]


def resume_arguments(host, port, account, domain, drive=None):
    """A connection that brings back the windows already open in Windows'
    session, after a save to disk or a lost connection: Remote Desktop
    shows every window of the session it reconnects to. It asks for no
    app (wild-guest's --start with nothing to start ends at once), so a
    reconnect later on starts nothing again either."""
    return arguments(host, port, account, domain, "Windows mode", "", [], drive=drive)


def prepare_session(args, password, log_path, done, native=NATIVE_CLIENT):
    """Runs the sign-in-only connection; done(ok) when it ends. Returns the
    process, so a launch that comes first can end it."""
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "wb") as log:
        proc = subprocess.Popen([native, "--prepare-session", "/args-from:stdin"], stdin=subprocess.PIPE, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
    proc.stdin.write(("\n".join(args + [f"/p:{password}"]) + "\n").encode())
    proc.stdin.close()
    GLib.child_watch_add(GLib.PRIORITY_DEFAULT, proc.pid, lambda pid, status: done(status == 0))
    return proc


def drive_arguments(drive):
    """The shared folders, as the redirected drive wild-guest maps to Z:."""
    return [f"/drive:wildos,{drive}"] if drive else []


def desktop_arguments(host, port, account, domain, drive=None, title="Windows desktop"):
    """The whole Windows desktop in a window of its own, for Settings'
    Windows desktop button. It takes the one session over, so the app
    windows go into it until it closes."""
    out = [f"/v:{host}:{port}", f"/u:{plain(account)}"]
    if domain:
        out.append(f"/d:{plain(domain)}")
    return out + [
        "/cert:tofu",
        "/sec:nla",
        "/auth-pkg-list:!kerberos",
        f"/title:{plain(title)}",
        "/dynamic-resolution",
        *drive_arguments(drive),
        "+clipboard",
        "/sound:sys:pulse",
        *scale_arguments(display_scale()),
        "/log-level:WARN",
    ]


def certificate_path(host, port, config_home=None):
    """Where FreeRDP keeps the certificate /cert:tofu trusted for a host."""
    base = config_home or os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "freerdp", "server", f"{str(host).lower()}_{int(port)}.pem")


def forget_certificate(host, port, config_home=None):
    """A new Windows (set up again, or removed) has a certificate of its
    own, and FreeRDP refuses a host whose certificate changed, so every
    connection to it would fail. Forgetting the old one lets the next
    connection trust the new one, as on the first."""
    try:
        os.remove(certificate_path(host, port, config_home))
        return True
    except OSError:
        return False


def loading_arguments(app_id, title, icon, reason="", program=""):
    """wildos-rdp's window for the app being opened, before the connection's
    own arguments arrive on standard input. Nothing here is secret: an app
    id, the app's name, its icon's name, why it may take a while and the
    program in Windows whose first window becomes the app's window. Each
    is one argument (no shell), kept to one line."""
    def line(text):
        return "".join(" " if ch < " " or ord(ch) == 127 else ch for ch in str(text))
    out = []
    for option, value in (("--app-id", app_id), ("--title", title), ("--icon", icon), ("--reason", reason),
                          ("--program", program)):
        if value:
            out += [option, line(value)]
    return out


class Connection:
    """One wildos-rdp process: the RemoteApp connection, or with desktop
    the whole Windows desktop in one window. on_change(open) runs when it
    opens or ends; on_auth_failed runs when Windows refused the password.

    For apps, wildos-rdp can start before Windows is awake (spawn()), with
    the app's window already on screen, and take the connection's arguments
    when they are known (start()). Until then the connection does not count
    as open."""

    # Where wildos-rdp is; tests point it elsewhere.
    native_client = NATIVE_CLIENT

    def __init__(self, on_change, on_auth_failed, log_path, desktop=False):
        self.proc = None
        self.on_change = on_change
        self.on_auth_failed = on_auth_failed
        self.log_path = log_path
        self.started = None
        self.desktop = desktop
        self.waiting = False

    @property
    def running(self):
        return self.proc is not None and self.proc.poll() is None

    @property
    def open(self):
        return self.running and not self.waiting

    def _popen(self, cmd, env):
        os.makedirs(os.path.dirname(self.log_path), exist_ok=True)
        # The connection before this one is kept beside it: when a launch
        # fails, the client that ended is usually the one that says why.
        if os.path.exists(self.log_path):
            os.replace(self.log_path, self.log_path + ".1")
        log = open(self.log_path, "wb")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, env=env,
                                     start_new_session=True)
        log.close()
        GLib.child_watch_add(GLib.PRIORITY_DEFAULT, self.proc.pid, self._ended, self.proc)

    def spawn(self, app_id="", title="", icon="", reason="", program="", env=None):
        """Starts wildos-rdp now, the app's window up at once, the
        connection's arguments to follow. False for the desktop, or when a
        client is already running."""
        if self.running or self.desktop:
            return False
        self.waiting = True
        self._popen([self.native_client, *loading_arguments(app_id, title, icon, reason, program), "/args-from:stdin"], env)
        return True

    def abandon(self):
        """The launch that spawned the client failed before it connected:
        its input closes with nothing on it, and it closes its window and
        ends."""
        if self.waiting and self.proc is not None:
            self.waiting = False
            try:
                self.proc.stdin.close()
            except OSError:
                pass

    def start(self, args, password, env=None):
        if not (self.waiting and self.running):
            self.waiting = False
            self._popen([self.native_client, *(["--desktop"] if self.desktop else []), "/args-from:stdin"], env)
        self.waiting = False
        self.proc.stdin.write(("\n".join(args + [f"/p:{password}"]) + "\n").encode())
        self.proc.stdin.close()
        self.started = GLib.get_monotonic_time()
        self.on_change(True)

    def _ended(self, pid, status, proc):
        if proc is not self.proc:
            return
        was_open = not self.waiting
        self.proc = None
        self.waiting = False
        if not was_open:
            return
        failed = False
        try:
            with open(self.log_path, "rb") as f:
                f.seek(max(0, os.path.getsize(self.log_path) - 8192))
                tail = f.read().decode("utf-8", "replace")
            failed = any(word in tail for word in AUTH_FAILED)
        except OSError:
            pass
        self.on_change(False)
        if failed:
            self.on_auth_failed()

    def stop_now(self):
        """For the agent's own exit, when no main loop is left to finish
        close()'s work."""
        if self.running:
            proc = self.proc
            for sig, wait in ((signal.SIGTERM, 3), (signal.SIGKILL, 2)):
                try:
                    os.killpg(proc.pid, sig)
                    proc.wait(wait)
                    return
                except subprocess.TimeoutExpired:
                    continue
                except OSError:
                    return

    def close(self):
        if self.running:
            proc = self.proc
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except OSError:
                return
            # FreeRDP has been seen to catch SIGTERM and never exit, leaving
            # its windows frozen on screen; three seconds is plenty to say
            # goodbye to Windows.
            def kill():
                if proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except OSError:
                        pass
                return False
            GLib.timeout_add_seconds(3, kill)


class RemoteApp:
    """wildos-rdp's org.wildos.RemoteApp while a connection is open: the
    app's window for later launches, news for it, and each window's taskbar
    group. Calls never wait, and do nothing when no wildos-rdp is on the
    bus."""

    def __init__(self, bus, on_window=None, on_window_gone=None, on_loading=None, on_details=None, on_close_stalled=None,
                 on_retry=None):
        self.bus = bus
        name, path, iface = REMOTE_APP_BUS
        handlers = {"WindowAdded": on_window, "WindowRemoved": on_window_gone, "LoadingFinished": on_loading,
                    "DetailsRequested": on_details, "CloseStalled": on_close_stalled, "RetryRequested": on_retry}

        def signal(conn, sender, obj, interface, member, params):
            handler = handlers.get(member)
            if handler:
                handler(*params.unpack())

        self.subscription = bus.signal_subscribe(name, iface, None, path, None, Gio.DBusSignalFlags.NONE, signal)

    def call(self, method, signature, args, done=None):
        name, path, iface = REMOTE_APP_BUS

        def finished(conn, res):
            try:
                value = conn.call_finish(res).unpack()
            except GLib.Error:
                value = None
            if done:
                done(value[0] if value else None)

        self.bus.call(name, path, iface, method, GLib.Variant(signature, args) if signature else None, None,
                      Gio.DBusCallFlags.NO_AUTO_START, 2000, None, finished)

    def prepare(self, app_id, title, icon, program="", delay_ms=0, done=None):
        self.call("Prepare", "(ssssu)", (app_id or "", title or "", icon or "", program or "", delay_ms), done)

    def update(self, token, stage="", reason="", percent=-1, progress=""):
        """Token 0 is the loading window the client was started with (spawn),
        which the agent has no token for."""
        self.call("Update", "(ussis)", (token, stage, reason, percent, progress))

    def cancel(self, token):
        if token:
            self.call("Cancel", "(u)", (token,))

    def set_window_app(self, window_id, app_id):
        self.call("SetWindowApp", "(ts)", (window_id, app_id))
