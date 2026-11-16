# wtests.ps1 <fetch|run>: the CPU and disk tests on Windows, matched to the
# PTS profiles and bench.sh the Linux systems ran. fetch downloads everything;
# run measures, three runs each, nothing downloaded meanwhile.
param([string]$Action = "run")
$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"
$T = "C:\bt"; $Out = "C:\bench-out\tests"; New-Item -ItemType Directory -Force $T, $Out | Out-Null

function Fetch {
  $files = @{
    "7z2601-x64.exe" = "https://www.7-zip.org/a/7z2601-x64.exe"
    "stockfish.zip" = "https://github.com/official-stockfish/Stockfish/releases/download/sf_19/stockfish-windows-x86-64-universal.zip"
    "zstd.zip" = "https://github.com/facebook/zstd/releases/download/v1.5.4/zstd-v1.5.4-win64.zip"
    "silesia.tar" = "http://wanos.co/assets/silesia.tar"
    "openssl.zip" = "https://download.firedaemon.com/FireDaemon-OpenSSL/openssl-3.5.5.zip"
    "fio.msi" = "https://github.com/axboe/fio/releases/download/fio-3.43/fio-3.43-x64.msi"
  }
  foreach ($f in $files.Keys) {
    if (-not (Test-Path "$T\$f")) { Invoke-WebRequest -UseBasicParsing $files[$f] -OutFile "$T\$f" }
    "$f $((Get-Item "$T\$f").Length)"
  }
  $h = (Get-FileHash "$T\silesia.tar" -Algorithm SHA256).Hash
  "silesia sha256 $h (profile: EA122ED051DC7A6C58D2BB56BB05B34D9F1537C4DC9E71519142E2CA8CD6338D)"
  Start-Process "$T\7z2601-x64.exe" -ArgumentList "/S" -Wait
  Start-Process msiexec -ArgumentList "/i `"$T\fio.msi`" /qn /norestart" -Wait
  foreach ($z in "stockfish", "zstd", "openssl") {
    if (-not (Test-Path "$T\$z")) { Expand-Archive "$T\$z.zip" "$T\$z" -Force }
  }
  "7z: $(& 'C:\Program Files\7-Zip\7z.exe' i | Select-Object -First 2 | Select-Object -Last 1)"
  "stockfish: $(Get-ChildItem -Recurse "$T\stockfish" -Filter *.exe | ForEach-Object FullName)"
  "zstd: $(& (Get-ChildItem -Recurse "$T\zstd" -Filter zstd.exe | Select-Object -First 1).FullName -V)"
  "openssl: $(Get-ChildItem -Recurse "$T\openssl" -Filter openssl.exe | ForEach-Object FullName)"
  "fio: $(& 'C:\Program Files\fio\fio.exe' --version)"
}

function Stat($name, $vals, $unit) {
  $m = ($vals | Measure-Object -Average).Average
  $sd = if ($vals.Count -gt 1) { [math]::Sqrt(($vals | ForEach-Object { ($_ - $m) * ($_ - $m) } | Measure-Object -Sum).Sum / ($vals.Count - 1)) } else { 0 }
  "$name|$('{0:N2}' -f $m) $unit|runs $(($vals | ForEach-Object { '{0:N2}' -f $_ }) -join ' / ')|stdev $('{0:N2}' -f (100 * $sd / $m))%|range $('{0:N2}' -f (100 * (($vals | Measure-Object -Maximum).Maximum - ($vals | Measure-Object -Minimum).Minimum) / $m))%"
}

function Run {
  $r = @()
  # 7-Zip: PTS reads the Avr: line's Rating columns; Tot: kept in the raw log.
  $c = @(); $d = @()
  for ($i = 1; $i -le 3; $i++) {
    $o = & "C:\Program Files\7-Zip\7z.exe" b 2>&1 | Out-String; $o | Set-Content "$Out\7zip-$i.txt"
    $a = ($o -split "`n" | Where-Object { $_ -match '^Avr:' }) -replace '\|', ' ' -split '\s+' | Where-Object { $_ }
    $c += [double]$a[4]; $d += [double]$a[8]
  }
  $r += Stat "7zip 26.01 compression rating (Avr)" $c "MIPS"
  $r += Stat "7zip 26.01 decompression rating (Avr)" $d "MIPS"

  $sf = (Get-ChildItem -Recurse "$T\stockfish" -Filter *.exe | Select-Object -First 1).FullName
  $v = @()
  for ($i = 1; $i -le 3; $i++) {
    $o = cmd /c "`"$sf`" bench 128 4 24 default depth 2>&1" | Out-String; $o | Set-Content "$Out\stockfish-$i.txt"
    $v += [double](($o -split "`n" | Where-Object { $_ -match 'Nodes/second' }) -replace '\D', '')
  }
  $r += Stat "stockfish 19 bench 128 4 24 default depth" $v "nodes/s"

  $zs = (Get-ChildItem -Recurse "$T\zstd" -Filter zstd.exe | Select-Object -First 1).FullName
  $c = @(); $d = @()
  Push-Location $T
  for ($i = 1; $i -le 3; $i++) {
    $o = cmd /c "`"$zs`" -T4 -S -i30 -b19 silesia.tar 2>&1" | Out-String; $o | Set-Content "$Out\zstd-$i.txt"
    $last = ($o -replace "`r", "`n" -split "`n" | Where-Object { $_ -match 'MB/s.*MB/s' } | Select-Object -Last 1)
    $mb = [regex]::Matches($last, '([\d.]+) MB/s') | ForEach-Object { [double]$_.Groups[1].Value }
    $c += $mb[0]; $d += $mb[1]
  }
  Pop-Location
  $r += Stat "zstd 1.5.4 level 19 compression (-T4 -S -i30 -b19 silesia.tar)" $c "MB/s"
  $r += Stat "zstd 1.5.4 level 19 decompression" $d "MB/s"

  $ossl = (Get-ChildItem -Recurse "$T\openssl" -Filter openssl.exe | Where-Object FullName -match 'x64' | Select-Object -First 1).FullName
  $v = @()
  for ($i = 1; $i -le 3; $i++) {
    $ps = 1..4 | ForEach-Object { Start-Process $ossl -ArgumentList "speed -seconds 10 -evp aes-256-gcm" -NoNewWindow -PassThru -RedirectStandardOutput "$Out\ssl-$i-$_.txt" -RedirectStandardError "$Out\ssl-$i-$_.err" }
    $ps | Wait-Process
    $sum = 0; foreach ($k in 1..4) { $l = Get-Content "$Out\ssl-$i-$k.txt" | Where-Object { $_ -match '^AES-256-GCM' }; $sum += [double](($l -split '\s+' | Where-Object { $_ })[-1] -replace 'k', '') }
    $v += $sum / 1e6
  }
  $r += Stat "openssl $(& $ossl version) aes-256-gcm 16 KB, 4 processes" $v "GB/s"

  $fio = "C:\Program Files\fio\fio.exe"
  New-Item -ItemType Directory -Force C:\fio | Out-Null; Push-Location C:\fio
  foreach ($job in "randread 4k", "randwrite 4k", "read 2m", "write 2m") {
    $rw, $bs = $job -split ' '
    $iops = @(); $bw = @()
    for ($i = 1; $i -le 3; $i++) {
      $o = & $fio --name=$rw-$bs --rw=$rw --bs=$bs --size=2g --ioengine=windowsaio --thread --direct=1 --iodepth=32 --numjobs=1 --runtime=30 --time_based --group_reporting --output-format=terse --terse-version=3 2>&1 | Out-String
      $o | Set-Content "$Out\fio-$rw-$bs-$i.txt"
      $f = ($o -split "`n" | Where-Object { $_ -match '^3;' } | Select-Object -First 1) -split ';'
      if ($rw -match 'read') { $iops += [double]$f[7]; $bw += [double]$f[6] } else { $iops += [double]$f[48]; $bw += [double]$f[47] }
      Remove-Item C:\fio\* -Force
    }
    $r += Stat "fio $rw-$bs iops" $iops "IOPS"
    $r += Stat "fio $rw-$bs bandwidth" $bw "KB/s"
  }
  Pop-Location
  $r | Set-Content "C:\bench-out\tests.txt"; $r
}

switch ($Action) { "fetch" { Fetch } "run" { Run } }
