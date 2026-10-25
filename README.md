# Open Download Manager

A fast, native Windows download manager written in C++ with Win32, Direct2D and
DirectWrite. Single self-contained executable, no runtime dependencies beyond
the Windows system libraries.

## Download

Grab the latest installer from the
[Releases](https://github.com/ItsAyoubSE/Open-Download-Manager/releases) page:

- `OpenDownloadManager-0.4.2-setup.exe` - installs per-user without admin
  rights, adds Start Menu and optional desktop shortcuts, and registers an
  uninstaller.

`SHA256SUMS.txt` lists the installer's SHA-256 checksum. The installer and the
bundled `odm.exe` are Authenticode-signed. Releases currently use a
**self-signed** certificate, so SmartScreen may still show *"Windows protected
your PC"* until the certificate is trusted on your machine - see
[Code signing](#code-signing).

> **Building from source?** `build.bat` produces an **unsigned** `odm.exe`.
> Run `sign.bat` afterwards to Authenticode-sign it, or Windows Defender
> SmartScreen may show *"Windows protected your PC"* on first launch. Choose
> **More info -> Run anyway** to proceed.

## Features

**Implemented**

- Segmented downloads - up to 8 parallel HTTP Range requests, with resume and
  per-segment file offsets.
- Resume across restarts - per-segment progress is saved while downloading and
  restored after a crash or reboot. Resume is allowed only with a strong `ETag`
  matching the saved value; weak ETags and `Last-Modified` force a clean
  restart. Before continuing, the tail of every saved segment is re-fetched and
  compared with the bytes on disk, so a server that reuses a validator for
  different content is detected. Every range response must be `206` with an
  exact `Content-Range` start/end/total and the expected byte count, and
  completion requires both the byte counters and on-disk size to match.
  Downloads that were running resume automatically on launch; items you paused
  stay paused.
- Pause, resume and cancel; per-download play/pause controls; progress, speed
  and ETA per item.
- System tray icon - closing or minimizing the window hides it to the tray
  instead of quitting. Double-click the icon (or choose **Open Download
  Manager**) to restore it. The tray menu can pause or resume all downloads,
  toggle minimize-to-tray, and exit cleanly. Minimize-to-tray is on by default
  and can be changed from the tray menu; the choice is remembered.
- Check for updates - queries the GitHub Releases API in the background and
  compares the latest stable tag with the installed version. If a newer version
  exists, ODM offers to open its release page; network or server errors are
  reported without blocking the UI.
- Speed limiting - LOW / MEDIUM / HIGH traffic mode throttles active downloads.
- Categories and tags - documents, video, music, programs, archives, other.
- Queues, sorting (8 modes) and filtering (6 modes).
- Add dialog, New download dialog with editable folder and filename, live probe
  of remote size and free disk space. Filename is read from the server's
  `Content-Disposition` header when available, including percent-encoded names.
- Clipboard URL handling, optional clipboard monitoring, and drag-and-drop of
  URLs.
- Double-click a completed download to open the file, or use the bin icon to
  delete it from disk (with confirmation).
- Dark, light and system themes - custom-drawn UI and a native dark title bar.
- Preferences screen with sidebar and General page.
- Settings persistence - theme, language, folder, traffic mode, minimize-to-tray
  and window position are saved to disk and restored on launch.
- Download-list persistence - items are remembered between sessions;
  unfinished downloads keep their partial data and resume from saved offsets.
- Animations - press feedback, dialog pop-in, menu slide-in.
- Torrent/magnet/.torrent input is detected and reported as "coming soon".
- Unfinished sections (proxy, add-ons, remote client, portable mode, auto
  shutdown, export/import) show a "coming soon" notice.

**Planned**

- Proxy configuration.
- Browser integration.
- BitTorrent engine and auto-extract.

## System requirements

| | |
|---|---|
| **OS** | Windows 10 version 1809 (October 2018 Update) or newer, or Windows 11 |
| **Architecture** | 64-bit (x64) |
| **Disk** | ~1 MB for the app, plus space for your downloads |
| **Memory** | 64 MB RAM or more |
| **Display** | 1100 x 720 or larger recommended |
| **Privileges** | None - per-user install, runs as a normal user, no admin rights |
| **Runtime** | None - the C++ runtime is statically linked. No .NET, Qt, Java, VC++ Redistributable or extra DLLs required |
| **Network** | Internet connection for downloads; uses the built-in Windows WinHTTP stack, so system proxy and TLS settings apply automatically |

Windows 7/8.1 are not supported - the app relies on Windows 10 APIs and will
not start there.

## Build

Requirements: Visual Studio 2022 (MSVC, Windows 10/11 SDK).

```bat
build.bat
```

This produces `odm.exe` - statically linked CRT, Windows GUI subsystem, no
console window.

`build.bat` assumes Visual Studio is installed at
`C:\Program Files\Microsoft Visual Studio\2022\Community`. Edit `VCVARS` at the
top of the file if yours is elsewhere.

### Installer

The Windows installer is built with [Inno Setup 7](https://jrsoftware.org/isinfo.php):

```bat
build-installer.bat
```

It packages `odm.exe`, `LICENSE` and `README.md` into
`dist\OpenDownloadManager-<version>-setup.exe`. The installer is per-user by
default (`%LOCALAPPDATA%\Programs\Open Download Manager`), requires no admin
rights, and can be removed from *Settings > Apps*.

## Code signing

Release builds are Authenticode-signed. Local builds produced by `build.bat`
are not, so run `sign.bat` to sign them. To sign a build:

```bat
set ODM_PFX=C:\path\to\cert.pfx
set ODM_PFX_PASS=yourpassword
sign.bat
```

To sign the installer as well, run the same `signtool` command on
`dist\OpenDownloadManager-<version>-setup.exe` after building it.

A **self-signed** signature proves integrity and identifies the signing
certificate, but it does not establish public trust, so Windows SmartScreen may
still warn. Adding a self-signed certificate to *Trusted Root* and *Trusted
Publishers* changes the machine's security configuration and should be an
explicit, deliberate decision - it is not recommended as a routine way to
silence warnings. For warning-free public distribution you need a CA-issued code
signing certificate (OV or EV); for free open-source projects look at
[SignPath Foundation](https://signpath.org/) or Certum's open-source code
signing certificate.

Signing does not prevent antivirus false positives. If a scanner flags the
build, submit it to Microsoft at
<https://www.microsoft.com/en-us/wdsi/filesubmission>.

A self-signed signature proves integrity and identifies the signing
certificate, but - unlike a certificate from a publicly trusted CA - it does
**not** by itself clear SmartScreen warnings; that requires the certificate to
be trusted on the target machine.

The private key is sensitive: keep the `.pfx` outside the repository, protect
its password, and back it up somewhere safe. Signing material is excluded by
`.gitignore` and is never bundled into the installer or release archives.

## Releasing

`release.ps1` automates the whole pipeline so nothing is published unsigned or
with a stale checksum:

1. Builds `odm.exe`.
2. Signs the executable and **verifies** the signature against the expected
   certificate.
3. Builds the installer.
4. Signs and verifies the installer.
5. Writes `SHA256SUMS.txt` from the **final signed** installer (checksums are
   always computed *after* signing, since signing changes the file's bytes).
6. With `-Publish`, creates the GitHub release and uploads the assets.

The pipeline **fails closed** on known credential risks and never prints secret
material:

- It refuses to proceed if the PFX lives inside the repository.
- It refuses to build, sign or publish if a plaintext `password.txt` is found in
  the PFX's directory, the repository's parent directory, or the well-known
  `OpenDownloadManager-signing\password.txt` path. (This is a targeted guard,
  not a scan of the whole machine.) The error prints the offending path, never
  its contents.
- The PFX password is only ever read from `ODM_PFX_PASS`, the Windows
  Credential Manager (see below), or a secure interactive prompt - never from a
  file and never hardcoded or passed as a command-line argument.
- `-Publish` reuses an existing release for the tag and replaces its assets, so
  re-signing a release does not require deleting and recreating it.

That is why `password.txt` must not exist: store the PFX password in a password
manager, and keep the `.pfx` and its backup offline.

For unattended/agent use the password can live in the Windows Credential Manager
(DPAPI-protected, user-scoped) instead of the environment:

```powershell
.\rotate-cert.ps1 -OutDir "C:\keys" -UseVault   # generates + stores the password
.\show-secret.ps1                               # run locally to copy it into your password manager
.\release.ps1 -Pfx "C:\keys\odm-selfsigned.pfx" -Publish   # reads the vault
```

### Rotating the signing certificate

A deleted password file does not by itself prove the private key is compromised
- that depends on who could read the password and whether it leaked through
shell history, logs, backups or other copies. Rotate if you cannot establish
that the password stayed private. `rotate-cert.ps1` creates a new self-signed
code-signing certificate with a securely-prompted password:

```powershell
.\rotate-cert.ps1 -OutDir "D:\keys"
```

It prints the new thumbprint; `release.ps1` picks it up automatically because it
derives the expected thumbprint from the PFX it is given. After rotating: store
the password in your password manager, back up the new `.pfx` offline, then sign
and verify a fresh release and regenerate its checksum. Note that a self-signed
certificate is not equivalent to one from a publicly trusted CA - rotating it
changes the signing identity users see and does not by itself clear SmartScreen
warnings.

```powershell
$env:ODM_PFX      = "D:\keys\odm.pfx"   # outside the repo
$env:ODM_PFX_PASS = "yourpassword"
.\release.ps1                 # build + sign + verify + checksums
.\release.ps1 -Publish        # ... and publish the release
```

## Usage

1. Run the installer, then launch **Open Download Manager** from the Start Menu
   (or run `odm.exe` directly if you built it yourself).
2. Click **+ Add download**, paste a URL, press **Enter**, then **DOWNLOAD**.
3. Downloads are saved to your Downloads folder by default; the destination can
   be changed per download in the New download dialog.
4. Settings and the download list are stored in
   `%APPDATA%\Open Download Manager\` (`settings.ini` and `downloads.tsv`).

## Project layout

```
main.cpp              UI, engine, palette, dialogs, preferences, animations
app.rc                icon, manifest and version resources
app.ico               application icon (multiple sizes)
app.manifest          DPI awareness, supported OS, execution level
build.bat             MSVC build script
build-installer.bat   Inno Setup build script
installer.iss         Inno Setup installer definition
installer-wizard.bmp  installer welcome/finish artwork
installer-small.bmp   installer header artwork
sign.bat              Authenticode signing helper (odm.exe)
release.ps1           full release pipeline (build, sign, verify, checksum, publish)
rotate-cert.ps1       create a new self-signed code-signing cert + PFX
show-secret.ps1       read the stored PFX password from Credential Manager (run locally)
LICENSE               MIT license
README.md             this file
dist/                 installer output (generated)
odm.exe               built binary (generated)
```

Generated files (`odm.exe`, `app.res`, `main.obj`, `dist/`, `SHA256SUMS.txt`)
are ignored by Git.

## License

MIT - see [LICENSE](LICENSE).
