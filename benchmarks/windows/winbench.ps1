# winbench.ps1 <info|nolock|boot>: the Windows side of bench.sh, measured
# the same way where Windows allows it. Runs as SYSTEM through the guest agent.
param([string]$Action = "info")
$ErrorActionPreference = "Continue"
$Out = "C:\bench-out"; New-Item -ItemType Directory -Force $Out | Out-Null

function Info {
  $os = Get-CimInstance Win32_OperatingSystem
  $cv = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
  "== os"; "$($os.Caption) $($cv.DisplayVersion) build $($cv.CurrentBuild).$($cv.UBR)"
  "== cpu"; (Get-CimInstance Win32_Processor).Name; "logical $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)"
  "== memory_mb $([int]($os.TotalVisibleMemorySize/1024))"
  "== disk"; Get-Volume -DriveLetter C | Format-Table -AutoSize FileSystem, Size, SizeRemaining | Out-String
  "== secure_boot $(try { Confirm-SecureBootUEFI } catch { 'unknown' })"
  "== tpm $((Get-Tpm).TpmPresent)"
  $dg = Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction SilentlyContinue
  "== vbs_status $($dg.VirtualizationBasedSecurityStatus) services_running $($dg.SecurityServicesRunning -join ',')"
  $bl = Get-BitLockerVolume -MountPoint C: -ErrorAction SilentlyContinue
  "== bitlocker protection $($bl.ProtectionStatus) volume $($bl.VolumeStatus) encrypted_pct $($bl.EncryptionPercentage) method $($bl.EncryptionMethod)"
  "== manage-bde"; manage-bde -status C: | Select-String "Conversion|Percentage|Protection|Method|Key Protectors" | ForEach-Object { $_.Line.Trim() }
  $mp = Get-MpComputerStatus -ErrorAction SilentlyContinue
  "== defender realtime $($mp.RealTimeProtectionEnabled) antivirus $($mp.AntivirusEnabled) tamper $($mp.IsTamperProtected) engine $($mp.AMEngineVersion) signatures $($mp.AntivirusSignatureVersion)"
  "== smart_app_control $((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -ErrorAction SilentlyContinue).VerifiedAndReputablePolicyState)"
  "== hvci $((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity' -ErrorAction SilentlyContinue).Enabled) vbs_configured $($dg.VirtualizationBasedSecurityConfigured -join ',') hypervisor_present $((Get-CimInstance Win32_ComputerSystem).HypervisorPresent)"
  "== power plan"; powercfg /getactivescheme
  "== hotfixes"; Get-HotFix | Sort-Object InstalledOn | Select-Object -Last 5 | Format-Table -AutoSize HotFixID, InstalledOn | Out-String
}

# Keeps the machine awake through the runs: no lock, no screen blanking and no sleep.
function NoLock {
  powercfg /change monitor-timeout-ac 0
  powercfg /change standby-timeout-ac 0
  powercfg /change hibernate-timeout-ac 0
  $k = "HKLM:\SOFTWARE\Policies\Microsoft\Windows\Personalization"
  New-Item -Force $k | Out-Null; Set-ItemProperty $k NoLockScreen 1 -Type DWord
  "nolock: done"
}

# Windows writes event 100 only once the boot trace ends (the system has gone
# idle after sign-in), often minutes after the desktop is up, so this can wait.
function Ev100([int]$WaitS) {
  $bootT = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
  $until = (Get-Date).AddSeconds($WaitS)
  do {
    $e = Get-WinEvent -FilterHashtable @{LogName = "Microsoft-Windows-Diagnostics-Performance/Operational"; Id = 100} -MaxEvents 1 -ErrorAction SilentlyContinue
    if ($e -and $e.TimeCreated -gt $bootT) {
      $x = [xml]$e.ToXml(); $d = @{}; foreach ($i in $x.Event.EventData.Data) { $d[$i.Name] = $i.'#text' }
      return "event100 BootTime_ms $($d.BootTime) MainPathBootTime_ms $($d.MainPathBootTime) BootPostBootTime_ms $($d.BootPostBootTime) BootUserProfileProcessingTime_ms $($d.BootUserProfileProcessingTime) BootExplorerInitTime_ms $($d.BootExplorerInitTime) written_after_s $('{0:N0}' -f ($e.TimeCreated - $bootT).TotalSeconds)"
    }
    if ((Get-Date) -ge $until) { return "event100 not written for this boot (waited $WaitS s)" }
    Start-Sleep 10
  } while ($true)
}

function Boot {
  $n = (Get-ChildItem "$Out\boot-*.txt" -ErrorAction SilentlyContinue).Count + 1
  $os = Get-CimInstance Win32_OperatingSystem
  $bootT = $os.LastBootUpTime
  $r = @()
  $fw = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power" -ErrorAction SilentlyContinue).FwPOSTTime
  $r += "firmware_ms $fw"
  $r += Ev100 0
  $ex = Get-Process explorer -IncludeUserName -ErrorAction SilentlyContinue | Where-Object UserName -like "*bench" | Sort-Object StartTime | Select-Object -First 1
  if ($ex) { $r += "shell_start explorer $('{0:N2}' -f ($ex.StartTime - $bootT).TotalSeconds)" }
  $sh = Get-Process StartMenuExperienceHost, SearchHost -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
  if ($sh) { $r += "start_menu_host $('{0:N2}' -f ($sh.StartTime - $bootT).TotalSeconds)" }
  $r += "uptime_s $('{0:N0}' -f ((Get-Date) - $bootT).TotalSeconds)"
  $r | Set-Content "$Out\boot-$n.txt"; $r
}


switch ($Action) { "info" { Info } "nolock" { NoLock } "boot" { Boot } "ev100" { Ev100 600 } }
