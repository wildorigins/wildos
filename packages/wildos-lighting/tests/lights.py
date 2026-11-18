#!/usr/bin/env python3
"""Runs wildos-lighting against the stand-in OpenRGB server and checks what
it sends, what it says on the session bus and what it keeps.

  dbus-run-session -- python3 lights.py WILDOS-LIGHTING FAKE-OPENRGB.py [--cpu]

No hardware is touched: the only server is the stand-in, on a loopback port
of its own. --cpu also prints how much processor time the program takes
with the lights steady and with an effect moving.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

program, fake = sys.argv[1], sys.argv[2]
measure = "--cpu" in sys.argv
failures = []
home = tempfile.mkdtemp(prefix="lights-test-")
log_path = os.path.join(home, "server.log")


def check(name, ok, detail=""):
    print(("PASS: " if ok else "FAIL: ") + name + ("" if ok else "  " + str(detail)))
    if not ok:
        failures.append(name)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


port = free_port()
env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=os.path.join(home, ".config"), WILDOS_LIGHTING_PORT=str(port),
           WILDOS_LIGHTING_SYSFS=os.path.join(home, "sys"), QT_LOGGING_RULES="*.debug=false")
os.makedirs(env["XDG_CONFIG_HOME"])
with open(os.path.join(env["XDG_CONFIG_HOME"], "wildosrc"), "w") as f:
    f.write("[Lighting]\nRgb=true\n\n[Underglow]\nColour=custom\nCustom=#102030\nEffect=steady\n")
with open(os.path.join(env["XDG_CONFIG_HOME"], "kdeglobals"), "w") as f:
    f.write("[General]\nAccentColor=143,180,255\n")

# A graphics card that is recognised by its PCI IDs and a processor sensor.
card = os.path.join(home, "sys/bus/pci/devices/0000:01:00.0")
os.makedirs(card)
for name, value in (("class", "0x030000"), ("vendor", "0x10de"), ("device", "0x2204"),
                    ("subsystem_vendor", "0x10de"), ("subsystem_device", "0x147d")):
    with open(os.path.join(card, name), "w") as f:
        f.write(value + "\n")
sensor = os.path.join(home, "sys/class/hwmon/hwmon0")
os.makedirs(sensor)
with open(os.path.join(sensor, "name"), "w") as f:
    f.write("coretemp\n")


def temperature(celsius):
    with open(os.path.join(sensor, "temp1_input"), "w") as f:
        f.write("%d\n" % (celsius * 1000))


temperature(40)


def call(method, *args):
    out = subprocess.run(["dbus-send", "--session", "--print-reply", "--dest=io.wildos.Lighting", "/io/wildos/Lighting",
                          "io.wildos.Lighting1." + method, *args], capture_output=True, text=True, env=env)
    return out.stdout


def describe():
    text = call("Describe")
    if 'string "' not in text:
        return None
    return json.loads(text[text.index('string "') + 8:text.rindex('"')])


def colours():
    text = call("Colours")
    if "[" not in text:
        return []
    raw = bytes.fromhex("".join(text[text.index("[") + 1:text.rindex("]")].split()))
    return ["%02x%02x%02x" % tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]


def log():
    if not os.path.exists(log_path):
        return []
    with open(log_path) as f:
        return [json.loads(line) for line in f if line.strip()]


def start_server(scene, protocol=6):
    server = subprocess.Popen([sys.executable, fake, scene, "--port", str(port), "--protocol", str(protocol), "--log", log_path],
                              stdout=subprocess.PIPE, text=True)
    server.stdout.readline()
    return server


def start_program():
    return subprocess.Popen([program], env=env, stderr=subprocess.DEVNULL)


def wait_ready(parts=None):
    for _ in range(100):
        d = describe()
        if d and d["state"] == "ready" and (parts is None or len(d["parts"]) == parts):
            return d
        time.sleep(0.1)
    return describe()


def part(d, name):
    return next(p for p in d["parts"] if p["name"] == name)


def ticks(pid):
    with open("/proc/%d/stat" % pid) as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return int(fields[11]) + int(fields[12])


def cpu(pid, seconds):
    before = ticks(pid)
    time.sleep(seconds)
    return 100.0 * (ticks(pid) - before) / os.sysconf("SC_CLK_TCK") / seconds


server = start_server("parts")
daemon = start_program()
try:
    d = wait_ready(4)
    check("the parts are read", d is not None and d["state"] == "ready" and len(d["parts"]) == 4, d)
    nzxt = part(d, "NZXT RGB & Fan Controller")
    board = part(d, "Gigabyte Z690 GAMING X DDR4")
    bar = part(d, "White Light Bar")
    seen = part(d, "GeForce RTX 3090")
    check("the strip controller has one zone of ten lights", [z["lights"] for z in nzxt["zones"]] == [10])
    check("the board has a fixed zone and two strip connectors at none",
          [(z["lights"], z["resizable"]) for z in board["zones"]] == [(12, False), (0, True), (0, True)])
    check("kinds come from the part", (nzxt["kind"], board["kind"], bar["kind"]) == ("strip", "motherboard", "light"))
    check("the white-only part says so", bar["white"] and not nzxt["white"])
    check("a white-only part is offered only what it can show",
          [e["id"] for e in bar["effects"] if e["offered"]] == ["breathe", "steady", "off"]
          and {e["why"] for e in bar["effects"] if not e["offered"]} == {"colour"})
    check("the graphics card is seen and not controlled",
          not seen["controlled"] and seen["why"] == "bus" and [z["white"] for z in seen["zones"]] == [False, True])
    check("nothing is set before Cooler chooses", not d["inCharge"] and [e["op"] for e in log()] == ["name"], log())
    check("before that the page is told the taskbar's colour", colours()[:10] == ["102030"] * 10, colours()[:10])

    call("Flash", "string:" + nzxt["key"], "int32:0")
    time.sleep(0.2)
    check("a flash is announced", part(describe(), nzxt["name"])["zones"][0]["flashing"])
    time.sleep(3.3)
    flashes = [e for e in log() if e["op"] == "leds"]
    check("a flash blinks white", flashes and {c for e in flashes for c in e["colours"]} == {"ffffff", "000000"} and len(flashes) >= 8, len(flashes))
    check("a flash ends", not part(describe(), nzxt["name"])["zones"][0]["flashing"])

    call("SetEverything", "string:steady", "int32:100", "int32:35")
    time.sleep(0.5)
    d = describe()
    entries = log()
    last = {e["device"]: e for e in entries if e["op"] == "leds"}
    check("choosing an effect puts the lights in Cooler's hands", d["inCharge"] and d["everything"]["effect"] == "steady")
    check("steady sends the accent to every light", last[0]["colours"] == ["8fb4ff"] * 10 and last[1]["colours"] == ["8fb4ff"] * 12, last)
    check("the lights are put in their direct mode first", [e["device"] for e in entries if e["op"] == "custom"][-2:] == [0, 1])
    modes = [e for e in entries if e["op"] == "mode" and e["device"] == 2]
    check("a white-only part in sync is lit steadily", modes and modes[-1]["name"] == "Static" and modes[-1]["brightness"] == 100, modes)
    before = len(log())
    steady_cpu = cpu(daemon.pid, 3.0)
    check("nothing is sent while the lights are steady", len(log()) == before)
    check("nothing spins while the lights are steady", steady_cpu < 0.5, steady_cpu)

    call("SetEverything", "string:steady", "int32:60", "int32:35")
    time.sleep(0.4)
    modes = [e for e in log() if e["op"] == "mode" and e["device"] == 2]
    check("a white-only part follows the brightness and ignores the colour", modes[-1]["name"] == "Static" and modes[-1]["brightness"] == 60, modes[-1])

    call("SetEverything", "string:theme", "int32:100", "int32:35")
    time.sleep(0.3)
    before = len([e for e in log() if e["op"] == "leds" and e["device"] == 0])
    moving_cpu = cpu(daemon.pid, 4.0)
    frames = (len([e for e in log() if e["op"] == "leds" and e["device"] == 0]) - before) / 4.0
    check("a moving effect is streamed at a modest rate", 10 <= frames <= 16, frames)
    frame = [e for e in log() if e["op"] == "leds" and e["device"] == 0][-1]["colours"]
    check("the theme runs along the strip", len(set(frame)) == 10, frame)
    check("the page is told it moves", describe()["moving"])
    if measure:
        print("CPU: %.2f%% of one core steady, %.2f%% streaming %.1f frames a second to 22 lights" % (steady_cpu, moving_cpu, frames))

    call("SetZoneCount", "string:" + board["key"], "int32:1", "int32:30")
    time.sleep(0.6)
    d = describe()
    check("a strip connector is told how many lights are plugged in",
          part(d, board["name"])["zones"][1]["lights"] == 30 and any(e["op"] == "resize" and e["size"] == 30 for e in log()))
    check("frames cover the new lights", [e for e in log() if e["op"] == "leds" and e["device"] == 1][-1]["colours"].__len__() == 42)
    call("SetZoneSplit", "string:" + board["key"], "int32:1", "int32:3")
    call("SetZoneShape", "string:" + board["key"], "int32:1", "string:ring")
    call("SetZoneName", "string:" + board["key"], "int32:1", "string:Case fans")
    call("SetEverything", "string:theme", "int32:100", "int32:0")
    time.sleep(0.4)
    zone = part(describe(), board["name"])["zones"][1]
    check("names, shapes and sections are taken", (zone["name"], zone["shape"], zone["split"], zone["reported"]) == ("Case fans", "ring", 3, "D_LED1"), zone)
    frame = [e for e in log() if e["op"] == "leds" and e["device"] == 1][-1]["colours"]
    check("each section shows the whole effect", frame[12:22] == frame[22:32] == frame[32:42] and len(set(frame[12:22])) > 3, frame[12:])
    check("a ring's last light meets its first", frame[13] == frame[21], frame[12:22])
    check("the page is told the same colours", colours()[10:52] == frame, colours()[10:52])
    before = len(log())
    time.sleep(1.0)
    check("a theme that stands still is sent once", len(log()) == before and not describe()["moving"])

    call("SetPart", "string:" + bar["key"], "string:breathe", "int32:80", "int32:100")
    time.sleep(0.4)
    modes = [e for e in log() if e["op"] == "mode" and e["device"] == 2]
    check("a white-only part breathes by its own mode", modes[-1]["name"] == "Breathing" and modes[-1]["brightness"] == 80 and modes[-1]["speed"] == 4, modes[-1])
    call("SetPart", "string:" + bar["key"], "string:theme", "int32:80", "int32:100")
    check("a white-only part refuses an effect that needs colour", part(describe(), bar["name"])["effect"] == "breathe")
    call("SetPartOwn", "string:" + bar["key"], "boolean:false")
    time.sleep(0.3)
    check("a part goes back in sync", not part(describe(), bar["name"])["own"]
          and [e for e in log() if e["op"] == "mode" and e["device"] == 2][-1]["name"] == "Static")

    call("SetPart", "string:" + nzxt["key"], "string:steady", "int32:50", "int32:35")
    time.sleep(0.3)
    frame = [e for e in log() if e["op"] == "leds" and e["device"] == 0][-1]["colours"]
    check("a part set on its own takes its own effect and brightness", frame == ["485a80"] * 10, frame)
    call("Flash", "string:" + nzxt["key"], "int32:0")
    time.sleep(3.5)
    frame = [e for e in log() if e["op"] == "leds" and e["device"] == 0][-1]["colours"]
    check("after a flash the zone shows what it did before", frame == ["485a80"] * 10, frame)

    call("SetEverything", "string:temperature", "int32:100", "int32:35")
    time.sleep(0.5)
    cool = [e for e in log() if e["op"] == "leds" and e["device"] == 1][-1]["colours"]
    temperature(85)
    time.sleep(2.6)
    hot = [e for e in log() if e["op"] == "leds" and e["device"] == 1][-1]["colours"]
    check("temperature follows the processor", set(cool) == {"5fa8ff"} and set(hot) == {"ff6b6b"}, (cool[0], hot[0]))

    call("SetEverything", "string:off", "int32:100", "int32:35")
    time.sleep(0.4)
    frame = [e for e in log() if e["op"] == "leds" and e["device"] == 1][-1]["colours"]
    check("off is dark", set(frame) == {"000000"})
    call("SetEverything", "string:theme", "int32:100", "int32:35")
    time.sleep(0.3)

    # A restart of both: the server has forgotten the count, the program
    # has to bring everything back from what it kept.
    daemon.terminate()
    daemon.wait()
    server.terminate()
    server.wait()
    os.remove(log_path)
    server = start_server("parts", protocol=4)
    daemon = start_program()
    d = wait_ready(4)
    time.sleep(0.8)
    d = describe()
    zone = part(d, board["name"])["zones"][1]
    check("an older server is spoken to in its own version", d["state"] == "ready" and len(d["parts"]) == 4)
    check("everything comes back after a restart",
          (zone["name"], zone["shape"], zone["split"], zone["lights"]) == ("Case fans", "ring", 3, 30)
          and d["everything"]["effect"] == "theme" and part(d, nzxt["name"])["own"], zone)
    check("the count is given to the server again", any(e["op"] == "resize" and e["size"] == 30 for e in log()))
    check("the effect carries on with no window", len([e for e in log() if e["op"] == "leds" and e["device"] == 1]) > 3)

    daemon.terminate()
    daemon.wait()
    server.terminate()
    server.wait()
    os.remove(log_path)
    shutil.rmtree(os.path.join(env["XDG_CONFIG_HOME"], "wildoslightingrc"), ignore_errors=True)
    if os.path.exists(os.path.join(env["XDG_CONFIG_HOME"], "wildoslightingrc")):
        os.remove(os.path.join(env["XDG_CONFIG_HOME"], "wildoslightingrc"))
    server = start_server("odd")
    daemon = start_program()
    d = wait_ready(4)
    call("SetEverything", "string:theme", "int32:100", "int32:35")
    time.sleep(1.5)
    d = describe()
    entries = log()
    check("a part with no direct mode is set once and never streamed to",
          len([e for e in entries if e["op"] == "leds" and e["device"] == 0]) == 1 and part(d, "Saved Keyboard")["still"], entries)
    check("a part with one colour for all of it is set through its mode",
          [e["colours"] for e in entries if e["op"] == "mode" and e["device"] == 1] == [[0xffb48f]], entries)
    check("a zone the server will not resize is not offered a count",
          [z["resizable"] for z in part(d, "Saved Keyboard")["zones"]] == [False, False])
    check("a part with no way in is left alone and says so",
          not part(d, "Silent Part")["controlled"] and not [e for e in entries if e.get("device") == 2])
finally:
    daemon.terminate()
    server.terminate()
    shutil.rmtree(home, ignore_errors=True)

print("%d failed" % len(failures))
sys.exit(1 if failures else 0)
