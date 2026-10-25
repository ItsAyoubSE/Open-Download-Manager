# SPDX-License-Identifier: MIT
# Rotate the Authenticode self-signed code-signing certificate for ODM.
#
# Two modes:
#   -UseVault  (recommended, non-interactive): generates a strong random PFX
#              password, stores it in the Windows Credential Manager
#              (DPAPI-protected, user-scoped) under target "ODM-PFX-PASS", and
#              never prints or writes the password anywhere.
#   (default)  prompts for a password twice via a secure prompt.
#
# Usage:
#   .\rotate-cert.ps1 -OutDir "C:\Users\you\OpenDownloadManager-signing" -UseVault
#   .\rotate-cert.ps1 -OutDir "D:\keys"
#
# The helper never writes the password to a file and never passes it as a
# command-line argument. After -UseVault, copy the password into your password
# manager with show-secret.ps1 (run locally by you).

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutDir,
    [string]$Subject = "CN=ItsAyoubSE, O=ItsAyoubSE, C=MA",
    [int]$Years = 5,
    [switch]$UseVault,
    [string]$VaultTarget = "ODM-PFX-PASS",
    [string]$VaultUser = "odm"
)

$ErrorActionPreference = 'Stop'
function Fail($m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
function Info($m) { Write-Host $m -ForegroundColor Cyan }

# --- minimal Windows Credential Manager access (DPAPI, user-scoped) --------
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
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool CredWrite(ref CREDENTIAL cred, uint flags);
    [DllImport("advapi32.dll")] public static extern void CredFree(IntPtr buf);
    public static void Write(string target, string user, string secret) {
        byte[] b = System.Text.Encoding.Unicode.GetBytes(secret);
        CREDENTIAL c = new CREDENTIAL();
        c.Type = 1; c.TargetName = target; c.UserName = user; c.Persist = 2;
        c.CredentialBlobSize = (uint)b.Length;
        c.CredentialBlob = Marshal.AllocHGlobal(b.Length);
        Marshal.Copy(b, 0, c.CredentialBlob, b.Length);
        try { if (!CredWrite(ref c, 0)) throw new Exception("CredWrite failed " + Marshal.GetLastWin32Error()); }
        finally { Marshal.FreeHGlobal(c.CredentialBlob); }
    }
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

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }
$outFull = (Resolve-Path $OutDir).Path

# --- obtain the new password without exposing it ---------------------------
if ($UseVault) {
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $bytes = New-Object 'byte[]' 24
    $rng.GetBytes($bytes)
    $pfxPassPlain = [Convert]::ToBase64String($bytes)
    [OdmCred]::Write($VaultTarget, $VaultUser, $pfxPassPlain)
    Info "Stored new password in Windows Credential Manager (target '$VaultTarget')."
} else {
    $sec  = Read-Host -AsSecureString "New PFX password"
    $sec2 = Read-Host -AsSecureString "Confirm new PFX password"
    $b1 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
    $b2 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec2)
    $p1 = [Runtime.InteropServices.Marshal]::PtrToStringAuto($b1)
    $p2 = [Runtime.InteropServices.Marshal]::PtrToStringAuto($b2)
    if ($p1 -ne $p2) { Fail "Passwords do not match." }
    if ($p1.Length -lt 12) { Fail "Use a password of at least 12 characters." }
    $pfxPassPlain = $p1
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b1)
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b2)
    $p1 = $null; $p2 = $null
}

# --- create the certificate ------------------------------------------------
Info "==> Creating new self-signed code-signing certificate"
$cert = New-SelfSignedCertificate `
    -Type Custom `
    -Subject $Subject `
    -KeyUsage DigitalSignature `
    -FriendlyName "Open Download Manager Code Signing" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}") `
    -NotAfter (Get-Date).AddYears($Years)

$pfx = Join-Path $outFull "odm-selfsigned.pfx"
$cer = Join-Path $outFull "odm-selfsigned.cer"

# keep the previous material for rollback
if (Test-Path $pfx) { Move-Item $pfx "$pfx.old" -Force }
if (Test-Path $cer) { Move-Item $cer "$cer.old" -Force }

$pfxPass = ConvertTo-SecureString -String $pfxPassPlain -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pfxPass | Out-Null
Export-Certificate   -Cert $cert -FilePath $cer | Out-Null
$pfxPassPlain = $null

# thumbprint is public; recording it is safe
$thumb = $cert.Thumbprint
Set-Content -Path (Join-Path $outFull "odm-thumbprint.txt") -Value $thumb -Encoding ASCII

Write-Host ""
Write-Host "New certificate created." -ForegroundColor Green
Write-Host "  Subject:    $($cert.Subject)"
Write-Host "  Thumbprint: $thumb"
Write-Host "  PFX:        $pfx"
Write-Host "  CER:        $cer"
Write-Host ""
Write-Host "Next:"
if ($UseVault) {
    Write-Host "  - Copy the password into your password manager by running show-secret.ps1 locally."
}
Write-Host "  - Sign/verify with:  `$env:ODM_PFX = '$pfx'; .\release.ps1 -Publish"
Write-Host "  - Delete the old .pfx/.cer (.old) once the new release is verified."
