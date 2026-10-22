# Open Download Manager

A fast, native Windows download manager written in C++ with Win32, Direct2D and
DirectWrite. Single self-contained executable, no runtime dependencies beyond
the Windows system libraries.

## Download

Grab the latest installer from the
[Releases](https://github.com/ItsAyoubSE/Open-Download-Manager/releases) page:

- `OpenDownloadManager-0.2.1-setup.exe` - installs per-user without admin
  rights, adds Start Menu and optional desktop shortcuts, and registers an
  uninstaller.

The installer is signed, and `SHA256SUMS.txt` lists its SHA-256 checksum.

> **Building from source?** `build.bat` produces an **unsigned** `odm.exe`.
> Code signing has to be done manually after every build, so Windows Defender
> SmartScreen may show *"Windows protected your PC"* on first launch. Choose
> **More info -> Run anyway**, or sign the binary yourself - see
> [Code signing](#code-signing). Official releases are already signed.

## Features

**Implemented**

- Segmented downloads - up to 8 parallel HTTP Range requests, with resume and
  per-segment file offsets.
- Pause, resume and cancel; progress, speed and ETA per item.
- Categories and tags - documents, video, music, programs, archives, other.
- Queues, sorting (8 modes) and filtering (6 modes).
- Add dialog, New download dialog with editable folder and filename, live probe
  of remote size and free disk space.
- Clipboard URL handling and drag-and-drop of URLs.
- Double-click a completed download to open the file, or use the bin icon to
  delete it from disk (with confirmation).
- Dark, light and system themes - custom-drawn UI and a native dark title bar.
- Preferences screen with sidebar and General page.
- Animations - press feedback, dialog pop-in, menu slide-in.
- Torrent/magnet/.torrent input is detected and reported as "coming soon".
- Unfinished sections (proxy, speed limits, add-ons, remote client, portable mode,
  auto shutdown, export/import) show a "coming soon" notice.

**Planned**

- Speed limiting and proxy configuration.
- System tray icon and minimize-to-tray.
- Browser integration and clipboard auto-capture toggle.
- BitTorrent engine, auto-extract and update checks.

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

Official release builds are signed. Local builds produced by `build.bat` are
not, so Windows SmartScreen may warn on first launch until you sign them. To
sign a build:

```bat
set ODM_PFX=C:\path\to\cert.pfx
set ODM_PFX_PASS=yourpassword
sign.bat
```

To sign the installer as well, run the same `signtool` command on
`dist\OpenDownloadManager-<version>-setup.exe` after building it.

A **self-signed** certificate only removes warnings on machines that trust that
certificate (install the `.cer` into *Trusted Root* and *Trusted Publishers*).
For a public release you need a CA-issued code signing certificate (OV or EV);
for free open-source projects look at [SignPath Foundation](https://signpath.org/)
or Certum's open-source code signing certificate.

Signing does not prevent antivirus false positives. If a scanner flags the
build, submit it to Microsoft at
<https://www.microsoft.com/en-us/wdsi/filesubmission>.

## Usage

1. Run the installer, then launch **Open Download Manager** from the Start Menu
   (or run `odm.exe` directly if you built it yourself).
2. Click **+ Add download**, paste a URL, press **Enter**, then **DOWNLOAD**.
3. Downloads are saved to your Downloads folder by default; the destination can
   be changed per download in the New download dialog.

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
sign.bat              Authenticode signing helper
LICENSE               MIT license
README.md             this file
dist/                 installer output (generated)
odm.exe               built binary (generated)
```

Generated files (`odm.exe`, `app.res`, `main.obj`, `dist/`, `SHA256SUMS.txt`)
are ignored by Git.

## License

MIT - see [LICENSE](LICENSE).
