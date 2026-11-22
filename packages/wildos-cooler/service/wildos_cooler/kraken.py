"""The NZXT Kraken with a screen, through liquidctl.

liquidctl does the cooler itself: finding it, readings, pump and fan curves,
brightness and rotation. It can only put a still picture or a GIF on the
screen, and its picture upload manages about five a second, so the frame
sender here is ours.

How frames reach the screen. The cooler holds pictures in numbered slots
("buckets") in its own memory and shows one of them. The maker's software
keeps sixteen slots, each one frame long, and writes them in turn; so do we.
For every frame the slot is deleted, set up again at its fixed place, filled
over the bulk endpoint, then shown. Writing over a slot without setting it up
again is quicker (20 frames a second against 13 on a Kraken Z, firmware
5.11.0) and the cooler acknowledges every frame, but the picture rolls: do
not bring that shortcut back without watching a real screen.

Every command waits for its own reply. The cooler answers a command with
the command's first byte plus one and the same second byte (0x74 0x01 gets
0x75 0x01), and sends status reports of its own in between. liquidctl's
helpers take whatever comes next, so one unread reply put every later read
a step behind: readings came back as zeros while faces streamed, and the
screen was switched before the cooler said it had stored the picture.
"""

from __future__ import annotations

import dataclasses
import logging
import math
import sys
import time

log = logging.getLogger("wildos-cooler")

VENDOR = 0x1E71
# USB product id: (name, screen pixels across).
MODELS = {
    0x3008: ("Kraken Z", 320),
    0x300C: ("Kraken Elite", 640),
    0x300E: ("Kraken", 240),
    0x3012: ("Kraken Elite RGB", 640),
    0x3014: ("Kraken Plus", 240),
}

SLOTS = 16
_TRANSFER_HEADER = bytes([0x12, 0xFA, 0x01, 0xE8, 0xAB, 0xCD, 0xEF, 0x98, 0x76, 0x54, 0x32, 0x10])
_KIND_PICTURE = 0x02
_SHOW_PICTURE = 0x04
_SHOW_LIQUID = 0x02
# Replies looked through for the one a command wants; the cooler's status
# reports come between them.
_REPLY_ATTEMPTS = 24
class CoolerError(Exception):
    pass


@dataclasses.dataclass
class Readings:
    liquid: float
    pump_rpm: int
    pump_duty: int
    fan_rpm: int
    fan_duty: int


def _supported(device) -> bool:
    return getattr(device, "vendor_id", None) == VENDOR and getattr(device, "product_id", None) in MODELS


def find():
    """The first supported cooler, connected and initialised, or None."""
    from liquidctl import find_liquidctl_devices

    for device in find_liquidctl_devices():
        if _supported(device) and hasattr(device, "lcd_resolution"):
            return Kraken(device)
    return None


class Kraken:
    def __init__(self, device):
        self._dev = device
        self.model, self.size = MODELS[device.product_id]
        self.serial = getattr(device, "serial_number", "") or ""
        self.firmware = ""
        self.brightness = 0
        self.rotation = 0
        self._next_slot = 0
        self._ring_ready = False
        self._frame_bytes = self.size * self.size * 4
        # Slot sizes and places are counted in units of 1024 bytes.
        self._slot_units = math.ceil((len(_TRANSFER_HEADER) + 8 + self._frame_bytes) / 1024)
        if sys.platform == "win32" and device.bulk_device is None:
            self._open_winusb()
        device.connect()
        self._initialise()

    def _open_winusb(self):
        # Only the test tools run on Windows. liquidctl looks for the generic
        # USB device interface there and misses the one NZXT's driver
        # registers for the screen, so the test names its path.
        import os

        from winusbcdc import WinUsbPy

        path = os.environ.get("WILDOS_COOLER_WINUSB_PATH")
        bulk = WinUsbPy()
        if path and bulk.init_winusb_device_with_path(path):
            self._dev.bulk_device = bulk

    def _initialise(self):
        for name, value, _unit in self._dev.initialize() or []:
            if name == "Firmware version":
                self.firmware = str(value)
            elif name == "LCD Brightness":
                self.brightness = int(value)
            elif name == "LCD Orientation":
                self.rotation = int(value)

    def close(self):
        try:
            self._dev.disconnect()
        except Exception:
            log.debug("closing the cooler", exc_info=True)

    @property
    def can_stream(self) -> bool:
        return self._dev.bulk_device is not None

    def readings(self) -> Readings:
        msg = self._request([0x74, 0x01])
        return Readings(
            liquid=msg[15] + msg[16] / 10,
            pump_rpm=msg[18] << 8 | msg[17],
            pump_duty=msg[19],
            fan_rpm=msg[24] << 8 | msg[23],
            fan_duty=msg[25],
        )

    def set_curve(self, channel: str, points: list[tuple[int, int]]):
        """Speed against liquid temperature, kept by the cooler while it has
        power. `channel` is "pump" or "fan"; points are (degrees, per cent)."""
        self._dev.set_speed_profile(channel, points)

    def set_brightness(self, percent: int):
        percent = max(0, min(100, int(percent)))
        self._dev.set_screen("lcd", "brightness", percent)
        self.brightness = percent

    def set_rotation(self, degrees: int):
        if degrees not in (0, 90, 180, 270):
            raise CoolerError("rotation is 0, 90, 180 or 270")
        self._dev.set_screen("lcd", "orientation", degrees)
        self.rotation = degrees

    def show_liquid(self):
        """The cooler's own liquid temperature screen, which needs no PC.
        The picture slots are emptied too, so no frame of ours stays in the
        cooler's memory: after a session of faces, a restart used to leave
        the last frame frozen on the screen and the cooler unseen by any
        system until the power was cut."""
        if not self._switch(0, _SHOW_LIQUID):
            log.warning("the cooler did not confirm its liquid screen")
        if self._ring_ready:
            for slot in range(SLOTS):
                self._delete(slot)
        self._ring_ready = False

    # Frames

    def _request(self, command: list[int]) -> list[int]:
        """Sends one command and returns its own reply, skipping the status
        reports and anything left over from before."""
        self._dev.device.clear_enqueued_reports()
        self._dev._write(command)
        want = (command[0] + 1, command[1])
        for _ in range(_REPLY_ATTEMPTS):
            msg = self._dev._read()
            if len(msg) > 1 and (msg[0], msg[1]) == want:
                return msg
        raise CoolerError("the cooler did not answer %02x %02x" % (command[0], command[1]))

    def _ack(self, command: list[int]) -> bool:
        return self._request(command)[14] == 0x01

    def _switch(self, slot: int, mode: int) -> bool:
        return self._ack([0x38, 0x01, mode, slot])

    def _delete(self, slot: int) -> bool:
        return self._ack([0x32, 0x02, slot])

    def _setup(self, slot: int) -> bool:
        place = (slot * self._slot_units).to_bytes(2, "little")
        length = self._slot_units.to_bytes(2, "little")
        return self._ack([0x32, 0x01, slot, slot + 1, place[0], place[1], length[0], length[1], 0x01])

    def _bulk(self, data: bytes):
        self._dev.bulk_device.write(0x2, data)

    def start_frames(self):
        """Empties the cooler's picture memory, ready for send_frame."""
        if not self.can_stream:
            raise CoolerError("the screen's USB interface could not be opened")
        self._switch(0, _SHOW_LIQUID)
        for slot in range(SLOTS):
            self._delete(slot)
        self._next_slot = 0
        self._ring_ready = True

    def send_frame(self, pixels: bytes) -> bool:
        """One frame: size x size pixels, four bytes each (red, green, blue,
        one unused), already turned to the screen's rotation. True when the
        cooler took it and is showing it; on False the last frame stays up."""
        if len(pixels) != self._frame_bytes:
            raise CoolerError("a frame is %d bytes, not %d" % (self._frame_bytes, len(pixels)))
        if not self._ring_ready:
            self.start_frames()
        slot = self._next_slot
        self._next_slot = (slot + 1) % SLOTS
        self._request([0x36, 0x03])
        self._delete(slot)
        if not self._setup(slot):
            return False
        self._request([0x36, 0x01, slot])
        self._bulk(_TRANSFER_HEADER + bytes([_KIND_PICTURE, 0, 0, 0]) + len(pixels).to_bytes(4, "little"))
        self._bulk(pixels)
        # Only once the cooler says the picture is stored is it shown.
        if not self._ack([0x36, 0x02]):
            return False
        return self._switch(slot, _SHOW_PICTURE)


def _self_test(seconds: float):
    """Streams a moving test card and prints the rate. Needs Pillow."""
    from PIL import Image, ImageDraw

    cooler = find()
    if cooler is None:
        print("no supported cooler found")
        return
    print("%s, firmware %s, screen %d, brightness %d, rotation %d" % (
        cooler.model, cooler.firmware, cooler.size, cooler.brightness, cooler.rotation))
    print(cooler.readings())
    size = cooler.size
    sent = shown = 0
    start = time.perf_counter()
    try:
        while time.perf_counter() - start < seconds:
            t = time.perf_counter() - start
            picture = Image.new("RGB", (size, size), (6, 8, 12))
            draw = ImageDraw.Draw(picture)
            edge = size // 20
            draw.arc((edge, edge, size - edge, size - edge), t * 120, t * 120 + 100, fill=(0, 190, 255), width=edge)
            pixels = picture.rotate(-cooler.rotation).convert("RGBX").tobytes()
            shown += cooler.send_frame(pixels)
            sent += 1
    finally:
        cooler.close()
    print("%d frames, %d shown, %.1f a second" % (sent, shown, sent / seconds))


if __name__ == "__main__":
    _self_test(float(sys.argv[1]) if len(sys.argv) > 1 else 10.0)
