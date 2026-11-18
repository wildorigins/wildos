"""A cooler that is not there, for tests: WILDOS_COOLER_FAKE=kraken or none.

With WILDOS_COOLER_FAKE_DIR set to a folder, a test can steer the pretend
cooler and see what reached it, through files in that folder:

  liquid     its liquid temperature, as a number (34 without the file)
  unplugged  while this exists the cooler has gone: readings fail and a new
             search finds nothing
  snap       asks for the frame on the screen now; it is written to
             frame.png and snap is removed
  state.json written once a second: brightness, rotation, curves, how many
             frames have arrived and whether the cooler's own liquid screen
             is showing
"""

from __future__ import annotations

import json
import os
import struct
import time
import zlib

from . import kraken

CONTROL = os.environ.get("WILDOS_COOLER_FAKE_DIR")


def _png(size, pixels):
    """A PNG of size x size pixels of four bytes, the fourth unused."""

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    colours = bytearray(size * size * 3)
    for part in range(3):
        colours[part::3] = pixels[part::4]
    row = size * 3
    rows = b"".join(b"\x00" + colours[y * row:(y + 1) * row] for y in range(size))
    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b"")


def _there(name):
    return CONTROL is not None and os.path.exists(os.path.join(CONTROL, name))


class FakeKraken:
    model = "Kraken Z"
    size = 320
    firmware = "5.11.0"
    serial = "TEST"
    can_stream = True

    def __init__(self):
        self.brightness = 50
        self.rotation = 0
        self.curves = {}
        self.frames = 0
        self.liquid_screen = True
        self.last_frame = None

    def _event(self, what):
        # What reached the pretend cooler, in order, for the test of a clean stop.
        if CONTROL is not None:
            with open(os.path.join(CONTROL, "events"), "a", encoding="utf-8") as handle:
                handle.write(what + "\n")

    def close(self):
        self._event("closed")

    def readings(self):
        liquid = 34.0
        if CONTROL is not None:
            if _there("unplugged"):
                raise kraken.CoolerError("the pretend cooler is unplugged")
            try:
                with open(os.path.join(CONTROL, "liquid"), encoding="utf-8") as handle:
                    liquid = float(handle.read())
            except (OSError, ValueError):
                pass
            self._report()
        return kraken.Readings(liquid=liquid, pump_rpm=2100, pump_duty=70, fan_rpm=840, fan_duty=38)

    def _report(self):
        if _there("snap") and self.last_frame is not None:
            with open(os.path.join(CONTROL, "frame.png.new"), "wb") as handle:
                handle.write(_png(self.size, self.last_frame))
            os.replace(os.path.join(CONTROL, "frame.png.new"), os.path.join(CONTROL, "frame.png"))
            os.unlink(os.path.join(CONTROL, "snap"))
        state = {
            "brightness": self.brightness,
            "rotation": self.rotation,
            "curves": self.curves,
            "frames": self.frames,
            "liquidScreen": self.liquid_screen,
        }
        with open(os.path.join(CONTROL, "state.json.new"), "w", encoding="utf-8") as handle:
            json.dump(state, handle)
        os.replace(os.path.join(CONTROL, "state.json.new"), os.path.join(CONTROL, "state.json"))

    def set_curve(self, channel, points):
        self.curves[channel] = points

    def set_brightness(self, percent):
        self.brightness = max(0, min(100, int(percent)))

    def set_rotation(self, degrees):
        if degrees not in (0, 90, 180, 270):
            raise kraken.CoolerError("rotation is 0, 90, 180 or 270")
        self.rotation = degrees

    def show_liquid(self):
        self._event("liquid")
        self.liquid_screen = True

    def start_frames(self):
        self.liquid_screen = False

    def send_frame(self, pixels):
        if len(pixels) != self.size * self.size * 4:
            raise kraken.CoolerError("wrong frame size")
        self._event("frame")
        # About as long as the real cooler takes.
        time.sleep(0.075)
        self._event("frame done")
        self.frames += 1
        self.last_frame = pixels
        self.liquid_screen = False
        return True


def finder(kind):
    made = []

    def find():
        if kind == "none" or _there("unplugged"):
            return None
        if not made:
            made.append(FakeKraken())
        return made[0]

    find.made = made
    return find
