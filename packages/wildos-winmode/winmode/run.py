"""wild-run: the default handler for .exe, .msi and .msix files (brief
section 7). It decides where a Windows program runs; App Bridge's dialog
shows that decision and acts on it.

  wild-run <file>           open a Windows program (the file type handler)
  wild-run --json <file>    print the decision, for App Bridge and tests
  wild-run --install <file> hand it straight to Windows mode
"""

import json
import os
import shutil
import sys

from . import compat

APPBRIDGE = "wildos-appbridge"


def host_facts():
    """What decides whether this PC can run it in Windows mode: memory,
    the edition Windows mode has, and whether it is set up. Asked of the
    Windows mode agent when it is installed; nothing is started for it."""
    facts = {"memory": 0, "edition": None, "installed": bool(shutil.which("wildos-winmode")), "state": "unknown"}
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    facts["memory"] = int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    if not facts["installed"]:
        return facts
    try:
        from gi.repository import Gio
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        agent = json.loads(bus.call_sync("org.wildos.WindowsMode1", "/org/wildos/WindowsMode1", "org.wildos.WindowsMode1",
                                         "Status", None, None, Gio.DBusCallFlags.NONE, 10000, None).unpack()[0])
        facts["state"] = agent.get("state", "")
        system = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        host = json.loads(system.call_sync("org.wildos.WindowsMode1", "/org/wildos/WindowsMode1", "org.wildos.WindowsMode1.Host",
                                           "Status", None, None, Gio.DBusCallFlags.NONE, 10000, None).unpack()[0])
        edition = (host.get("edition") or "").lower()
        facts["edition"] = "home" if edition in ("core", "home", "corecountryspecific", "coresinglelanguage") else (edition or None)
    except Exception:
        pass
    return facts


def decision(path):
    info = compat.identify(path)
    entry = compat.lookup(info, compat.load_db())
    out = compat.decide(info, entry, host_facts())
    out["program"] = info
    return out


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__.strip())
        return 0 if argv else 2
    if argv[0] == "--json" and len(argv) == 2:
        print(json.dumps(decision(argv[1]), indent=1))
        return 0
    if argv[0] == "--install" and len(argv) == 2:
        os.execvp("wildos-winmode", ["wildos-winmode", "run-installer", argv[1]])
    path = argv[0]
    if path.startswith("file://"):
        from urllib.parse import unquote, urlparse
        path = unquote(urlparse(path).path)
    # The dialog reads the same decision (wild-run --json) and shows it.
    if shutil.which(APPBRIDGE):
        os.execvp(APPBRIDGE, [APPBRIDGE, path])
    d = decision(path)
    if d["action"] == "windows-mode":
        os.execvp("wildos-winmode", ["wildos-winmode", "run-installer", path])
    print(d.get("reason") or "Windows mode isn't set up on this PC.", file=sys.stderr)
    return 1
