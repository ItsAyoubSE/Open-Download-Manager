# SPDX-License-Identifier: MIT
# Show the ODM PFX password stored in the Windows Credential Manager so you can
# copy it into your password manager. Run this yourself; do not redirect its
# output to a file or a log.
#
#   .\show-secret.ps1

[CmdletBinding()]
param(
    [string]$VaultTarget = "ODM-PFX-PASS"
)

$ErrorActionPreference = 'Stop'

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

$secret = [OdmCred]::Read($VaultTarget)
if (-not $secret) {
    Write-Host "No credential stored under target '$VaultTarget'." -ForegroundColor Yellow
    exit 1
}
Write-Host ""
Write-Host "PFX password (store this in your password manager, then close this window):" -ForegroundColor Cyan
Write-Host $secret -ForegroundColor White
Write-Host ""
