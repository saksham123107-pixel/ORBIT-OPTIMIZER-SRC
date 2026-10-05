#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <objbase.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string>
#include <vector>

#define WM_U_STATUS (WM_APP + 1)
#define WM_U_DONE   (WM_APP + 2)
#define WM_U_ERR    (WM_APP + 3)

static HINSTANCE g_hInst;
static HWND g_hwnd, g_title, g_sub, g_steps, g_feat, g_path, g_status, g_pct, g_prog;
static HWND g_btnPrimary, g_btnClose, g_footer;
static COLORREF g_statusCol = 0x00BFD42D;
static bool g_finished = false;
static HBRUSH g_bg, g_brushHdr;
static HFONT g_fTitle, g_fSub, g_fText;
static bool g_gui = false, g_silent = false, g_uninstall = false, g_go = false;
static bool g_running = false;
static bool g_hovPrimary = false, g_hovClose = false;
static WNDPROC g_btnOldProc = nullptr;
static std::wstring g_dir, g_log, g_err;
static int g_exitCode = 0;
static int g_progPos = 0;
static unsigned long long g_bytes = 0;

static const std::wstring kAppName = L"ORBIT OPTIMIZER";
static const std::wstring kKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ORBIT OPTIMIZER";

static std::wstring ExePath() {
    wchar_t b[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    return b;
}
static std::wstring DirOf(const std::wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? std::wstring() : p.substr(0, i);
}
static std::wstring TrimSlash(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}
static std::wstring TempDir() {
    wchar_t b[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, b);
    return b;
}
static std::wstring Roaming() {
    wchar_t b[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, b);
    return b;
}
static std::wstring Local() {
    wchar_t b[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, b);
    return b;
}
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void Log(const std::wstring& s) {
    if (g_log.empty()) return;
    FILE* f = _wfopen(g_log.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"[%02u:%02u:%02u] %ls\r\n", st.wHour, st.wMinute, st.wSecond, s.c_str());
    fclose(f);
}

static void StatusSink(int pct, const wchar_t* msg) {
    Log(msg);
    if (g_gui && g_hwnd) {
        size_t n = wcslen(msg);
        wchar_t* c = new wchar_t[n + 1];
        wmemcpy(c, msg, n + 1);
        PostMessageW(g_hwnd, WM_U_STATUS, (WPARAM)pct, (LPARAM)c);
    }
}
static void DoneSink(const wchar_t* msg) {
    Log(msg);
    if (g_gui && g_hwnd) {
        size_t n = wcslen(msg);
        wchar_t* c = new wchar_t[n + 1];
        wmemcpy(c, msg, n + 1);
        PostMessageW(g_hwnd, WM_U_DONE, 100, (LPARAM)c);
    }
}
static void Fail(const wchar_t* msg) {
    g_err = msg;
    Log(std::wstring(L"ERROR: ") + msg);
    if (g_gui && g_hwnd) {
        size_t n = wcslen(msg);
        wchar_t* c = new wchar_t[n + 1];
        wmemcpy(c, msg, n + 1);
        PostMessageW(g_hwnd, WM_U_ERR, 0, (LPARAM)c);
    }
}

struct PayloadItem {
    int id;
    bool tmp;
    std::wstring dest;
};

static bool LoadManifest(std::vector<PayloadItem>& out) {
    HRSRC hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(900), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hg = LoadResource(g_hInst, hr);
    if (!hg) return false;
    DWORD sz = SizeofResource(g_hInst, hr);
    const char* p = (const char*)LockResource(hg);
    std::string txt(p, p + sz);
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t nl = txt.find('\n', pos);
        if (nl == std::string::npos) nl = txt.size();
        std::string line = txt.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        PayloadItem it;
        it.id = atoi(line.substr(0, t1).c_str());
        it.tmp = (line.compare(t1 + 1, t2 - t1 - 1, "tmp") == 0);
        std::string dest = line.substr(t2 + 1);
        int wn = MultiByteToWideChar(CP_UTF8, 0, dest.c_str(), (int)dest.size(), nullptr, 0);
        it.dest.resize(wn);
        MultiByteToWideChar(CP_UTF8, 0, dest.c_str(), (int)dest.size(), &it.dest[0], wn);
        if (it.id > 0 && !it.dest.empty()) out.push_back(it);
    }
    return !out.empty();
}

static bool WriteResource(int id, const std::wstring& full, unsigned long long& bytes) {
    HRSRC hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hg = LoadResource(g_hInst, hr);
    if (!hg) return false;
    DWORD sz = SizeofResource(g_hInst, hr);
    const char* p = (const char*)LockResource(hg);
    size_t slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) SHCreateDirectoryExW(nullptr, full.substr(0, slash).c_str(), nullptr);
    HANDLE f = CreateFileW(full.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, p, sz, &wrote, nullptr) && wrote == sz;
    CloseHandle(f);
    if (ok) bytes += sz;
    return ok;
}

static void RemoveTree(const std::wstring& dir) {
    std::wstring pat = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(dir.c_str());
        return;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring child = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(child);
        else if (!DeleteFileW(child.c_str()))
            MoveFileExW(child.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!RemoveDirectoryW(dir.c_str()))
        MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

static void CloseTargetApp() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    std::vector<DWORD> pids;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ORBIT OPTIMIZER.exe") == 0) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (pids.empty()) return;
    Log(L"Closing running ORBIT OPTIMIZER instance(s)...");
    for (DWORD pid : pids) {
        HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
        if (!pr) continue;
        struct Ctx { DWORD pid; HWND hwnd; } ctx = {pid, nullptr};
        EnumWindows([](HWND w, LPARAM lp) -> BOOL {
            auto* c = (Ctx*)lp;
            DWORD id = 0;
            GetWindowThreadProcessId(w, &id);
            if (id == c->pid && GetWindow(w, GW_OWNER) == nullptr && IsWindowVisible(w)) {
                c->hwnd = w;
                return FALSE;
            }
            return TRUE;
        }, (LPARAM)&ctx);
        if (ctx.hwnd) PostMessageW(ctx.hwnd, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(pr, 2500) != WAIT_OBJECT_0) {
            TerminateProcess(pr, 0);
            WaitForSingleObject(pr, 1500);
        }
        CloseHandle(pr);
    }
    Sleep(400);
}

static int PurgeAuth() {
    int n = 0;
    const wchar_t* roam[] = {L"auth.json", L"session.dat", L"discord-session.dat"};
    for (auto f : roam) {
        std::wstring p = Roaming() + L"\\PRIMEx Optimizer\\" + f;
        if (DeleteFileW(p.c_str())) n++;
    }
    const wchar_t* loc[] = {L"session.dat", L"discord-session.dat"};
    for (auto f : loc) {
        std::wstring p = Local() + L"\\PRIMEx Optimizer\\" + f;
        if (DeleteFileW(p.c_str())) n++;
    }
    Log(L"Purged " + std::to_wstring(n) + L" cached sign-in file(s)");
    return n;
}

static bool PrepareDir() {
    if (!DirExists(g_dir)) {
        if (SHCreateDirectoryExW(nullptr, g_dir.c_str(), nullptr) != ERROR_SUCCESS &&
            !DirExists(g_dir)) {
            Fail(L"Could not create the install folder.");
            return false;
        }
        return true;
    }
    bool empty = true;
    std::wstring pat = g_dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { empty = false; break; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (empty) return true;
    if (!FileExists(g_dir + L"\\ORBIT OPTIMIZER.exe") &&
        !FileExists(g_dir + L"\\Uninstall ORBIT OPTIMIZER.exe")) {
        Fail(L"Install folder is not empty and was not created by ORBIT OPTIMIZER. Aborting.");
        return false;
    }
    RemoveTree(g_dir);
    if (DirExists(g_dir)) {
        Sleep(300);
        if (DirExists(g_dir) && !FileExists(g_dir + L"\\ORBIT OPTIMIZER.exe")) {
            Fail(L"Could not clear the previous installation (files in use). Close ORBIT OPTIMIZER and retry.");
            return false;
        }
    }
    SHCreateDirectoryExW(nullptr, g_dir.c_str(), nullptr);
    return true;
}

static bool WebView2PvOk(HKEY root, const wchar_t* sub, REGSAM view) {
    HKEY k;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | view, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[64] = {};
    DWORD cb = sizeof(buf) - sizeof(wchar_t), t = 0;
    bool ok = RegQueryValueExW(k, L"pv", nullptr, &t, (LPBYTE)buf, &cb) == ERROR_SUCCESS &&
              t == REG_SZ && buf[0] && wcscmp(buf, L"0.0.0.0") != 0;
    RegCloseKey(k);
    return ok;
}
static bool WebView2Installed() {
    const wchar_t* sub = L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    return WebView2PvOk(HKEY_CURRENT_USER, sub, 0) ||
           WebView2PvOk(HKEY_LOCAL_MACHINE, sub, 0) ||
           WebView2PvOk(HKEY_LOCAL_MACHINE, sub, KEY_WOW64_32KEY) ||
           WebView2PvOk(HKEY_LOCAL_MACHINE, sub, KEY_WOW64_64KEY);
}

static bool InstallWebView2() {
    if (WebView2Installed()) {
        StatusSink(88, L"WebView2 Runtime already installed.");
        return true;
    }
    StatusSink(78, L"Installing WebView2 Runtime (required for the interface)...");
    std::wstring tmp = TempDir() + L"MicrosoftEdgeWebView2RuntimeInstaller.exe";
    unsigned long long b = 0;
    if (!WriteResource(103, tmp, b)) {
        StatusSink(88, L"WebView2 Runtime installer missing; app shows a repair prompt if needed.");
        return false;
    }
    std::wstring cmd = L"\"" + tmp + L"\" /silent /install";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    bool ran = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0;
    DWORD code = (DWORD)-1;
    if (ran) {
        WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    Log(L"WebView2 bootstrapper exit=" + std::to_wstring((int)code));
    DeleteFileW(tmp.c_str());
    for (int i = 0; i < 90 && !WebView2Installed(); i++) Sleep(2000);
    if (WebView2Installed()) {
        StatusSink(88, L"WebView2 Runtime installed.");
        return true;
    }
    StatusSink(88, L"WebView2 Runtime not detected; installer will retry on first app start.");
    return false;
}

static bool MakeShortcut(const std::wstring& lnk) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl)) || !sl)
        return false;
    std::wstring app = g_dir + L"\\ORBIT OPTIMIZER.exe";
    sl->SetPath(app.c_str());
    sl->SetWorkingDirectory(g_dir.c_str());
    sl->SetIconLocation(app.c_str(), 0);
    sl->SetDescription(kAppName.c_str());
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && pf) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

static void WriteArp() {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    std::wstring app = g_dir + L"\\ORBIT OPTIMIZER.exe";
    std::wstring unins = L"\"" + g_dir + L"\\Uninstall ORBIT OPTIMIZER.exe\" --uninstall";
    DWORD kb = (DWORD)((g_bytes + 1023) / 1024);
    auto sz = [](HKEY h, const wchar_t* n, const wchar_t* v) {
        RegSetValueExW(h, n, 0, REG_SZ, (const BYTE*)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    };
    auto dw = [](HKEY h, const wchar_t* n, DWORD v) {
        RegSetValueExW(h, n, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    };
    sz(k, L"DisplayName", kAppName.c_str());
    sz(k, L"DisplayVersion", L"1.0.0");
    sz(k, L"Publisher", L"PRIMEx");
    sz(k, L"DisplayIcon", app.c_str());
    sz(k, L"InstallLocation", g_dir.c_str());
    sz(k, L"UninstallString", unins.c_str());
    sz(k, L"QuietUninstallString", (unins + L" --silent").c_str());
    dw(k, L"NoModify", 1);
    dw(k, L"NoRepair", 1);
    dw(k, L"EstimatedSize", kb);
    RegCloseKey(k);
    Log(L"ARP entry written");
}

static bool DoInstall() {
    StatusSink(3, L"Closing running ORBIT OPTIMIZER instance(s)...");
    CloseTargetApp();
    StatusSink(8, L"Preparing install folder...");
    if (!PrepareDir()) return false;

    std::vector<PayloadItem> items;
    if (!LoadManifest(items)) {
        Fail(L"Installer payload is corrupted.");
        return false;
    }
    int nFiles = 0;
    for (auto& it : items)
        if (!it.tmp) nFiles++;
    int done = 0;
    StatusSink(12, L"Extracting files...");
    for (auto& it : items) {
        std::wstring full = it.tmp ? (TempDir() + it.dest) : (g_dir + L"\\" + it.dest);
        if (!WriteResource(it.id, full, g_bytes)) {
            Fail(L"Could not write files. Close ORBIT OPTIMIZER and retry.");
            return false;
        }
        if (!it.tmp) {
            done++;
            StatusSink(12 + done * 46 / (nFiles ? nFiles : 1), L"Extracting files...");
        }
    }

    StatusSink(62, L"Creating shortcuts...");
    // The dedicated uninstaller arrives with the payload (manifest id 104).
    {
        wchar_t base[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, base);
        MakeShortcut(std::wstring(base) + L"\\" + kAppName + L".lnk");
        SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, base);
        MakeShortcut(std::wstring(base) + L"\\" + kAppName + L".lnk");
    }

    StatusSink(70, L"Registering in Apps & features...");
    WriteArp();

    StatusSink(75, L"Checking WebView2 Runtime...");
    InstallWebView2();

    Log(L"Install complete: " + g_dir);
    DoneSink(L"Installation complete.");
    return true;
}

static bool DoUninstall() {
    CloseTargetApp();
    StatusSink(4, L"Removing saved sign-in data...");
    PurgeAuth();
    StatusSink(8, L"Removing shortcuts...");
    {
        wchar_t base[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, base);
        DeleteFileW((std::wstring(base) + L"\\" + kAppName + L".lnk").c_str());
        SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, base);
        DeleteFileW((std::wstring(base) + L"\\" + kAppName + L".lnk").c_str());
    }
    StatusSink(30, L"Removing Apps & features entry...");
    RegDeleteKeyExW(HKEY_CURRENT_USER, kKey.c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteKeyW(HKEY_CURRENT_USER, kKey.c_str());
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kKey.c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kKey.c_str(), KEY_WOW64_32KEY, 0);

    StatusSink(45, L"Removing program files...");
    if (DirExists(g_dir)) RemoveTree(g_dir);

    MoveFileExW(ExePath().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    Log(L"Uninstall complete");
    DoneSink(L"Uninstall complete.");
    return true;
}

static bool RelocateForUninstall() {
    std::wstring me = ExePath();
    if (_wcsicmp(TrimSlash(DirOf(me)).c_str(), TrimSlash(g_dir).c_str()) != 0)
        return false;
    std::wstring dst = TempDir() + L"orbit_unins.exe";
    DeleteFileW(dst.c_str());
    if (!CopyFileW(me.c_str(), dst.c_str(), FALSE)) return false;
    std::wstring cmd = L"\"" + dst + L"\" --uninstall --go" +
                       (g_silent ? L" --silent" : L"") + L" --dir=\"" + g_dir + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static DWORD WINAPI Worker(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = g_uninstall ? DoUninstall() : DoInstall();
    if (!ok && !g_gui) g_exitCode = g_uninstall ? 6 : 2;
    if (!ok && g_gui && g_err.empty()) Fail(L"Operation failed.");
    CoUninitialize();
    return ok ? 0 : 1;
}

static void RunSteps() {
    if (g_uninstall && g_go) Sleep(900);
    if (g_silent) {
        Worker(nullptr);
        ExitProcess(g_exitCode);
    }
    HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    else Fail(L"Could not start worker thread.");
}

static const wchar_t* kFeatInstall =
    L"\x2022  Installs for your account - no admin rights needed\n"
    L"\x2022  Includes every file the app needs to run\n"
    L"\x2022  Installs the WebView2 runtime automatically\n"
    L"\x2022  Clears any saved sign-in data before installing";
static const wchar_t* kFeatUninstall =
    L"\x2022  Removes the program files and shortcuts\n"
    L"\x2022  Removes the Apps & features entry\n"
    L"\x2022  Keeps your saved tweaks and settings";

static std::wstring StepsText(int stage) {
    const wchar_t *mid = g_uninstall ? L"Removing" : L"Installing";
    const wchar_t *rdy = L"\x25CF", *w0 = L"\x25CB", *w1 = L"\x25CB", *d = L"\x25CB";
    if (stage >= 1) w1 = L"\x25CF";
    if (stage >= 2) { w0 = L"\x25CF"; d = L"\x25CF"; }
    wchar_t b[160]{};
    swprintf(b, 160, L"%ls Ready    \x2192    %ls %ls    \x2192    %ls Done", rdy, w1, mid, d);
    (void)w0;
    return b;
}
static void SetSteps(int stage) {
    if (g_steps) SetWindowTextW(g_steps, StepsText(stage).c_str());
}

static void ApplyFrameDark(HWND w) {
    BOOL v = TRUE;
    DwmSetWindowAttribute(w, 20, &v, sizeof(v));
    DwmSetWindowAttribute(w, 19, &v, sizeof(v));
    int pref = 2;
    DwmSetWindowAttribute(w, 33, &pref, sizeof(pref));
}

static LRESULT CALLBACK BtnProc(HWND b, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEMOVE) {
        bool* hv = (b == g_btnPrimary) ? &g_hovPrimary : &g_hovClose;
        if (!*hv) {
            *hv = true;
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, b, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(b, nullptr, FALSE);
        }
    } else if (m == WM_MOUSELEAVE) {
        bool* hv = (b == g_btnPrimary) ? &g_hovPrimary : &g_hovClose;
        if (*hv) {
            *hv = false;
            InvalidateRect(b, nullptr, FALSE);
        }
    } else if (m == WM_ERASEBKGND) {
        return 1;
    }
    return CallWindowProcW(g_btnOldProc, b, m, w, l);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_hwnd = h;
        ApplyFrameDark(h);
        g_fTitle = CreateFontW(-34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fSub = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fText = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD st, int id) {
            HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, 0, 0, 0, 0, h, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)g_fText, TRUE);
            return c;
        };
        g_title = mk(L"Static", L"ORBIT OPTIMIZER", 0, 1001);
        SendMessageW(g_title, WM_SETFONT, (WPARAM)g_fTitle, TRUE);
        std::wstring subTxt = g_uninstall ? L"Uninstall  \x00B7  Version 1.0" : L"Setup  \x00B7  Version 1.0";
        g_sub = mk(L"Static", subTxt.c_str(), 0, 1002);
        SendMessageW(g_sub, WM_SETFONT, (WPARAM)g_fSub, TRUE);
        g_steps = mk(L"Static", StepsText(0).c_str(), 0, 1009);
        SendMessageW(g_steps, WM_SETFONT, (WPARAM)g_fSub, TRUE);
        g_feat = mk(L"Static", g_uninstall ? kFeatUninstall : kFeatInstall, 0, 1007);
        SendMessageW(g_feat, WM_SETFONT, (WPARAM)g_fSub, TRUE);
        std::wstring pathTxt = (g_uninstall ? L"Location:  " : L"Install to:  ") + g_dir;
        g_path = mk(L"Static", pathTxt.c_str(), SS_LEFT | SS_PATHELLIPSIS, 1010);
        SendMessageW(g_path, WM_SETFONT, (WPARAM)g_fSub, TRUE);
        g_status = mk(L"Static", g_uninstall ? L"Ready to remove." : L"Ready to install.", 0, 1003);
        g_pct = mk(L"Static", L"", SS_RIGHT, 1011);
        SendMessageW(g_pct, WM_SETFONT, (WPARAM)g_fSub, TRUE);
        g_prog = mk(L"Static", L"", SS_OWNERDRAW, 1004);
        g_btnPrimary = mk(L"Button", g_uninstall ? L"Uninstall" : L"Install", BS_OWNERDRAW, 1005);
        g_btnClose = mk(L"Button", L"Close", BS_OWNERDRAW, 1006);
        g_btnOldProc = (WNDPROC)SetWindowLongPtrW(g_btnPrimary, GWLP_WNDPROC, (LONG_PTR)BtnProc);
        SetWindowLongPtrW(g_btnClose, GWLP_WNDPROC, (LONG_PTR)BtnProc);
        g_footer = mk(L"Static", L"\x00A9 2026 PRIMEx  \x00B7  v1.0.0", 0, 1008);
        SendMessageW(g_footer, WM_SETFONT, (WPARAM)g_fSub, TRUE);
        RECT rc;
        GetClientRect(h, &rc);
        int W = rc.right, M = 36;
        MoveWindow(g_title, 100, 28, W - 100 - M, 46, TRUE);
        MoveWindow(g_sub, 100, 76, W - 100 - M, 22, TRUE);
        MoveWindow(g_steps, M, 122, W - 2 * M, 22, TRUE);
        MoveWindow(g_feat, M, 156, W - 2 * M, 92, TRUE);
        MoveWindow(g_path, M, 258, W - 2 * M, 22, TRUE);
        MoveWindow(g_status, M, 296, W - 2 * M - 76, 24, TRUE);
        MoveWindow(g_pct, W - M - 70, 296, 70, 24, TRUE);
        MoveWindow(g_prog, M, 326, W - 2 * M, 14, TRUE);
        MoveWindow(g_btnPrimary, W - M - 292, 372, 152, 42, TRUE);
        MoveWindow(g_btnClose, W - M - 128, 372, 128, 42, TRUE);
        MoveWindow(g_footer, M, 382, 260, 20, TRUE);
        if (g_uninstall && g_go) RunSteps();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        RECT hdr = {0, 0, cr.right, 116};
        FillRect(dc, &hdr, g_brushHdr);
        HICON ic = LoadIconW(g_hInst, MAKEINTRESOURCEW(100));
        if (ic) DrawIconEx(dc, 36, 34, ic, 48, 48, 0, nullptr, DI_NORMAL);
        RECT bar = {100, 102, 244, 105};
        HBRUSH b = CreateSolidBrush(0x00BFD42D);
        FillRect(dc, &bar, b);
        DeleteObject(b);
        RECT div = {36, 356, cr.right - 36, 357};
        HBRUSH db = CreateSolidBrush(0x00443A30);
        FillRect(dc, &div, db);
        DeleteObject(db);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        HWND c = (HWND)lp;
        COLORREF col;
        if (c == g_status) col = g_statusCol;
        else if (c == g_title) col = 0x00F3EDE6;
        else if (c == g_sub || c == g_footer) col = 0x00B4A79A;
        else if (c == g_steps || c == g_pct) col = 0x00BFD42D;
        else if (c == g_path) col = 0x00CBB89F;
        else col = 0x00EAE1D7;
        SetTextColor(dc, col);
        bool inHeader = (c == g_title || c == g_sub);
        return (LRESULT)(inHeader ? g_brushHdr : g_bg);
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)g_bg;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (d->CtlType == ODT_STATIC && d->CtlID == 1004) {
            RECT r = d->rcItem;
            HBRUSH tb = CreateSolidBrush(0x00322820);
            HRGN track = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, 14, 14);
            FillRgn(d->hDC, track, tb);
            DeleteObject(tb);
            if (g_progPos > 0) {
                int w = ((r.right - r.left) * g_progPos) / 100;
                if (w > 0) {
                    HBRUSH fb = CreateSolidBrush(g_finished ? 0x0068D44C : 0x00BFD42D);
                    HRGN fill = CreateRoundRectRgn(r.left, r.top, r.left + w + 1, r.bottom + 1, 14, 14);
                    FillRgn(d->hDC, fill, fb);
                    DeleteObject(fb);
                    DeleteObject(fill);
                }
            }
            DeleteObject(track);
            return TRUE;
        }
        if (d->CtlType != ODT_BUTTON) break;
        bool primary = d->CtlID == 1005;
        bool launch = false;
        wchar_t cap[64] = {};
        GetWindowTextW(d->hwndItem, cap, 64);
        launch = wcscmp(cap, L"Launch") == 0;
        bool en = (d->itemState & ODS_DISABLED) == 0;
        HBRUSH fill;
        COLORREF txt, brd;
        if (launch || primary) {
            fill = CreateSolidBrush(en ? (g_hovPrimary ? 0x00CDE63C : 0x00BFD42D) : 0x002E2620);
            txt = en ? 0x00101010 : 0x00746A60;
            brd = 0x00BFD42D;
        } else {
            fill = CreateSolidBrush(g_hovClose ? 0x00322820 : 0x001E1712);
            txt = en ? (g_hovClose ? 0x00BFD42D : 0x00F3EDE6) : 0x00746A60;
            brd = g_hovClose ? 0x00BFD42D : 0x0051443A;
        }
        HPEN p = CreatePen(PS_SOLID, 1, brd);
        HGDIOBJ old = SelectObject(d->hDC, p);
        HGDIOBJ oldb = SelectObject(d->hDC, fill);
        RoundRect(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right, d->rcItem.bottom, 18, 18);
        SelectObject(d->hDC, old);
        SelectObject(d->hDC, oldb);
        DeleteObject(p);
        DeleteObject(fill);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, txt);
        HGDIOBJ of = SelectObject(d->hDC, g_fText);
        DrawTextW(d->hDC, cap, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(d->hDC, of);
        return TRUE;
    }
    case WM_U_STATUS: {
        wchar_t* s = (wchar_t*)lp;
        if (g_status) {
            SetWindowTextW(g_status, s);
            InvalidateRect(g_status, nullptr, TRUE);
        }
        g_progPos = (int)wp;
        if (g_pct) {
            wchar_t p[16]{};
            swprintf(p, 16, L"%d%%", g_progPos);
            SetWindowTextW(g_pct, p);
        }
        if (g_prog) InvalidateRect(g_prog, nullptr, TRUE);
        delete[] s;
        return 0;
    }
    case WM_U_DONE: {
        wchar_t* s = (wchar_t*)lp;
        g_finished = true;
        g_statusCol = 0x0068D44C;
        std::wstring ok = std::wstring(L"\x2713  ") + s;
        if (g_status) {
            SetWindowTextW(g_status, ok.c_str());
            InvalidateRect(g_status, nullptr, TRUE);
        }
        g_progPos = 100;
        if (g_pct) SetWindowTextW(g_pct, L"100%");
        if (g_prog) InvalidateRect(g_prog, nullptr, TRUE);
        delete[] s;
        SetSteps(2);
        if (!g_uninstall) {
            SetWindowTextW(g_btnPrimary, L"Launch");
        } else {
            SetWindowTextW(g_btnPrimary, L"Close");
        }
        EnableWindow(g_btnPrimary, TRUE);
        EnableWindow(g_btnClose, TRUE);
        SetFocus(g_btnPrimary);
        g_running = false;
        return 0;
    }
    case WM_U_ERR: {
        wchar_t* s = (wchar_t*)lp;
        g_statusCol = 0x005050E8;
        std::wstring bad = std::wstring(L"\x2717  ") + s;
        if (g_status) {
            SetWindowTextW(g_status, bad.c_str());
            InvalidateRect(g_status, nullptr, TRUE);
        }
        if (g_pct) SetWindowTextW(g_pct, L"");
        MessageBoxW(h, s, kAppName.c_str(), MB_OK | MB_ICONERROR);
        delete[] s;
        SetWindowTextW(g_btnPrimary, g_uninstall ? L"Uninstall" : L"Install");
        SetSteps(0);
        EnableWindow(g_btnPrimary, TRUE);
        EnableWindow(g_btnClose, TRUE);
        g_running = false;
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 1006) {
            DestroyWindow(h);
            return 0;
        }
        if (id == 1005) {
            wchar_t cap[32] = {};
            GetWindowTextW(g_btnPrimary, cap, 32);
            if (wcscmp(cap, L"Launch") == 0) {
                std::wstring app = g_dir + L"\\ORBIT OPTIMIZER.exe";
                ShellExecuteW(nullptr, L"open", app.c_str(), nullptr, g_dir.c_str(), SW_SHOWNORMAL);
                DestroyWindow(h);
                return 0;
            }
            if (g_finished && g_uninstall) {
                DestroyWindow(h);
                return 0;
            }
            if (g_uninstall) {
                int r = MessageBoxW(h,
                                    (L"Remove " + kAppName + L"?\n\nProgram files, shortcuts and the Apps & features entry will be deleted.\n"
                                     L"Saved tweaks are kept.")
                                        .c_str(),
                                    (kAppName + L" Uninstall").c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
                if (r != IDYES) return 0;
                if (RelocateForUninstall()) {
                    DestroyWindow(h);
                    return 0;
                }
            }
            if (g_running) return 0;
            g_running = true;
            g_finished = false;
            g_statusCol = 0x00BFD42D;
            g_progPos = 0;
            if (g_pct) SetWindowTextW(g_pct, L"0%");
            if (g_prog) InvalidateRect(g_prog, nullptr, TRUE);
            SetSteps(1);
            EnableWindow(g_btnPrimary, FALSE);
            EnableWindow(g_btnClose, FALSE);
            SetWindowTextW(g_btnPrimary, g_uninstall ? L"Removing..." : L"Installing...");
            SetWindowTextW(g_status, g_uninstall ? L"Removing..." : L"Installing...");
            RunSteps();
            return 0;
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ParseArgs() {
    int n = 0;
    LPWSTR* a = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!a) return;
    for (int i = 1; i < n; i++) {
        std::wstring s = a[i];
        if (s == L"--silent" || s == L"/S" || s == L"/silent") g_silent = true;
        else if (s == L"--uninstall" || s == L"/uninstall") g_uninstall = true;
        else if (s == L"--go") g_go = true;
        else if (s.rfind(L"--dir=", 0) == 0) g_dir = s.substr(6);
    }
    LocalFree(a);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_hInst = hInst;
    ParseArgs();
#ifdef ORBIT_UNINSTALLER_ONLY
    // Dedicated uninstaller binary: never installs, no matter the arguments.
    g_uninstall = true;
#endif
    if (g_dir.empty()) g_dir = Local() + L"\\Programs\\" + kAppName;
    g_dir = TrimSlash(g_dir);
    g_log = TempDir() + L"orbit-setup.log";
    if (!g_go) DeleteFileW(g_log.c_str());

    if (g_uninstall && !g_go && g_silent) {
        if (RelocateForUninstall()) return 0;
        Worker(nullptr);
        return g_exitCode;
    }
    if (g_silent) {
        Worker(nullptr);
        return g_exitCode;
    }

    g_gui = true;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    g_bg = CreateSolidBrush(0x1A1410);
    g_brushHdr = CreateSolidBrush(0x28201A);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bg;
    wc.lpszClassName = L"OrbitSetupWnd";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(100));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    int W = 640, H = 480;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    HWND h = CreateWindowExW(0, L"OrbitSetupWnd",
                             (kAppName + (g_uninstall ? L" Uninstall" : L" Setup")).c_str(),
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             wa.left + ((wa.right - wa.left) - W) / 2,
                             wa.top + ((wa.bottom - wa.top) - H) / 2,
                             W, H, nullptr, nullptr, hInst, nullptr);
    if (!h) return 1;
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
