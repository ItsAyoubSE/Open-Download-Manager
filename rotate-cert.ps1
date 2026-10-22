# SPDX-License-Identifier: MIT
# Rotate the Authenticode self-signed code-signing certificate for ODM.
#
# Creates a new self-signed code-signing certificate, exports a password-
# protected PFX and the public .cer, and prints the new thumbprint. The new
# password is entered through a secure prompt and is never written to disk here.
#
# Usage:
#   .\rotate-cert.ps1 -OutDir "D:\keys" [-Subject "CN=ItsAyoubSE, O=ItsAyoubSE, C=MA"] [-Years 5]
#
# After running:
#   1. Store the new password in your password manager (only place it lives).
#   2. Back the new .pfx up offline.
#   3. Delete the old .pfx/.cer once the new release is verified.

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutDir,
    [string]$Subject = "CN=ItsAyoubSE, O=ItsAyoubSE, C=MA",
    [int]$Years = 5
)

$ErrorActionPreference = 'Stop'

function Fail($m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }
$outFull = (Resolve-Path $OutDir).Path

$sec  = Read-Host -AsSecureString "New PFX password"
$sec2 = Read-Host -AsSecureString "Confirm new PFX password"
$bstr1 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
$bstr2 = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec2)
$p1 = [Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr1)
$p2 = [Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr2)
if ($p1 -ne $p2) { Fail "Passwords do not match." }
if ($p1.Length -lt 12) { Fail "Use a password of at least 12 characters." }

Write-Host "==> Creating new self-signed code-signing certificate" -ForegroundColor Cyan
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

$pfxPass = ConvertTo-SecureString -String $p1 -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pfxPass | Out-Null
Export-Certificate   -Cert $cert -FilePath $cer | Out-Null

# clear plaintext copies from memory
[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr1)
[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr2)
$p1 = $null; $p2 = $null

Write-Host ""
Write-Host "New certificate created." -ForegroundColor Green
Write-Host "  Subject:    $($cert.Subject)"
Write-Host "  Thumbprint: $($cert.Thumbprint)"
Write-Host "  PFX:        $pfx"
Write-Host "  CER:        $cer"
Write-Host ""
Write-Host "Next:"
Write-Host "  - Store the password in your password manager and back the .pfx up offline."
Write-Host "  - Release with:"
Write-Host "      `$env:ODM_PFX = '$pfx'"
Write-Host "      `$env:ODM_PFX_PASS = ''   # or let release.ps1 prompt you"
Write-Host "      .\release.ps1 -Publish"
