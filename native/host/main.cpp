// main.cpp — borderless Win32 window hosting WebView2.
// C++ replacement for shell/MainWindow.xaml + MainWindow.xaml.cs.
// UI (frontend/dist) and IPC contract are unchanged.
//
// WebView2 notes (MinGW-friendly):
//  - No WRL/ATL: COM event sinks are hand-rolled IUnknown classes.
//  - No static link: WebView2Loader.dll is loaded at runtime from the
//    exe directory (shipped from the NuGet package in thirdparty/).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <dwmapi.h>

#include <future>
#include <string>
#include <vector>
#include <cstring>

#include "host.h"
#include "../src/guard.h"
#include "../src/util.h"
#include "ui_embedded.h"

#if __has_include("WebView2.h")
#define HAVE_WEBVIEW2 1
#include "WebView2.h"
#else
#define HAVE_WEBVIEW2 0
#endif

namespace {

#define WM_APP_SEND (WM_APP + 1)

HWND g_hwnd = nullptr;

#if HAVE_WEBVIEW2

ICoreWebView2Controller *g_ctrl = nullptr;
ICoreWebView2 *g_web = nullptr;

typedef HRESULT(STDAPICALLTYPE *PFN_CreateEnvWithOptions)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

void SendToPage(const std::string &json) {
  // WebView2 is an STA object: PostWebMessageAsJson must run on the UI
  // thread. Worker threads (Dispatch) hop via a window message instead —
  // calling it directly would fail/hang and the page would wait forever
  // (e.g. stuck on "Contacting auth servers...").
  if (!g_hwnd)
    return;
  std::string *copy = new std::string(json);
  if (!PostMessageW(g_hwnd, WM_APP_SEND, 0, (LPARAM)copy))
    delete copy;
}

  // Runs on the UI thread: drain one queued message per WM_APP_SEND.
  // (Each message carries its own string; see WndProc.)

std::wstring ExeDir();
std::wstring FindDist();

void OnIpcMessage(const std::wstring &jsonW);

class MsgHandler
    : public ICoreWebView2WebMessageReceivedEventHandler {
  long m_refs = 1;

public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **pp) override {
    if (!pp)
      return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(
            riid,
            IID_ICoreWebView2WebMessageReceivedEventHandler)) {
      *pp = static_cast<ICoreWebView2WebMessageReceivedEventHandler *>(this);
      AddRef();
      return S_OK;
    }
    *pp = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return (ULONG)InterlockedIncrement(&m_refs);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    long r = InterlockedDecrement(&m_refs);
    if (r == 0)
      delete this;
    return (ULONG)r;
  }
  HRESULT STDMETHODCALLTYPE Invoke(
      ICoreWebView2 *sender,
      ICoreWebView2WebMessageReceivedEventArgs *args) override {
    (void)sender;
    LPWSTR msg = nullptr;
    if (SUCCEEDED(args->get_WebMessageAsJson(&msg)) && msg) {
      OnIpcMessage(msg);
      CoTaskMemFree(msg);
    }
    return S_OK;
  }
};

class CtrlHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
  long m_refs = 1;
  HWND m_hwnd;
  bool m_isDev;

public:
  CtrlHandler(HWND hwnd, bool isDev) : m_hwnd(hwnd), m_isDev(isDev) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **pp) override {
    if (!pp)
      return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(
            riid,
            IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
      *pp = static_cast<
          ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
      AddRef();
      return S_OK;
    }
    *pp = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return (ULONG)InterlockedIncrement(&m_refs);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    long r = InterlockedDecrement(&m_refs);
    if (r == 0)
      delete this;
    return (ULONG)r;
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2Controller *ctrl) override {
    if (FAILED(hr) || !ctrl)
      return S_OK;
    g_ctrl = ctrl;
    g_ctrl->AddRef();
    if (FAILED(g_ctrl->get_CoreWebView2(&g_web)) || !g_web)
      return S_OK;
    ICoreWebView2Settings *settings = nullptr;
    if (SUCCEEDED(g_web->get_Settings(&settings)) && settings) {
      settings->put_AreDefaultContextMenusEnabled(FALSE);
      settings->put_AreDevToolsEnabled(TRUE);
      settings->Release();
    }
    MsgHandler *mh = new MsgHandler();
    EventRegistrationToken tok{};
    g_web->add_WebMessageReceived(mh, &tok);
    mh->Release();
    RECT r{};
    GetClientRect(m_hwnd, &r);
    g_ctrl->put_Bounds(r);
    if (m_isDev) {
      g_web->Navigate(L"http://localhost:1420/");
      return S_OK;
    }
    std::wstring dist = FindDist();
    DWORD a = GetFileAttributesW(dist.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
      std::wstring m = L"UI folder not found:\n" + dist +
                       L"\n\nNo embedded UI available.";
      MessageBoxW(m_hwnd, m.c_str(), L"ORBIT OPTIMIZER",
                  MB_OK | MB_ICONERROR);
      PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
      return S_OK;
    }
    ICoreWebView2_3 *wv3 = nullptr;
    HRESULT mapHr =
        g_web->QueryInterface(IID_ICoreWebView2_3, (void **)&wv3);
    if (FAILED(mapHr) || !wv3 ||
        FAILED(wv3->SetVirtualHostNameToFolderMapping(
            L"primex.local", dist.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) {
      if (wv3)
        wv3->Release();
      MessageBoxW(m_hwnd, L"Failed to start UI.", L"ORBIT OPTIMIZER",
                  MB_OK | MB_ICONERROR);
      PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
      return S_OK;
    }
    wv3->Release();
    g_web->Navigate(L"https://primex.local/index.html?v=1.0.0");
    return S_OK;
  }
};

class EnvHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
  long m_refs = 1;
  HWND m_hwnd;
  bool m_isDev;

public:
  EnvHandler(HWND hwnd, bool isDev) : m_hwnd(hwnd), m_isDev(isDev) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **pp) override {
    if (!pp)
      return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(
            riid,
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
      *pp = static_cast<
          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
      AddRef();
      return S_OK;
    }
    *pp = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return (ULONG)InterlockedIncrement(&m_refs);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    long r = InterlockedDecrement(&m_refs);
    if (r == 0)
      delete this;
    return (ULONG)r;
  }
  HRESULT STDMETHODCALLTYPE Invoke(
      HRESULT hr, ICoreWebView2Environment *env) override {
    if (FAILED(hr) || !env)
      return S_OK;
    CtrlHandler *ch = new CtrlHandler(m_hwnd, m_isDev);
    HRESULT r = env->CreateCoreWebView2Controller(m_hwnd, ch);
    ch->Release();
    return r;
  }
};

std::wstring ExeDir() {
  wchar_t p[MAX_PATH]{};
  GetModuleFileNameW(nullptr, p, MAX_PATH);
  std::wstring s = p;
  auto i = s.find_last_of(L"\\/");
  return i == std::wstring::npos ? L"." : s.substr(0, i);
}

// ── embedded runtime extraction: UI + WebView2 loader live inside the exe ──
std::wstring TempRoot() {
  wchar_t t[MAX_PATH]{};
  GetTempPathW(MAX_PATH, t);
  std::wstring dir = std::wstring(t) + L"PRIMEx\\";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

// Decode one hex blob (2 chars per byte) into a byte vector.
static bool HexDecode(const char *hex, std::size_t hexLen,
                      std::vector<unsigned char> &out) {
  static const auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  out.clear();
  out.reserve(hexLen / 2);
  for (std::size_t i = 0; i + 1 < hexLen; i += 2) {
    int hi = nib(hex[i]), lo = nib(hex[i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out.push_back((unsigned char)((hi << 4) | lo));
  }
  return true;
}

// Materialise the embedded dist/ tree under %TEMP%\PRIMEx\ui and return it.
std::wstring ExtractUi() {
  std::wstring root = TempRoot() + L"ui\\";
  CreateDirectoryW(root.c_str(), nullptr);
  for (std::size_t i = 0; i < kUiPackCount && kUiPackTable[i].rel; ++i) {
    const UiPackFile &f = kUiPackTable[i];
    std::vector<unsigned char> bytes;
    if (!HexDecode(f.hex, std::strlen(f.hex), bytes))
      continue;
    std::wstring rel = px::Widen(f.rel);
    for (auto &ch : rel)
      if (ch == L'/')
        ch = L'\\';
    std::wstring path = root + rel;
    // ensure parent dirs exist
    std::size_t pos = root.size();
    while ((pos = path.find(L'\\', pos)) != std::wstring::npos) {
      std::wstring dir = path.substr(0, pos);
      CreateDirectoryW(dir.c_str(), nullptr);
      pos++;
    }
    HANDLE h =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
      continue;
    DWORD written = 0;
    WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(h);
  }
  return root;
}

std::wstring FindDist() {
  // Prefer a local dist\ beside the exe (non-standalone ship layout).
  // Embedded UI remains only as a fallback for single-file dev builds.
  std::wstring base = ExeDir();
  const wchar_t *local[] = {
      L"\\dist",
      L"\\..\\..\\frontend\\dist",
      L"\\..\\..\\..\\..\\frontend\\dist",
  };
  for (auto c : local) {
    std::wstring p = base + c;
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
      DWORD html = GetFileAttributesW((p + L"\\index.html").c_str());
      if (html != INVALID_FILE_ATTRIBUTES)
        return p;
    }
  }
  if (kUiPackCount > 0) {
    std::wstring ex = ExtractUi();
    DWORD a = GetFileAttributesW((ex + L"index.html").c_str());
    if (a != INVALID_FILE_ATTRIBUTES)
      return ex;
  }
  return base + L"\\dist";
}

HMODULE LoadWebView2Loader() {
  // Prefer the DLL shipped next to the exe, then the embedded copy extracted
  // to %TEMP%, then the system search path.
  std::wstring local = ExeDir() + L"\\WebView2Loader.dll";
  HMODULE h = LoadLibraryW(local.c_str());
  if (!h && kWebView2LoaderLen > 0) {
    std::vector<unsigned char> bytes;
    if (HexDecode(kWebView2LoaderHex, kWebView2LoaderLen * 2, bytes)) {
      std::wstring path = TempRoot() + L"WebView2Loader.dll";
      HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (hf != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hf, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
        CloseHandle(hf);
        h = LoadLibraryW(path.c_str());
      }
    }
  }
  if (!h)
    h = LoadLibraryW(L"WebView2Loader.dll");
  return h;
}

void OnIpcMessage(const std::wstring &jsonW) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(px::Narrow(jsonW));
  } catch (...) {
    return;
  }
  long long id = root.value("id", -1LL);
  std::string cmd = root.value("cmd", "");
  nlohmann::json args =
      root.contains("args") ? root["args"] : nlohmann::json::object();

  if (cmd.rfind("win.", 0) == 0) {
    // window chrome runs on the UI thread (WebView2 events already fire
    // on the creating thread)
    px::host::HandleWindowCommand(cmd, args, g_hwnd);
    px::host::Respond(id, nlohmann::json::object(), true);
    return;
  }
  // everything else on a worker (mirrors C# Task.Run DispatchAsync)
  std::async(std::launch::async, [id, cmd, args] {
    try {
      nlohmann::json data = px::host::Dispatch(cmd, args);
      px::host::Respond(id, data, true);
    } catch (std::exception &e) {
      px::host::Respond(id, nlohmann::json::object(), false, e.what());
    }
  });
}

void InitWebView(HWND hwnd) {
  wchar_t *dev = nullptr;
  size_t devLen = 0;
  _wdupenv_s(&dev, &devLen, L"PRIMEUX_DEV");
  bool isDev = dev && *dev;
  if (dev)
    free(dev);

  HMODULE loader = LoadWebView2Loader();
  if (!loader) {
    MessageBoxW(hwnd,
                L"WebView2Loader.dll not found.\n\nPRIMEx normally runs "
                L"self-contained; the WebView2 Runtime is still required "
                L"(ships with Microsoft Edge).",
                L"ORBIT OPTIMIZER", MB_OK | MB_ICONERROR);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return;
  }
  auto createEnv = (PFN_CreateEnvWithOptions)GetProcAddress(
      loader, "CreateCoreWebView2EnvironmentWithOptions");
  if (!createEnv) {
    MessageBoxW(hwnd, L"WebView2 loader entry point missing.",
                L"ORBIT OPTIMIZER", MB_OK | MB_ICONERROR);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return;
  }
  EnvHandler *h = new EnvHandler(hwnd, isDev);
  HRESULT hr = createEnv(nullptr, nullptr, nullptr, h);
  h->Release();
  if (FAILED(hr)) {
    MessageBoxW(hwnd, L"WebView2 runtime missing or broken.\n\nInstall "
                       L"the WebView2 Runtime (ships with Edge).",
                L"ORBIT OPTIMIZER", MB_OK | MB_ICONERROR);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
}

#endif // HAVE_WEBVIEW2

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_APP_SEND: {
    // UI thread hop for worker-thread sends (see SendToPage).
    std::string *copy = (std::string *)lp;
    if (copy) {
      if (g_web)
        g_web->PostWebMessageAsJson(px::Widen(*copy).c_str());
      delete copy;
    }
    return 0;
  }
  case WM_SIZE:
#if HAVE_WEBVIEW2
    if (g_ctrl) {
      RECT r{};
      GetClientRect(hwnd, &r);
      g_ctrl->put_Bounds(r);
    }
#endif
    px::host::PostEvent("win.state", {{"maximized", IsZoomed(hwnd) == TRUE}});
    return 0;
  case WM_NCCALCSIZE:
    if (wp == TRUE) {
      // borderless: strip the standard frame, keep client area
      return 0;
    }
    break;
  case WM_NCHITTEST: {
    // resizable borderless edges (8px grip like CanResizeWithGrip)
    RECT r{};
    GetWindowRect(hwnd, &r);
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    const int g = 8;
    bool left = x < r.left + g, right = x >= r.right - g;
    bool top = y < r.top + g, bottom = y >= r.bottom - g;
    if (top && left)
      return HTTOPLEFT;
    if (top && right)
      return HTTOPRIGHT;
    if (bottom && left)
      return HTBOTTOMLEFT;
    if (bottom && right)
      return HTBOTTOMRIGHT;
    if (left)
      return HTLEFT;
    if (right)
      return HTRIGHT;
    if (top)
      return HTTOP;
    if (bottom)
      return HTBOTTOM;
    return HTCLIENT;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  case WM_TIMER:
    // periodic anti-debug re-check (attach after startup, tools spawned late)
    if (wp == 1) {
      if (!px::guard::Poll()) {
        KillTimer(hwnd, 1);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Add/Remove Programs entry (Settings > Installed apps).
// ---------------------------------------------------------------------------
const wchar_t *kUninstallKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ORBIT OPTIMIZER";

unsigned long long TreeBytes(const std::wstring &dir) {
  unsigned long long total = 0;
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      total += TreeBytes(dir + L"\\" + fd.cFileName);
    else
      total += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return total;
}

void TreeDelete(const std::wstring &dir) {
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
      continue;
    std::wstring full = dir + L"\\" + fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      TreeDelete(full);
      RemoveDirectoryW(full.c_str());
    } else {
      DeleteFileW(full.c_str());
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  RemoveDirectoryW(dir.c_str());
}

void RegisterUninstallEntry() {
  // An installer-managed install owns the per-user (HKCU) entry; only write
  // the elevated (HKLM) entry for standalone runs so Settings never lists
  // the app twice.
  HKEY owned = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                    L"ORBIT OPTIMIZER",
                    0, KEY_READ, &owned) == ERROR_SUCCESS) {
    RegCloseKey(owned);
    return;
  }
  wchar_t exe[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
  std::wstring exeW(exe), dir = exeW;
  size_t slash = dir.find_last_of(L'\\');
  if (slash != std::wstring::npos) dir.resize(slash);
  unsigned long long bytes = 0;
  WIN32_FILE_ATTRIBUTE_DATA fad{};
  if (GetFileAttributesExW(exeW.c_str(), GetFileExInfoStandard, &fad))
    bytes += ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
  bytes += TreeBytes(dir + L"\\dist");
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0,
                      KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key,
                      nullptr) != ERROR_SUCCESS)
    return;
  auto setSz = [&key](const wchar_t *name, const std::wstring &value) {
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  };
  auto setDw = [&key](const wchar_t *name, unsigned long value) {
    RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&value), sizeof(value));
  };
  setSz(L"DisplayName", L"ORBIT OPTIMIZER");
  setSz(L"DisplayVersion", L"1.0");
  setSz(L"Publisher", L"PRIMEx");
  setSz(L"DisplayIcon", exeW);
  setSz(L"InstallLocation", dir);
  setSz(L"UninstallString", L"\"" + exeW + L"\" --uninstall");
  setSz(L"QuietUninstallString", L"\"" + exeW + L"\" --uninstall --quiet");
  setDw(L"NoModify", 1);
  setDw(L"NoRepair", 1);
  setDw(L"EstimatedSize", static_cast<unsigned long>(bytes / 1024ull));
  RegCloseKey(key);
}

int RunUninstall(bool quiet) {
  if (!quiet) {
    int r = MessageBoxW(nullptr,
                        L"Remove ORBIT OPTIMIZER from this PC?\n\n"
                        L"Tweaks you already applied stay applied.",
                        L"ORBIT OPTIMIZER", MB_YESNO | MB_ICONQUESTION);
    if (r != IDYES) return 0;
  }
  RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);
  wchar_t exe[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
    std::wstring exeW(exe), dir = exeW;
    size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos) dir.resize(slash);
    TreeDelete(dir + L"\\dist");
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\ORBIT OPTIMIZER.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (_wcsicmp(full.c_str(), exeW.c_str()) == 0) continue;
        DeleteFileW(full.c_str());
      } while (FindNextFileW(h, &fd));
      FindClose(h);
    }
    std::wstring pending = exeW + L".uninstall";
    if (MoveFileExW(exeW.c_str(), pending.c_str(), 0))
      MoveFileExW(pending.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    else
      MoveFileExW(exeW.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    RemoveDirectoryW(dir.c_str());
  }
  if (!quiet)
    MessageBoxW(nullptr,
                L"ORBIT OPTIMIZER has been removed.\n"
                L"Restart your PC to finish.",
                L"ORBIT OPTIMIZER", MB_OK | MB_ICONINFORMATION);
  return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  // Uninstall mode — Settings > Installed apps runs: "<exe>" --uninstall
  {
    int argc = 0;
    if (LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
      bool uninstall = false, quiet = false;
      for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--uninstall") == 0)
          uninstall = true;
        else if (_wcsicmp(argv[i], L"--quiet") == 0 ||
                 _wcsicmp(argv[i], L"/S") == 0)
          quiet = true;
      }
      LocalFree(argv);
      if (uninstall) return RunUninstall(quiet);
    }
  }
#if !HAVE_WEBVIEW2
  MessageBoxW(nullptr,
              L"WebView2 SDK headers not found.\n\n"
              L"Download Microsoft.Web.WebView2 into "
              L"native/thirdparty/webview2 (see native/README).",
              L"ORBIT OPTIMIZER", MB_OK | MB_ICONERROR);
  return 1;
#else
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  // Security gate before any UI: modified exe refuses to run (anti-tamper),
  // debuggers / reversing tools exit silently (anti-debug).
  {
    std::string reason;
    px::guard::CheckResult cr = px::guard::StartupCheck(reason);
    if (cr == px::guard::CheckResult::SilentExit) {
      CoUninitialize();
      return 0;
    }
    if (cr == px::guard::CheckResult::Tampered) {
      MessageBoxW(nullptr, px::Widen(reason).c_str(), L"ORBIT OPTIMIZER",
                  MB_OK | MB_ICONERROR);
      CoUninitialize();
      return 1;
    }
  }

  // Keep Settings > Installed apps current (app name, version, publisher).
  RegisterUninstallEntry();

  const wchar_t *kClass = L"PRIMExGlassHost";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  int W = 1120, H = 720;
  g_hwnd = CreateWindowExW(0, kClass, L"ORBIT OPTIMIZER",
                           WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                               WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                           (sw - W) / 2, (sh - H) / 2, W, H, nullptr, nullptr,
                           hInst, nullptr);
  if (!g_hwnd)
    return 1;

  // Rounded app boundary (Win11 DWM corner preference; no-op on older builds).
  {
    const DWORD kDwmwaCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
    const DWORD kRound = 2;                  // DWMWCP_ROUND
    const DWORD kRoundSmall = 3;             // DWMWCP_ROUNDSMALL
    DWORD pref = kRound;
    if (FAILED(DwmSetWindowAttribute(g_hwnd, kDwmwaCornerPreference, &pref,
                                     sizeof(pref)))) {
      pref = kRoundSmall;
      DwmSetWindowAttribute(g_hwnd, kDwmwaCornerPreference, &pref,
                            sizeof(pref));
    }
  }

  px::host::InitSender(SendToPage);
  px::host::StartAuthInitAsync(); // never blocks window creation

  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);
  InitWebView(g_hwnd);

  MSG m{};
  while (GetMessageW(&m, nullptr, 0, 0)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
#if HAVE_WEBVIEW2
  if (g_web) {
    g_web->Release();
    g_web = nullptr;
  }
  if (g_ctrl) {
    g_ctrl->Close();
    g_ctrl->Release();
    g_ctrl = nullptr;
  }
#endif
  CoUninitialize();
  return (int)m.wParam;
#endif
}
