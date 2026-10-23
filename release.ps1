# SPDX-License-Identifier: MIT
# Release pipeline for Open Download Manager.
#
# Builds, signs, verifies signatures, then generates SHA-256 checksums from the
# FINAL signed files and (optionally) publishes a GitHub release. Signing must
# happen before the checksum is computed: Authenticode signing changes bytes.
#
# Usage:
#   $env:ODM_PFX      = "C:\path\to\cert.pfx"   # or pass -Pfx
#   $env:ODM_PFX_PASS = "password"              # or pass -PfxPass
#   .\release.ps1                                # build + sign + checksums
#   .\release.ps1 -Publish                       # ... and publish release
#
# The test suites run against the freshly built (unsigned) binary and must pass
# before anything is signed. Use -SkipTests only for an explicit emergency.
#
# The private PFX must live OUTSIDE this repository.

[CmdletBinding()]
param(
    [string]$Pfx      = $env:ODM_PFX,
    [string]$PfxPass  = $env:ODM_PFX_PASS,
    [switch]$Publish,
    [switch]$SkipTests,
    [string]$Repo     = "ItsAyoubSE/Open-Download-Manager",
    [string]$Notes    = ""
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# Read the PFX password from the Windows Credential Manager (if stored there).
function Get-OdmVaultSecret {
    param([string]$Target = "ODM-PFX-PASS")
    if (-not ("OdmCred" -as [type])) {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public class OdmCred {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct CREDENTIAL {
        public uint Flags; public uint Type; public string TargetName; public string Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint CredentialBlobSize; public IntPtr CredentialBlob;
        public uint Persist; public uint AttributeCount; public IntPtr Attributes;
        public string TargetAlias; public string UserName;
    }
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool CredRead(string target, uint type, uint flags, out IntPtr cred);
    [DllImport("advapi32.dll")] public static extern void CredFree(IntPtr buf);
    public static string Read(string target) {
        IntPtr p;
        if (!CredRead(target, 1, 0, out p)) return null;
        try {
            CREDENTIAL c = (CREDENTIAL)Marshal.PtrToStructure(p, typeof(CREDENTIAL));
            return Marshal.PtrToStringUni(c.CredentialBlob, (int)c.CredentialBlobSize / 2);
        } finally { CredFree(p); }
    }
}
"@
    }
    return [OdmCred]::Read($Target)
}

# --- Validate signing material --------------------------------------------
if (-not $Pfx)      { Fail "Set ODM_PFX or pass -Pfx (path to your .pfx)." }
if (-not (Test-Path $Pfx)) { Fail "PFX not found: $Pfx" }
$pfxFull = (Resolve-Path $Pfx).Path
if ($pfxFull.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    Fail "The PFX is inside the repository ($pfxFull). Move it outside before releasing."
}

# --- Fail closed on known plaintext-credential storage risks --------------
# This is a targeted guard, not a scan of the whole machine: it only looks at
# the PFX's own directory, the repository's parent directory, and the well-known
# OpenDownloadManager-signing\password.txt path.
function Get-CredentialRisks {
    $dirs = @()
    $dirs += (Split-Path $pfxFull -Parent)
    $dirs += (Split-Path $root -Parent)
    $dirs += (Join-Path (Split-Path $root -Parent) 'OpenDownloadManager-signing')
    $risks = @()
    foreach ($d in ($dirs | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique)) {
        $f = Join-Path $d 'password.txt'
        if (Test-Path $f) { $risks += (Resolve-Path $f).Path }
    }
    return $risks
}
$credRisks = Get-CredentialRisks
if ($credRisks.Count -gt 0) {
    Write-Host "ERROR: refusing to build, sign or publish." -ForegroundColor Red
    Write-Host "Plaintext credential file(s) detected:" -ForegroundColor Red
    foreach ($f in $credRisks) { Write-Host "  - $f" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Move the PFX password into a password manager, delete the file(s), and"
    Write-Host "rotate the signing key if the password may have been exposed. The"
    Write-Host "pipeline will not continue while these files exist." -ForegroundColor Red
    exit 1
}
if (-not $PfxPass) {
    # Prefer the Windows Credential Manager so no secret touches the shell,
    # an argument, or a file.
    $PfxPass = Get-OdmVaultSecret
}
if (-not $PfxPass) {
    $sec = Read-Host -AsSecureString "PFX password"
    $PfxPass = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
}

# Expected signer thumbprint (so we can verify what we just signed).
try {
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($pfxFull, $PfxPass)
} catch {
    Fail "Could not open the PFX (wrong password?): $($_.Exception.Message)"
}
$expectedThumb = $cert.Thumbprint

# --- Locate signtool -------------------------------------------------------
$signtool = $null
$cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
if ($cmd) { $signtool = $cmd.Source }
if (-not $signtool) {
    $cand = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\signtool.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -Last 1
    if ($cand) { $signtool = $cand.FullName }
}
if (-not $signtool) { Fail "signtool.exe not found. Install the Windows SDK." }

# --- Version (from installer.iss) -----------------------------------------
$iss = Get-Content (Join-Path $root 'installer.iss') -Raw
if ($iss -match '#define\s+AppVersion\s+"([^"]+)"') { $version = $Matches[1] }
else { Fail "Could not parse AppVersion from installer.iss" }
$tag = "v$version"
Step "Releasing version $version ($tag)"

function Sign-And-Verify($file, $what) {
    Step "Signing $what"
    & $signtool sign /f $pfxFull /p $PfxPass /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $file | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "signtool sign failed for $file" }

    Step "Verifying $what signature"
    $sig = Get-AuthenticodeSignature $file
    if ($sig.Status -eq 'NotSigned') { Fail "$file is not signed." }
    if ($sig.SignerCertificate.Thumbprint -ne $expectedThumb) {
        Fail "$file signer thumbprint $($sig.SignerCertificate.Thumbprint) != expected $expectedThumb"
    }
    Write-Host "    signer: $($sig.SignerCertificate.Subject)"
    Write-Host "    status: $($sig.Status) (self-signed certs are expected to be untrusted)"
}

# --- Build -----------------------------------------------------------------
Step "Building odm.exe"
& (Join-Path $root 'build.bat') | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "build.bat failed" }

$exe = Join-Path $root 'odm.exe'

# --- Tests (must pass before signing) --------------------------------------
if ($SkipTests) {
    Write-Host "WARNING: -SkipTests was given; release is NOT test-verified." -ForegroundColor Yellow
} else {
    Stop-Process -Name odm -Force -ErrorAction SilentlyContinue
    foreach ($test in @('resume-tests.ps1', 'tray-tests.ps1')) {
        $path = Join-Path $root "tests\$test"
        if (-not (Test-Path $path)) { Fail "Missing test: $path" }
        Step "Running $test"
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail "$test failed (exit $LASTEXITCODE); refusing to sign" }
        Stop-Process -Name odm -Force -ErrorAction SilentlyContinue
    }
    Step "All release tests passed"
}

Sign-And-Verify $exe "odm.exe"

# --- Installer -------------------------------------------------------------
Step "Building installer"
& (Join-Path $root 'build-installer.bat') | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "build-installer.bat failed" }

$setup = Join-Path $root "dist\OpenDownloadManager-$version-setup.exe"
if (-not (Test-Path $setup)) { Fail "Installer not found: $setup" }
Sign-And-Verify $setup "installer"

# --- Checksums from FINAL signed files -------------------------------------
Step "Writing SHA-256 checksum from the signed installer"
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
$sums = Join-Path $root 'dist\SHA256SUMS.txt'
"$hash  OpenDownloadManager-$version-setup.exe" | Set-Content -Path $sums -Encoding ASCII
Get-Content $sums | Out-Host

# --- Publish (optional) ----------------------------------------------------
if ($Publish) {
    Step "Publishing GitHub release $tag"
    $git = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
    if (-not $git) {
        $g = "C:\Program Files\Git\cmd\git.exe"
        if (Test-Path $g) { $git = $g }
    }
    if (-not $git) { Fail "git.exe not found; needed to read GitHub credentials." }
    $cred  = "protocol=https`nhost=github.com`n`n" | & $git credential fill
    $token = ($cred | Where-Object { $_ -like "password=*" }) -replace "^password=", ""
    if (-not $token) { Fail "No GitHub token available from git credential manager." }
    $hdr = @{ Authorization = "Bearer $token"; Accept = "application/vnd.github+json"; "User-Agent" = "odm-release" }

    # Create the release, or reuse it if the tag already has one (asset refresh).
    try {
        $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/tags/$tag" -Headers $hdr
        Step "Release $tag already exists - refreshing assets"
        foreach ($a in $rel.assets) {
            Invoke-RestMethod -Method Delete -Uri "https://api.github.com/repos/$Repo/releases/assets/$($a.id)" -Headers $hdr | Out-Null
        }
    } catch {
        $notes = if ($Notes) { $Notes } else { "See the commits for details." }
        $body = @{ tag_name = $tag; target_commitish = "main"; name = "Open Download Manager $version";
                   body = $notes; draft = $false; prerelease = $false } | ConvertTo-Json
        try { $rel = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$Repo/releases" -Headers $hdr -ContentType "application/json" -Body $body }
        catch { Fail "Release creation failed: $($_.Exception.Message)" }
    }

    foreach ($f in @($setup, $sums)) {
        $name = [IO.Path]::GetFileName($f)
        Step "Uploading $name"
        $type = if ($name -like '*.txt') { 'text/plain' } else { 'application/octet-stream' }
        Invoke-RestMethod -Method Post -Uri "https://uploads.github.com/repos/$Repo/releases/$($rel.id)/assets?name=$name" -Headers $hdr -ContentType $type -InFile $f | Out-Null
    }
    Write-Host "    https://github.com/$Repo/releases/tag/$tag"
}

Step "Done."
