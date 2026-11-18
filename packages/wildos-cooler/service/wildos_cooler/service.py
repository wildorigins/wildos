"""wildos-coolerd: the one program that talks to the cooler.

It runs as a system service so that the pump and fan curves are put back at
start-up, before anyone signs in: the cooler only keeps them while it has
power. Everything else (the app, the program that draws the screen faces)
asks for what it wants over the system bus, and only the person at the
screen is allowed to (polkit, io.wildos.cooler.control).

Frames for the screen do not go over the bus. OpenFrames hands back one end
of a socket; whoever draws the faces writes whole frames into it and this
side sends the newest to the cooler as fast as the cooler takes them,
dropping any it has no time for. When the socket closes, the cooler goes
back to its own liquid temperature screen rather than freezing on a face
whose readings are no longer true.
"""

from __future__ import annotations

import json
import logging
import os
import signal
import socket
import threading
import time

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

from . import kraken

log = logging.getLogger("wildos-cooler")

BUS_NAME = "io.wildos.Cooler"
PATH = "/io/wildos/Cooler"
IFACE = "io.wildos.Cooler1"
ACTION = "io.wildos.cooler.control"
STATE = os.environ.get("STATE_DIRECTORY", "/var/lib/wildos-cooler") + "/state.json"
# Exists, as a file that can be run, only while a cooler is connected and
# answering. Cooler's entry in Start names it as TryExec, so the app is only
# offered on a PC that has such a cooler.
MARKER = os.environ.get("RUNTIME_DIRECTORY", "/run/wildos-cooler") + "/present"
# The name udev gives a supported cooler's USB device (70-wildos-cooler.rules).
DEVICE = "/dev/wildos-cooler"
# Flat out, for SetFullSpeed.
FULL_SPEED = [(20, 100), (59, 100)]
READ_EVERY_MS = 1000
# Started by the bus on a PC with no cooler, it leaves again once nobody has
# asked it anything for this long.
LEAVE_IDLE_AFTER = 120
# A fan header with nothing on it reports no speed however hard it is
# driven. This many readings in a row say so.
FAN_ABSENT_AFTER = 5


class NotAllowed(dbus.DBusException):
    _dbus_error_name = "io.wildos.Cooler.NotAllowed"


class NoCooler(dbus.DBusException):
    _dbus_error_name = "io.wildos.Cooler.NoCooler"


class Failed(dbus.DBusException):
    _dbus_error_name = "io.wildos.Cooler.Failed"


def _load_state() -> dict:
    try:
        with open(STATE, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def _mark(present: bool):
    try:
        if present:
            os.makedirs(os.path.dirname(MARKER), exist_ok=True)
            # Empty: TryExec only asks whether it is there and can be run.
            with open(MARKER, "w", encoding="utf-8"):
                pass
            os.chmod(MARKER, 0o755)
        elif os.path.exists(MARKER):
            os.unlink(MARKER)
    except OSError:
        log.exception("marking whether a cooler is connected")


def _save_state(state: dict):
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    temporary = STATE + ".new"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(state, handle)
    os.replace(temporary, STATE)


class Service(dbus.service.Object):
    def __init__(self, bus, find=kraken.find, check_caller=True, leave=None):
        super().__init__(bus, PATH)
        self._bus = bus
        self._find = find
        self._check_caller = check_caller
        # Called when there is nothing left to do here: the cooler was
        # unplugged (udev starts the service afresh when it is back, which
        # also puts its curves back), or there never was one and nobody is
        # asking. Tests leave it out and look again by hand.
        self._leave = leave
        self._asked = time.monotonic()
        self.lost = False
        # One user of the cooler at a time: readings, settings and frames
        # share its USB conversation.
        self._lock = threading.Lock()
        self._cooler = None
        self._readings = None
        self._silent_fan = 0
        self._state = _load_state()
        self._frames = None
        # The bus name of whoever asked for full speed, while it is on.
        self._full_speed = None
        bus.add_signal_receiver(
            self._owner_changed,
            "NameOwnerChanged",
            "org.freedesktop.DBus",
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
        )
        self._look()
        GLib.timeout_add(READ_EVERY_MS, self._read)

    # The cooler

    def _look(self):
        with self._lock:
            if self._cooler is not None:
                self._cooler.close()
                self._cooler = None
            try:
                self._cooler = self._find()
            except Exception:
                log.exception("looking for a cooler")
                self._cooler = None
            self._full_speed = None
            _mark(self._cooler is not None)
            if self._cooler is None:
                return
            self.lost = False
            log.info("%s, firmware %s, screen %d", self._cooler.model, self._cooler.firmware, self._cooler.size)
            for channel, points in self._state.get("curves", {}).items():
                try:
                    self._cooler.set_curve(channel, [tuple(p) for p in points])
                except Exception:
                    log.exception("putting back the %s curve", channel)

    def _lost(self):
        log.warning("the cooler stopped answering")
        try:
            self._cooler.close()
        finally:
            self._cooler = None
            self._readings = None
            self._full_speed = None
            self.lost = True
            _mark(False)
            if self._leave is not None:
                GLib.idle_add(self._leave)

    def _read(self):
        changed = False
        with self._lock:
            if self._cooler is not None:
                try:
                    self._readings = self._cooler.readings()
                    quiet = self._readings.fan_rpm == 0 and self._readings.fan_duty > 0
                    self._silent_fan = self._silent_fan + 1 if quiet else 0
                    changed = True
                except Exception:
                    log.exception("reading the cooler")
                    self._lost()
                    changed = True
            elif self._leave is not None and time.monotonic() - self._asked > LEAVE_IDLE_AFTER:
                GLib.idle_add(self._leave)
        if changed:
            self.Changed()
        return True

    def _need(self):
        if self._cooler is None:
            raise NoCooler("no supported cooler is connected")
        return self._cooler

    def _allow(self, sender):
        if not self._check_caller:
            return
        authority = dbus.Interface(
            self._bus.get_object("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority"),
            "org.freedesktop.PolicyKit1.Authority",
        )
        subject = ("system-bus-name", {"name": dbus.String(sender, variant_level=1)})
        allowed, _challenge, _details = authority.CheckAuthorization(subject, ACTION, {}, dbus.UInt32(0), "")
        if not allowed:
            raise NotAllowed("only the person at the screen can change the cooler")

    def _do(self, sender, work):
        self._allow(sender)
        with self._lock:
            cooler = self._need()
            try:
                return work(cooler)
            except kraken.CoolerError as error:
                raise Failed(str(error))
            except (dbus.DBusException, ValueError):
                raise
            except Exception as error:
                log.exception("talking to the cooler")
                self._lost()
                raise Failed(str(error))

    # The bus

    @dbus.service.method(IFACE, out_signature="a{sv}")
    def Describe(self):
        self._asked = time.monotonic()
        with self._lock:
            cooler = self._cooler
            if cooler is None:
                return {"present": False}
            return {
                "present": True,
                "model": cooler.model,
                "firmware": cooler.firmware,
                "screen": dbus.Int32(cooler.size),
                "brightness": dbus.Int32(cooler.brightness),
                "rotation": dbus.Int32(cooler.rotation),
                "canShowFaces": cooler.can_stream,
                "fansOnCooler": self._silent_fan < FAN_ABSENT_AFTER,
                "curves": json.dumps(self._state.get("curves", {})),
                "fullSpeed": self._full_speed is not None,
            }

    @dbus.service.method(IFACE, out_signature="a{sv}")
    def Readings(self):
        with self._lock:
            got = self._readings
            if got is None:
                return {}
            return {
                "liquid": dbus.Double(got.liquid),
                "pumpRpm": dbus.Int32(got.pump_rpm),
                "pumpDuty": dbus.Int32(got.pump_duty),
                "fanRpm": dbus.Int32(got.fan_rpm),
                "fanDuty": dbus.Int32(got.fan_duty),
            }

    @dbus.service.method(IFACE, sender_keyword="sender")
    def LookAgain(self, sender=None):
        self._asked = time.monotonic()
        self._allow(sender)
        self._look()
        self.Changed()

    @dbus.service.method(IFACE, in_signature="i", sender_keyword="sender")
    def SetBrightness(self, percent, sender=None):
        self._do(sender, lambda cooler: cooler.set_brightness(int(percent)))
        self.Changed()

    @dbus.service.method(IFACE, in_signature="i", sender_keyword="sender")
    def SetRotation(self, degrees, sender=None):
        self._do(sender, lambda cooler: cooler.set_rotation(int(degrees)))
        self.Changed()

    @dbus.service.method(IFACE, in_signature="sa(ii)", sender_keyword="sender")
    def SetCurve(self, channel, points, sender=None):
        channel = str(channel)
        if channel not in ("pump", "fan"):
            raise Failed("a curve is for the pump or the fan")
        curve = sorted((int(t), max(0, min(100, int(duty)))) for t, duty in points)
        if len(curve) < 2:
            raise Failed("a curve needs at least two points")
        # At full speed the new curve waits its turn.
        self._do(sender, lambda cooler: self._full_speed or cooler.set_curve(channel, curve))
        # Kept so it can be put back after the cooler has lost power.
        self._state.setdefault("curves", {})[channel] = curve
        _save_state(self._state)
        self.Changed()

    @dbus.service.method(IFACE, in_signature="b", sender_keyword="sender")
    def SetFullSpeed(self, full, sender=None):
        """Pump and fans flat out, for as long as the caller says and no
        longer than it stays on the bus; then the kept curves again. Only
        channels with a kept curve are touched, so there is always something
        to go back to."""
        full = bool(full)
        if full and not self._state.get("curves"):
            raise Failed("no curve has been set to go back to")
        self._do(sender, lambda cooler: self._run_curves(cooler, full))
        self._full_speed = sender if full else None
        self.Changed()

    @dbus.service.method(IFACE, sender_keyword="sender")
    def ShowLiquid(self, sender=None):
        self._stop_frames()
        self._do(sender, lambda cooler: cooler.show_liquid())

    @dbus.service.method(IFACE, out_signature="h", sender_keyword="sender")
    def OpenFrames(self, sender=None):
        """A socket to write frames into: each one screen x screen pixels of
        four bytes (red, green, blue, one unused), turned to the screen's
        rotation. A new caller takes over from the last."""
        self._allow(sender)
        self._stop_frames()
        self._do(sender, lambda cooler: cooler.start_frames())
        ours, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        self._frames = _FramePump(self, ours)
        self._frames.start()
        handle = dbus.types.UnixFd(theirs.fileno())
        theirs.close()
        return handle

    @dbus.service.signal(IFACE)
    def Changed(self):
        pass

    def _run_curves(self, cooler, full):
        for channel, points in self._state.get("curves", {}).items():
            cooler.set_curve(channel, FULL_SPEED if full else [tuple(p) for p in points])

    def _owner_changed(self, name, _old, new):
        if new or name != self._full_speed:
            return
        log.info("whoever asked for full speed has gone")
        with self._lock:
            self._full_speed = None
            if self._cooler is not None:
                try:
                    self._run_curves(self._cooler, False)
                except Exception:
                    log.exception("putting the curves back")
        self.Changed()

    def shutdown(self):
        streaming = self._frames is not None
        self._stop_frames()
        # Nothing may reach the cooler after this: a process that ends in the
        # middle of a picture transfer leaves the cooler waiting for the rest,
        # and it keeps waiting through a restart, so the next system (Windows
        # included) cannot find it until the power is cut.
        with self._lock:
            if self._cooler is None:
                return
            if streaming:
                try:
                    self._cooler.show_liquid()
                except Exception:
                    log.exception("going back to the liquid screen")
            self._cooler.close()
            self._cooler = None

    def _stop_frames(self):
        if self._frames is not None:
            self._frames.stop()
            self._frames = None

    # For the frame pump

    def frame_bytes(self):
        with self._lock:
            return 0 if self._cooler is None else self._cooler.size * self._cooler.size * 4

    def send_frame(self, pixels):
        with self._lock:
            if self._cooler is None:
                return False
            try:
                return self._cooler.send_frame(pixels)
            except Exception:
                log.exception("sending a frame")
                self._lost()
                return False

    def frames_ended(self):
        with self._lock:
            if self._cooler is not None:
                try:
                    self._cooler.show_liquid()
                except Exception:
                    log.exception("going back to the liquid screen")


class _FramePump:
    """Reads whole frames from the socket on one thread and sends the newest
    to the cooler on another, so a slow cooler never holds up the reader and
    old frames are dropped instead of queued."""

    def __init__(self, service, sock):
        self._service = service
        self._sock = sock
        self._size = service.frame_bytes()
        self._newest = None
        self._wake = threading.Condition()
        self._running = True
        self._sender = None

    def start(self):
        threading.Thread(target=self._read, name="frames-in", daemon=True).start()
        self._sender = threading.Thread(target=self._send, name="frames-out", daemon=True)
        self._sender.start()

    def stop(self):
        with self._wake:
            self._running = False
            self._wake.notify_all()
        try:
            self._sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._sock.close()
        # A frame already on its way finishes first. Whatever comes next
        # (the liquid screen, a new stream, leaving) must not start while
        # the cooler is half way through taking a picture.
        if self._sender is not None:
            self._sender.join(5)

    def _read(self):
        frame = bytearray(self._size)
        view = memoryview(frame)
        try:
            while self._running and self._size:
                filled = 0
                while filled < self._size:
                    got = self._sock.recv_into(view[filled:])
                    if got == 0:
                        raise EOFError
                    filled += got
                with self._wake:
                    self._newest = bytes(frame)
                    self._wake.notify_all()
        except (OSError, EOFError):
            pass
        ended_by_writer = self._running
        with self._wake:
            self._running = False
            self._wake.notify_all()
        if ended_by_writer:
            self._service.frames_ended()

    def _send(self):
        while True:
            with self._wake:
                while self._running and self._newest is None:
                    self._wake.wait()
                if not self._running:
                    return
                frame, self._newest = self._newest, None
            self._service.send_frame(frame)


def main():
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    fake = os.environ.get("WILDOS_COOLER_FAKE")
    if fake and os.environ.get("WILDOS_COOLER_FAKE_BUS") == "system":
        # For tests of who is allowed to ask, in a test PC with no cooler:
        # the pretend cooler behind the real bus name and the real polkit
        # check. Only this service's own environment can ask for it, which
        # on the system bus means whoever may already write its unit: root.
        from . import fake as fakes

        bus = dbus.SystemBus()
        service = Service(bus, find=fakes.finder(fake), leave=lambda: loop.quit())
    elif fake:
        # For tests on a private bus, with no cooler and no polkit.
        from . import fake as fakes

        bus = dbus.SessionBus()
        service = Service(bus, find=fakes.finder(fake), check_caller=False)
    else:
        bus = dbus.SystemBus()
        service = Service(bus, leave=lambda: loop.quit())
    name = dbus.service.BusName(BUS_NAME, bus)
    # Stopped by systemd, the cooler is left on its own screen and not on a
    # face that has stopped moving.
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, loop.quit)
    try:
        loop.run()
    finally:
        service.shutdown()
        _mark(False)
        still_there = service.lost and os.path.lexists(DEVICE)
        del name, service
    if still_there:
        # It stopped answering but is still plugged in. Leaving with an
        # error has systemd start the service again, which looks afresh.
        raise SystemExit(1)


if __name__ == "__main__":
    main()
