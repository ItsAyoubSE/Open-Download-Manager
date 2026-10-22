# Open Download Manager

A fast, native Windows download manager written in C++ with Win32, Direct2D and
DirectWrite. Single self-contained executable, no runtime dependencies beyond
the Windows system libraries.

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
| **Privileges** | None - runs as a normal user, no installer, no admin rights |
| **Runtime** | None - the C++ runtime is statically linked. No .NET, Qt, Java, VC++ Redistributable or extra DLLs required |
| **Network** | Internet connection for downloads; uses the built-in Windows WinHTTP stack, so system proxy and TLS settings apply automatically |

Windows 7/8.1 are not supported - the app relies on Windows 10 APIs and will
not start there.

> The binary is currently unsigned. If Windows SmartScreen shows
> "Windows protected your PC" on first launch, choose **More info** ->
> **Run anyway**.

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

## Usage

1. Run `odm.exe`.
2. Click **+ Add download**, paste a URL, press **Enter**, then **DOWNLOAD**.
3. Downloads are saved to your Downloads folder by default; the destination can
   be changed per download in the New download dialog.

## Project layout

```
main.cpp    UI, engine, palette, dialogs, preferences, animations
build.bat   MSVC build script
odm.exe     built binary (not tracked)
```

## License

MIT - see [LICENSE](LICENSE).
