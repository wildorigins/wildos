# Wild OS Windows mode: keeps Windows lean. The machine-wide policies,
# services, scheduled tasks, apps and features Windows mode does without.
#
# setup.ps1 runs it with -Install, and the host runs it the same way when a
# Windows setup gave it to has an older $version (a Windows set up before
# this existed is left as it is). -Install
# puts a copy in C:\Program Files\Wild OS and a start-up task that runs it at
# every boot, because Windows and its apps switch things back: Office turns
# Windows Search on for Outlook, and services setup had disabled were found
# back on Automatic. After every Windows update the boot run also clears the
# superseded components the update leaves behind (about 5 GB each month).
#
# Only supported switches: Group Policy values, service start types,
# provisioned app removal, DISM capabilities and features. Measured on
# build 26300 in November 2026: disk 22.4 GB to 15.5 GB, a cumulative update
# installed on top without errors and left every setting in place.

param([switch]$Install)

$version = '3'
# The capabilities and features list below, which takes a while to check.
$featuresVersion = '2'
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
$dir = 'C:\Program Files\Wild OS'
$stateDir = 'C:\ProgramData\WildOS'
$stateFile = "$stateDir\tune.json"
$taskName = 'Keep Windows lean'
New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
$changes = New-Object System.Collections.Generic.List[string]

function Log($line) {
    Add-Content -Path "$stateDir\tune.log" -Value "$((Get-Date).ToString('s')) $line"
}

function Set-Policy($path, $name, $value) {
    $cur = (Get-ItemProperty -Path $path -Name $name -ErrorAction SilentlyContinue).$name
    if ($cur -eq $value) { return }
    if (-not (Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
    New-ItemProperty -Path $path -Name $name -Value $value -PropertyType DWord -Force | Out-Null
    $changes.Add("policy $name")
}

function Set-StartType($name, $type) {
    $s = Get-Service $name -ErrorAction SilentlyContinue
    if (-not $s -or $s.StartType -eq $type) { return }
    if ($type -eq 'Disabled') { Stop-Service $name -Force -ErrorAction SilentlyContinue }
    Set-Service $name -StartupType $type -ErrorAction SilentlyContinue
    $changes.Add("service $name $type")
}

$state = @{}
if (Test-Path $stateFile) {
    try { (Get-Content $stateFile -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $state[$_.Name] = $_.Value } } catch { }
}

if ($Install) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    if ($PSCommandPath -and ($PSCommandPath -ne "$dir\tune.ps1")) { Copy-Item -Force $PSCommandPath "$dir\tune.ps1" }
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$dir\tune.ps1`""
    $trigger = New-ScheduledTaskTrigger -AtStartup
    # Two minutes in, so it never competes with the first app of the session.
    $trigger.Delay = 'PT2M'
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Hours 1) -Priority 7 -StartWhenAvailable `
        -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    Register-ScheduledTask -TaskPath '\Wild OS\' -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings `
        -Principal $principal -Force | Out-Null
}

$p = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows'
# Diagnostic data at Required (1), the lowest Pro honours.
Set-Policy "$p\DataCollection" 'AllowTelemetry' 1
Set-Policy "$p\AdvertisingInfo" 'DisabledByGroupPolicy' 1
Set-Policy "$p\CloudContent" 'DisableWindowsConsumerFeatures' 1
Set-Policy "$p\CloudContent" 'DisableSoftLanding' 1
Set-Policy "$p\CloudContent" 'DisableTailoredExperiencesWithDiagnosticData' 1
Set-Policy "$p\CloudContent" 'DisableConsumerAccountStateContent' 1
Set-Policy "$p\Windows Search" 'DisableWebSearch' 1
Set-Policy "$p\Windows Search" 'ConnectedSearchUseWeb' 0
Set-Policy "$p\Windows Search" 'AllowCortana' 0
Set-Policy "$p\WindowsCopilot" 'TurnOffWindowsCopilot' 1
Set-Policy "$p\WindowsAI" 'DisableAIDataAnalysis' 1
Set-Policy "$p\WindowsAI" 'AllowRecallEnablement' 0
Set-Policy "$p\OneDrive" 'DisableFileSyncNGSC' 1
Set-Policy "$p\System" 'EnableCdp' 0
Set-Policy "$p\System" 'EnableActivityFeed' 0
Set-Policy "$p\System" 'PublishUserActivities' 0
Set-Policy "$p\GameDVR" 'AllowGameDVR' 0
Set-Policy "$p\DeliveryOptimization" 'DODownloadMode' 0
Set-Policy 'HKLM:\SOFTWARE\Policies\Microsoft\Dsh' 'AllowNewsAndInterests' 0
Set-Policy 'HKLM:\SOFTWARE\Policies\Microsoft\Edge' 'StartupBoostEnabled' 0
Set-Policy 'HKLM:\SOFTWARE\Policies\Microsoft\Edge' 'BackgroundModeEnabled' 0
# Windows mode signs in to its account with a password it keeps. Turning
# that account into a Microsoft account (activation's troubleshooter, the
# Store and Office all offer to) swaps the password for the Microsoft one,
# and Windows 11 then allows only Windows Hello for it, so every sign-in
# from Wild OS stops at "The username or password is incorrect". Office and
# the Store can still sign in to a Microsoft account for themselves.
# While Windows mode activates Windows with a licence on a Microsoft account
# (Settings, issue 56), the policy is off for a second account made only for
# that, and the host puts it back and removes the marker afterwards. A
# marker older than a day was left behind and doesn't count.
$activation = Get-Item "$stateDir\activation.json" -ErrorAction SilentlyContinue
if (-not $activation -or $activation.LastWriteTime -lt (Get-Date).AddDays(-1)) {
    Set-Policy 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' 'NoConnectedUser' 1
}
Set-Policy 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\PasswordLess\Device' 'DevicePasswordLessBuildVersion' 0
# With 3.5 GB or more Windows gives every service its own process. Grouped
# as on smaller PCs they share one: 71 svchost processes became 38 and
# about 80 MB came back, with Remote Desktop unaffected.
Set-Policy 'HKLM:\SYSTEM\CurrentControlSet\Control' 'SvcHostSplitThresholdInKB' 0x2000000

# Telemetry upload and its push routing, maps, Xbox, fax, the retail demo,
# the compatibility assistant (which checks every program as it starts),
# error reporting, SysMain's preloading, inventory collection, Windows' AI
# fabric and link tracking. Defender, Windows Update and everything
# RemoteApp needs stay.
foreach ($svc in 'DiagTrack', 'dmwappushservice', 'MapsBroker', 'XblAuthManager', 'XblGameSave', 'XboxNetApiSvc', 'XboxGipSvc',
                 'Fax', 'RetailDemo', 'PcaSvc', 'WerSvc', 'SysMain', 'InventorySvc', 'WSAIFabricSvc', 'TrkWks') {
    Set-StartType $svc 'Disabled'
}
# Connected devices (Phone Link, resume on another device) only when asked.
Set-StartType 'CDPSvc' 'Manual'
# Indexing costs disk and processor time, but Outlook's search is built on
# it, so it stays whenever Outlook is installed.
$outlook = Test-Path 'C:\Program Files\Microsoft Office\root\Office16\OUTLOOK.EXE'
if (-not $outlook) {
    Set-StartType 'WSearch' 'Disabled'
} elseif ((Get-Service 'WSearch' -ErrorAction SilentlyContinue).StartType -eq 'Disabled') {
    # Setup runs this before Office is installed, so a run after Outlook
    # arrives puts indexing back as Windows has it (delayed start).
    sc.exe config WSearch start= delayed-auto | Out-Null
    Start-Service 'WSearch' -ErrorAction SilentlyContinue
    $changes.Add('service WSearch back on for Outlook')
}

$tasks = '\Microsoft\Windows\Application Experience\Microsoft Compatibility Appraiser',
         '\Microsoft\Windows\Application Experience\ProgramDataUpdater',
         '\Microsoft\Windows\Application Experience\StartupAppTask',
         '\Microsoft\Windows\Application Experience\MareBackup',
         '\Microsoft\Windows\Application Experience\PcaPatchDbTask',
         '\Microsoft\Windows\Customer Experience Improvement Program\Consolidator',
         '\Microsoft\Windows\Customer Experience Improvement Program\UsbCeip',
         '\Microsoft\Windows\Device Information\Device',
         '\Microsoft\Windows\Device Information\Device User',
         '\Microsoft\Windows\DiskDiagnostic\Microsoft-Windows-DiskDiagnosticDataCollector',
         '\Microsoft\Windows\Feedback\Siuf\DmClient',
         '\Microsoft\Windows\Feedback\Siuf\DmClientOnScenarioDownload',
         '\Microsoft\Windows\Flighting\FeatureConfig\UsageDataReporting',
         '\Microsoft\Windows\Flighting\OneSettings\RefreshCache',
         '\Microsoft\Windows\Maintenance\WinSAT',
         '\Microsoft\Windows\Maps\MapsUpdateTask',
         '\Microsoft\Windows\Maps\MapsToastTask',
         '\Microsoft\Windows\Windows Error Reporting\QueueReporting',
         '\Microsoft\XblGameSave\XblGameSaveTask'
foreach ($t in $tasks) {
    $cut = $t.LastIndexOf([char]92)
    $task = Get-ScheduledTask -TaskPath $t.Substring(0, $cut + 1) -TaskName $t.Substring($cut + 1) -ErrorAction SilentlyContinue
    if ($task -and $task.State -ne 'Disabled') {
        $task | Disable-ScheduledTask -ErrorAction SilentlyContinue | Out-Null
        $changes.Add("task $($t.Substring($cut + 1))")
    }
}

# Apps nobody asked for, removed for every account and from the image, so
# new accounts and feature updates do not bring them back. The Store, Edge,
# Photos, Notepad, Paint, Calculator and the Snipping Tool stay.
$unwanted = 'Clipchamp.Clipchamp', 'Microsoft.BingNews', 'Microsoft.BingWeather', 'Microsoft.BingSearch', 'Microsoft.GamingApp',
            'Microsoft.MicrosoftSolitaireCollection', 'Microsoft.Xbox.TCUI', 'Microsoft.XboxGamingOverlay', 'Microsoft.XboxIdentityProvider',
            'Microsoft.XboxSpeechToTextOverlay', 'Microsoft.GetHelp', 'Microsoft.Getstarted', 'MicrosoftTeams', 'MSTeams', 'Microsoft.YourPhone',
            'Microsoft.OutlookForWindows', 'Microsoft.Todos', 'Microsoft.PowerAutomateDesktop', 'Microsoft.MicrosoftOfficeHub', 'Microsoft.People',
            'Microsoft.WindowsFeedbackHub', 'Microsoft.ZuneMusic', 'Microsoft.ZuneVideo', 'Microsoft.549981C3F5F10', 'Microsoft.Copilot',
            'Microsoft.Windows.DevHome', 'Microsoft.MicrosoftStickyNotes', 'Microsoft.WindowsMaps', 'Microsoft.WindowsAlarms',
            'MicrosoftWindows.CrossDevice', 'MicrosoftWindows.Client.WebExperience', 'Microsoft.WidgetsPlatformRuntime',
            'MicrosoftCorporationII.QuickAssist', 'Microsoft.WindowsSoundRecorder', 'MicrosoftCorporationII.MicrosoftFamily',
            'Microsoft.MicrosoftJournal'
foreach ($pkg in Get-AppxProvisionedPackage -Online -ErrorAction SilentlyContinue) {
    if ($unwanted -contains $pkg.DisplayName) {
        Remove-AppxProvisionedPackage -Online -PackageName $pkg.PackageName -ErrorAction SilentlyContinue | Out-Null
        $changes.Add("provisioned $($pkg.DisplayName)")
    }
}
foreach ($pkg in Get-AppxPackage -AllUsers -ErrorAction SilentlyContinue) {
    if ($unwanted -contains $pkg.Name -and -not $pkg.NonRemovable) {
        Remove-AppxPackage -Package $pkg.PackageFullName -AllUsers -ErrorAction SilentlyContinue
        $changes.Add("app $($pkg.Name)")
    }
}

# Windows holds about 7 GB back for updates; this disk has room to spare.
if (((dism /Online /Get-ReservedStorageState) -join ' ') -notmatch 'Disabled') {
    dism /Online /Set-ReservedStorageState /State:Disabled /Quiet | Out-Null
    $changes.Add('reserved storage off')
}
# Restore points cannot be reached in Windows mode, which has no desktop;
# Wild OS keeps Windows' disk itself.
$restore = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SystemRestore' -ErrorAction SilentlyContinue
if ($restore -and $restore.RPSessionInterval -ne 0) {
    Disable-ComputerRestore -Drive 'C:\' -ErrorAction SilentlyContinue
    vssadmin delete shadows /for=C: /all /quiet | Out-Null
    $changes.Add('restore points off')
}

$build = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$buildId = "$($build.CurrentBuild).$($build.UBR)"

# Listing capabilities and features takes a while, so only for a new
# list ($featuresVersion) or a new Windows build.
if ($state.features -ne "$featuresVersion/$buildId") {
    foreach ($c in Get-WindowsCapability -Online -ErrorAction SilentlyContinue | Where-Object State -eq 'Installed') {
        foreach ($want in 'App.StepsRecorder', 'Hello.Face', 'MathRecognizer', 'Media.WindowsMediaPlayer', 'Microsoft.Windows.WordPad',
                          'Microsoft.Wallpapers.Extended', 'OneCoreUAP.OneSync', 'Microsoft.Windows.PowerShell.ISE', 'App.Support.QuickAssist') {
            if ($c.Name -like "$want*") {
                Remove-WindowsCapability -Online -Name $c.Name -ErrorAction SilentlyContinue | Out-Null
                $changes.Add("capability $want")
            }
        }
    }
    foreach ($f in 'WorkFolders-Client', 'Recall') {
        $s = Get-WindowsOptionalFeature -Online -FeatureName $f -ErrorAction SilentlyContinue
        if ($s -and $s.State -eq 'Enabled') {
            Disable-WindowsOptionalFeature -Online -FeatureName $f -NoRestart -ErrorAction SilentlyContinue | Out-Null
            $changes.Add("feature $f")
        }
    }
    $state.features = "$featuresVersion/$buildId"
}

# Each update keeps the components it replaced. Cleared once per build, at
# start-up rather than when the host installs this (it takes up to 12
# minutes), and never with a restart pending, when DISM refuses anyway.
# /ResetBase means an update can no longer be uninstalled from inside
# Windows; Wild OS has no way to reach that in Windows mode anyway.
$pending = Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending'
if (-not $Install -and -not $pending -and $state.cleaned -ne $buildId) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    dism /Online /Cleanup-Image /StartComponentCleanup /ResetBase /Quiet | Out-Null
    if ($LASTEXITCODE -eq 0) { $state.cleaned = $buildId }
    $changes.Add("component cleanup exit $LASTEXITCODE in $([int]$sw.Elapsed.TotalMinutes) min")
}

$state.version = $version
$state.build = $buildId
$state.last = (Get-Date).ToString('s')
$state | ConvertTo-Json -Compress | Set-Content -Encoding UTF8 $stateFile
$summary = if ($changes.Count) { $changes -join ', ' } else { 'nothing to change' }
Log "version $version, build ${buildId}: $summary"
"tuned: $($changes.Count) change(s)"
