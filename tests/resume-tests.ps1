# SPDX-License-Identifier: MIT
# End-to-end resume/correctness tests for ODM.
#
# These drive the real odm.exe against a local range-capable HTTP server and
# cover: crash/resume, changed validator, incorrectly reused ETag, weak
# validator, malformed Content-Range, truncated responses, and a simulated
# full disk (via the ODM_TEST_WRITE_BUDGET test hook).
#
# Run from the repo root after building:
#   powershell -ExecutionPolicy Bypass -File .\tests\resume-tests.ps1
#
# The user's downloads.tsv and settings.ini are backed up and restored.

[CmdletBinding()]
param([int]$BasePort = 9400)

$ErrorActionPreference = 'Stop'

$repo    = Split-Path -Parent $PSScriptRoot
$odm     = Join-Path $repo 'odm.exe'
$server  = Join-Path $PSScriptRoot 'resume-server.ps1'
$work    = Join-Path $env:TEMP 'odm-resume-tests'
$appDir  = Join-Path $env:APPDATA 'Open Download Manager'
$tsv     = Join-Path $appDir 'downloads.tsv'
$ini     = Join-Path $appDir 'settings.ini'
$v1      = Join-Path $work 'testfile.bin'
$v2      = Join-Path $work 'testfile-v2.bin'
$dest    = Join-Path $work 'download.bin'
$log     = Join-Path $work 'server.log'
$total   = 8MB
$port    = $BasePort
$script:failures = 0
$script:srv = $null

if (-not (Test-Path $odm)) { throw "Build odm.exe first (missing $odm)" }
New-Item -ItemType Directory -Force -Path $work | Out-Null
New-Item -ItemType Directory -Force -Path $appDir | Out-Null

# Back up user state so the tests never destroy real data.
$tsvBak = Join-Path $env:TEMP 'odm-resume-tests-tsv.bak'
$iniBak = Join-Path $env:TEMP 'odm-resume-tests-ini.bak'
$hadTsv = Test-Path $tsv
$hadIni = Test-Path $ini
if ($hadTsv) { Copy-Item $tsv $tsvBak -Force }
if ($hadIni) { Copy-Item $ini $iniBak -Force }

function Write-NoBom([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false)))
}
function Set-Ctl([string]$name, [string]$value) { Write-NoBom (Join-Path $work $name) $value }
function Hash([string]$p) { (Get-FileHash $p -Algorithm SHA256).Hash }
function Check([bool]$cond, [string]$msg) {
    if ($cond) { Write-Host "  PASS  $msg" -ForegroundColor Green }
    else { Write-Host "  FAIL  $msg" -ForegroundColor Red; $script:failures++ }
}

function Reset-Files([string]$mode) {
    Remove-Item $dest -Force -ErrorAction SilentlyContinue
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Set-Ctl 'mode.txt' $mode
    Set-Ctl 'version.txt' '1'
    Remove-Item (Join-Path $work 'etag.txt') -Force -ErrorAction SilentlyContinue
}
function Seed-Tsv {
    $url = "http://127.0.0.1:$port/testfile.bin"
    # state=1 so the app auto-resumes it on launch; no progress yet.
    $line = "1`t1`t0`t0`tGeneral`tbin`tdownload.bin`t$dest`t$url`t0" +
            ("`t0" * 8) + "`t`t0"
    Write-NoBom $tsv $line
}
function Get-Fields {
    if (-not (Test-Path $tsv)) { return @() }
    $text = Get-Content $tsv -Raw
    if (-not $text) { return @() }
    return (($text -split "`r?`n")[0]).Split("`t")
}
function Start-Server {
    $script:srv = Start-Process powershell -ArgumentList @(
        '-NoProfile','-ExecutionPolicy','Bypass','-File',$server,
        '-Root',$work,'-Port',"$port",'-ChunkDelayMs','18'
    ) -WindowStyle Hidden -PassThru
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 12) {
        if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) { return }
        Start-Sleep -Milliseconds 150
    }
    throw "test server did not start on port $port"
}
function Stop-Server {
    if ($script:srv -and -not $script:srv.HasExited) {
        Stop-Process -Id $script:srv.Id -Force -ErrorAction SilentlyContinue
    }
    $script:srv = $null
    Start-Sleep -Milliseconds 200
}
function Start-App([int]$WriteBudget = 0) {
    if ($WriteBudget -gt 0) { $env:ODM_TEST_WRITE_BUDGET = "$WriteBudget" }
    else { Remove-Item Env:\ODM_TEST_WRITE_BUDGET -ErrorAction SilentlyContinue }
    return Start-Process -FilePath $odm -PassThru
}
function Stop-Gently($p) {
    if ($p -and -not $p.HasExited) {
        [void]$p.CloseMainWindow()
        if (-not $p.WaitForExit(5000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    }
}
function Wait-State([string]$want, [int]$seconds = 90) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        $f = Get-Fields
        if ($f.Count -ge 2 -and $f[1] -eq $want) {
            if ($want -ne '2') { return $true }
            if ((Test-Path $dest) -and ((Get-Item $dest).Length -eq $total)) { return $true }
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}
function Crash-Partial([int]$ms = 1500) {
    $p = Start-App
    Start-Sleep -Milliseconds $ms
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 350
}

try {
    # Create test payloads outside the repo. Both have non-zero, distinct bytes
    # so a file with unwritten holes can never hash-match the source.
    $chunk = New-Object 'byte[]' 65536
    $reps = [int]($total / $chunk.Length)
    for ($i = 0; $i -lt $chunk.Length; $i++) { $chunk[$i] = 0x5A }
    $fs = [IO.File]::Open($v1, 'Create', 'ReadWrite')
    for ($k = 0; $k -lt $reps; $k++) { $fs.Write($chunk, 0, $chunk.Length) }
    $fs.Close()
    for ($i = 0; $i -lt $chunk.Length; $i++) { $chunk[$i] = 0xAB }
    $fs = [IO.File]::Open($v2, 'Create', 'ReadWrite')
    for ($k = 0; $k -lt $reps; $k++) { $fs.Write($chunk, 0, $chunk.Length) }
    $fs.Close()
    Stop-Process -Name odm -Force -ErrorAction SilentlyContinue

    # ------------------------------------------------------------------
    Write-Host "`nTEST: crash then resume" -ForegroundColor Cyan
    Reset-Files 'normal'; Seed-Tsv; Start-Server
    Crash-Partial
    $f = Get-Fields
    $done = [uint64]$f[3]
    Check ($f.Count -ge 20) "extended tsv written"
    Check ($done -gt 0 -and $done -lt $total) "partial progress saved ($done of $total bytes)"
    $p = Start-App
    $ok = Wait-State '2' 90
    Start-Sleep -Milliseconds 800
    Stop-Gently $p
    $f = Get-Fields
    Check $ok "download reached full size after resume"
    Check ($f.Count -ge 20 -and $f[1] -eq '2') "state is completed (2)"
    Check ((Hash $dest) -eq (Hash $v1)) "final content matches source"
    Stop-Server

    # ------------------------------------------------------------------
    Write-Host "`nTEST: changed validator and content" -ForegroundColor Cyan
    $port++
    Reset-Files 'normal'; Seed-Tsv; Start-Server
    Crash-Partial
    $f = Get-Fields
    $oldDone = [uint64]$f[3]
    Check ($oldDone -gt 0 -and $oldDone -lt $total) "partial progress saved"
    Set-Ctl 'version.txt' '2'
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $p = Start-App
    $ok = Wait-State '2' 90
    Start-Sleep -Milliseconds 800
    Stop-Gently $p
    $f = Get-Fields
    $phase2 = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
    Check $ok "download completed after validator change"
    Check ((Hash $dest) -eq (Hash $v2)) "content matches the new version (no mixed bytes)"
    Check ($phase2 -match 'bytes=0-') "restarted from byte 0 after validator change"
    Check ($f[1] -eq '2') "state is completed (2)"
    Stop-Server

    # ------------------------------------------------------------------
    Write-Host "`nTEST: strong ETag reused for different content" -ForegroundColor Cyan
    $port++
    Reset-Files 'normal'; Seed-Tsv; Start-Server
    Crash-Partial
    $f = Get-Fields
    Check ($f[3] -ne '0') "partial progress saved"
    Set-Ctl 'version.txt' '2'
    Set-Ctl 'etag.txt' '"resume-test-v1"'   # lie: same validator, new bytes
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $p = Start-App
    $ok = Wait-State '2' 90
    Start-Sleep -Milliseconds 800
    Stop-Gently $p
    $f = Get-Fields
    $phase2 = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
    Check $ok "download completed"
    Check ((Hash $dest) -eq (Hash $v2)) "overlap check discarded stale bytes and matched new content"
    Check ($phase2 -match 'bytes=0-') "restarted from byte 0 when overlap mismatched"
    Check ($f[1] -eq '2') "state is completed (2)"
    Stop-Server

    # ------------------------------------------------------------------
    Write-Host "`nTEST: malformed Content-Range" -ForegroundColor Cyan
    $port++
    Reset-Files 'malformed'; Seed-Tsv; Start-Server
    $p = Start-App
    $failed = Wait-State '3' 20
    Stop-Gently $p
    $f = Get-Fields
    Check $failed "engine reported failure promptly"
    $dl = [uint64]$f[3]
    $size = if (Test-Path $dest) { (Get-Item $dest).Length } else { 0 }
    Check ($f[1] -eq '3') "state is failed (3), not completed"
    Check ($dl -lt $total) "did not count the whole file as downloaded"
    Check ($size -lt $total) "file is not reported complete"
    Stop-Server

    # ------------------------------------------------------------------
    Write-Host "`nTEST: truncated response body" -ForegroundColor Cyan
    $port++
    Reset-Files 'truncated'; Seed-Tsv; Start-Server
    $p = Start-App
    $failed = Wait-State '3' 20
    Stop-Gently $p
    $f = Get-Fields
    Check $failed "engine reported failure promptly"
    $size = if (Test-Path $dest) { (Get-Item $dest).Length } else { 0 }
    Check ($f[1] -eq '3') "state is failed (3), not completed"
    Check ($size -lt $total) "short response is not accepted as complete"
    Stop-Server

    # ------------------------------------------------------------------
    Write-Host "`nTEST: weak validator does not resume" -ForegroundColor Cyan
    $port++
    Reset-Files 'weak'; Seed-Tsv; Start-Server
    Crash-Partial
    $f = Get-Fields
    $seg0 = [uint64]$f[10]
    Check ($f[3] -ne '0') "partial progress saved"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $p = Start-App
    $ok = Wait-State '2' 90
    Start-Sleep -Milliseconds 800
    Stop-Gently $p
    $f = Get-Fields
    $phase2 = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
    Check $ok "download completed"
    Check ((Hash $dest) -eq (Hash $v1)) "final content matches source"
    Check ($phase2 -match 'bytes=0-') "weak validator forced a restart from byte 0"
    if ($seg0 -gt 0) { Check ($phase2 -notmatch ("bytes=" + $seg0 + "-")) "did not resume from saved offset" }
    Check ($f[1] -eq '2') "state is completed (2)"
    Stop-Server

    # ------------------------------------------------------------------
    Write-Host "`nTEST: simulated full disk" -ForegroundColor Cyan
    $port++
    Reset-Files 'normal'; Seed-Tsv; Start-Server
    $p = Start-App -WriteBudget 262144
    $failed = Wait-State '3' 20
    Stop-Gently $p
    Remove-Item Env:\ODM_TEST_WRITE_BUDGET -ErrorAction SilentlyContinue
    $f = Get-Fields
    Check $failed "engine reported failure promptly"
    $dl = [uint64]$f[3]
    $size = if (Test-Path $dest) { (Get-Item $dest).Length } else { 0 }
    Check ($f[1] -eq '3') "state is failed (3), not completed"
    Check ($dl -lt $total) "did not count the whole file as downloaded"
    Check ($size -lt $total) "write failure left an incomplete file"
    Stop-Server
}
finally {
    Stop-Server
    Stop-Process -Name odm -Force -ErrorAction SilentlyContinue
    Remove-Item Env:\ODM_TEST_WRITE_BUDGET -ErrorAction SilentlyContinue
    if ($hadTsv) { Copy-Item $tsvBak $tsv -Force } else { Remove-Item $tsv -Force -ErrorAction SilentlyContinue }
    if ($hadIni) { Copy-Item $iniBak $ini -Force } else { Remove-Item $ini -Force -ErrorAction SilentlyContinue }
}

Write-Host ""
if ($script:failures -eq 0) {
    Write-Host "All resume tests passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($script:failures) check(s) failed." -ForegroundColor Red
    exit 1
}
