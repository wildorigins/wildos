"""Where Windows mode keeps things, and the settings an installation can
override in /etc/wildos/winmode.json (a test rig points it at another VM
or share; nothing else needs to).
"""

import json
import os

DATA = os.environ.get("WILDOS_WINMODE_DATA", "/usr/share/wildos/winmode")
STATE_DIR = "/var/lib/wildos/winmode"
CONFIG_FILE = "/etc/wildos/winmode.json"

BUS_NAME = "org.wildos.WindowsMode1"
OBJECT_PATH = "/org/wildos/WindowsMode1"
HOST_INTERFACE = "org.wildos.WindowsMode1.Host"
AGENT_INTERFACE = "org.wildos.WindowsMode1"
POLKIT_ACTION = "org.wildos.winmode.use"

DEFAULTS = {
    "domain": "wildos-windows",
    "network": "wildos-win",
    "bridge": "virbr-wildos",
    "host_address": "192.168.207.1",
    "guest_address": "192.168.207.10",
    "rdp_port": 3389,
    # The Windows account and the name Windows gave itself, which is the
    # domain for Network Level Authentication.
    "account": "wild",
    "computer_name": "",
    # The folder shared into Windows as one drive, with each shared folder
    # at <drive>:\<Name> inside it.
    "share_root": "/var/lib/wildos/winmode/share",
    "drive": "Z",
    "folders": ["Documents", "Downloads", "Pictures", "Desktop"],
    # Seconds with no Windows app open before the idle policy applies
    # (brief 5.6).
    "idle_seconds": 60,
    # keep, save or automatic
    "when_idle": "automatic",
    "guest_exe": DATA + "/guest/wild-guest.exe",
}

OPTION_KEYS = {"when_idle": ("keep", "save", "automatic")}


def load():
    cfg = dict(DEFAULTS)
    try:
        with open(os.environ.get("WILDOS_WINMODE_CONFIG", CONFIG_FILE), encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            cfg.update({k: v for k, v in data.items() if k in DEFAULTS})
    except (OSError, ValueError):
        pass
    return cfg


def state_file():
    return os.path.join(STATE_DIR, "state.json")


def read_json(path, default=None):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def write_json(path, data, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".new"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, sort_keys=True)
        f.write("\n")
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def user_data_dir():
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "wildos", "winmode")


def user_config_file():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "wildos", "winmode.json")


def applications_dir():
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "applications")


def icons_dir():
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "icons", "hicolor", "256x256", "apps")
