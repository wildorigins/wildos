"""Drives wildos-coolerd with a pretend cooler on a private bus.

  dbus-run-session -- python3 tests/service_test.py
"""

import json
import os
import socket
import subprocess
import sys
import time

import dbus

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def start(kind, state):
    env = dict(
        os.environ,
        WILDOS_COOLER_FAKE=kind,
        WILDOS_COOLER_FAKE_DIR=state + "/fake",
        STATE_DIRECTORY=state,
        RUNTIME_DIRECTORY=state + "/run",
        PYTHONPATH=ROOT,
    )
    process = subprocess.Popen([sys.executable, "-m", "wildos_cooler.service"], env=env)
    bus = dbus.SessionBus()
    for _ in range(100):
        if bus.name_has_owner("io.wildos.Cooler"):
            break
        time.sleep(0.05)
    else:
        process.kill()
        raise SystemExit("the service did not start")
    cooler = dbus.Interface(bus.get_object("io.wildos.Cooler", "/io/wildos/Cooler"), "io.wildos.Cooler1")
    return process, cooler


def stop(process):
    process.terminate()
    process.wait(5)
    bus = dbus.SessionBus()
    while bus.name_has_owner("io.wildos.Cooler"):
        time.sleep(0.05)


def check(what, ok):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        check.failed = True


check.failed = False


def seen(state):
    """What reached the pretend cooler, once it has next written it down."""
    time.sleep(1.3)
    with open(state + "/fake/state.json", encoding="utf-8") as handle:
        return json.load(handle)


def main():
    state = "/tmp/wildos-cooler-test"
    subprocess.run(["rm", "-rf", state])
    os.makedirs(state + "/fake")
    marker = state + "/run/present"

    process, cooler = start("none", state)
    check("no cooler is reported as absent", not cooler.Describe()["present"])
    try:
        cooler.SetBrightness(40)
        check("a change with no cooler is refused", False)
    except dbus.DBusException as error:
        check("a change with no cooler is refused", error.get_dbus_name() == "io.wildos.Cooler.NoCooler")
    check("nothing marks a cooler as connected", not os.path.exists(marker))
    stop(process)

    process, cooler = start("kraken", state)
    about = cooler.Describe()
    check("the cooler is described", about["present"] and about["model"] == "Kraken Z" and about["screen"] == 320)
    check("the cooler is marked as connected, with a file that can be run", os.access(marker, os.X_OK))
    time.sleep(1.3)
    check("readings arrive", abs(cooler.Readings()["liquid"] - 34.0) < 0.01)
    cooler.SetBrightness(40)
    cooler.SetRotation(90)
    about = cooler.Describe()
    check("brightness and rotation are set", about["brightness"] == 40 and about["rotation"] == 90)
    try:
        cooler.SetRotation(45)
        check("a rotation that is not a quarter turn is refused", False)
    except dbus.DBusException:
        check("a rotation that is not a quarter turn is refused", True)
    cooler.SetCurve("pump", [(20, 60), (40, 80), (59, 100)])
    check("the curve is kept on disk", json.load(open(state + "/state.json"))["curves"]["pump"][1] == [40, 80])

    handle = cooler.OpenFrames()
    sock = socket.socket(fileno=handle.take())
    frame = bytes(320 * 320 * 4)
    began = time.time()
    for _ in range(60):
        sock.sendall(frame)
    wrote_in = time.time() - began
    time.sleep(0.5)
    sock.close()
    check("sixty frames are taken without waiting for the cooler (%.2f s)" % wrote_in, wrote_in < 1.0)
    check("with the socket closed the cooler shows its own screen again", seen(state)["liquidScreen"])

    flat = [[20, 100], [59, 100]]
    cooler.SetCurve("fan", [(20, 25), (59, 100)])
    cooler.SetFullSpeed(True)
    curves = seen(state)["curves"]
    check("full speed runs the pump and the fans flat out", curves["pump"] == flat and curves["fan"] == flat)
    check("and the service says it is on", bool(cooler.Describe()["fullSpeed"]))
    cooler.SetCurve("pump", [(20, 50), (59, 100)])
    check("a curve set meanwhile waits", seen(state)["curves"]["pump"] == flat)
    cooler.SetFullSpeed(False)
    curves = seen(state)["curves"]
    check("the kept curves come back", curves["pump"] == [[20, 50], [59, 100]] and curves["fan"] == [[20, 25], [59, 100]])
    asker = (
        "import dbus; dbus.Interface(dbus.SessionBus().get_object('io.wildos.Cooler', '/io/wildos/Cooler'),"
        " 'io.wildos.Cooler1').SetFullSpeed(True)"
    )
    subprocess.run([sys.executable, "-c", asker], check=True)
    curves = seen(state)["curves"]
    check("full speed ends when whoever asked for it has gone", curves["pump"] == [[20, 50], [59, 100]])

    open(state + "/fake/unplugged", "w").close()
    time.sleep(1.3)
    check("an unplugged cooler is reported as absent", not cooler.Describe()["present"] and not os.path.exists(marker))
    cooler.LookAgain()
    check("and stays absent when looked for", not cooler.Describe()["present"])
    os.unlink(state + "/fake/unplugged")
    cooler.LookAgain()
    check("plugged in again it is found and marked", bool(cooler.Describe()["present"]) and os.access(marker, os.X_OK))

    handle = cooler.OpenFrames()
    sock = socket.socket(fileno=handle.take())
    sock.sendall(bytes([200, 100, 50, 0]) * (320 * 320))
    time.sleep(0.3)
    open(state + "/fake/snap", "w").close()
    seen(state)
    with open(state + "/fake/frame.png", "rb") as picture:
        check("the pretend cooler hands over the frame it shows", picture.read(4)[1:] == b"PNG")
    sock.close()
    stop(process)
    check("a stopped service leaves no mark", not os.path.exists(marker))

    process, cooler = start("kraken", state)
    check("the curve is still there after a restart", "pump" in json.loads(cooler.Describe()["curves"]))
    events = state + "/fake/events"
    for attempt in range(20):
        if os.path.exists(events):
            os.unlink(events)
        sock = socket.socket(fileno=cooler.OpenFrames().take())
        sock.setblocking(False)
        frame = bytes(320 * 320 * 4)
        until = time.time() + 0.2 + attempt * 0.013
        while time.time() < until:
            try:
                sock.send(frame)
            except BlockingIOError:
                time.sleep(0.002)
        stop(process)
        sock.close()
        with open(events, encoding="utf-8") as handle:
            seq = handle.read().splitlines()
        clean = (
            seq[-2:] == ["liquid", "closed"]
            and seq.count("frame") == seq.count("frame done")
            and all(seq[i + 1] == "frame done" for i, e in enumerate(seq) if e == "frame")
        )
        if not clean:
            break
        process, cooler = start("kraken", state)
    check("stopped mid-stream, the last frame finishes, then the liquid screen, then nothing", clean)
    if clean:
        stop(process)
    raise SystemExit(1 if check.failed else 0)


main()
