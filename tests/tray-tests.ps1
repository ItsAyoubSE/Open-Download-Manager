# SPDX-License-Identifier: MIT
# System-tray smoke test for ODM.
#
# Verifies: tray mode hides on close/minimize instead of exiting, the window can
# be restored, and a graceful quit still removes the icon. Run after building:
#   powershell -ExecutionPolicy Bypass -File .\tests\tray-tests.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$odm  = Join-Path $repo 'odm.exe'
$script:failures = 0

if (-not (Test-Path $odm)) { throw "Build odm.exe first (missing $odm)" }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class OdmWin {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
"@

function Check([bool]$cond, [string]$msg) {
    if ($cond) { Write-Host "  PASS  $msg" -ForegroundColor Green }
    else { Write-Host "  FAIL  $msg" -ForegroundColor Red; $script:failures++ }
}
function Get-MainWindow {
    for ($i = 0; $i -lt 50; $i++) {
        $h = [OdmWin]::FindWindow("ODMWindowClass", "Open Download Manager")
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 200
    }
    return [IntPtr]::Zero
}

Remove-Item Env:\ODM_TEST_NO_TRAY -ErrorAction SilentlyContinue
Stop-Process -Name odm -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $odm -PassThru
try {
    $h = Get-MainWindow
    Check ($h -ne [IntPtr]::Zero) "main window created"
    if ($h -eq [IntPtr]::Zero) { throw "no window" }
    Start-Sleep -Milliseconds 400
    Check ([OdmWin]::IsWindowVisible($h)) "window visible at startup"

    # Close button / WM_CLOSE should hide, not exit.
    [void][OdmWin]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_CLOSE
    Start-Sleep -Milliseconds 700
    Check (-not [OdmWin]::IsWindowVisible($h)) "WM_CLOSE hid the window"
    Check (-not $p.HasExited) "process kept running after WM_CLOSE"

    # Restore, then minimize; both should keep the process alive.
    [void][OdmWin]::ShowWindow($h, 9)  # SW_RESTORE
    Start-Sleep -Milliseconds 400
    Check ([OdmWin]::IsWindowVisible($h)) "window restored from tray"
    [void][OdmWin]::ShowWindow($h, 6)  # SW_MINIMIZE
    Start-Sleep -Milliseconds 700
    Check (-not [OdmWin]::IsWindowVisible($h)) "minimize-to-tray hid the window"
    Check (-not $p.HasExited) "process kept running after minimize"

    # Graceful quit message: should exit and remove the tray icon.
    [void][OdmWin]::ShowWindow($h, 9)
    Start-Sleep -Milliseconds 300
    [void][OdmWin]::PostMessage($h, 0x8000 + 11, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_APP+11
    $exited = $p.WaitForExit(6000)
    Check $exited "graceful quit exited the process"
}
finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item Env:\ODM_TEST_NO_TRAY -ErrorAction SilentlyContinue
}

Write-Host ""
if ($script:failures -eq 0) {
    Write-Host "All tray tests passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($script:failures) check(s) failed." -ForegroundColor Red
    exit 1
}
