#!/usr/bin/env python3
"""A stand-in for `openrgb --server`, for testing without any hardware.

It speaks OpenRGB's SDK protocol as Documentation/OpenRGBSDK.md describes
it (the formats of versions 3 to 5, saying it is version 6 as OpenRGB 1.0
and 1.1 do), holds a few made-up controllers in memory and writes
every change a client asks for to a log, one JSON object a line.

  fake-openrgb.py [--port 6742] [--protocol 5] [--log FILE] SCENE

Scenes:
  parts   A 10-light strip controller, a motherboard with one fixed
          12-light zone and two strip connectors at 0 lights, and a
          controller whose light can only do white.
  boards  The PC that boards 10.11, 10.15 and 10.16 draw.
  odd     Parts that cannot be streamed to: one with no Direct mode, one
          with a single colour for all of it, one with no modes at all.
"""
import argparse
import json
import socketserver
import struct
import sys
import threading
import time

HAS_SPEED = 1 << 0
HAS_BRIGHTNESS = 1 << 4
PER_LED = 1 << 5
MODE_COLOUR = 1 << 6
AUTO_SAVE = 1 << 9

SINGLE, LINEAR, MATRIX, LOOP = 0, 1, 2, 3
MOTHERBOARD, MEMORY, GRAPHICS, COOLER, STRIP, KEYBOARD, MOUSE = 0, 1, 2, 3, 4, 5, 6
LIGHT = 11


def mode(name, flags, colours=0, speed=(0, 0), brightness=(0, 0), colour_mode=0):
    return {
        "name": name, "value": 0, "flags": flags,
        "speed_min": speed[0], "speed_max": speed[1],
        "brightness_min": brightness[0], "brightness_max": brightness[1],
        "colours_min": colours, "colours_max": colours,
        "speed": speed[0], "brightness": brightness[1], "direction": 0,
        "colour_mode": colour_mode, "colours": [0] * colours,
    }


def zone(name, kind, count, least=None, most=None, flags=None):
    # A zone with a range is one the server lets a client resize, which
    # OpenRGB 1.0 and later say with a flag (bit 1).
    ranged = least is not None and least != most
    return {"name": name, "type": kind, "min": count if least is None else least,
            "max": count if most is None else most, "count": count,
            "flags": (2 if ranged else 0) if flags is None else flags}


def colour_modes():
    return [
        mode("Direct", PER_LED, colour_mode=1),
        mode("Static", MODE_COLOUR, colours=1, colour_mode=2),
        mode("Breathing", MODE_COLOUR | HAS_SPEED, colours=1, speed=(1, 5), colour_mode=2),
        mode("Rainbow Wave", HAS_SPEED, speed=(1, 5)),
    ]


def white_modes():
    return [
        mode("Static", HAS_BRIGHTNESS, brightness=(0, 100)),
        mode("Breathing", HAS_BRIGHTNESS | HAS_SPEED, speed=(1, 4), brightness=(0, 100)),
        mode("Off", 0),
    ]


def controller(kind, name, zones, modes=None, serial="", vendor="Wild OS test"):
    return {"type": kind, "name": name, "vendor": vendor, "description": "Made-up controller",
            "version": "1.0", "serial": serial, "location": "HID: /dev/hidraw-fake",
            "modes": colour_modes() if modes is None else modes, "active": 0, "zones": zones,
            "colours": [0] * sum(z["count"] for z in zones)}


SCENES = {
    "parts": lambda: [
        controller(STRIP, "NZXT RGB & Fan Controller", [zone("Channel 1", LINEAR, 10)], serial="0001"),
        controller(MOTHERBOARD, "Gigabyte Z690 GAMING X DDR4", [
            zone("Motherboard", LINEAR, 12),
            zone("D_LED1", LINEAR, 0, 0, 1024),
            zone("D_LED2", LINEAR, 0, 0, 1024),
        ]),
        controller(LIGHT, "White Light Bar", [zone("Bar", SINGLE, 1)], modes=white_modes()),
    ],
    "boards": lambda: [
        controller(COOLER, "Liquid Cooler", [zone("Pump ring", LOOP, 24), zone("Fans", LINEAR, 24)]),
        controller(MOTHERBOARD, "Motherboard", [
            zone("Zone 1", LINEAR, 12),
            zone("Zone 2", LINEAR, 0, 0, 300),
            zone("Zone 3", LINEAR, 0, 0, 300),
        ]),
        controller(GRAPHICS, "Graphics Card", [zone("Top light", SINGLE, 1), zone("Side light", SINGLE, 1)],
                   modes=white_modes()),
        controller(MEMORY, "Memory", [zone("Sticks", LINEAR, 32)]),
    ],
    "odd": lambda: [
        controller(KEYBOARD, "Saved Keyboard", [zone("Keys", LINEAR, 6), zone("Unflagged", LINEAR, 0, 0, 50, flags=0)],
                   modes=[mode("Custom", PER_LED | AUTO_SAVE, colour_mode=1)]),
        controller(MOUSE, "One Colour Mouse", [zone("Logo", SINGLE, 1)],
                   modes=[mode("Static", MODE_COLOUR, colours=1, colour_mode=2)]),
        controller(STRIP, "Silent Part", [zone("Lights", LINEAR, 4)], modes=[]),
    ],
}


def string(text):
    raw = text.encode() + b"\0"
    return struct.pack("<H", len(raw)) + raw


def mode_block(m, protocol):
    out = string(m["name"]) + struct.pack("<iIII", m["value"], m["flags"], m["speed_min"], m["speed_max"])
    if protocol >= 3:
        out += struct.pack("<II", m["brightness_min"], m["brightness_max"])
    out += struct.pack("<III", m["colours_min"], m["colours_max"], m["speed"])
    if protocol >= 3:
        out += struct.pack("<I", m["brightness"])
    out += struct.pack("<IIH", m["direction"], m["colour_mode"], len(m["colours"]))
    return out + b"".join(struct.pack("<I", c) for c in m["colours"])


def controller_block(c, protocol):
    out = struct.pack("<i", c["type"]) + string(c["name"])
    if protocol >= 1:
        out += string(c["vendor"])
    out += string(c["description"]) + string(c["version"]) + string(c["serial"]) + string(c["location"])
    out += struct.pack("<Hi", len(c["modes"]), c["active"])
    out += b"".join(mode_block(m, protocol) for m in c["modes"])
    out += struct.pack("<H", len(c["zones"]))
    for z in c["zones"]:
        out += string(z["name"]) + struct.pack("<iIIIH", z["type"], z["min"], z["max"], z["count"], 0)
        if protocol >= 4:
            out += struct.pack("<H", 0)
        if protocol >= 5:
            out += struct.pack("<I", z["flags"])
    leds = sum(z["count"] for z in c["zones"])
    out += struct.pack("<H", leds)
    for i in range(leds):
        out += string("LED %d" % (i + 1)) + struct.pack("<I", 0)
    out += struct.pack("<H", leds) + b"".join(struct.pack("<I", v) for v in c["colours"])
    if protocol >= 5:
        out += struct.pack("<HI", 0, 0)
    return struct.pack("<I", len(out) + 4) + out


class State:
    def __init__(self, scene, protocol, log):
        self.controllers = SCENES[scene]()
        self.protocol = protocol
        self.lock = threading.Lock()
        self.clients = []
        self.log_file = open(log, "a", buffering=1) if log else None
        self.started = time.monotonic()

    def log(self, **entry):
        entry["at"] = round(time.monotonic() - self.started, 3)
        if self.log_file:
            self.log_file.write(json.dumps(entry) + "\n")


def packet(device, ident, data=b""):
    return b"ORGB" + struct.pack("<III", device, ident, len(data)) + data


def read_mode(data):
    at = 4
    index = struct.unpack_from("<i", data, at)[0]
    at += 4
    length = struct.unpack_from("<H", data, at)[0]
    name = data[at + 2:at + 2 + length - 1].decode()
    at += 2 + length
    value, flags, speed_min, speed_max, bright_min, bright_max, colours_min, colours_max, speed, brightness, direction, colour_mode, count = \
        struct.unpack_from("<iIIIIIIIIIIIH", data, at)
    at += 50
    colours = list(struct.unpack_from("<%dI" % count, data, at))
    return index, {"name": name, "speed": speed, "brightness": brightness, "colour_mode": colour_mode, "colours": colours}


class Handler(socketserver.BaseRequestHandler):
    def setup(self):
        self.state = self.server.state
        self.protocol = 0
        with self.state.lock:
            self.state.clients.append(self.request)

    def finish(self):
        with self.state.lock:
            if self.request in self.state.clients:
                self.state.clients.remove(self.request)

    def exactly(self, count):
        out = b""
        while len(out) < count:
            more = self.request.recv(count - len(out))
            if not more:
                raise ConnectionError
            out += more
        return out

    def handle(self):
        state = self.state
        try:
            while True:
                magic, device, ident, size = struct.unpack("<4sIII", self.exactly(16))
                if magic != b"ORGB":
                    return
                data = self.exactly(size)
                with state.lock:
                    self.answer(device, ident, data)
        except (ConnectionError, OSError):
            pass

    def late(self, answer):
        try:
            with self.state.lock:
                self.request.sendall(answer)
        except OSError:
            pass

    def answer(self, device, ident, data):
        state = self.state
        known = device < len(state.controllers)
        if ident == 40:
            self.protocol = min(struct.unpack("<I", data)[0], state.protocol)
            self.request.sendall(packet(0, 40, struct.pack("<I", state.protocol)))
        elif ident == 50:
            state.log(op="name", name=data.rstrip(b"\0").decode())
        elif ident == 0:
            self.request.sendall(packet(0, 0, struct.pack("<I", len(state.controllers))))
        elif ident == 1 and known:
            asked = struct.unpack("<I", data)[0] if len(data) >= 4 else 0
            answer = packet(device, 1, controller_block(state.controllers[device], asked))
            # The real server answers from several threads, so the answers
            # to requests sent together come back in any order. Here the
            # first one is held back until after the others.
            if device == 0 and len(state.controllers) > 1:
                threading.Timer(0.05, self.late, [answer]).start()
            else:
                self.request.sendall(answer)
        elif ident == 1000 and known:
            index, size = struct.unpack("<ii", data)
            c = state.controllers[device]
            allowed = state.protocol < 6 or c["zones"][index]["flags"] & 3 if 0 <= index < len(c["zones"]) else False
            if allowed and c["zones"][index]["min"] <= size <= c["zones"][index]["max"]:
                c["zones"][index]["count"] = size
                c["colours"] = [0] * sum(z["count"] for z in c["zones"])
                state.log(op="resize", device=device, zone=index, size=size)
                for client in state.clients:
                    client.sendall(packet(0, 100))
        elif ident == 1100 and known:
            state.log(op="custom", device=device)
        elif ident == 1050 and known:
            count = struct.unpack_from("<H", data, 4)[0]
            colours = list(struct.unpack_from("<%dI" % count, data, 6))
            state.controllers[device]["colours"] = colours
            state.log(op="leds", device=device, colours=["%06x" % (((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff)) for c in colours])
        elif ident == 1101 and known:
            index, changed = read_mode(data)
            state.controllers[device]["active"] = index
            state.log(op="mode", device=device, index=index, **changed)
        else:
            state.log(op="other", device=device, id=ident, size=len(data))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scene", choices=sorted(SCENES))
    parser.add_argument("--port", type=int, default=6742)
    parser.add_argument("--protocol", type=int, default=6, help="the newest version the server says it speaks")
    parser.add_argument("--log")
    args = parser.parse_args()
    server = Server(("127.0.0.1", args.port), Handler)
    server.state = State(args.scene, args.protocol, args.log)
    print("listening on 127.0.0.1:%d" % args.port, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.exit(0)


main()
