# SPDX-License-Identifier: MIT
# Update-check tests for ODM.
#
# Exercises the real WinHTTP/JSON/version-comparison path against a local
# server, including newer/same/older releases, malformed JSON and HTTP errors.
# Run after building:
#   powershell -ExecutionPolicy Bypass -File .\tests\update-tests.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$odm  = Join-Path $repo 'odm.exe'
$server = Join-Path $PSScriptRoot 'update-server.ps1'
$script:failures = 0

if (-not (Test-Path $odm)) { throw "Build odm.exe first (missing $odm)" }

$work = Join-Path $env:TEMP ("odm-update-test-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

# Derive the installed version so this suite keeps working across releases.
$iss = Get-Content (Join-Path $repo 'installer.iss') -Raw
if ($iss -match '#define\s+AppVersion\s+"([^"]+)"') { $currentVersion = $Matches[1] }
else { throw "Could not parse AppVersion from installer.iss" }
$v = $currentVersion.Split('.')
$newerVersion = "$($v[0]).$($v[1]).$([int]$v[2] + 1)"
if ([int]$v[2] -gt 0) {
    $olderVersion = "$($v[0]).$($v[1]).$([int]$v[2] - 1)"
} elseif ([int]$v[1] -gt 0) {
    $olderVersion = "$($v[0]).$([int]$v[1] - 1).0"
} else {
    $olderVersion = "0.0.0"
}

# Pick a free loopback port, then start the server on it.
$probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$probe.Start()
$port = ([System.Net.IPEndPoint]$probe.LocalEndpoint).Port
$probe.Stop()

$srv = Start-Process powershell -ArgumentList @(
    '-NoProfile','-ExecutionPolicy','Bypass','-File',$server,
    '-Root',$work,'-Port',"$port"
) -WindowStyle Hidden -PassThru

function Check([bool]$cond, [string]$msg) {
    if ($cond) { Write-Host "  PASS  $msg" -ForegroundColor Green }
    else { Write-Host "  FAIL  $msg" -ForegroundColor Red; $script:failures++ }
}

function Stop-Server {
    if ($script:srv -and -not $script:srv.HasExited) {
        Stop-Process -Id $script:srv.Id -Force -ErrorAction SilentlyContinue
    }
    $script:srv = $null
    Start-Sleep -Milliseconds 200
}

function Run-Case {
    param(
        [string]$Name,
        [string]$Json,
        [int]$Status,
        [bool]$ExpectOk,
        [bool]$ExpectAvailable = $false,
        [string]$ExpectLatest = ""
    )
    Set-Content -Path (Join-Path $work 'update.json') -Value $Json -NoNewline -Encoding ASCII
    Set-Content -Path (Join-Path $work 'status.txt') -Value "$Status" -NoNewline -Encoding ASCII
    $out = Join-Path $work 'result.txt'
    Remove-Item $out -Force -ErrorAction SilentlyContinue

    $env:ODM_TEST_UPDATE_URL = "http://127.0.0.1:$port/latest"
    $env:ODM_TEST_UPDATE_OUT = $out
    $p = Start-Process -FilePath $odm -PassThru
    $exited = $p.WaitForExit(15000)
    Check $exited "$Name`: app exited"
    if (-not $exited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Remove-Item Env:\ODM_TEST_UPDATE_URL -ErrorAction SilentlyContinue
        Remove-Item Env:\ODM_TEST_UPDATE_OUT -ErrorAction SilentlyContinue
        return
    }

    $code = $p.ExitCode
    $wantCode = if ($ExpectOk) { 0 } else { 2 }
    Check ($code -eq $wantCode) "$Name`: exit code $code (wanted $wantCode)"

    $text = if (Test-Path $out) { Get-Content $out -Raw } else { '' }
    if ($ExpectOk) {
        Check ($text -match '(?m)^status=ok$') "$Name`: status is ok"
        $avail = if ($ExpectAvailable) { '1' } else { '0' }
        Check ($text -match ("(?m)^available=" + $avail + "$")) "$Name`: available=$avail"
        if ($ExpectLatest) {
            Check ($text -match ("(?m)^latest=" + [regex]::Escape($ExpectLatest) + "$")) `
                "$Name`: latest=$ExpectLatest"
        }
    } else {
        Check ($text -match '(?m)^status=error$') "$Name`: status is error"
        Check ($text -match '(?m)^error=.+') "$Name`: error message was written"
    }

    Remove-Item Env:\ODM_TEST_UPDATE_URL -ErrorAction SilentlyContinue
    Remove-Item Env:\ODM_TEST_UPDATE_OUT -ErrorAction SilentlyContinue
}

try {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $listening = $false
    while ($sw.Elapsed.TotalSeconds -lt 12) {
        if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) {
            $listening = $true
            break
        }
        Start-Sleep -Milliseconds 150
    }
    if (-not $listening) { throw "update test server did not start on port $port" }

    Run-Case "newer release" `
        ('{"tag_name":"v' + $newerVersion + '","html_url":"https://example.test/releases/tag/v' + $newerVersion + '"}') `
        200 $true $true "v$newerVersion"
    Run-Case "same version" `
        ('{"tag_name":"v' + $currentVersion + '","html_url":"https://example.test/releases/tag/v' + $currentVersion + '"}') `
        200 $true $false "v$currentVersion"
    Run-Case "older release" `
        ('{"tag_name":"v' + $olderVersion + '","html_url":"https://example.test/releases/tag/v' + $olderVersion + '"}') `
        200 $true $false "v$olderVersion"
    Run-Case "missing tag" `
        '{"message":"unexpected response"}' `
        200 $false
    Run-Case "http 500" `
        '{}' `
        500 $false

    $logPath = Join-Path $work 'server.log'
    $log = if (Test-Path $logPath) { Get-Content $logPath -Raw } else { '' }
    Check ($log -match 'GET /latest') "server received GET /latest"
    Check ($log -match 'User-Agent: OpenDownloadManager/\d+\.\d+\.\d+') "request used ODM user agent"
} finally {
    Stop-Server
    Remove-Item Env:\ODM_TEST_UPDATE_URL -ErrorAction SilentlyContinue
    Remove-Item Env:\ODM_TEST_UPDATE_OUT -ErrorAction SilentlyContinue
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
if ($script:failures -eq 0) {
    Write-Host "All update tests passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($script:failures) check(s) failed." -ForegroundColor Red
    exit 1
}
