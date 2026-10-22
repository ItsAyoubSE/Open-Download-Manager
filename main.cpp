// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ItsAyoubSE
//
// Open Download Manager - a native Win32 download manager.
// See LICENSE for the full license text.

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <winhttp.h>
#include <shlobj.h>
#include <shellapi.h>
#include <knownfolders.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <fstream>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::ComPtr;

static HWND g_hwnd = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static D2D1_RECT_F R(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
static D2D1_COLOR_F C(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }

struct Palette {
    D2D1_COLOR_F bg, panel, panelHi, border, text, muted, accent, accentHi,
        green, red, amber, track, hover, dim, white;
};

static Palette PAL_DARK = {
    C(0.090f, 0.098f, 0.114f), C(0.122f, 0.133f, 0.153f), C(0.155f, 0.171f, 0.196f),
    C(0.227f, 0.247f, 0.278f), C(0.867f, 0.878f, 0.898f), C(0.561f, 0.584f, 0.620f),
    C(0.176f, 0.498f, 0.976f), C(0.247f, 0.560f, 0.992f), C(0.204f, 0.780f, 0.349f),
    C(0.910f, 0.322f, 0.322f), C(0.902f, 0.706f, 0.275f), C(0.090f, 0.098f, 0.114f),
    C(0.165f, 0.184f, 0.212f), C(0.0f, 0.0f, 0.0f, 0.45f), C(1.0f, 1.0f, 1.0f)
};
static Palette PAL_LIGHT = {
    C(0.949f, 0.953f, 0.961f), C(1.000f, 1.000f, 1.000f), C(0.925f, 0.933f, 0.945f),
    C(0.831f, 0.851f, 0.878f), C(0.118f, 0.137f, 0.161f), C(0.420f, 0.447f, 0.502f),
    C(0.176f, 0.498f, 0.976f), C(0.137f, 0.420f, 0.851f), C(0.122f, 0.616f, 0.333f),
    C(0.839f, 0.271f, 0.271f), C(0.718f, 0.475f, 0.122f), C(0.886f, 0.898f, 0.914f),
    C(0.902f, 0.914f, 0.929f), C(0.0f, 0.0f, 0.0f, 0.30f), C(1.0f, 1.0f, 1.0f)
};

static Palette* g_pal = &PAL_DARK;

#define C_BG       (g_pal->bg)
#define C_PANEL    (g_pal->panel)
#define C_PANEL_HI (g_pal->panelHi)
#define C_BORDER   (g_pal->border)
#define C_TEXT     (g_pal->text)
#define C_MUTED    (g_pal->muted)
#define C_ACCENT   (g_pal->accent)
#define C_ACCENT_HI (g_pal->accentHi)
#define C_GREEN    (g_pal->green)
#define C_RED      (g_pal->red)
#define C_AMBER    (g_pal->amber)
#define C_TRACK    (g_pal->track)
#define C_HOVER    (g_pal->hover)
#define C_DIM      (g_pal->dim)
#define C_WHITE    (g_pal->white)

static const float TOP_H    = 52.0f;
static const float STATUS_H = 34.0f;
static const float CARD_H   = 84.0f;
static const float GAP      = 8.0f;
static const float PAD      = 14.0f;

static bool InRect(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
static std::wstring TrimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) b--;
    return s.substr(a, b - a);
}

static std::wstring FmtBytes(uint64_t b) {
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB" };
    double v = (double)b;
    int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; i++; }
    wchar_t buf[64];
    if (i == 0) swprintf(buf, 64, L"%llu %s", (unsigned long long)b, u[i]);
    else swprintf(buf, 64, L"%.1f %s", v, u[i]);
    return buf;
}
static std::wstring FmtSpeed(double s) {
    return FmtBytes((uint64_t)(s < 0 ? 0 : s)) + L"/s";
}

static bool SystemIsDark() {
    HKEY k;
    DWORD v = 1, sz = sizeof(v);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&v, &sz);
        RegCloseKey(k);
    }
    return v == 0;
}
static void ApplyTheme(HWND hwnd, int mode) {
    bool dark = (mode == 1) || (mode == 0 && SystemIsDark());
    g_pal = dark ? &PAL_DARK : &PAL_LIGHT;
    BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof(d));
    DwmFlush();
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

static std::wstring ClipboardText() {
    std::wstring out;
    if (!OpenClipboard(g_hwnd)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        auto* p = (wchar_t*)GlobalLock(h);
        if (p) { out = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}

static std::wstring ReadTextFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    DWORD sz = GetFileSize(h, nullptr);
    if (sz == INVALID_FILE_SIZE || sz > 10 * 1024 * 1024) { CloseHandle(h); return L""; }
    std::string buf(sz, '\0');
    DWORD rd = 0;
    ReadFile(h, buf.data(), sz, &rd, nullptr);
    CloseHandle(h);
    buf.resize(rd);
    if (buf.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), nullptr, 0);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), &w[0], wlen);
    return w;
}

static std::vector<std::wstring> SplitLines(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            std::wstring t = TrimW(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::wstring t = TrimW(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

static bool PickFolder(std::wstring& out) {
    BROWSEINFOW bi{};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"Select the download folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return false;
    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(id, path) != FALSE;
    CoTaskMemFree(id);
    if (ok) out = path;
    return ok;
}

static std::wstring DiskFreeText(const std::wstring& folder) {
    ULARGE_INTEGER freeBytes{}, total{}, totalFree{};
    if (GetDiskFreeSpaceExW(folder.c_str(), &freeBytes, &total, &totalFree))
        return FmtBytes(freeBytes.QuadPart);
    return L"?";
}

static std::wstring PickUrlFile() {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"URL list (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return file;
    return L"";
}

// ---------------------------------------------------------------------------
// Download model
// ---------------------------------------------------------------------------
// state: 0 pending, 1 downloading, 2 completed, 3 failed, 4 paused
struct DownloadItem {
    std::wstring url;
    std::wstring filename;
    std::wstring ext;
    std::wstring category;
    std::wstring destPath;
    std::atomic<uint64_t> total{ 0 };
    std::atomic<uint64_t> downloaded{ 0 };
    std::atomic<int> state{ 0 };   // 0 pending,1 downloading,2 completed,3 failed,4 paused
    std::atomic<bool> started{ false };
    std::atomic<bool> cancel{ false };
    std::atomic<bool> paused{ false };
    std::wstring error;
    uint64_t seq = 0;
    double speed = 0;
    uint64_t lastBytes = 0;
    ULONGLONG lastTick = 0;
};

struct ProbeState {
    std::atomic<uint64_t> size{ 0 };
    std::atomic<int> done{ 0 };
    std::atomic<int> ok{ 1 };
};

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
struct App {
    HWND hwnd = nullptr;
    std::vector<std::shared_ptr<DownloadItem>> items;
    int sortMode = 0;
    int filterMode = 0;
    bool sortOpen = false;
    bool filterOpen = false;
    bool menuOpen = false;
    std::wstring search;
    std::wstring urlInput;
    int focus = 0; // 0 none, 1 search, 2 add-url, 3 new-name
    bool addOpen = false;
    bool newOpen = false;
    bool aboutOpen = false;
    int view = 0; // 0 downloads, 1 preferences
    bool on = true;
    int traffic = 2;
    int pressKind = 0;
    float pressAnim = 0.0f;
    float openAnim = 1.0f;
    float scrollY = 0;
    float mouseX = -1, mouseY = -1;
    uint64_t seq = 0;

    // New-download dialog
    std::wstring newUrl, newName, newFolder, newExt;
    bool newScheduler = false;
    std::shared_ptr<ProbeState> probe;

    // Preferences
    int prefSection = 0;
    int themeMode = 0;
    int uiStyle = 0;
    int language = 0;
    bool launchStartup = false;
    int folderMode = 0;
    bool suggestType = false;
    bool suggestUrl = false;
    bool clipboardWatch = false;
    std::wstring lastClip;
    std::wstring fixedFolder;
    int prefDropdown = 0; // 0 none, 1 theme, 2 uistyle, 3 language

    // Saved window geometry (0 = use default)
    int winX = 0, winY = 0, winW = 0, winH = 0;

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<IDWriteTextFormat> fTitle, fBody, fSmall, fTiny, fBold, fHead;
    ComPtr<ID2D1StrokeStyle> dash;
};

static App* g_app = nullptr;
static std::atomic<int> g_traffic{ 2 }; // 0 low,1 med,2 high

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------
static std::wstring SettingsDir() {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) {
        std::wstring s = p;
        CoTaskMemFree(p);
        return s + L"\\Open Download Manager";
    }
    wchar_t up[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", up, MAX_PATH))
        return std::wstring(up) + L"\\Open Download Manager";
    return L".";
}
static std::wstring SettingsPath() { return SettingsDir() + L"\\settings.ini"; }
static std::wstring DownloadsPath() { return SettingsDir() + L"\\downloads.tsv"; }

static bool WriteUtf8File(const std::wstring& path, const std::wstring& content) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string utf8;
    int n = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(),
                                nullptr, 0, nullptr, nullptr);
    utf8.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(),
                        &utf8[0], n, nullptr, nullptr);
    DWORD wr = 0;
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &wr, nullptr);
    CloseHandle(h);
    return true;
}

static void SaveDownloads(App& a);
static void LoadDownloads(App& a);

static void SaveSettings(App& a) {
    std::wstring dir = SettingsDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring s;
    auto add = [&](const wchar_t* k, const std::wstring& v) { s += k; s += L"="; s += v; s += L"\r\n"; };
    auto addi = [&](const wchar_t* k, int v) { s += k; s += L"="; s += std::to_wstring(v); s += L"\r\n"; };
    addi(L"theme", a.themeMode);
    addi(L"uiStyle", a.uiStyle);
    addi(L"language", a.language);
    addi(L"folderMode", a.folderMode);
    add(L"fixedFolder", a.fixedFolder);
    addi(L"launchStartup", a.launchStartup ? 1 : 0);
    addi(L"traffic", a.traffic);
    addi(L"suggestType", a.suggestType ? 1 : 0);
    addi(L"suggestUrl", a.suggestUrl ? 1 : 0);
    addi(L"clipboardWatch", a.clipboardWatch ? 1 : 0);
    addi(L"winX", a.winX);
    addi(L"winY", a.winY);
    addi(L"winW", a.winW);
    addi(L"winH", a.winH);

    HANDLE h = CreateFileW(SettingsPath().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string utf8;
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    utf8.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &utf8[0], n, nullptr, nullptr);
    DWORD wr = 0;
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &wr, nullptr);
    CloseHandle(h);
}

static void LoadSettings(App& a) {
    std::wstring text = ReadTextFile(SettingsPath());
    for (auto& line : SplitLines(text)) {
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = TrimW(line.substr(0, eq));
        std::wstring v = TrimW(line.substr(eq + 1));
        int iv = _wtoi(v.c_str());
        if (k == L"theme") a.themeMode = iv;
        else if (k == L"uiStyle") a.uiStyle = iv;
        else if (k == L"language") a.language = iv;
        else if (k == L"folderMode") a.folderMode = iv;
        else if (k == L"fixedFolder") a.fixedFolder = v;
        else if (k == L"launchStartup") a.launchStartup = iv != 0;
        else if (k == L"traffic") a.traffic = iv;
        else if (k == L"suggestType") a.suggestType = iv != 0;
        else if (k == L"suggestUrl") a.suggestUrl = iv != 0;
        else if (k == L"clipboardWatch") a.clipboardWatch = iv != 0;
        else if (k == L"winX") a.winX = iv;
        else if (k == L"winY") a.winY = iv;
        else if (k == L"winW") a.winW = iv;
        else if (k == L"winH") a.winH = iv;
    }
    if (a.traffic < 0 || a.traffic > 2) a.traffic = 2;
    g_traffic = a.traffic;
}

static std::vector<std::wstring> SplitTabs(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L'\t') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

static void SaveDownloads(App& a) {
    std::wstring s;
    for (auto& it : a.items) {
        s += std::to_wstring(it->seq); s += L"\t";
        s += std::to_wstring(it->state.load()); s += L"\t";
        s += std::to_wstring(it->total.load()); s += L"\t";
        s += std::to_wstring(it->downloaded.load()); s += L"\t";
        s += it->category; s += L"\t";
        s += it->ext; s += L"\t";
        s += it->filename; s += L"\t";
        s += it->destPath; s += L"\t";
        s += it->url; s += L"\r\n";
    }
    WriteUtf8File(DownloadsPath(), s);
}

static void LoadDownloads(App& a) {
    std::wstring text = ReadTextFile(DownloadsPath());
    for (auto& line : SplitLines(text)) {
        auto f = SplitTabs(line);
        if (f.size() < 9) continue;
        auto it = std::make_shared<DownloadItem>();
        it->seq = _wcstoui64(f[0].c_str(), nullptr, 10);
        int st = _wtoi(f[1].c_str());
        // Incomplete downloads from a previous session start paused.
        it->state = (st == 0 || st == 1) ? 4 : st;
        it->total = _wcstoui64(f[2].c_str(), nullptr, 10);
        it->downloaded = _wcstoui64(f[3].c_str(), nullptr, 10);
        it->category = f[4];
        it->ext = f[5];
        it->filename = f[6];
        it->destPath = f[7];
        it->url = f[8];
        it->started = true;
        it->lastTick = GetTickCount64();
        if (it->seq > a.seq) a.seq = it->seq;
        a.items.push_back(it);
    }
}

static float MeasureText(App& a, IDWriteTextFormat* f, const std::wstring& s) {
    if (!f || s.empty()) return 0;
    ComPtr<IDWriteTextLayout> lay;
    a.dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 4000.0f, 200.0f, &lay);
    DWRITE_TEXT_METRICS m{};
    if (lay) lay->GetMetrics(&m);
    return m.width;
}

// ---------------------------------------------------------------------------
// Text / shape drawing
// ---------------------------------------------------------------------------
static ComPtr<IDWriteTextFormat> MakeFormat(IDWriteFactory* dw, float size, DWRITE_FONT_WEIGHT weight) {
    ComPtr<IDWriteTextFormat> f;
    dw->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    if (f) {
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> ell;
        dw->CreateEllipsisTrimmingSign(f.Get(), &ell);
        f->SetTrimming(&trim, ell.Get());
    }
    return f;
}

static void Txt(ID2D1RenderTarget* rt, IDWriteTextFormat* f, const std::wstring& s,
                const D2D1_RECT_F& r, D2D1_COLOR_F col,
                DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT p = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
    if (!f || s.empty()) return;
    f->SetTextAlignment(a);
    f->SetParagraphAlignment(p);
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, br.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static void FillRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float rad, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get());
}
static void StrokeRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float rad,
                        D2D1_COLOR_F col, float w = 1.0f, ID2D1StrokeStyle* style = nullptr) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get(), w, style);
}
static void FillRect2(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->FillRectangle(r, br.Get());
}
static void Line(ID2D1RenderTarget* rt, float x1, float y1, float x2, float y2, D2D1_COLOR_F col, float w) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), br.Get(), w);
}
static void Dot(ID2D1RenderTarget* rt, float cx, float cy, float rad, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), br.Get());
}

static void Chevron(ID2D1RenderTarget* rt, float cx, float cy, float s, D2D1_COLOR_F col) {
    Line(rt, cx - s, cy - s * 0.4f, cx, cy + s * 0.4f, col, 1.6f);
    Line(rt, cx, cy + s * 0.4f, cx + s, cy - s * 0.4f, col, 1.6f);
}
static void ChevronRight(ID2D1RenderTarget* rt, float cx, float cy, D2D1_COLOR_F col) {
    Line(rt, cx - 3, cy - 4, cx + 1, cy, col, 1.6f);
    Line(rt, cx + 1, cy, cx - 3, cy + 4, col, 1.6f);
}
static void CheckBox(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool on, bool enabled = true) {
    FillRound(rt, r, 4.0f, on ? C_ACCENT : (enabled ? C_PANEL_HI : C_TRACK));
    StrokeRound(rt, r, 4.0f, on ? C_ACCENT : C_BORDER, 1.0f);
    if (on) {
        float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        Line(rt, cx - 4, cy, cx - 1, cy + 3, C_WHITE, 1.8f);
        Line(rt, cx - 1, cy + 3, cx + 5, cy - 4, C_WHITE, 1.8f);
    }
}
static void Radio(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool on) {
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(on ? C_ACCENT : C_BORDER, &br);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8, 8), br.Get(), 1.5f);
    if (on) Dot(rt, cx, cy, 4.0f, C_ACCENT);
}
static void LinkIcon(ID2D1RenderTarget* rt, float cx, float cy, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-45.0f, D2D1::Point2F(cx, cy)));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(R(cx - 15, cy - 5.5f, cx + 3, cy + 5.5f), 5.5f, 5.5f), br.Get(), 1.6f);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(R(cx - 3, cy - 5.5f, cx + 15, cy + 5.5f), 5.5f, 5.5f), br.Get(), 1.6f);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}
static void FolderIcon(ID2D1RenderTarget* rt, float cx, float cy, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(R(cx - 9, cy - 5, cx + 9, cy + 7), 2.0f, 2.0f), br.Get(), 1.5f);
    rt->DrawLine(D2D1::Point2F(cx - 9, cy - 2), D2D1::Point2F(cx - 3, cy - 2), br.Get(), 1.5f);
    rt->DrawLine(D2D1::Point2F(cx - 3, cy - 2), D2D1::Point2F(cx - 1, cy - 5), br.Get(), 1.5f);
}
static void HeartIcon(ID2D1RenderTarget* rt, float cx, float cy, D2D1_COLOR_F col) {
    Dot(rt, cx - 3.6f, cy - 1.6f, 4.2f, col);
    Dot(rt, cx + 3.6f, cy - 1.6f, 4.2f, col);
    ComPtr<ID2D1PathGeometry> geo;
    g_app->d2d->CreatePathGeometry(&geo);
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(&sink);
    sink->BeginFigure(D2D1::Point2F(cx - 7.6f, cy), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(cx + 7.6f, cy));
    sink->AddLine(D2D1::Point2F(cx, cy + 8.0f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->FillGeometry(geo.Get(), br.Get());
}
static void Magnifier(ID2D1RenderTarget* rt, float cx, float cy, float s, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(col, &br);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s, s), br.Get(), 1.6f);
    Line(rt, cx + s * 0.7f, cy + s * 0.7f, cx + s * 1.5f, cy + s * 1.5f, col, 1.6f);
}
static void DownArrow(ID2D1RenderTarget* rt, float cx, float cy, float h, D2D1_COLOR_F col, float w) {
    Line(rt, cx, cy - h, cx, cy + h, col, w);
    Line(rt, cx - h * 0.62f, cy + h * 0.30f, cx, cy + h, col, w);
    Line(rt, cx + h * 0.62f, cy + h * 0.30f, cx, cy + h, col, w);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
struct Layout {
    D2D1_RECT_F toolbar, sort, filter, add, search, gear, status, content;
};

static Layout MakeLayout(float W, float H) {
    Layout L{};
    L.toolbar = R(0, 0, W, TOP_H);
    L.status = R(0, H - STATUS_H, W, H);
    L.content = R(0, TOP_H, W, H - STATUS_H);

    float y = 12.0f, h = 28.0f;
    L.sort = R(10, y, 106, y + h);
    L.filter = R(112, y, 226, y + h);
    L.gear = R(W - 38, y, W - 10, y + h);

    float searchR = W - 48;
    float searchL = std::max(280.0f, searchR - 250);
    L.search = R(searchL, y, searchR, y + h);

    float addR = std::max(430.0f, searchL - 10);
    L.add = R(addR - 150, y, addR, y + h);
    return L;
}

static const wchar_t* SORT_LABELS[] = {
    L"Newest", L"Old", L"A-Z", L"Z-A",
    L"By size: large", L"By size: small", L"By status: ascending", L"By status: descending"
};
static const wchar_t* SORT_SHORT[] = {
    L"Newest", L"Old", L"A-Z", L"Z-A",
    L"Size \x2193", L"Size \x2191", L"Status \x2191", L"Status \x2193"
};
static const wchar_t* FILTER_LABELS[] = {
    L"All files", L"Active", L"Completed", L"Torrent", L"Video", L"Music"
};
static const wchar_t* THEME_LABELS[] = { L"System", L"Dark", L"Light" };
static const wchar_t* UISTYLE_LABELS[] = { L"New", L"Classic" };
static const wchar_t* LANGUAGE_LABELS[] = {
    L"English (United States)", L"English (United Kingdom)", L"Arabic",
    L"French", L"German", L"Spanish", L"Japanese"
};
static const wchar_t* LANGUAGE_CODES[] = { L"EN", L"EN", L"AR", L"FR", L"DE", L"ES", L"JA" };
static const wchar_t* PREF_SECTIONS[] = {
    L"General", L"Browser Integration", L"Network", L"Traffic Limits",
    L"Antivirus", L"BitTorrent", L"Remote Access", L"Advanced"
};

struct MenuItemDef { const wchar_t* label; int action; bool disabled; bool arrow; bool sep; };
static const MenuItemDef APP_MENU[] = {
    { L"Paste urls from clipboard", 0, false, false, false },
    { L"Paste urls from file",      1, false, false, false },
    { L"Export/Import",             2, false, true,  false },
    { L"Add-ons",                   3, false, false, false },
    { L"Preferences",               4, false, false, false },
    { nullptr,                     -1, false, false, true  },
    { L"Contact support",           5, false, false, false },
    { L"Submit a bug report",       6, false, false, false },
    { L"Auto shutdown",             7, false, false, false },
    { L"Connect to remote client",   8, false, false, false },
    { L"Create portable version",   9, false, false, false },
    { L"Check for updates",        10, false, false, false },
    { L"About",                    12, false, false, false },
    { nullptr,                     -1, false, false, true  },
    { L"Quit",                     13, false, false, false },
};
static const int APP_MENU_N = (int)(sizeof(APP_MENU) / sizeof(APP_MENU[0]));
static const float MENU_W = 236.0f;

static float MenuHeight() {
    float h = 8;
    for (int i = 0; i < APP_MENU_N; i++) h += APP_MENU[i].sep ? 9.0f : 28.0f;
    return h;
}
static D2D1_RECT_F MenuRect(float W) {
    return R(W - 10 - MENU_W, TOP_H + 2, W - 10, TOP_H + 2 + MenuHeight());
}
static int HitMenu(float W, float x, float y) {
    D2D1_RECT_F r = MenuRect(W);
    if (!InRect(r, x, y)) return -1;
    float yy = r.top + 4;
    for (int i = 0; i < APP_MENU_N; i++) {
        if (APP_MENU[i].sep) { yy += 9; continue; }
        if (y >= yy && y < yy + 28) return i;
        yy += 28;
    }
    return -1;
}

static D2D1_RECT_F SortDropRect(const Layout& L) {
    float t = L.sort.bottom + 4;
    return R(L.sort.left, t, L.sort.left + 220, t + 8 * 28 + 8);
}
static D2D1_RECT_F FilterDropRect(const Layout& L) {
    float t = L.filter.bottom + 4;
    return R(L.filter.left, t, L.filter.left + 176, t + 6 * 28 + 9 + 8);
}
static int HitList(const D2D1_RECT_F& d, float y, int n, int sepAfter) {
    float yy = d.top + 4;
    for (int i = 0; i < n; i++) {
        if (sepAfter >= 0 && i == sepAfter + 1) yy += 9;
        if (y >= yy && y < yy + 28) return i;
        yy += 28;
    }
    return -1;
}

static D2D1_RECT_F AddDialogRect(float W, float H) {
    float cw = 620, ch = 210;
    return R((W - cw) / 2, (H - ch) / 2, (W + cw) / 2, (H + ch) / 2);
}
static D2D1_RECT_F AddInputRect(const D2D1_RECT_F& d) {
    return R(d.left + 24, d.top + 82, d.right - 24 - 112, d.top + 120);
}
static D2D1_RECT_F AddBrowseRect(const D2D1_RECT_F& d) {
    return R(d.right - 24 - 104, d.top + 82, d.right - 24, d.top + 120);
}
static D2D1_RECT_F AddOkRect(const D2D1_RECT_F& d) {
    return R(d.right - 24 - 84, d.bottom - 24 - 34, d.right - 24, d.bottom - 24);
}
static D2D1_RECT_F AddCancelRect(const D2D1_RECT_F& d) {
    return R(d.right - 24 - 84 - 10 - 92, d.bottom - 24 - 34, d.right - 24 - 84 - 10, d.bottom - 24);
}
static D2D1_RECT_F AddCloseRect(const D2D1_RECT_F& d) {
    return R(d.right - 38, d.top + 10, d.right - 12, d.top + 36);
}

static D2D1_RECT_F NewDialogRect(float W, float H) {
    float cw = 600, ch = 330;
    return R((W - cw) / 2, (H - ch) / 2, (W + cw) / 2, (H + ch) / 2);
}
static D2D1_RECT_F NewFolderRect(const D2D1_RECT_F& d) {
    return R(d.left + 24, d.top + 78, d.right - 24 - 46, d.top + 112);
}
static D2D1_RECT_F NewFolderBtn(const D2D1_RECT_F& d) {
    return R(d.right - 24 - 36, d.top + 78, d.right - 24, d.top + 112);
}
static D2D1_RECT_F NewNameRect(const D2D1_RECT_F& d) {
    return R(d.left + 24, d.top + 144, d.right - 24, d.top + 178);
}
static D2D1_RECT_F NewSchedBox(const D2D1_RECT_F& d) {
    return R(d.left + 24, d.top + 192, d.left + 40, d.top + 208);
}
static D2D1_RECT_F NewOkRect(const D2D1_RECT_F& d) {
    return R(d.right - 24 - 112, d.bottom - 24 - 36, d.right - 24, d.bottom - 24);
}
static D2D1_RECT_F NewLaterRect(const D2D1_RECT_F& d) {
    return R(NewOkRect(d).left - 10 - 130, d.bottom - 24 - 36, NewOkRect(d).left - 10, d.bottom - 24);
}
static D2D1_RECT_F NewCancelRect(const D2D1_RECT_F& d) {
    return R(NewLaterRect(d).left - 10 - 90, d.bottom - 24 - 36, NewLaterRect(d).left - 10, d.bottom - 24);
}
static D2D1_RECT_F NewCloseRect(const D2D1_RECT_F& d) {
    return R(d.right - 38, d.top + 10, d.right - 12, d.top + 36);
}

static const float SIDEBAR_W = 240.0f;
static const float PREF_X = 270.0f;
static D2D1_RECT_F PrefBackRect() { return R(12, 12, 40, 40); }
static D2D1_RECT_F PrefMenuRect(float W) { return R(W - 38, 12, W - 10, 40); }
static D2D1_RECT_F PrefSideItem(int i) {
    float y = TOP_H + 14 + i * 36.0f;
    return R(12, y, 228, y + 34);
}
static D2D1_RECT_F PrefThemeBtn() { return R(PREF_X + 150, TOP_H + 58, PREF_X + 310, TOP_H + 88); }
static D2D1_RECT_F PrefUiBtn() { return R(PREF_X + 150, TOP_H + 122, PREF_X + 310, TOP_H + 152); }
static D2D1_RECT_F PrefLangBtn() { return R(PREF_X + 150, TOP_H + 170, PREF_X + 390, TOP_H + 200); }
static D2D1_RECT_F PrefLaunchBox() { return R(PREF_X, TOP_H + 272, PREF_X + 18, TOP_H + 290); }
static D2D1_RECT_F PrefAutoRadio() { return R(PREF_X, TOP_H + 330, PREF_X + 18, TOP_H + 348); }
static D2D1_RECT_F PrefTypeBox() { return R(PREF_X + 30, TOP_H + 358, PREF_X + 48, TOP_H + 376); }
static D2D1_RECT_F PrefUrlBox() { return R(PREF_X + 30, TOP_H + 384, PREF_X + 48, TOP_H + 402); }
static D2D1_RECT_F PrefFixedRadio() { return R(PREF_X, TOP_H + 410, PREF_X + 18, TOP_H + 428); }
static D2D1_RECT_F PrefPathBox(float W) { return R(PREF_X + 30, TOP_H + 436, W - 30 - 100, TOP_H + 468); }
static D2D1_RECT_F PrefPathBrowse(float W) { return R(W - 30 - 90, TOP_H + 436, W - 30, TOP_H + 468); }
static D2D1_RECT_F PrefClipBox() { return R(PREF_X, TOP_H + 474, PREF_X + 18, TOP_H + 492); }

static D2D1_RECT_F AboutRect(float W, float H) {
    float cw = 440, ch = 250;
    return R((W - cw) / 2, (H - ch) / 2, (W + cw) / 2, (H + ch) / 2);
}
static D2D1_RECT_F AboutOkRect(const D2D1_RECT_F& d) {
    return R(d.right - 24 - 100, d.bottom - 24 - 34, d.right - 24, d.bottom - 24);
}

// ---------------------------------------------------------------------------
// Filename / category helpers
// ---------------------------------------------------------------------------
static std::wstring ExtOf(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= name.size()) return L"";
    std::wstring e = name.substr(dot + 1);
    if (e.size() > 5) return L"";
    return Lower(e);
}
static std::wstring CategoryOf(const std::wstring& ext) {
    static const wchar_t* video[] = { L"mp4",L"mkv",L"avi",L"mov",L"webm",L"flv",L"wmv",L"m4v",L"mpg",L"mpeg",L"ts",L"3gp" };
    static const wchar_t* music[] = { L"mp3",L"flac",L"wav",L"aac",L"ogg",L"m4a",L"opus",L"wma" };
    static const wchar_t* docs[]  = { L"pdf",L"doc",L"docx",L"xls",L"xlsx",L"ppt",L"pptx",L"txt",L"epub",L"csv",L"rtf" };
    static const wchar_t* soft[]  = { L"exe",L"msi",L"apk",L"dmg",L"appx",L"deb",L"rpm" };
    static const wchar_t* arch[]  = { L"zip",L"rar",L"7z",L"tar",L"gz",L"bz2",L"xz",L"iso" };
    auto has = [&](const wchar_t** list, size_t n) {
        for (size_t i = 0; i < n; i++) if (ext == list[i]) return true;
        return false;
    };
    if (has(video, sizeof(video) / sizeof(video[0]))) return L"Video";
    if (has(music, sizeof(music) / sizeof(music[0]))) return L"Music";
    if (has(docs, sizeof(docs) / sizeof(docs[0]))) return L"Documents";
    if (has(soft, sizeof(soft) / sizeof(soft[0]))) return L"Software";
    if (has(arch, sizeof(arch) / sizeof(arch[0]))) return L"Archives";
    return L"General";
}
static D2D1_COLOR_F CategoryColor(const std::wstring& cat) {
    if (cat == L"Video") return C(0.478f, 0.353f, 0.902f);
    if (cat == L"Music") return C(0.902f, 0.298f, 0.549f);
    if (cat == L"Documents") return C(0.247f, 0.549f, 0.902f);
    if (cat == L"Software") return C(0.204f, 0.702f, 0.435f);
    if (cat == L"Archives") return C(0.851f, 0.596f, 0.231f);
    return C(0.365f, 0.412f, 0.478f);
}
static std::wstring UrlDecode(const std::wstring& s);

static std::wstring FilenameFromUrl(const std::wstring& url) {
    std::wstring u = url;
    size_t q = u.find_first_of(L"?#");
    if (q != std::wstring::npos) u = u.substr(0, q);
    size_t slash = u.find_last_of(L'/');
    std::wstring name = (slash == std::wstring::npos) ? u : u.substr(slash + 1);
    name = UrlDecode(name);
    if (name.empty()) name = L"download";
    for (auto& c : name) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return name;
}
static std::wstring DownloadsDir() {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p)) && p) {
        std::wstring s = p;
        CoTaskMemFree(p);
        return s;
    }
    wchar_t up[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return std::wstring(up) + L"\\Downloads";
    return L".";
}
static std::wstring UniquePath(const std::wstring& dir, const std::wstring& name) {
    std::wstring base = name, ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) { base = name.substr(0, dot); ext = name.substr(dot); }
    std::wstring p = dir + L"\\" + name;
    int i = 1;
    while (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
        wchar_t buf[32];
        swprintf(buf, 32, L" (%d)", i++);
        p = dir + L"\\" + base + buf + ext;
    }
    return p;
}

// ---------------------------------------------------------------------------
// WinHTTP engine
// ---------------------------------------------------------------------------
struct HttpHandle {
    HINTERNET h = nullptr;
    explicit HttpHandle(HINTERNET x = nullptr) : h(x) {}
    ~HttpHandle() { if (h) WinHttpCloseHandle(h); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    HttpHandle(HttpHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

static HINTERNET OpenSession() {
    HINTERNET h = WinHttpOpen(L"OpenDownloadManager/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h)
        h = WinHttpOpen(L"OpenDownloadManager/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (h) {
        DWORD pol = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(h, WINHTTP_OPTION_REDIRECT_POLICY, &pol, sizeof(pol));
    }
    return h;
}

static std::wstring RawHeaders(HINTERNET h) {
    DWORD len = 0;
    WinHttpQueryHeaders(h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        nullptr, &len, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return L"";
    std::wstring s(len / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                             &s[0], &len, WINHTTP_NO_HEADER_INDEX))
        return L"";
    s.resize(len / sizeof(wchar_t));
    while (!s.empty() && s.back() == L'\0') s.pop_back();
    return s;
}
static std::wstring HeaderValue(const std::wstring& raw, const std::wstring& name) {
    std::wstring lraw = Lower(raw), lname = Lower(name) + L":";
    size_t pos = 0;
    while (pos < lraw.size()) {
        size_t e = lraw.find(L"\r\n", pos);
        if (e == std::wstring::npos) e = lraw.size();
        if (e - pos >= lname.size() && lraw.compare(pos, lname.size(), lname) == 0) {
            size_t vs = pos + lname.size();
            while (vs < e && (lraw[vs] == L' ' || lraw[vs] == L'\t')) vs++;
            return raw.substr(vs, e - vs);
        }
        pos = e + 2;
    }
    return L"";
}
static DWORD StatusCode(HINTERNET h) {
    DWORD s = 0, len = sizeof(s);
    WinHttpQueryHeaders(h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &s, &len, WINHTTP_NO_HEADER_INDEX);
    return s;
}

static bool Crack(const std::wstring& url, wchar_t* host, DWORD hostLen,
                  wchar_t* path, DWORD pathLen, INTERNET_PORT& port, bool& https) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = hostLen;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = pathLen;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    port = (INTERNET_PORT)uc.nPort;
    https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

static std::wstring UrlDecode(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'%' && i + 2 < s.size()) {
            wchar_t hex[3] = { s[i+1], s[i+2], 0 };
            wchar_t* end = nullptr;
            long v = wcstol(hex, &end, 16);
            if (end == hex + 2) { out.push_back((wchar_t)v); i += 2; continue; }
        }
        if (s[i] == L'+') out.push_back(L' ');
        else out.push_back(s[i]);
    }
    return out;
}

static std::wstring ExtractFilenameCD(const std::wstring& cd) {
    std::wstring l = Lower(cd);
    // Prefer filename* (RFC 5987)
    size_t pos = l.find(L"filename*");
    bool star = (pos != std::wstring::npos);
    if (!star) {
        pos = l.find(L"filename");
        if (pos == std::wstring::npos) return L"";
    }
    size_t eq = l.find(L'=', pos);
    if (eq == std::wstring::npos) return L"";
    size_t i = eq + 1;
    while (i < cd.size() && (cd[i] == L' ' || cd[i] == L'\t')) i++;
    if (i >= cd.size()) return L"";
    std::wstring name;
    if (star) {
        // format: charset'lang'percent-encoded
        size_t apos = cd.find(L'\'', i);
        if (apos != std::wstring::npos) {
            size_t apos2 = cd.find(L'\'', apos + 1);
            if (apos2 != std::wstring::npos) i = apos2 + 1;
        }
        size_t end = cd.find_first_of(L"; \t\r\n", i);
        if (end == std::wstring::npos) end = cd.size();
        name = UrlDecode(cd.substr(i, end - i));
    } else if (cd[i] == L'"') {
        size_t end = cd.find(L'"', i + 1);
        if (end == std::wstring::npos) return L"";
        name = UrlDecode(cd.substr(i + 1, end - i - 1));
    } else {
        size_t end = cd.find_first_of(L"; \t\r\n", i);
        if (end == std::wstring::npos) end = cd.size();
        name = UrlDecode(cd.substr(i, end - i));
    }
    while (!name.empty() && (name.back() == L'"')) name.pop_back();
    return TrimW(name);
}

static bool Probe(const std::wstring& url, uint64_t& size, bool& ranges, std::wstring& filename) {
    size = 0; ranges = false;
    wchar_t host[256]{}, path[4096]{};
    INTERNET_PORT port = 0; bool https = false;
    if (!Crack(url, host, 256, path, 4096, port, https)) return false;

    HttpHandle hs(OpenSession());
    if (!hs) return false;
    HttpHandle hc(WinHttpConnect(hs, host, port, 0));
    if (!hc) return false;
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;

    {
        HttpHandle hr(WinHttpOpenRequest(hc, L"HEAD", path, nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (hr && WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(hr, nullptr)) {
            std::wstring raw = RawHeaders(hr);
            std::wstring cl = HeaderValue(raw, L"content-length");
            if (!cl.empty()) size = _wcstoui64(cl.c_str(), nullptr, 10);
            std::wstring ar = Lower(HeaderValue(raw, L"accept-ranges"));
            if (ar.find(L"bytes") != std::wstring::npos) ranges = true;
            std::wstring cd = HeaderValue(raw, L"content-disposition");
            std::wstring fn = ExtractFilenameCD(cd);
            if (!fn.empty()) filename = fn;
        }
    }

    if (size == 0 || !ranges) {
        HttpHandle hr(WinHttpOpenRequest(hc, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!hr) return false;
        WinHttpAddRequestHeaders(hr, L"Range: bytes=0-0", (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (!WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(hr, nullptr))
            return false;
        DWORD st = StatusCode(hr);
        std::wstring raw = RawHeaders(hr);
        {
            std::wstring cd = HeaderValue(raw, L"content-disposition");
            std::wstring fn = ExtractFilenameCD(cd);
            if (!fn.empty()) filename = fn;
        }
        if (st == 206) {
            ranges = true;
            std::wstring cr = HeaderValue(raw, L"content-range");
            size_t slash = cr.find(L'/');
            if (slash != std::wstring::npos) {
                std::wstring tot = TrimW(cr.substr(slash + 1));
                size = _wcstoui64(tot.c_str(), nullptr, 10);
            }
        } else if (st == 200) {
            std::wstring cl = HeaderValue(raw, L"content-length");
            if (!cl.empty()) size = _wcstoui64(cl.c_str(), nullptr, 10);
        } else if (st >= 400) {
            return false;
        }
    }
    return true;
}

static bool DownloadRange(const std::wstring& url, uint64_t start, uint64_t end,
                          const std::wstring& dest, std::shared_ptr<DownloadItem> item, bool useRange) {
    wchar_t host[256]{}, path[4096]{};
    INTERNET_PORT port = 0; bool https = false;
    if (!Crack(url, host, 256, path, 4096, port, https)) return false;

    HttpHandle hs(OpenSession());
    if (!hs) return false;
    HttpHandle hc(WinHttpConnect(hs, host, port, 0));
    if (!hc) return false;
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;

    HttpHandle hr(WinHttpOpenRequest(hc, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!hr) return false;
    if (useRange) {
        wchar_t rng[128];
        swprintf(rng, 128, L"Range: bytes=%llu-%llu",
                 (unsigned long long)start, (unsigned long long)end);
        WinHttpAddRequestHeaders(hr, rng, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    if (!WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(hr, nullptr))
        return false;

    DWORD st = StatusCode(hr);
    if (useRange && st != 206) return false;
    if (!useRange && st != 200 && st != 206) return false;

    HANDLE hf = CreateFileW(dest.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li; li.QuadPart = (LONGLONG)start;
    SetFilePointerEx(hf, li, nullptr, FILE_BEGIN);

    std::vector<char> buf(64 * 1024);
    bool ok = true;
    for (;;) {
        while (item->paused.load() && !item->cancel.load()) Sleep(100);
        if (item->cancel.load()) { ok = false; break; }
        DWORD n = 0;
        if (!WinHttpReadData(hr, buf.data(), (DWORD)buf.size(), &n)) { ok = false; break; }
        if (n == 0) break;
        DWORD wrote = 0;
        if (!WriteFile(hf, buf.data(), n, &wrote, nullptr) || wrote != n) { ok = false; break; }
        item->downloaded.fetch_add(n);
        int tr = g_traffic.load();
        if (tr == 0) Sleep(60);
        else if (tr == 1) Sleep(12);
    }
    CloseHandle(hf);
    return ok;
}

static void NotifyUI() {
    if (g_hwnd) PostMessage(g_hwnd, WM_APP + 1, 0, 0);
}

static void RunDownload(std::shared_ptr<DownloadItem> item) {
    item->state = 1;
    uint64_t size = 0;
    bool ranges = false;
    std::wstring _fn;
    if (!Probe(item->url, size, ranges, _fn)) {
        item->error = L"Could not connect";
        item->state = 3;
        NotifyUI();
        return;
    }
    item->total = size;

    HANDLE hf = CreateFileW(item->destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        item->error = L"Cannot create file";
        item->state = 3;
        NotifyUI();
        return;
    }
    CloseHandle(hf);

    bool segmented = ranges && size > 0;
    int n = 1;
    if (segmented) {
        uint64_t seg = 512 * 1024;
        n = (int)(size / seg);
        if (n < 1) n = 1;
        if (n > 8) n = 8;
    }

    std::atomic<int> fails{ 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i < n; i++) {
        uint64_t s = segmented ? (size * (uint64_t)i) / (uint64_t)n : 0;
        uint64_t e = segmented ? ((size * (uint64_t)(i + 1)) / (uint64_t)n) - 1 : 0;
        threads.emplace_back([=, &fails]() {
            if (!DownloadRange(item->url, s, e, item->destPath, item, segmented)) fails++;
        });
    }
    for (auto& t : threads) t.join();

    if (item->cancel.load()) {
        item->state = 4;
    } else if (fails.load() > 0) {
        item->error = L"Download failed";
        item->state = 3;
    } else {
        item->state = 2;
    }
    NotifyUI();
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------
static void EnsureRT(App& a) {
    if (a.rt) return;
    RECT rc; GetClientRect(a.hwnd, &rc);
    UINT dpi = GetDpiForWindow(a.hwnd);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = (float)dpi;
    props.dpiY = (float)dpi;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp =
        D2D1::HwndRenderTargetProperties(a.hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    if (FAILED(a.d2d->CreateHwndRenderTarget(props, hp, &a.rt))) return;
    a.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

static void ButtonBg(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool hover, D2D1_COLOR_F base) {
    FillRound(rt, r, 7.0f, hover ? C_PANEL_HI : base);
}

static void StartAnim(App& a) {
    SetTimer(a.hwnd, 2, 16, nullptr);
}
static void PressAnim(App& a, int kind) {
    a.pressKind = kind;
    a.pressAnim = 1.0f;
    StartAnim(a);
}
static D2D1_RECT_F ScaledRect(const D2D1_RECT_F& r, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float f = 0.94f + 0.06f * t;
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    float hw = (r.right - r.left) * 0.5f * f, hh = (r.bottom - r.top) * 0.5f * f;
    return R(cx - hw, cy - hh, cx + hw, cy + hh);
}
static D2D1_COLOR_F DimColor(float t) {
    D2D1_COLOR_F c = C_DIM;
    c.a *= (t < 0 ? 0 : (t > 1 ? 1 : t));
    return c;
}
static D2D1_RECT_F PressedRect(App& a, int kind, const D2D1_RECT_F& r, bool& pressed) {
    pressed = (a.pressKind == kind && a.pressAnim > 0.001f);
    if (!pressed) return r;
    float inset = 1.6f * a.pressAnim;
    return R(r.left + inset, r.top + inset, r.right - inset, r.bottom - inset);
}
static void DrawToolbar(App& a, const Layout& L) {
    auto* rt = a.rt.Get();

    auto drawDropdownButton = [&](const D2D1_RECT_F& r, const wchar_t* label, bool open, int kind) {
        bool hov = InRect(r, a.mouseX, a.mouseY);
        bool pressed = false;
        D2D1_RECT_F rr = PressedRect(a, kind, r, pressed);
        ButtonBg(rt, rr, hov || open || pressed, C_BG);
        Txt(rt, a.fSmall.Get(), label, R(r.left + 10, r.top, r.right - 20, r.bottom), C_TEXT);
        Chevron(rt, r.right - 11, (r.top + r.bottom) / 2, 4.0f, C_MUTED);
    };
    drawDropdownButton(L.sort, SORT_SHORT[a.sortMode], a.sortOpen, 2);
    drawDropdownButton(L.filter, FILTER_LABELS[a.filterMode], a.filterOpen, 3);

    // add button
    {
        bool hov = InRect(L.add, a.mouseX, a.mouseY);
        bool pressed = false;
        D2D1_RECT_F ar = PressedRect(a, 1, L.add, pressed);
        FillRound(rt, ar, 7.0f, pressed ? C_ACCENT_HI : (hov ? C_ACCENT_HI : C_ACCENT));
        Txt(rt, a.fBold.Get(), L"+  Add download", L.add, C_WHITE, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // search
    {
        bool focused = (a.focus == 1);
        FillRound(rt, L.search, 7.0f, C_PANEL);
        if (focused) StrokeRound(rt, L.search, 7.0f, C_ACCENT, 1.2f);
        Magnifier(rt, L.search.left + 15, (L.search.top + L.search.bottom) / 2, 5.0f, C_MUTED);
        std::wstring shown = a.search.empty() ? L"Search in downloads..." : a.search;
        D2D1_COLOR_F col = a.search.empty() ? C_MUTED : C_TEXT;
        Txt(rt, a.fSmall.Get(), shown, R(L.search.left + 28, L.search.top, L.search.right - 8, L.search.bottom), col);
    }

    // right menu button
    {
        bool hov = InRect(L.gear, a.mouseX, a.mouseY);
        bool pressed = false;
        D2D1_RECT_F gr = PressedRect(a, 5, L.gear, pressed);
        ButtonBg(rt, gr, hov || a.menuOpen || pressed, C_BG);
        float cx = (L.gear.left + L.gear.right) / 2, cy = (L.gear.top + L.gear.bottom) / 2;
        for (int i = -1; i <= 1; i++) {
            Dot(rt, cx - 7, cy + i * 5.0f, 1.6f, C_MUTED);
            Line(rt, cx - 2, cy + i * 5.0f, cx + 8, cy + i * 5.0f, C_MUTED, 1.6f);
        }
    }
}

static void DrawDropArea(App& a, const Layout& L) {
    auto* rt = a.rt.Get();
    D2D1_RECT_F d = R(PAD + 2, L.content.top + PAD, L.content.right - PAD - 2, L.content.bottom - PAD);
    bool hov = InRect(d, a.mouseX, a.mouseY);
    FillRound(rt, d, 12.0f, hov ? C_HOVER : C_BG);
    StrokeRound(rt, d, 12.0f, hov ? C_ACCENT : C_BORDER, 1.4f, a.dash.Get());

    std::wstring label = L"Drag-and-drop URL or torrent file";
    float tw = MeasureText(a, a.fBody.Get(), label);
    float cx = (d.left + d.right) / 2, cy = (d.top + d.bottom) / 2;
    LinkIcon(rt, cx - tw / 2 - 22, cy, C_MUTED);
    Txt(rt, a.fBody.Get(), label, R(cx - tw / 2, cy - 14, cx + tw / 2, cy + 14), C_MUTED);

    Txt(rt, a.fSmall.Get(), L"or click  +  Add download  to paste a link",
        R(d.left, cy + 20, d.right, cy + 40), C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
}

static void DrawDropdown(App& a, const Layout& L, bool sort) {
    auto* rt = a.rt.Get();
    D2D1_RECT_F d = sort ? SortDropRect(L) : FilterDropRect(L);
    FillRound(rt, d, 8.0f, C_PANEL_HI);
    StrokeRound(rt, d, 8.0f, C_BORDER, 1.0f);

    const wchar_t** labels = sort ? SORT_LABELS : FILTER_LABELS;
    int n = sort ? 8 : 6;
    int current = sort ? a.sortMode : a.filterMode;
    int sepAfter = sort ? -1 : 2;

    float yy = d.top + 4;
    for (int i = 0; i < n; i++) {
        if (sepAfter >= 0 && i == sepAfter + 1) {
            Line(rt, d.left + 12, yy + 4, d.right - 12, yy + 4, C_BORDER, 1.0f);
            yy += 9;
        }
        D2D1_RECT_F row = R(d.left + 4, yy, d.right - 4, yy + 28);
        bool hov = InRect(row, a.mouseX, a.mouseY);
        if (hov) FillRound(rt, row, 6.0f, C_HOVER);
        if (i == current) Dot(rt, row.left + 9, (row.top + row.bottom) / 2, 2.5f, C_ACCENT);
        Txt(rt, a.fSmall.Get(), labels[i], R(row.left + 20, row.top, row.right - 8, row.bottom),
            i == current ? C_TEXT : C_MUTED);
        yy += 28;
    }
}

static void DrawAppMenu(App& a, float W) {
    auto* rt = a.rt.Get();
    float t = a.openAnim;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    D2D1_RECT_F r = MenuRect(W);
    float slide = (1.0f - t) * -10.0f;
    r.top += slide;
    r.bottom += slide;
    FillRound(rt, r, 8.0f, C_PANEL_HI);
    StrokeRound(rt, r, 8.0f, C_BORDER, 1.0f);

    float yy = r.top + 4;
    for (int i = 0; i < APP_MENU_N; i++) {
        const MenuItemDef& m = APP_MENU[i];
        if (m.sep) {
            Line(rt, r.left + 12, yy + 4, r.right - 12, yy + 4, C_BORDER, 1.0f);
            yy += 9;
            continue;
        }
        D2D1_RECT_F row = R(r.left + 4, yy, r.right - 4, yy + 28);
        bool hov = InRect(row, a.mouseX, a.mouseY);
        if (hov && !m.disabled) FillRound(rt, row, 6.0f, C_HOVER);
        D2D1_COLOR_F col = m.disabled ? C_MUTED : C_TEXT;
        Txt(rt, a.fSmall.Get(), m.label, R(row.left + 14, row.top, row.right - 26, row.bottom), col);
        if (m.arrow) ChevronRight(rt, row.right - 14, (row.top + row.bottom) / 2, C_MUTED);
        yy += 28;
    }
}

static bool MatchesFilter(const DownloadItem& it, int f) {
    int s = it.state.load();
    switch (f) {
    case 1: return s == 0 || s == 1;
    case 2: return s == 2;
    case 3: return it.category == L"Torrent";
    case 4: return it.category == L"Video";
    case 5: return it.category == L"Music";
    default: return true;
    }
}

static std::vector<std::shared_ptr<DownloadItem>> BuildView(App& a) {
    std::vector<std::shared_ptr<DownloadItem>> v;
    std::wstring q = Lower(a.search);
    for (auto& it : a.items) {
        if (!MatchesFilter(*it, a.filterMode)) continue;
        if (!q.empty()) {
            if (Lower(it->filename).find(q) == std::wstring::npos &&
                Lower(it->url).find(q) == std::wstring::npos) continue;
        }
        v.push_back(it);
    }
    std::stable_sort(v.begin(), v.end(), [&](const std::shared_ptr<DownloadItem>& x,
                                             const std::shared_ptr<DownloadItem>& y) {
        switch (a.sortMode) {
        case 0: return x->seq > y->seq;
        case 1: return x->seq < y->seq;
        case 2: return Lower(x->filename) < Lower(y->filename);
        case 3: return Lower(x->filename) > Lower(y->filename);
        case 4: return x->total.load() > y->total.load();
        case 5: return x->total.load() < y->total.load();
        case 6: return x->state.load() < y->state.load();
        case 7: return x->state.load() > y->state.load();
        default: return x->seq > y->seq;
        }
    });
    return v;
}

static D2D1_RECT_F CardBinRect(const D2D1_RECT_F& card) {
    return R(card.right - 44, card.top + 26, card.right - 12, card.top + 58);
}
static D2D1_RECT_F CardPauseRect(const D2D1_RECT_F& card) {
    return R(card.right - 80, card.top + 26, card.right - 48, card.top + 58);
}

static void DrawTrash(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F col) {
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    float w = 7.0f, top = cy - 5.0f;
    Line(rt, cx - w - 1, top, cx + w + 1, top, col, 1.6f);
    Line(rt, cx - 2.5f, top - 3, cx + 2.5f, top - 3, col, 1.5f);
    Line(rt, cx - 2.5f, top - 3, cx - 2.5f, top, col, 1.4f);
    Line(rt, cx + 2.5f, top - 3, cx + 2.5f, top, col, 1.4f);
    Line(rt, cx - w, top + 3, cx - w + 1.5f, top + 12, col, 1.6f);
    Line(rt, cx + w, top + 3, cx + w - 1.5f, top + 12, col, 1.6f);
    Line(rt, cx - w + 1.5f, top + 12, cx + w - 1.5f, top + 12, col, 1.6f);
    Line(rt, cx - 2.5f, top + 5, cx - 2.5f, top + 9.5f, col, 1.2f);
    Line(rt, cx + 2.5f, top + 5, cx + 2.5f, top + 9.5f, col, 1.2f);
}

static std::shared_ptr<DownloadItem> ItemAt(App& a, const Layout& L, float W, float x, float y,
                                            D2D1_RECT_F* cardOut = nullptr) {
    if (!InRect(L.content, x, y)) return nullptr;
    auto view = BuildView(a);
    float top = L.content.top + GAP - a.scrollY;
    for (auto& it : view) {
        D2D1_RECT_F card = R(PAD, top, W - PAD, top + CARD_H);
        if (InRect(card, x, y)) {
            if (cardOut) *cardOut = card;
            return it;
        }
        top += CARD_H + GAP;
    }
    return nullptr;
}

static void DrawCard(App& a, const Layout& L, const std::shared_ptr<DownloadItem>& it, float top, float W) {
    auto* rt = a.rt.Get();
    D2D1_RECT_F card = R(PAD, top, W - PAD, top + CARD_H);
    bool hov = InRect(card, a.mouseX, a.mouseY);
    FillRound(rt, card, 10.0f, hov ? C_PANEL_HI : C_PANEL);

    int st = it->state.load();
    uint64_t d = it->downloaded.load();
    uint64_t tot = it->total.load();

    D2D1_RECT_F icon = R(card.left + 12, card.top + 12, card.left + 52, card.top + 52);
    FillRound(rt, icon, 9.0f, CategoryColor(it->category));
    std::wstring extUp = Lower(it->ext);
    for (auto& c : extUp) c = (wchar_t)towupper(c);
    if (extUp.empty()) extUp = L"FILE";
    if (extUp.size() > 4) extUp = extUp.substr(0, 4);
    Txt(rt, a.fSmall.Get(), extUp, icon, C(1, 1, 1, 0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);

    float textL = icon.right + 12;
    Txt(rt, a.fBold.Get(), it->filename, R(textL, card.top + 8, card.right - 96, card.top + 27), C_TEXT);
    Txt(rt, a.fTiny.Get(), it->category, R(card.right - 128, card.top + 9, card.right - 50, card.top + 26),
        C_MUTED, DWRITE_TEXT_ALIGNMENT_TRAILING);
    Txt(rt, a.fTiny.Get(), it->url, R(textL, card.top + 27, card.right - 50, card.top + 42), C_MUTED);

    // progress
    D2D1_RECT_F bar = R(textL, card.top + 46, card.right - 50, card.top + 53);
    float frac = 0;
    if (st == 2) frac = 1;
    else if (tot > 0) frac = Clamp01((float)((double)d / (double)tot));
    FillRound(rt, bar, 3.5f, C_TRACK);
    if (frac > 0) {
        D2D1_RECT_F fill = bar;
        fill.right = bar.left + (bar.right - bar.left) * frac;
        if (fill.right - fill.left < 7) fill.right = fill.left + 7;
        D2D1_COLOR_F fc = (st == 2) ? C_GREEN : (st == 3 ? C_RED : C_ACCENT);
        FillRound(rt, fill, 3.5f, fc);
    }

    // meta
    std::wstring left;
    if (tot > 0) left = FmtBytes(d) + L" of " + FmtBytes(tot);
    else left = FmtBytes(d);
    if (st == 1 && it->speed > 1) left += L"   \x2022   " + FmtSpeed(it->speed);
    Txt(rt, a.fTiny.Get(), left, R(textL, card.top + 55, card.right - 120, card.top + 75), C_MUTED);

    std::wstring right; D2D1_COLOR_F rc = C_MUTED;
    if (st == 2) { right = L"Completed"; rc = C_GREEN; }
    else if (st == 3) { right = L"Failed"; rc = C_RED; }
    else if (st == 0) { right = L"Starting..."; rc = C_AMBER; }
    else if (st == 4) { right = L"Paused"; rc = C_AMBER; }
    else {
        wchar_t b[32];
        if (tot > 0) swprintf(b, 32, L"%.0f%%", frac * 100.0f);
        else swprintf(b, 32, L"%s", L"Downloading");
        right = b;
        rc = C_ACCENT;
    }
    Txt(rt, a.fTiny.Get(), right, R(card.right - 176, card.top + 55, card.right - 50, card.top + 75),
        rc, DWRITE_TEXT_ALIGNMENT_TRAILING);

    // pause/play button (only for active/paused items)
    int st2 = it->state.load();
    if (st2 == 1 || st2 == 4 || st2 == 0) {
        D2D1_RECT_F pr = CardPauseRect(card);
        bool prHov = InRect(pr, a.mouseX, a.mouseY);
        if (prHov) FillRound(rt, pr, 7.0f, C_HOVER);
        float cx = (pr.left + pr.right) / 2, cy = (pr.top + pr.bottom) / 2;
        if (st2 == 4) {
            // play triangle
            ComPtr<ID2D1PathGeometry> geo;
            a.d2d->CreatePathGeometry(&geo);
            ComPtr<ID2D1GeometrySink> sink;
            geo->Open(&sink);
            sink->BeginFigure(D2D1::Point2F(cx - 5, cy - 6), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx - 5, cy + 6));
            sink->AddLine(D2D1::Point2F(cx + 6, cy));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            ComPtr<ID2D1SolidColorBrush> br;
            rt->CreateSolidColorBrush(C_ACCENT, &br);
            rt->FillGeometry(geo.Get(), br.Get());
        } else {
            // pause bars
            FillRect2(rt, R(cx - 5, cy - 6, cx - 1.5f, cy + 6), C_MUTED);
            FillRect2(rt, R(cx + 1.5f, cy - 6, cx + 5, cy + 6), C_MUTED);
        }
    }

    // delete button
    D2D1_RECT_F bin = CardBinRect(card);
    bool binHov = InRect(bin, a.mouseX, a.mouseY);
    if (binHov) FillRound(rt, bin, 7.0f, C(0.86f, 0.24f, 0.26f, 0.16f));
    DrawTrash(rt, bin, binHov ? C_RED : C_MUTED);
}

static void DrawX(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F col) {
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    Line(rt, cx - 6, cy - 6, cx + 6, cy + 6, col, 1.6f);
    Line(rt, cx + 6, cy - 6, cx - 6, cy + 6, col, 1.6f);
}

static void DrawAddDialog(App& a, float W, float H) {
    auto* rt = a.rt.Get();
    float t = a.openAnim;
    FillRect2(rt, R(0, 0, W, H), DimColor(t));

    D2D1_RECT_F d = ScaledRect(AddDialogRect(W, H), t);
    FillRound(rt, d, 12.0f, C_PANEL);
    StrokeRound(rt, d, 12.0f, C_BORDER, 1.0f);

    Txt(rt, a.fTitle.Get(), L"Add download", R(d.left + 24, d.top + 16, d.right - 48, d.top + 44), C_TEXT);
    DrawX(rt, AddCloseRect(d), C_MUTED);
    Txt(rt, a.fSmall.Get(), L"Enter URL or choose torrent file",
        R(d.left + 24, d.top + 50, d.right - 24, d.top + 70), C_MUTED);

    D2D1_RECT_F in = AddInputRect(d);
    FillRound(rt, in, 7.0f, C_PANEL_HI);
    if (a.focus == 2) StrokeRound(rt, in, 7.0f, C_ACCENT, 1.4f);
    else StrokeRound(rt, in, 7.0f, C_BORDER, 1.0f);
    if (a.urlInput.empty())
        Txt(rt, a.fBody.Get(), L"https://example.com/file.zip",
            R(in.left + 12, in.top, in.right - 12, in.bottom), C_MUTED);
    else
        Txt(rt, a.fBody.Get(), a.urlInput, R(in.left + 12, in.top, in.right - 12, in.bottom), C_TEXT);

    D2D1_RECT_F browse = AddBrowseRect(d);
    bool brHov = InRect(browse, a.mouseX, a.mouseY);
    FillRound(rt, browse, 7.0f, brHov ? C_HOVER : C_PANEL_HI);
    StrokeRound(rt, browse, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fSmall.Get(), L"Browse...", browse, C_TEXT, DWRITE_TEXT_ALIGNMENT_CENTER);

    D2D1_RECT_F ok = AddOkRect(d), cancel = AddCancelRect(d);
    bool enabled = !TrimW(a.urlInput).empty();
    bool okHov = InRect(ok, a.mouseX, a.mouseY) && enabled;
    FillRound(rt, ok, 7.0f, !enabled ? C_PANEL_HI : (okHov ? C_ACCENT_HI : C_ACCENT));
    Txt(rt, a.fSmall.Get(), L"OK", ok, enabled ? C_WHITE : C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);

    bool canHov = InRect(cancel, a.mouseX, a.mouseY);
    FillRound(rt, cancel, 7.0f, canHov ? C_HOVER : C_BG);
    StrokeRound(rt, cancel, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fSmall.Get(), L"Cancel", cancel, C_TEXT, DWRITE_TEXT_ALIGNMENT_CENTER);
}

static void DrawNewDialog(App& a, float W, float H) {
    auto* rt = a.rt.Get();
    float t = a.openAnim;
    FillRect2(rt, R(0, 0, W, H), DimColor(t));

    D2D1_RECT_F d = ScaledRect(NewDialogRect(W, H), t);
    FillRound(rt, d, 12.0f, C_PANEL);
    StrokeRound(rt, d, 12.0f, C_BORDER, 1.0f);

    Txt(rt, a.fTitle.Get(), L"New download", R(d.left + 24, d.top + 16, d.right - 48, d.top + 44), C_TEXT);
    DrawX(rt, NewCloseRect(d), C_MUTED);

    Txt(rt, a.fSmall.Get(), L"Save to", R(d.left + 24, d.top + 56, d.right - 24, d.top + 74), C_MUTED);
    D2D1_RECT_F folder = NewFolderRect(d);
    FillRound(rt, folder, 7.0f, C_PANEL_HI);
    StrokeRound(rt, folder, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fBody.Get(), a.newFolder, R(folder.left + 12, folder.top, folder.right - 28, folder.bottom), C_TEXT);
    Chevron(rt, folder.right - 14, (folder.top + folder.bottom) / 2, 4.0f, C_MUTED);

    D2D1_RECT_F fbtn = NewFolderBtn(d);
    bool fbHov = InRect(fbtn, a.mouseX, a.mouseY);
    FillRound(rt, fbtn, 7.0f, fbHov ? C_HOVER : C_PANEL_HI);
    StrokeRound(rt, fbtn, 7.0f, C_BORDER, 1.0f);
    FolderIcon(rt, (fbtn.left + fbtn.right) / 2, (fbtn.top + fbtn.bottom) / 2, C_TEXT);

    Txt(rt, a.fSmall.Get(), L"File name", R(d.left + 24, d.top + 122, d.right - 24, d.top + 140), C_MUTED);
    D2D1_RECT_F name = NewNameRect(d);
    FillRound(rt, name, 7.0f, C_PANEL_HI);
    if (a.focus == 3) StrokeRound(rt, name, 7.0f, C_ACCENT, 1.4f);
    else StrokeRound(rt, name, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fBody.Get(), a.newName, R(name.left + 12, name.top, name.right - 12, name.bottom), C_TEXT);

    D2D1_RECT_F sched = NewSchedBox(d);
    CheckBox(rt, sched, a.newScheduler);
    Txt(rt, a.fSmall.Get(), L"Scheduler", R(sched.right + 8, sched.top - 2, d.right - 24, sched.bottom + 2), C_TEXT);

    std::wstring sizeText;
    if (a.probe && a.probe->done.load()) {
        uint64_t s = a.probe->size.load();
        if (s > 0)
            sizeText = L"Size: " + FmtBytes(s) + L"   (disk space: " + DiskFreeText(a.newFolder) + L")";
        else
            sizeText = L"Size: unknown   (disk space: " + DiskFreeText(a.newFolder) + L")";
    } else {
        sizeText = L"Size: fetching...   (disk space: " + DiskFreeText(a.newFolder) + L")";
    }
    Txt(rt, a.fTiny.Get(), sizeText, R(d.left + 24, d.top + 222, d.right - 24, d.top + 240), C_MUTED);

    D2D1_RECT_F ok = NewOkRect(d), later = NewLaterRect(d), cancel = NewCancelRect(d);
    bool canStart = !TrimW(a.newName).empty();
    bool okHov = InRect(ok, a.mouseX, a.mouseY);
    FillRound(rt, ok, 7.0f, canStart ? (okHov ? C_ACCENT_HI : C_ACCENT) : C_PANEL_HI);
    Txt(rt, a.fSmall.Get(), L"DOWNLOAD", ok, canStart ? C_WHITE : C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);

    bool laterHov = InRect(later, a.mouseX, a.mouseY);
    FillRound(rt, later, 7.0f, laterHov ? C_HOVER : C_PANEL_HI);
    StrokeRound(rt, later, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fSmall.Get(), L"Download later", later, C_TEXT, DWRITE_TEXT_ALIGNMENT_CENTER);

    bool canHov = InRect(cancel, a.mouseX, a.mouseY);
    FillRound(rt, cancel, 7.0f, canHov ? C_HOVER : C_BG);
    StrokeRound(rt, cancel, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fSmall.Get(), L"Cancel", cancel, C_TEXT, DWRITE_TEXT_ALIGNMENT_CENTER);
}

static void DrawAbout(App& a, float W, float H) {
    auto* rt = a.rt.Get();
    float t = a.openAnim;
    FillRect2(rt, R(0, 0, W, H), DimColor(t));
    D2D1_RECT_F d = ScaledRect(AboutRect(W, H), t);
    FillRound(rt, d, 12.0f, C_PANEL);
    StrokeRound(rt, d, 12.0f, C_BORDER, 1.0f);

    float cx = (d.left + d.right) / 2;
    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(C_ACCENT, &br);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, d.top + 50), 22, 22), br.Get());
    DownArrow(rt, cx, d.top + 50, 8.0f, C_WHITE, 2.0f);

    Txt(rt, a.fTitle.Get(), L"Open Download Manager",
        R(d.left, d.top + 84, d.right, d.top + 110), C_TEXT, DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(rt, a.fSmall.Get(), L"Version 0.2.4",
        R(d.left, d.top + 112, d.right, d.top + 132), C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(rt, a.fTiny.Get(), L"A free, open-source download manager.",
        R(d.left, d.top + 146, d.right, d.top + 164), C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(rt, a.fTiny.Get(), L"Built with Win32, Direct2D and WinHTTP.",
        R(d.left, d.top + 164, d.right, d.top + 182), C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);

    D2D1_RECT_F ok = AboutOkRect(d);
    bool hov = InRect(ok, a.mouseX, a.mouseY);
    FillRound(rt, ok, 7.0f, hov ? C_ACCENT_HI : C_ACCENT);
    Txt(rt, a.fSmall.Get(), L"OK", ok, C_WHITE, DWRITE_TEXT_ALIGNMENT_CENTER);
}

static void DrawStatus(App& a, const Layout& L, const std::vector<std::shared_ptr<DownloadItem>>& view) {
    auto* rt = a.rt.Get();
    FillRect2(rt, L.status, C_PANEL);
    Line(rt, 0, L.status.top, L.status.right, L.status.top, C_BORDER, 1.0f);

    float cy = (L.status.top + L.status.bottom) / 2;

    // on/off pill
    D2D1_RECT_F pill = R(8, cy - 11, 66, cy + 11);
    bool pillPressed = false;
    D2D1_RECT_F pr = PressedRect(a, 20, pill, pillPressed);
    FillRound(rt, pr, 11.0f, C_PANEL_HI);
    Txt(rt, a.fSmall.Get(), a.on ? L"on" : L"off",
        R(pill.left + 12, pill.top, pill.left + 38, pill.bottom),
        a.on ? C_TEXT : C_MUTED);
    D2D1_RECT_F track = R(pill.right - 24, cy - 6, pill.right - 6, cy + 6);
    FillRound(rt, track, 6.0f, a.on ? C_GREEN : C_BORDER);
    Dot(rt, a.on ? track.right - 6 : track.left + 6, cy, 4.5f, C_WHITE);

    double total = 0;
    int active = 0;
    for (auto& it : a.items) { total += it->speed; if (it->state.load() == 1) active++; }

    float x = 76;
    std::wstring dl = std::wstring(L"\x2193 ") + FmtSpeed(total);
    Txt(rt, a.fSmall.Get(), dl, R(x, L.status.top, x + 150, L.status.bottom), C_MUTED);

    x = 168;
    std::wstring up = std::wstring(L"\x2191 ") + FmtSpeed(0);
    Txt(rt, a.fSmall.Get(), up, R(x, L.status.top, x + 130, L.status.bottom), C_MUTED);

    // traffic mode
    x = 262;
    D2D1_RECT_F trArea = R(x - 6, cy - 12, x + 84, cy + 12);
    bool trPressed = false;
    D2D1_RECT_F trRect = PressedRect(a, 21, trArea, trPressed);
    if (InRect(trArea, a.mouseX, a.mouseY) || trPressed)
        FillRound(rt, trRect, 6.0f, C_PANEL_HI);
    D2D1_COLOR_F tc = a.traffic == 0 ? C_MUTED : (a.traffic == 1 ? C_AMBER : C_GREEN);
    for (int i = 0; i < 3; i++) {
        float bh = 4 + i * 3.2f;
        FillRect2(rt, R(x + i * 4, cy + 5 - bh, x + i * 4 + 2.6f, cy + 5), tc);
    }
    Txt(rt, a.fSmall.Get(), a.traffic == 0 ? L"LOW" : (a.traffic == 1 ? L"MEDIUM" : L"HIGH"),
        R(x + 18, L.status.top, x + 84, L.status.bottom), tc);

    // support button
    x = 348;
    D2D1_RECT_F sup = R(x, cy - 12, x + 154, cy + 12);
    bool supHov = InRect(sup, a.mouseX, a.mouseY);
    bool supPressed = false;
    D2D1_RECT_F sr = PressedRect(a, 22, sup, supPressed);
    FillRound(rt, sr, 6.0f, sr.top != sup.top
             ? C(0.204f, 0.780f, 0.349f, 0.20f)
             : (supHov ? C(0.204f, 0.780f, 0.349f, 0.12f) : C_BG));
    StrokeRound(rt, sr, 6.0f, C_GREEN, 1.0f);
    HeartIcon(rt, sup.left + 16, cy - 1, C_GREEN);
    Txt(rt, a.fSmall.Get(), L"Support the project",
        R(sup.left + 28, sup.top, sup.right - 6, sup.bottom), C_GREEN);

    Txt(rt, a.fSmall.Get(), L"Open Download Manager",
        R(L.status.right - 240 - PAD, L.status.top, L.status.right - PAD, L.status.bottom),
        C_MUTED, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

static int PrefDropCount(int which) {
    return which == 1 ? 3 : (which == 2 ? 2 : 7);
}
static D2D1_RECT_F PrefDropRect(const App& a) {
    D2D1_RECT_F btn = a.prefDropdown == 1 ? PrefThemeBtn()
                    : (a.prefDropdown == 2 ? PrefUiBtn() : PrefLangBtn());
    int n = PrefDropCount(a.prefDropdown);
    return R(btn.left, btn.bottom + 4, btn.right, btn.bottom + 4 + n * 28 + 8);
}
static int HitPrefDrop(const App& a, float x, float y) {
    if (!a.prefDropdown) return -1;
    D2D1_RECT_F d = PrefDropRect(a);
    if (!InRect(d, x, y)) return -1;
    int n = PrefDropCount(a.prefDropdown);
    int i = (int)((y - (d.top + 4)) / 28);
    return (i >= 0 && i < n) ? i : -1;
}

static void DrawPrefDropdown(App& a) {
    if (!a.prefDropdown) return;
    auto* rt = a.rt.Get();
    D2D1_RECT_F d = PrefDropRect(a);
    FillRound(rt, d, 8.0f, C_PANEL_HI);
    StrokeRound(rt, d, 8.0f, C_BORDER, 1.0f);

    const wchar_t** labels = a.prefDropdown == 1 ? THEME_LABELS
                            : (a.prefDropdown == 2 ? UISTYLE_LABELS : LANGUAGE_LABELS);
    int n = PrefDropCount(a.prefDropdown);
    int cur = a.prefDropdown == 1 ? a.themeMode : (a.prefDropdown == 2 ? a.uiStyle : a.language);
    for (int i = 0; i < n; i++) {
        D2D1_RECT_F row = R(d.left + 4, d.top + 4 + i * 28.0f, d.right - 4, d.top + 4 + (i + 1) * 28.0f);
        bool hov = InRect(row, a.mouseX, a.mouseY);
        if (hov) FillRound(rt, row, 6.0f, C_HOVER);
        if (i == cur) Dot(rt, row.left + 9, (row.top + row.bottom) / 2, 2.5f, C_ACCENT);
        Txt(rt, a.fSmall.Get(), labels[i], R(row.left + 20, row.top, row.right - 8, row.bottom),
            i == cur ? C_TEXT : C_MUTED);
    }
}

static void DrawPreferences(App& a, float W, float H) {
    auto* rt = a.rt.Get();
    Layout L = MakeLayout(W, H);

    // top bar
    D2D1_RECT_F back = PrefBackRect();
    bool backPressed = false;
    D2D1_RECT_F br = PressedRect(a, 30, back, backPressed);
    ButtonBg(rt, br, InRect(back, a.mouseX, a.mouseY) || backPressed, C_BG);
    float bcx = (back.left + back.right) / 2, bcy = (back.top + back.bottom) / 2;
    Line(rt, bcx + 5, bcy - 6, bcx - 2, bcy, C_TEXT, 1.8f);
    Line(rt, bcx - 2, bcy, bcx + 5, bcy + 6, C_TEXT, 1.8f);

    D2D1_RECT_F gm = PrefMenuRect(W);
    bool gmPressed = false;
    D2D1_RECT_F gmr = PressedRect(a, 31, gm, gmPressed);
    ButtonBg(rt, gmr, InRect(gm, a.mouseX, a.mouseY) || a.menuOpen || gmPressed, C_BG);
    float gcx = (gm.left + gm.right) / 2, gcy = (gm.top + gm.bottom) / 2;
    for (int i = -1; i <= 1; i++) {
        Dot(rt, gcx - 7, gcy + i * 5.0f, 1.6f, C_MUTED);
        Line(rt, gcx - 2, gcy + i * 5.0f, gcx + 8, gcy + i * 5.0f, C_MUTED, 1.6f);
    }

    // sidebar
    for (int i = 0; i < 8; i++) {
        D2D1_RECT_F it = PrefSideItem(i);
        bool sel = (i == a.prefSection);
        bool hov = InRect(it, a.mouseX, a.mouseY);
        if (sel) FillRound(rt, it, 7.0f, C_PANEL_HI);
        else if (hov) FillRound(rt, it, 7.0f, C_HOVER);
        Txt(rt, a.fSmall.Get(), PREF_SECTIONS[i], R(it.left + 12, it.top, it.right - 8, it.bottom),
            sel ? C_TEXT : C_MUTED);
    }

    // heading
    Txt(rt, a.fHead.Get(), PREF_SECTIONS[a.prefSection],
        R(PREF_X, TOP_H + 14, W - 30, TOP_H + 48), C_TEXT);

    if (a.prefSection != 0) {
        const wchar_t* desc[] = {
            L"", L"Browser integration and monitoring options.",
            L"Proxy and connection settings.", L"Download and upload speed limits.",
            L"Antivirus scanning options.", L"BitTorrent port and DHT settings.",
            L"Remote access credentials.", L"Advanced connection options."
        };
        Txt(rt, a.fBody.Get(), desc[a.prefSection],
            R(PREF_X, TOP_H + 70, W - 30, TOP_H + 94), C_MUTED);
        D2D1_RECT_F soon = R(PREF_X, TOP_H + 104, PREF_X + 110, TOP_H + 130);
        FillRound(rt, soon, 13.0f, C(0.204f, 0.780f, 0.349f, 0.14f));
        StrokeRound(rt, soon, 13.0f, C_GREEN, 1.0f);
        Txt(rt, a.fTiny.Get(), L"Coming soon", soon, C_GREEN, DWRITE_TEXT_ALIGNMENT_CENTER);
        Txt(rt, a.fTiny.Get(), L"We're still working on this section.",
            R(PREF_X, TOP_H + 140, W - 30, TOP_H + 160), C_MUTED);
        DrawStatus(a, L, {});
        return;
    }

    Txt(rt, a.fSmall.Get(), L"Theme", R(PREF_X, TOP_H + 58, PREF_X + 140, TOP_H + 88), C_TEXT);
    D2D1_RECT_F theme = PrefThemeBtn();
    bool themePressed = false;
    D2D1_RECT_F thr = PressedRect(a, 32, theme, themePressed);
    FillRound(rt, thr, 7.0f, themePressed ? C_HOVER : C_PANEL_HI);
    StrokeRound(rt, thr, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fSmall.Get(), THEME_LABELS[a.themeMode],
        R(theme.left + 12, theme.top, theme.right - 24, theme.bottom), C_TEXT);
    Chevron(rt, theme.right - 14, (theme.top + theme.bottom) / 2, 4.0f, C_MUTED);
    Txt(rt, a.fTiny.Get(), L"A light theme will be used if the system theme is unknown.",
        R(PREF_X, TOP_H + 94, W - 30, TOP_H + 112), C_MUTED);

    Txt(rt, a.fSmall.Get(), L"UI style", R(PREF_X, TOP_H + 122, PREF_X + 140, TOP_H + 152), C_TEXT);
    D2D1_RECT_F ui = PrefUiBtn();
    bool uiPressed = false;
    D2D1_RECT_F uir = PressedRect(a, 33, ui, uiPressed);
    FillRound(rt, uir, 7.0f, uiPressed ? C_HOVER : C_PANEL_HI);
    StrokeRound(rt, uir, 7.0f, C_BORDER, 1.0f);
    Txt(rt, a.fSmall.Get(), UISTYLE_LABELS[a.uiStyle],
        R(ui.left + 12, ui.top, ui.right - 24, ui.bottom), C_TEXT);
    Chevron(rt, ui.right - 14, (ui.top + ui.bottom) / 2, 4.0f, C_MUTED);

    Txt(rt, a.fSmall.Get(), L"Language", R(PREF_X, TOP_H + 170, PREF_X + 140, TOP_H + 200), C_TEXT);
    D2D1_RECT_F lang = PrefLangBtn();
    bool langPressed = false;
    D2D1_RECT_F lr = PressedRect(a, 34, lang, langPressed);
    FillRound(rt, lr, 7.0f, langPressed ? C_HOVER : C_PANEL_HI);
    StrokeRound(rt, lr, 7.0f, C_BORDER, 1.0f);
    D2D1_RECT_F badge = R(lang.left + 10, lang.top + 7, lang.left + 30, lang.top + 21);
    FillRound(rt, badge, 3.0f, C_ACCENT);
    Txt(rt, a.fTiny.Get(), LANGUAGE_CODES[a.language], badge, C_WHITE, DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(rt, a.fSmall.Get(), LANGUAGE_LABELS[a.language],
        R(lang.left + 38, lang.top, lang.right - 24, lang.bottom), C_TEXT);
    Chevron(rt, lang.right - 14, (lang.top + lang.bottom) / 2, 4.0f, C_MUTED);

    std::wstring langHint = L"Your language is not listed or the translation isn't complete?";
    float lw = MeasureText(a, a.fTiny.Get(), langHint);
    Txt(rt, a.fTiny.Get(), langHint, R(PREF_X, TOP_H + 206, W - 30, TOP_H + 224), C_MUTED);
    Txt(rt, a.fTiny.Get(), L"Let's fix it!",
        R(PREF_X + lw + 6, TOP_H + 206, W - 30, TOP_H + 224), C_ACCENT);

    Txt(rt, a.fBold.Get(), L"Options", R(PREF_X, TOP_H + 240, W - 30, TOP_H + 264), C_TEXT);

    D2D1_RECT_F lb = PrefLaunchBox();
    CheckBox(rt, lb, a.launchStartup);
    Txt(rt, a.fSmall.Get(), L"Launch at startup (minimized)",
        R(lb.right + 10, lb.top - 2, W - 30, lb.bottom + 2), C_TEXT);

    Txt(rt, a.fSmall.Get(), L"Default download folder",
        R(PREF_X, TOP_H + 302, W - 30, TOP_H + 322), C_MUTED);

    D2D1_RECT_F r1 = PrefAutoRadio();
    Radio(rt, r1, a.folderMode == 0);
    Txt(rt, a.fSmall.Get(), L"Choose default download folder automatically",
        R(r1.right + 10, r1.top - 2, W - 30, r1.bottom + 2), C_TEXT);

    D2D1_RECT_F c1 = PrefTypeBox();
    CheckBox(rt, c1, a.suggestType, false);
    Txt(rt, a.fSmall.Get(), L"Suggest folders based on file type",
        R(c1.right + 10, c1.top - 2, W - 30, c1.bottom + 2), C_MUTED);

    D2D1_RECT_F c2 = PrefUrlBox();
    CheckBox(rt, c2, a.suggestUrl, false);
    Txt(rt, a.fSmall.Get(), L"Suggest folders based on download URL",
        R(c2.right + 10, c2.top - 2, W - 30, c2.bottom + 2), C_MUTED);

    D2D1_RECT_F r2 = PrefFixedRadio();
    Radio(rt, r2, a.folderMode == 1);
    Txt(rt, a.fSmall.Get(), L"Fixed default download folder",
        R(r2.right + 10, r2.top - 2, W - 30, r2.bottom + 2), C_TEXT);

    if (a.folderMode == 1) {
        D2D1_RECT_F pb = PrefPathBox(W);
        FillRound(rt, pb, 7.0f, C_PANEL_HI);
        StrokeRound(rt, pb, 7.0f, C_BORDER, 1.0f);
        std::wstring path = a.fixedFolder.empty() ? DownloadsDir() : a.fixedFolder;
        Txt(rt, a.fSmall.Get(), path, R(pb.left + 10, pb.top, pb.right - 8, pb.bottom), C_TEXT);

        D2D1_RECT_F bb = PrefPathBrowse(W);
        bool bh = InRect(bb, a.mouseX, a.mouseY);
        FillRound(rt, bb, 7.0f, bh ? C_HOVER : C_BG);
        StrokeRound(rt, bb, 7.0f, C_BORDER, 1.0f);
        Txt(rt, a.fSmall.Get(), L"Browse...", bb, C_TEXT, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    D2D1_RECT_F clip = PrefClipBox();
    CheckBox(rt, clip, a.clipboardWatch);
    Txt(rt, a.fSmall.Get(), L"Watch clipboard for download links",
        R(clip.right + 10, clip.top - 2, W - 30, clip.bottom + 2), C_TEXT);

    DrawStatus(a, L, {});
}

static void ScrollClamp(App& a, const Layout& L, size_t count) {
    float contentH = count * (CARD_H + GAP) + GAP;
    float viewH = (L.content.bottom - L.content.top);
    float maxScroll = std::max(0.0f, contentH - viewH);
    a.scrollY = std::max(0.0f, std::min(a.scrollY, maxScroll));
}

static void DrawScene(App& a) {
    auto sz = a.rt->GetSize();
    float W = sz.width, H = sz.height;
    Layout L = MakeLayout(W, H);
    auto* rt = a.rt.Get();

    rt->BeginDraw();
    rt->Clear(C_BG);

    if (a.view == 1) {
        DrawPreferences(a, W, H);
    } else {
        DrawToolbar(a, L);

        auto view = BuildView(a);
        ScrollClamp(a, L, view.size());

        if (view.empty()) {
            if (a.items.empty()) {
                DrawDropArea(a, L);
            } else {
                Txt(rt, a.fBody.Get(), L"No downloads match your filters",
                    R(L.content.left, L.content.top, L.content.right, L.content.bottom),
                    C_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        } else {
            float top = L.content.top + GAP - a.scrollY;
            for (auto& it : view) {
                if (top + CARD_H >= L.content.top && top <= L.content.bottom)
                    DrawCard(a, L, it, top, W);
                top += CARD_H + GAP;
            }
        }

        DrawStatus(a, L, view);
    }

    if (a.sortOpen) DrawDropdown(a, L, true);
    if (a.filterOpen) DrawDropdown(a, L, false);
    if (a.menuOpen) DrawAppMenu(a, W);
    if (a.prefDropdown) DrawPrefDropdown(a);
    if (a.aboutOpen) DrawAbout(a, W, H);
    if (a.addOpen) DrawAddDialog(a, W, H);
    if (a.newOpen) DrawNewDialog(a, W, H);

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) a.rt.Reset();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static void StartItem(App& a, std::shared_ptr<DownloadItem> it, bool startNow) {
    a.items.push_back(it);
    if (startNow && a.on && !it->started.exchange(true))
        std::thread(RunDownload, it).detach();
}

static void PauseItem(std::shared_ptr<DownloadItem> it) {
    if (!it) return;
    if (it->state.load() == 1) {
        it->paused = true;
        it->state = 4;
        NotifyUI();
    }
}
static void ResumeItem(std::shared_ptr<DownloadItem> it) {
    if (!it) return;
    if (it->state.load() == 4) {
        it->paused = false;
        it->state = 1;
        if (!it->started.exchange(true))
            std::thread(RunDownload, it).detach();
        NotifyUI();
    }
}
static void CancelItem(std::shared_ptr<DownloadItem> it) {
    if (!it) return;
    it->cancel = true;
    it->paused = false;
    NotifyUI();
}

static void SetEngineOn(App& a, bool on) {
    a.on = on;
    if (on) {
        for (auto& it : a.items) {
            if (it->state.load() == 0 && !it->started.exchange(true))
                std::thread(RunDownload, it).detach();
        }
    }
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void AddUrlsFromText(App& a, const std::wstring& text) {
    std::wstring folder = (a.folderMode == 1 && !a.fixedFolder.empty()) ? a.fixedFolder : DownloadsDir();
    bool any = false;
    for (auto& line : SplitLines(text)) {
        std::wstring u = TrimW(line);
        std::wstring low = Lower(u);
        if (low.rfind(L"http://", 0) != 0 && low.rfind(L"https://", 0) != 0) continue;
        auto it = std::make_shared<DownloadItem>();
        it->url = u;
        it->filename = FilenameFromUrl(u);
        it->ext = ExtOf(it->filename);
        it->category = CategoryOf(it->ext);
        it->destPath = UniquePath(folder, it->filename);
        it->seq = ++a.seq;
        it->lastTick = GetTickCount64();
        StartItem(a, it, true);
        any = true;
    }
    if (any) InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void OpenAdd(App& a) {
    a.addOpen = true;
    a.newOpen = false;
    a.menuOpen = false;
    a.sortOpen = a.filterOpen = false;
    a.view = 0;
    a.focus = 2;
    a.urlInput.clear();
    a.openAnim = 0.0f;
    StartAnim(a);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void StartNewProbe(App& a, const std::wstring& url) {
    a.probe = std::make_shared<ProbeState>();
    auto ps = a.probe;
    App* ap = &a;
    std::thread([ps, url, ap]() {
        uint64_t s = 0;
        bool r = false;
        std::wstring fn;
        bool ok = Probe(url, s, r, fn);
        ps->size = s;
        ps->ok = ok ? 1 : 0;
        ps->done = 1;
        if (ok && !fn.empty() && ap) {
            std::wstring ext = ExtOf(fn);
            ap->newName = ext.empty() ? fn : fn.substr(0, fn.size() - ext.size() - 1);
            if (ap->newName.empty()) ap->newName = fn;
            ap->newExt = ext.empty() ? L"" : (L"." + ext);
        }
        NotifyUI();
    }).detach();
}

static void AdvanceAdd(App& a) {
    std::wstring url = TrimW(a.urlInput);
    if (url.empty()) return;
    std::wstring low = Lower(url);
    bool torrent = (low.rfind(L"magnet:", 0) == 0) ||
                   (low.size() > 8 && low.substr(low.size() - 8) == L".torrent");
    if (torrent) {
        MessageBoxW(g_hwnd,
                    L"Torrent support is coming soon.\n\n"
                    L"Magnet links and .torrent files aren't handled yet. "
                    L"Paste a direct HTTP or HTTPS link for now.",
                    L"Coming soon", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring fname = FilenameFromUrl(url);
    std::wstring ext = ExtOf(fname);
    a.newUrl = url;
    a.newExt = ext.empty() ? L"" : (L"." + ext);
    a.newName = ext.empty() ? fname : fname.substr(0, fname.size() - ext.size() - 1);
    if (a.newName.empty()) a.newName = fname;
    a.newFolder = (a.folderMode == 1 && !a.fixedFolder.empty()) ? a.fixedFolder : DownloadsDir();
    a.newScheduler = false;
    a.addOpen = false;
    a.newOpen = true;
    a.focus = 3;
    a.openAnim = 0.0f;
    StartNewProbe(a, url);
    StartAnim(a);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void CommitNew(App& a, bool startNow) {
    std::wstring name = TrimW(a.newName);
    if (name.empty()) return;
    std::wstring folder = a.newFolder.empty() ? DownloadsDir() : a.newFolder;
    std::wstring full = name + a.newExt;
    auto it = std::make_shared<DownloadItem>();
    it->url = a.newUrl;
    it->filename = full;
    it->ext = ExtOf(full);
    it->category = CategoryOf(it->ext);
    it->destPath = UniquePath(folder, full);
    it->seq = ++a.seq;
    it->lastTick = GetTickCount64();
    if (!startNow) it->state = 4;
    StartItem(a, it, startNow);
    a.newOpen = false;
    a.focus = 0;
    a.probe.reset();
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void MenuAction(App& a, int action) {
    a.menuOpen = false;
    switch (action) {
    case 0:
        AddUrlsFromText(a, ClipboardText());
        break;
    case 1: {
        std::wstring f = PickUrlFile();
        if (!f.empty()) AddUrlsFromText(a, ReadTextFile(f));
        break;
    }
    case 2:
        MessageBoxW(g_hwnd, L"Export and import are coming soon.\n\nYou'll be able to save and restore your download list.",
                    L"Coming soon", MB_OK | MB_ICONINFORMATION);
        break;
    case 3:
        MessageBoxW(g_hwnd, L"Add-ons are coming soon.\n\nThe extension system isn't available yet.",
                    L"Coming soon", MB_OK | MB_ICONINFORMATION);
        break;
    case 7:
        MessageBoxW(g_hwnd, L"Auto shutdown is coming soon.\n\nSoon you'll be able to power off the PC when the queue finishes.",
                    L"Coming soon", MB_OK | MB_ICONINFORMATION);
        break;
    case 8:
        MessageBoxW(g_hwnd, L"Remote client connection is coming soon.\n\nFor now, manage downloads from this window.",
                    L"Coming soon", MB_OK | MB_ICONINFORMATION);
        break;
    case 9:
        MessageBoxW(g_hwnd, L"Portable mode is coming soon.\n\nThis will let you run the app from a USB drive with local settings.",
                    L"Coming soon", MB_OK | MB_ICONINFORMATION);
        break;
    case 4:
        a.view = 1;
        a.prefDropdown = 0;
        break;
    case 5:
        MessageBoxW(g_hwnd, L"Open Download Manager is free and open source.\n\nThank you for supporting the project!",
                    L"Support the project", MB_OK | MB_ICONINFORMATION);
        break;
    case 6:
        MessageBoxW(g_hwnd, L"To report a bug, open an issue in the project repository.",
                    L"Submit a bug report", MB_OK | MB_ICONINFORMATION);
        break;
    case 10:
        MessageBoxW(g_hwnd, L"Open Download Manager 0.2.4\n\nYou're up to date.",
                    L"Check for updates", MB_OK | MB_ICONINFORMATION);
        break;
    case 12:
        a.aboutOpen = true;
        a.openAnim = 0.0f;
        StartAnim(a);
        break;
    case 13:
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void DeleteDownload(App& a, std::shared_ptr<DownloadItem> it) {
    if (!it) return;
    int st = it->state.load();
    bool active = (st == 0 || st == 1);
    std::wstring msg;
    if (active)
        msg = L"Cancel this download and delete the partial file?\n\n" + it->filename;
    else
        msg = L"Delete this file from disk?\n\n" + it->filename + L"\n\nThis cannot be undone.";
    if (MessageBoxW(a.hwnd, msg.c_str(), L"Delete download", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    it->cancel = true;
    std::wstring path = it->destPath;
    a.items.erase(std::remove(a.items.begin(), a.items.end(), it), a.items.end());

    // Download threads may still hold the file open, so retry briefly in the background.
    std::thread([path]() {
        for (int i = 0; i < 60; i++) {
            if (DeleteFileW(path.c_str())) return;
            DWORD e = GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return;
            Sleep(100);
        }
    }).detach();
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void OpenDownloadedFile(App& a, std::shared_ptr<DownloadItem> it) {
    if (!it) return;
    if (it->state.load() != 2) {
        MessageBoxW(a.hwnd,
                    L"The file is not ready yet.\n\nWait for the download to finish, then double-click it again.",
                    L"Open file", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (GetFileAttributesW(it->destPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(a.hwnd,
                    L"The file was moved or deleted.\n\nUse the bin icon to remove this entry.",
                    L"Open file", MB_OK | MB_ICONWARNING);
        return;
    }
    ShellExecuteW(a.hwnd, L"open", it->destPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
static void OnMouseMove(App& a, int px, int py) {
    UINT dpi = GetDpiForWindow(a.hwnd);
    a.mouseX = px * 96.0f / dpi;
    a.mouseY = py * 96.0f / dpi;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void OnLButtonDown(App& a, int px, int py) {
    UINT dpi = GetDpiForWindow(a.hwnd);
    float x = px * 96.0f / dpi, y = py * 96.0f / dpi;

    RECT rc; GetClientRect(a.hwnd, &rc);
    float W = (rc.right - rc.left) * 96.0f / dpi;
    float H = (rc.bottom - rc.top) * 96.0f / dpi;
    Layout L = MakeLayout(W, H);
    auto inv = [&]() { InvalidateRect(a.hwnd, nullptr, FALSE); };

    if (a.newOpen) {
        D2D1_RECT_F d = NewDialogRect(W, H);
        if (InRect(NewNameRect(d), x, y)) { a.focus = 3; inv(); return; }
        if (InRect(NewFolderBtn(d), x, y)) {
            std::wstring f;
            if (PickFolder(f)) a.newFolder = f;
            inv(); return;
        }
        D2D1_RECT_F sb = NewSchedBox(d);
        if (InRect(sb, x, y) || InRect(R(sb.right + 8, sb.top - 4, d.right - 24, sb.bottom + 4), x, y)) {
            a.newScheduler = !a.newScheduler; inv(); return;
        }
        if (InRect(NewOkRect(d), x, y)) { CommitNew(a, true); return; }
        if (InRect(NewLaterRect(d), x, y)) { CommitNew(a, false); return; }
        if (InRect(NewCancelRect(d), x, y) || InRect(NewCloseRect(d), x, y)) {
            a.newOpen = false; a.focus = 0; a.probe.reset(); inv(); return;
        }
        if (InRect(d, x, y)) return;
        a.newOpen = false; a.focus = 0; a.probe.reset(); inv(); return;
    }

    if (a.aboutOpen) {
        D2D1_RECT_F d = AboutRect(W, H);
        if (InRect(AboutOkRect(d), x, y) || !InRect(d, x, y)) { a.aboutOpen = false; inv(); }
        return;
    }

    if (a.addOpen) {
        D2D1_RECT_F d = AddDialogRect(W, H);
        if (InRect(AddInputRect(d), x, y)) { a.focus = 2; inv(); return; }
        if (InRect(AddBrowseRect(d), x, y)) {
            MessageBoxW(g_hwnd,
                        L"Torrent files are coming soon.\n\n"
                        L"You'll be able to pick a .torrent file here once the "
                        L"BitTorrent engine lands. Paste a direct HTTP or HTTPS link for now.",
                        L"Coming soon", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (InRect(AddOkRect(d), x, y)) { AdvanceAdd(a); return; }
        if (InRect(AddCancelRect(d), x, y) || InRect(AddCloseRect(d), x, y)) {
            a.addOpen = false; a.focus = 0; inv(); return;
        }
        if (InRect(d, x, y)) return;
        a.addOpen = false; a.focus = 0; inv(); return;
    }

    if (a.menuOpen) {
        int mi = HitMenu(W, x, y);
        if (mi >= 0) {
            if (!APP_MENU[mi].disabled && !APP_MENU[mi].sep) {
                MenuAction(a, APP_MENU[mi].action);
            } else {
                a.menuOpen = false;
                inv();
            }
            return;
        }
        a.menuOpen = false;
        inv();
        return;
    }

    if (a.view == 1) {
        if (a.prefDropdown) {
            int oi = HitPrefDrop(a, x, y);
            if (oi >= 0) {
                if (a.prefDropdown == 1) { a.themeMode = oi; ApplyTheme(a.hwnd, a.themeMode); }
                else if (a.prefDropdown == 2) a.uiStyle = oi;
                else a.language = oi;
                a.prefDropdown = 0;
                inv();
                return;
            }
            a.prefDropdown = 0;
        }
        if (InRect(PrefBackRect(), x, y)) { PressAnim(a, 30); a.view = 0; inv(); return; }
        if (InRect(PrefMenuRect(W), x, y)) { PressAnim(a, 31); a.menuOpen = true; a.openAnim = 0.0f; StartAnim(a); inv(); return; }
        for (int i = 0; i < 8; i++) {
            if (InRect(PrefSideItem(i), x, y)) { a.prefSection = i; a.prefDropdown = 0; inv(); return; }
        }
        if (a.prefSection == 0) {
            if (InRect(PrefThemeBtn(), x, y)) { PressAnim(a, 32); a.prefDropdown = a.prefDropdown == 1 ? 0 : 1; inv(); return; }
            if (InRect(PrefUiBtn(), x, y)) { PressAnim(a, 33); a.prefDropdown = a.prefDropdown == 2 ? 0 : 2; inv(); return; }
            if (InRect(PrefLangBtn(), x, y)) { PressAnim(a, 34); a.prefDropdown = a.prefDropdown == 3 ? 0 : 3; inv(); return; }
            if (InRect(PrefLaunchBox(), x, y)) { a.launchStartup = !a.launchStartup; inv(); return; }
            if (InRect(PrefAutoRadio(), x, y)) { a.folderMode = 0; inv(); return; }
            if (InRect(PrefFixedRadio(), x, y)) { a.folderMode = 1; inv(); return; }
            if (a.folderMode == 1 && InRect(PrefPathBrowse(W), x, y)) {
                std::wstring f;
                if (PickFolder(f)) a.fixedFolder = f;
                inv(); return;
            }
            if (InRect(PrefClipBox(), x, y)) { a.clipboardWatch = !a.clipboardWatch; inv(); return; }
        }
        inv();
        return;
    }

    if (a.sortOpen) {
        D2D1_RECT_F sd = SortDropRect(L);
        if (InRect(sd, x, y)) {
            int i = HitList(sd, y, 8, -1);
            if (i >= 0) { a.sortMode = i; a.sortOpen = false; inv(); return; }
        }
        a.sortOpen = false;
    }
    if (a.filterOpen) {
        D2D1_RECT_F fd = FilterDropRect(L);
        if (InRect(fd, x, y)) {
            int i = HitList(fd, y, 6, 2);
            if (i >= 0) { a.filterMode = i; a.filterOpen = false; inv(); return; }
        }
        a.filterOpen = false;
    }

    if (InRect(L.sort, x, y)) { PressAnim(a, 2); a.sortOpen = !a.sortOpen; a.filterOpen = false; a.focus = 0; inv(); return; }
    if (InRect(L.filter, x, y)) { PressAnim(a, 3); a.filterOpen = !a.filterOpen; a.sortOpen = false; a.focus = 0; inv(); return; }
    if (InRect(L.add, x, y)) { PressAnim(a, 1); OpenAdd(a); return; }
    if (InRect(L.search, x, y)) { a.focus = 1; a.sortOpen = a.filterOpen = false; inv(); return; }
    if (InRect(L.gear, x, y)) { PressAnim(a, 5); a.menuOpen = !a.menuOpen; if (a.menuOpen) { a.openAnim = 0.0f; StartAnim(a); } a.sortOpen = a.filterOpen = false; a.focus = 0; inv(); return; }

    {
        float cy = (L.status.top + L.status.bottom) / 2;
        if (InRect(R(8, cy - 11, 66, cy + 11), x, y)) { PressAnim(a, 20); SetEngineOn(a, !a.on); return; }
        if (InRect(R(250, cy - 12, 340, cy + 12), x, y)) { PressAnim(a, 21); a.traffic = (a.traffic + 1) % 3; g_traffic = a.traffic; inv(); return; }
        if (InRect(R(348, cy - 12, 502, cy + 12), x, y)) { PressAnim(a, 22); MenuAction(a, 5); return; }
    }

    {
        D2D1_RECT_F card;
        auto it = ItemAt(a, L, W, x, y, &card);
        if (it && InRect(CardPauseRect(card), x, y)) {
            int s = it->state.load();
            if (s == 4) ResumeItem(it); else PauseItem(it);
            InvalidateRect(a.hwnd, nullptr, FALSE);
            return;
        }
        if (it && InRect(CardBinRect(card), x, y)) { DeleteDownload(a, it); return; }
    }

    D2D1_RECT_F drop = R(PAD + 2, L.content.top + PAD, L.content.right - PAD - 2, L.content.bottom - PAD);
    if (InRect(drop, x, y) && a.items.empty()) { OpenAdd(a); return; }

    a.focus = 0;
    a.sortOpen = a.filterOpen = false;
    inv();
}

static void OnLButtonDblClk(App& a, int px, int py) {
    if (a.view != 0 || a.addOpen || a.newOpen || a.aboutOpen || a.menuOpen ||
        a.sortOpen || a.filterOpen || a.prefDropdown)
        return;

    UINT dpi = GetDpiForWindow(a.hwnd);
    float x = px * 96.0f / dpi, y = py * 96.0f / dpi;
    RECT rc; GetClientRect(a.hwnd, &rc);
    float W = (rc.right - rc.left) * 96.0f / dpi;
    float H = (rc.bottom - rc.top) * 96.0f / dpi;
    Layout L = MakeLayout(W, H);

    D2D1_RECT_F card;
    auto it = ItemAt(a, L, W, x, y, &card);
    if (!it || InRect(CardBinRect(card), x, y)) return;
    OpenDownloadedFile(a, it);
}

static void OnKeyDown(App& a, WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_ESCAPE) {
        if (a.aboutOpen) a.aboutOpen = false;
        else if (a.newOpen) { a.newOpen = false; a.focus = 0; a.probe.reset(); }
        else if (a.addOpen) { a.addOpen = false; a.focus = 0; }
        else if (a.menuOpen) a.menuOpen = false;
        else if (a.prefDropdown) a.prefDropdown = 0;
        else if (a.view == 1) a.view = 0;
        else if (a.sortOpen || a.filterOpen) a.sortOpen = a.filterOpen = false;
        else if (a.focus != 0) a.focus = 0;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    if (key == VK_RETURN) {
        if (a.addOpen) AdvanceAdd(a);
        else if (a.newOpen) CommitNew(a, true);
        return;
    }
    if (key == VK_BACK) {
        if (a.focus == 1 && !a.search.empty()) a.search.pop_back();
        else if (a.focus == 2 && !a.urlInput.empty()) a.urlInput.pop_back();
        else if (a.focus == 3 && !a.newName.empty()) a.newName.pop_back();
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    if (ctrl && key == 'V') {
        if (a.focus == 1) a.search += ClipboardText();
        else if (a.focus == 2) a.urlInput += ClipboardText();
        else if (a.focus == 3) a.newName += ClipboardText();
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
}

static void OnChar(App& a, wchar_t ch) {
    if (ch < 32 || ch == 127) return;
    if (a.focus == 1) a.search.push_back(ch);
    else if (a.focus == 2) a.urlInput.push_back(ch);
    else if (a.focus == 3) a.newName.push_back(ch);
    else return;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* a = g_app;
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (a) { EnsureRT(*a); if (a->rt) DrawScene(*a); }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (a && a->rt) {
            a->rt->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        if (a && a->rt) {
            UINT d = LOWORD(wParam);
            a->rt->SetDpi((float)d, (float)d);
        }
        SetWindowPos(hwnd, nullptr, LOWORD(lParam), HIWORD(lParam),
                     LOWORD(lParam + 2) - LOWORD(lParam), HIWORD(lParam + 2) - HIWORD(lParam),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_MOUSEMOVE:
        if (a) OnMouseMove(*a, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDOWN:
        if (a) { SetFocus(hwnd); OnLButtonDown(*a, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (a) { SetFocus(hwnd); OnLButtonDblClk(*a, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); }
        return 0;
    case WM_MOUSEWHEEL:
        if (a) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            a->scrollY -= delta / 120.0f * 70.0f;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (a) OnKeyDown(*a, wParam);
        return 0;
    case WM_CHAR:
        if (a) OnChar(*a, (wchar_t)wParam);
        return 0;
    case WM_TIMER:
        if (a && wParam == 1) {
            ULONGLONG now = GetTickCount64();
            for (auto& it : a->items) {
                uint64_t cur = it->downloaded.load();
                ULONGLONG lt = it->lastTick;
                double dt = (now - lt) / 1000.0;
                if (dt > 0.25) {
                    it->speed = (cur >= it->lastBytes) ? (double)(cur - it->lastBytes) / dt : 0.0;
                    it->lastBytes = cur;
                    it->lastTick = now;
                }
                if (it->state.load() != 1) it->speed = 0;
            }
            if (a->clipboardWatch) {
                std::wstring clip = TrimW(ClipboardText());
                if (!clip.empty() && clip != a->lastClip) {
                    a->lastClip = clip;
                    std::wstring low = Lower(clip);
                    bool isUrl = (low.rfind(L"http://", 0) == 0) || (low.rfind(L"https://", 0) == 0);
                    if (isUrl) {
                        bool dup = false;
                        for (auto& it : a->items) if (it->url == clip) { dup = true; break; }
                        if (!dup) AddUrlsFromText(*a, clip);
                    }
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (a && wParam == 2) {
            a->pressAnim -= 0.11f;
            if (a->pressAnim < 0.0f) a->pressAnim = 0.0f;
            if (a->openAnim < 1.0f) {
                a->openAnim += 0.16f;
                if (a->openAnim > 1.0f) a->openAnim = 1.0f;
            }
            if (a->pressAnim <= 0.0f && a->openAnim >= 1.0f) {
                a->pressKind = 0;
                KillTimer(hwnd, 2);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_APP + 1:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        if (a) {
            RECT wr;
            if (GetWindowRect(hwnd, &wr)) {
                a->winX = wr.left;
                a->winY = wr.top;
                a->winW = wr.right - wr.left;
                a->winH = wr.bottom - wr.top;
            }
            SaveDownloads(*a);
            SaveSettings(*a);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Icon
// ---------------------------------------------------------------------------
static HICON MakeAppIcon(int S) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = S;
    bi.bV5Height = -S;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { DeleteDC(hdc); return nullptr; }

    auto segDist = [](float px, float py, float ax, float ay, float bx, float by) {
        float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
        float t = (wx * vx + wy * vy) / (vx * vx + vy * vy);
        t = std::max(0.0f, std::min(1.0f, t));
        float qx = ax + t * vx, qy = ay + t * vy;
        return sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy));
    };

    uint32_t* px = (uint32_t*)bits;
    float c = S * 0.5f, rad = S * 0.46f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float dist = sqrtf(dx * dx + dy * dy);
            float alpha = std::max(0.0f, std::min(1.0f, rad - dist + 0.5f));
            bool arrow = (fabsf(dx) < 0.06f * S && dy > -0.24f * S && dy < 0.10f * S)
                || segDist(dx, dy, -0.17f * S, 0.02f * S, 0.0f, 0.20f * S) < 0.058f * S
                || segDist(dx, dy, 0.17f * S, 0.02f * S, 0.0f, 0.20f * S) < 0.058f * S;
            float r = arrow ? 1.0f : 0.176f;
            float g = arrow ? 1.0f : 0.498f;
            float b = arrow ? 1.0f : 0.976f;
            uint32_t A = (uint32_t)(alpha * 255.0f + 0.5f);
            uint32_t Rr = (uint32_t)(r * 255.0f + 0.5f);
            uint32_t Gg = (uint32_t)(g * 255.0f + 0.5f);
            uint32_t Bb = (uint32_t)(b * 255.0f + 0.5f);
            px[y * S + x] = (A << 24) | (Rr << 16) | (Gg << 8) | Bb;
        }
    }

    std::vector<BYTE> zero((S * S + 7) / 8, 0);
    HBITMAP mask = CreateBitmap(S, S, 1, 1, zero.data());
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = dib;
    ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(dib);
    if (mask) DeleteObject(mask);
    DeleteDC(hdc);
    return ic;
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    App app;
    g_app = &app;

    D2D1CreateFactory<ID2D1Factory>(D2D1_FACTORY_TYPE_SINGLE_THREADED, app.d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(app.dw.GetAddressOf()));
    if (!app.d2d || !app.dw) return 1;

    app.fTitle = MakeFormat(app.dw.Get(), 17.0f, DWRITE_FONT_WEIGHT_BOLD);
    app.fHead  = MakeFormat(app.dw.Get(), 20.0f, DWRITE_FONT_WEIGHT_BOLD);
    app.fBold  = MakeFormat(app.dw.Get(), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    app.fBody  = MakeFormat(app.dw.Get(), 13.5f, DWRITE_FONT_WEIGHT_NORMAL);
    app.fSmall = MakeFormat(app.dw.Get(), 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
    app.fTiny  = MakeFormat(app.dw.Get(), 10.5f, DWRITE_FONT_WEIGHT_NORMAL);

    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties();
    sp.dashStyle = D2D1_DASH_STYLE_DASH;
    app.d2d->CreateStrokeStyle(sp, nullptr, 0, &app.dash);

    LoadSettings(app);
    LoadDownloads(app);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"ODMWindowClass";
    HICON icBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON icSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!icBig) icBig = MakeAppIcon(32);
    if (!icSmall) icSmall = MakeAppIcon(16);
    wc.hIcon = icBig;
    wc.hIconSm = icSmall;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForSystem();
    RECT rc = { 0, 0, 1100 * (LONG)dpi / 96, 720 * (LONG)dpi / 96 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - w) / 2, y = (sh - h) / 2;
    if (app.winW > 200 && app.winH > 200) {
        w = app.winW; h = app.winH;
        x = app.winX; y = app.winY;
        if (x < -50) x = 0;
        if (y < -50) y = 0;
        if (x + 100 > sw) x = sw - w;
        if (y + 100 > sh) y = sh - h;
    }

    HWND hwnd = CreateWindowExW(0, L"ODMWindowClass", L"Open Download Manager",
                                WS_OVERLAPPEDWINDOW, x, y, w, h,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    app.hwnd = hwnd;
    g_hwnd = hwnd;

    ApplyTheme(hwnd, app.themeMode);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 250, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
