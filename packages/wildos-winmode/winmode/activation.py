"""Activating Windows with a digital licence on the person's Microsoft
account (issue 56).

Microsoft moves such a licence to new hardware only through activation's
troubleshooter ("I changed hardware on this device"), and only for a
Microsoft account that is an administrator on the PC. Windows mode's own
account must never become one: that swaps its password for the Microsoft
one and every sign-in from Wild OS fails (issue 12), so tune.ps1 sets
NoConnectedUser. Instead, for the length of one activation, a second local
administrator ("activate") is made, the policy is lifted, and the person
does the troubleshooter in a Windows desktop signed in to that account.
Afterwards the account and its profile are deleted and the policy goes
back. The activation belongs to the virtual PC, so it stays.

The record in the service's state (state["activation"]) is written before
anything changes in Windows and removed only once everything is back, so a
restart of Wild OS or of Windows mode part way through still ends with the
clean-up (Service.slow_check resumes it).
"""

import re
import sys
import time

from . import qga, virsh

ACCOUNT = "activate"
# Keeps Windows from being paused or saved while the person works in it.
HOLD = "activation"
# The policy is off for at most this long; past it the clean-up runs even
# with the desktop still open.
LIMIT = 3600
MARKER = r"C:\ProgramData\WildOS\activation.json"
POLICY_KEY = r"HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"
LICENSE_STATUS_SCRIPT = (
    "(Get-CimInstance SoftwareLicensingProduct -Filter \"ApplicationID='55c92734-d682-4d71-983e-d6ec3f16059f'"
    " and PartialProductKey is not null\" | Select-Object -First 1).LicenseStatus")


def quoted(text):
    """A PowerShell single-quoted string."""
    return "'" + str(text).replace("'", "''") + "'"


def safe_name(account, keep):
    """Only a plain account name, and never Windows mode's own."""
    return bool(re.fullmatch(r"[A-Za-z0-9_-]{1,20}", account or "")) and account.lower() != (keep or "").lower()


def create_script(account, keep):
    """Makes the account (with no password until the guest agent sets one,
    so it never appears on a command line), an administrator, then the
    marker tune.ps1 honours and the policy lifted. Its last line is the
    account's SID. Windows allows descriptions of 48 characters at most."""
    return f"""
$ErrorActionPreference = 'Stop'
$name = {quoted(account)}
$keep = {quoted(keep)}
if ($name -ieq $keep) {{ throw 'refusing the Windows mode account' }}
$u = Get-LocalUser -Name $name -ErrorAction SilentlyContinue
if (-not $u) {{
    $u = New-LocalUser -Name $name -NoPassword -AccountNeverExpires -FullName 'Activation' `
        -Description 'Wild OS: for activating Windows, then removed'
}}
Set-LocalUser -Name $name -PasswordNeverExpires $true
if (-not (Get-LocalGroupMember -SID 'S-1-5-32-544' | Where-Object {{ $_.SID -eq $u.SID }})) {{
    Add-LocalGroupMember -SID 'S-1-5-32-544' -Member $u
}}
@{{ account = $name; sid = $u.SID.Value; started = (Get-Date).ToString('s') }} | ConvertTo-Json -Compress |
    Set-Content -Encoding UTF8 {quoted(MARKER)}
if (-not (Test-Path {quoted(POLICY_KEY)})) {{ New-Item -Path {quoted(POLICY_KEY)} -Force | Out-Null }}
New-ItemProperty -Path {quoted(POLICY_KEY)} -Name 'NoConnectedUser' -Value 0 -PropertyType DWord -Force | Out-Null
"sid $($u.SID.Value)"
"""


def cleanup_script(account, sid, keep):
    """Puts the policy back first, since that matters most, then signs the
    account's sessions out and deletes it and its profile. Never touches
    Windows mode's own account, even if the SID recorded were its. The last
    line is "done", or "left" and what is still there."""
    return f"""
$ErrorActionPreference = 'Continue'
$name = {quoted(account)}
$sid = {quoted(sid or '')}
$keep = {quoted(keep)}
New-ItemProperty -Path {quoted(POLICY_KEY)} -Name 'NoConnectedUser' -Value 1 -PropertyType DWord -Force | Out-Null
Remove-Item -Force {quoted(MARKER)} -ErrorAction SilentlyContinue
$keepSid = (Get-LocalUser -Name $keep -ErrorAction SilentlyContinue).SID.Value
$u = $null
if ($sid) {{ $u = Get-LocalUser -SID $sid -ErrorAction SilentlyContinue }}
if (-not $u) {{ $u = Get-LocalUser -Name $name -ErrorAction SilentlyContinue }}
if ($u -and ($u.Name -ieq $keep -or $u.SID.Value -eq $keepSid)) {{ 'refused'; exit 1 }}
if ($u) {{ $sid = $u.SID.Value; $name = $u.Name }}
# A Microsoft account keeps its local name, so sessions are found by it:
# from quser (any session, even one with nothing running yet) and from the
# processes running as it.
function Sessions {{
    $ids = @()
    foreach ($line in (quser 2>$null)) {{
        $t = $line.Trim().TrimStart('>') -split '\\s+'
        if ($t.Count -gt 1 -and $t[0] -ieq $name) {{ $ids += ($t[1..($t.Count - 1)] | Where-Object {{ $_ -match '^\\d+$' }} | Select-Object -First 1) }}
    }}
    foreach ($p in (Get-Process -IncludeUserName -ErrorAction SilentlyContinue)) {{
        if ($p.SessionId -ne 0 -and $p.UserName -and ($p.UserName -split '\\\\')[-1] -ieq $name) {{ $ids += $p.SessionId }}
    }}
    $ids | Where-Object {{ $_ -ne $null }} | ForEach-Object {{ [int]$_ }} | Sort-Object -Unique
}}
for ($i = 0; $i -lt 30; $i++) {{
    $s = @(Sessions)
    if ($s.Count -eq 0) {{ break }}
    foreach ($id in $s) {{ logoff.exe $id 2>$null }}
    Start-Sleep -Seconds 2
}}
if ($u) {{ Remove-LocalUser -SID $sid -ErrorAction SilentlyContinue }}
# The profile's registry hive unloads a moment after the sign-out.
if ($sid) {{
    for ($i = 0; $i -lt 20; $i++) {{
        $p = Get-CimInstance Win32_UserProfile -Filter "SID='$sid'" -ErrorAction SilentlyContinue
        if (-not $p) {{ break }}
        try {{ $p | Remove-CimInstance -ErrorAction Stop }} catch {{ Start-Sleep -Seconds 3 }}
    }}
}}
$left = @()
if (@(Sessions).Count) {{ $left += 'session' }}
if (Get-LocalUser -Name $name -ErrorAction SilentlyContinue) {{ $left += 'account' }}
if ($sid -and (Get-CimInstance Win32_UserProfile -Filter "SID='$sid'" -ErrorAction SilentlyContinue)) {{ $left += 'profile' }}
if ((Get-ItemProperty -Path {quoted(POLICY_KEY)} -Name 'NoConnectedUser' -ErrorAction SilentlyContinue).NoConnectedUser -ne 1) {{ $left += 'policy' }}
if ($left.Count) {{ "left $($left -join ' ')" }} else {{ 'done' }}
"""


def last_line(out):
    lines = [line.strip() for line in (out or "").splitlines() if line.strip()]
    return lines[-1] if lines else ""


class Activation:
    """The service's side: the record, and the scripts run through the
    guest agent. Every method runs on a worker thread; the record is the
    service's state, saved at each step."""

    def __init__(self, svc, new_password):
        self.svc = svc
        self.new_password = new_password
        # Started by this run of the service. A record found at start-up
        # belongs to a run that ended part way, and is cleaned up.
        self.owned = False
        self.ending = False

    @property
    def rec(self):
        return self.svc.state.get("activation")

    def status(self):
        rec = self.rec
        if not rec:
            return None
        return {"stage": rec.get("stage"), "account": rec.get("account"), "started": rec.get("started")}

    def save(self, rec):
        if rec is None:
            self.svc.state.pop("activation", None)
        else:
            self.svc.state["activation"] = rec
        self.svc.save_state()

    def overdue(self, now=None):
        rec = self.rec
        return bool(rec) and (now if now is not None else time.time()) - rec.get("started", 0) > LIMIT

    def needs_cleanup(self):
        return bool(self.rec) and not self.ending and (not self.owned or self.overdue())

    def keep(self):
        return self.svc.cfg["account"]

    def license_status(self):
        _, out, _ = qga.powershell(self.svc.domain, LICENSE_STATUS_SCRIPT, 60)
        line = last_line(out)
        return int(line) if line.isdigit() else None

    def start(self):
        """Windows is running. {account, password} for the desktop, or
        {message} for why not."""
        if self.rec:
            # Left from an earlier try: that one goes first, so this one
            # starts from a fresh account and profile.
            why = self.end()
            if why:
                return {"message": why}
        try:
            if self.license_status() == 1:
                self.record_activated(None)
                return {"message": "Windows is already activated.", "activated": True}
        except (virsh.VirshError, ValueError, KeyError, TimeoutError) as e:
            self.log("licence status:", e)
        account, keep = ACCOUNT, self.keep()
        if not safe_name(account, keep):
            return {"message": "Windows mode couldn't add the account for activating."}
        self.owned = True
        # Written before Windows changes, so an interruption from here on is
        # cleaned up.
        self.save({"stage": "creating", "account": account, "sid": None, "started": time.time()})
        try:
            code, out, err = qga.powershell(self.svc.domain, create_script(account, keep), 120)
            line = last_line(out)
            if code != 0 or not line.startswith("sid S-1-"):
                raise RuntimeError((err or out).strip()[-300:] or f"exit {code}")
            sid = line.split(" ", 1)[1]
            password = self.new_password()
            qga.set_password(self.svc.domain, account, password)
        except (virsh.VirshError, ValueError, KeyError, TimeoutError, RuntimeError) as e:
            self.log("adding the account for activating:", e)
            self.end()
            # Whatever is left is the service's to clean up from now on.
            self.owned = False
            return {"message": "Windows mode couldn't add the account for activating. Try again, or restart the PC."}
        self.save(dict(self.rec, stage="open", sid=sid))
        return {"account": account, "password": password}

    def check(self):
        """Whether Windows says it is activated, which is recorded when so."""
        if not self.rec:
            return False
        try:
            activated = self.license_status() == 1
        except (virsh.VirshError, ValueError, KeyError, TimeoutError) as e:
            self.log("licence status:", e)
            return False
        if activated:
            self.record_activated("account")
        return activated

    def record_activated(self, source):
        """Settings' licence row. Activated during this flow means the
        licence came from the Microsoft account; found activated before it
        started, the licence is whatever Windows already had."""
        setup = self.svc.setup
        lic = dict((setup.rec or {}).get("licence") or {})
        if lic.get("activated") is True:
            return
        if source:
            lic["source"] = source
        lic["activated"] = True
        setup.save(licence=lic)

    def end(self):
        """Signs the account out, deletes it and its profile and puts the
        policy back. "" when all of that is done (or there was nothing to
        do), else why not; the record then stays, and the clean-up is
        tried again."""
        rec = self.rec
        if not rec:
            self.owned = False
            return ""
        self.ending = True
        # Until it succeeds, the service retries by itself (needs_cleanup).
        self.owned = False
        try:
            try:
                if not qga.ping(self.svc.domain, 5):
                    return "Windows isn't running, so the account for activating is removed the next time it starts."
                self.save(dict(rec, stage="ending"))
                code, out, err = qga.powershell(self.svc.domain, cleanup_script(rec.get("account") or ACCOUNT, rec.get("sid"),
                                                                                self.keep()), 300)
            except (virsh.VirshError, ValueError, KeyError, TimeoutError) as e:
                self.log("removing the account for activating:", e)
                return "Windows mode couldn't remove the account for activating yet. It tries again by itself."
            line = last_line(out)
            if line != "done":
                self.log("removing the account for activating:", line or (err or "").strip()[-300:])
                return "Windows mode couldn't remove the account for activating yet. It tries again by itself."
            self.save(None)
            self.log("the account for activating is removed and Windows mode's own account is protected again")
            return ""
        finally:
            self.ending = False

    def log(self, *parts):
        print(*parts, file=sys.stderr, flush=True)
