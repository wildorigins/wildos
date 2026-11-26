"""What a Windows program is, and where it should run (brief section 7).

identify() reads a program without running it: SHA-256, the names in its
version information, its Authenticode signer when osslsigncode can check
the signature, and for .msix packages the manifest. lookup() finds it in the
compatibility database (/usr/share/wildos/compat/*.json, format below), and
decide() turns that into what wild-run does.

Database format (format 1):
  {"format": 1, "entries": [{
      "id": "teams", "name": "Microsoft Teams",
      "match": {"sha256": [...], "signer": [...], "product": [...], "words": [...]},
      "engine": "windows-mode" | "native" | "unsupported",
      "alternative": {"storeId": "...", "webApp": "https://...", "text": "..."},
      "reason": "shown when unsupported",
      "calls": true}]}
signer and product match whole names, case aside; words match anywhere in
the file name, product, company, description or signer. The most specific
match wins: SHA-256, then signer with product, then product, then signer,
then words.
"""

import glob
import hashlib
import json
import mmap
import os
import re
import shutil
import struct
import subprocess
import zipfile
import xml.etree.ElementTree as ET

DB_DIR = os.environ.get("WILDOS_COMPAT_DIR", "/usr/share/wildos/compat")
ENGINES = ("windows-mode", "native", "unsupported")


def load_db(directory=None):
    entries = []
    for path in sorted(glob.glob(os.path.join(directory or DB_DIR, "*.json"))):
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            continue
        if not isinstance(data, dict) or data.get("format") != 1:
            continue
        for e in data.get("entries", []):
            if isinstance(e, dict) and e.get("engine") in ENGINES and isinstance(e.get("match"), dict):
                entries.append(e)
    return entries


def _rva_to_offset(sections, rva):
    for va, size, raw in sections:
        if va <= rva < va + size:
            return rva - va + raw
    return None


def version_info(data):
    """The StringFileInfo names from a PE file's version resource:
    ProductName, CompanyName, FileDescription, ProductVersion."""
    try:
        if data[:2] != b"MZ":
            return None
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            return None
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from("<H", data, opt)[0]
        dirs = opt + (96 if magic == 0x10B else 112)
        res_rva = struct.unpack_from("<I", data, dirs + 2 * 8)[0]
        sections = []
        for i in range(nsec):
            s = opt + opt_size + i * 40
            vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, s + 8)
            sections.append((va, max(vsize, rawsize), raw))
        base = _rva_to_offset(sections, res_rva)
        if base is None:
            return {}

        def entries(off):
            named, ids = struct.unpack_from("<HH", data, off + 12)
            for i in range(named + ids):
                name, target = struct.unpack_from("<II", data, off + 16 + i * 8)
                yield name, target

        for name, target in entries(base):
            if name == 16 and target & 0x80000000:
                lvl2 = base + (target & 0x7FFFFFFF)
                for _, t2 in entries(lvl2):
                    lvl3 = base + (t2 & 0x7FFFFFFF)
                    for _, t3 in entries(lvl3):
                        rva, size = struct.unpack_from("<II", data, base + t3)
                        off = _rva_to_offset(sections, rva)
                        if off is not None:
                            return _strings(data[off:off + size])
        return {}
    except (struct.error, IndexError):
        return None


def _strings(block):
    """Name and value pairs from a VS_VERSIONINFO block. The layout is a tree
    of UTF-16 keys padded to 4 bytes; reading every key followed by its
    value text is enough for the StringFileInfo entries."""
    text = block.decode("utf-16-le", "ignore")
    out = {}
    for key in ("ProductName", "CompanyName", "FileDescription", "ProductVersion", "FileVersion", "OriginalFilename"):
        m = re.search(key + r"\x00+([^\x00]+)", text)
        if m:
            out[key] = m.group(1).strip()
    return out


def msix_info(path):
    try:
        with zipfile.ZipFile(path) as z:
            root = ET.fromstring(z.read("AppxManifest.xml"))
    except (OSError, KeyError, zipfile.BadZipFile, ET.ParseError):
        return {}
    out = {}
    for el in root.iter():
        tag = el.tag.split("}")[-1]
        if tag == "Identity":
            out["CompanyName"] = re.sub(r"^CN=([^,]+).*$", r"\1", el.get("Publisher", ""))
            out["ProductVersion"] = el.get("Version", "")
        elif tag == "DisplayName" and el.text and "ProductName" not in out:
            out["ProductName"] = el.text.strip()
    return out


def signer(path):
    """The Authenticode signer, only when the signature checks out against
    the trusted certificate authorities (as App Bridge does)."""
    tool = shutil.which("osslsigncode")
    if not tool:
        return None
    args = [tool, "verify", "-in", path]
    if os.path.exists("/etc/ssl/certs/ca-certificates.crt"):
        args += ["-CAfile", "/etc/ssl/certs/ca-certificates.crt"]
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if p.returncode != 0 or "Signature verification: ok" not in p.stdout:
        return None
    start = p.stdout.find("Signer's certificate:")
    m = re.search(r"Subject:.*?/CN=([^/\n]+)", p.stdout[max(start, 0):])
    return re.sub(r"\\(?=.)", "", m.group(1).strip()) if m else None


def identify(path, check_signature=True):
    # Mapped rather than read: an installer can be gigabytes, and reading
    # it all into memory first kept App Bridge waiting on a slow PC until it
    # gave up and said Windows mode wasn't set up.
    with open(path, "rb") as f:
        try:
            data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        except ValueError:
            data = b""
    info = {"file": os.path.basename(path), "sha256": hashlib.sha256(data).hexdigest(), "kind": "unknown"}
    lower = path.lower()
    if lower.endswith((".msix", ".appx", ".msixbundle", ".appxbundle")):
        info["kind"] = "msix"
        names = msix_info(path)
    elif data[:8] == b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1":
        info["kind"] = "msi"
        names = {}
    else:
        names = version_info(data)
        info["kind"] = "exe" if names is not None else "not-windows"
        names = names or {}
    info["product"] = names.get("ProductName", "")
    info["company"] = names.get("CompanyName", "")
    info["description"] = names.get("FileDescription", "")
    info["version"] = names.get("ProductVersion", "")
    info["signer"] = signer(path) if check_signature and info["kind"] in ("exe", "msi") else None
    return info


def lookup(info, entries):
    def norm(s):
        return (s or "").strip().lower()

    hay = "\n".join(norm(info.get(k)) for k in ("file", "product", "company", "description", "signer"))
    best, best_score = None, 0
    for e in entries:
        m = e["match"]
        score = 0
        if info.get("sha256") and info["sha256"].lower() in [norm(x) for x in m.get("sha256", [])]:
            score = 50
        else:
            signer_ok = bool(info.get("signer")) and norm(info["signer"]) in [norm(x) for x in m.get("signer", [])]
            product_ok = bool(info.get("product")) and norm(info["product"]) in [norm(x) for x in m.get("product", [])]
            if signer_ok and product_ok:
                score = 40
            elif product_ok:
                score = 30
            elif signer_ok:
                score = 20
            elif any(norm(w) and norm(w) in hay for w in m.get("words", [])):
                score = 10
        if score > best_score:
            best, best_score = e, score
    return best


def decide(info, entry, host):
    """What wild-run does with a program. host: {"memory": bytes, "edition":
    "home"|"pro"|None, "installed": bool, "state": the agent's state}.

    action is one of:
      not-windows   it isn't a Windows program at all
      native        offer the Wild OS version first, Windows mode second
      unsupported   explain why (reason)
      home          Windows 11 Home: upgrade to Pro, or the native
                    alternative and Office on the web (board 9.2)
      cannot        this PC can't have Windows mode (too little memory)
      setup         Windows mode first needs setting up (board 9.1)
      windows-mode  run it in Windows mode

    windows is what Windows mode could do with it, whatever action is
    recommended: home, cannot, setup or windows-mode.
    """
    out = {"engine": entry["engine"] if entry else "windows-mode", "entry": entry.get("id") if entry else None,
           "name": (entry or {}).get("name") or info.get("product") or info.get("file"),
           "alternative": (entry or {}).get("alternative"), "reason": (entry or {}).get("reason"),
           "calls": bool((entry or {}).get("calls"))}
    # What Windows mode itself could do with it, whatever is recommended. A
    # program with a Wild OS version still offers Windows mode as the second
    # choice, and that choice used to read as "set up Windows mode first" on
    # PCs where it was set up and running.
    if host.get("edition") == "home":
        windows = "home"
    elif host.get("memory", 0) and host["memory"] < 7.5 * 1024 ** 3:
        windows = "cannot"
    elif not host.get("installed") or host.get("state") not in ("ready", "", "unknown"):
        # A state that couldn't be read (Windows mode busy on a slow PC)
        # is not "not set up": Windows mode itself says if it can't run it.
        windows = "setup"
    else:
        windows = "windows-mode"
    out["windows"] = windows
    if info.get("kind") == "not-windows":
        out["action"] = "not-windows"
    elif out["engine"] in ("native", "unsupported"):
        out["action"] = out["engine"]
    else:
        out["action"] = windows
    if windows == "cannot" and not out.get("reason"):
        out["reason"] = "Windows mode needs at least 8 GB of memory, and this PC has less."
    return out
