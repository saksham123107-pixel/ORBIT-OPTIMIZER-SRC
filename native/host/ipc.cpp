// ipc.cpp — IPC router. C++ port of MainWindow.xaml.cs DispatchAsync +
// CleanBoost/AdvBoost/GameBoost/DeepClean/SessionDto/AimFixViaIpc.
// Command names and response shapes are identical so the TS UI works as-is.
#include "host.h"

#include "../src/aim.h"
#include "../src/auth.h"
#include "../src/booster.h"
#include "../src/cleaner.h"
#include "../src/debloat.h"
#include "../src/hotkey.h"
#include "../src/services.h"
#include "../src/system.h"
#include "../src/tweaks.h"
#include "../src/util.h"

#include <shellapi.h>
#include <shlobj.h>
#include <future>

namespace px {
namespace host {
namespace {

std::function<void(const std::string &)> g_sender;
std::mutex g_sendMu;

AuthInit g_authInit;
std::mutex g_authMu;

long long g_dragOX = 0, g_dragOY = 0;

nlohmann::json SessionDto(const auth::Session &s) {
  return auth::SessionJson(s);
}

// Premium-only features stay visible in the UI but are locked unless the
// GitHub-verified session is Lifetime (spec §6/§11: server authoritative,
// backend re-enforces the gate — never trust the client alone).
// Paid packs: full registry packs, network/CPU performance packs, mouse,
// and AIM engine. Core/Power/Personalization free tweaks stay open.
bool IsPremiumFeature(const std::string &what) {
  static const char *kPremium[] = {
      "primex-full", "mouse", "primex-performance",
      "network", "cpu-amd", "cpu-intel",
  };
  std::string base = what;
  auto sep = what.find("::");
  if (sep != std::string::npos)
    base = what.substr(0, sep);
  for (auto p : kPremium)
    if (base == p)
      return true;
  return false;
}

void RequirePremium() {
  if (auth::Current().tier != "Lifetime")
    throw std::runtime_error(
        "PREMIUM::This feature requires PRIMEx Premium.");
}

// Personalization and CPU Priority sections are free for everyone,
// even when the underlying pack (primex-full, cpu-amd/intel) is premium.
bool LockedCard(const std::string &what, const std::string &cat) {
  if (!IsPremiumFeature(what) && !tweaks::GroupPremium(what))
    return false;
  return !(cat == "Personalization" || cat == "CPU Priority");
}

bool LockedGroup(const std::string &what) {
  return LockedCard(what, tweaks::CardCategory(what));
}

std::string ArgStr(const nlohmann::json &args, const char *k) {
  if (args.contains(k) && args[k].is_string())
    return args[k].get<std::string>();
  return "";
}

// Dev-only IPC trace: set PRIMEUX_LOG_IPC=1 to append every command to
// %TEMP%\primex-ipc.log. Zero cost when unset.
void TraceCmd(const std::string &cmd) {
  static bool on = [] {
    wchar_t v[8]{};
    return GetEnvironmentVariableW(L"PRIMEUX_LOG_IPC", v, 8) > 0;
  }();
  if (!on)
    return;
  wchar_t tmp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tmp);
  std::wstring p = std::wstring(tmp) + L"primex-ipc.log";
  HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return;
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char line[256]{};
  int n = snprintf(line, sizeof(line), "%02d:%02d:%02d  %s\r\n", st.wHour,
                   st.wMinute, st.wSecond, cmd.c_str());
  DWORD w = 0;
  WriteFile(h, line, (DWORD)n, &w, nullptr);
  CloseHandle(h);
}

// Progress bridge: engine calls cb(pct,msg,user) synchronously on the
// worker thread; we forward to the page.
void EngineProgress(int pct, const char *msg, void *user) {
  (void)user;
  PostProgress(pct, msg ? msg : "");
}

nlohmann::json CleanBoost() {
  int n = 0;
  n += cleaner::CleanPath(sys::TempPath());
  n += cleaner::CleanPath(L"C:\\Windows\\Temp");
  n += cleaner::CleanPath(L"C:\\Windows\\Prefetch");
  sys::FlushRam();
  sys::FlushDns();
  return {{"message", "Clean & Boost complete - " + std::to_string(n) +
                          " items removed."}};
}

} // namespace

void InitSender(std::function<void(const std::string &)> sender) {
  g_sender = std::move(sender);
}

void Post(const nlohmann::json &payload) {
  std::lock_guard<std::mutex> lk(g_sendMu);
  if (g_sender)
    g_sender(payload.dump());
}

void PostEvent(const std::string &name, const nlohmann::json &payload) {
  Post({{"type", "event"}, {"name", name}, {"payload", payload}});
}

void PostProgress(int pct, const std::string &msg) {
  PostEvent("progress", {{"pct", pct}, {"msg", msg}});
}

void PostNotify(const std::string &title, const std::string &body) {
  PostEvent("notify", {{"title", title}, {"body", body}});
}

void Respond(long long id, const nlohmann::json &data, bool ok,
             const std::string &error) {
  if (ok)
    Post({{"id", id}, {"ok", true}, {"data", data}});
  else
    Post({{"id", id}, {"ok", false}, {"error", error}});
}

AuthInit GetAuthInit() {
  std::lock_guard<std::mutex> lk(g_authMu);
  return g_authInit;
}

void StartAuthInitAsync() {
  std::thread([] {
    std::string err;
    bool ok = auth::Init(err);
    std::lock_guard<std::mutex> lk(g_authMu);
    g_authInit.done = true;
    g_authInit.ok = ok;
    g_authInit.offline = !auth::NetworkOk() && !auth::VersionMismatch();
    g_authInit.versionMismatch = auth::VersionMismatch();
    g_authInit.error = auth::InitError();
  }).detach();
}

bool IsAdmin() { return px::IsAdmin(); }

void OnHotkeyPressed() {
  try {
    std::string msg = booster::Run(EngineProgress, nullptr);
    (void)msg;
  } catch (...) {
  }
  PostNotify("Hotkey", "Booster finished.");
}

nlohmann::json Dispatch(const std::string &cmd, const nlohmann::json &args) {
  using nlohmann::json;
  TraceCmd(cmd);
  if (cmd == "sys.getInfo") {
    int w = 0, h = 0;
    sys::ResolutionGet(w, h);
    return {{"width", w}, {"height", h}, {"admin", IsAdmin()}};
  }
  if (cmd == "sys.disk") {
    wchar_t winDir[MAX_PATH] = {};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring rootW(winDir, 2);
    rootW += L"\\";
    ULARGE_INTEGER total{}, avail{};
    if (!GetDiskFreeSpaceExW(rootW.c_str(), &avail, &total, nullptr))
      throw std::runtime_error("Disk info unavailable on this host.");
    std::string label;
    label.push_back((char)rootW[0]);
    label += ":\\";
    return {{"total", (unsigned long long)total.QuadPart},
            {"free", (unsigned long long)avail.QuadPart},
            {"label", label}};
  }
  if (cmd == "sys.restorePoint") {
    if (!IsAdmin())
      throw std::runtime_error(
          "Administrator required to create a restore point.");
    struct RpInfo {
      unsigned long dwEventType;
      unsigned long dwRestorePtType;
      wchar_t szDescription[256];
    };
    struct RpStatus {
      unsigned long nStatus;
      unsigned long lSequence;
    };
    using PfnSrSet = int(WINAPI *)(RpInfo *, RpStatus *);
    HMODULE h = LoadLibraryW(L"srclient.dll");
    if (!h)
      throw std::runtime_error(
          "System Restore is unavailable on this host.");
    auto fn = (PfnSrSet)GetProcAddress(h, "SRSetRestorePointW");
    if (!fn) {
      FreeLibrary(h);
      throw std::runtime_error(
          "System Restore is unavailable on this host.");
    }
    RpInfo info{};
    info.dwEventType = 10;  // BEGIN_SYSTEM_CHANGE
    info.dwRestorePtType = 0;  // APPLICATION_INSTALL
    const wchar_t *desc = L"ORBIT OPTIMIZER - safety restore point";
    wcsncpy(info.szDescription, desc, 255);
    info.szDescription[255] = L'\0';
    RpStatus st{};
    int ok = fn(&info, &st);
    FreeLibrary(h);
    if (!ok)
      throw std::runtime_error(
          "Restore point failed (status " + std::to_string(st.nStatus) +
          "). Is System Restore enabled for this drive?");
    return {{"message", "Restore point created."}};
  }
  if (cmd == "auth:getStatus") {
    AuthInit st = GetAuthInit();
    auth::Session s = auth::Current();
    json d = SessionDto(s);
    d["pending"] = !st.done;
    d["initOk"] = st.ok;
    d["offline"] = st.offline;
    d["versionMismatch"] = st.versionMismatch;
    d["initError"] = st.error;
    return d;
  }
  if (cmd == "auth:autoLogin") {
    bool ok = false;
    auth::Session s;
    std::string note;
    auth::AutoLogin(ok, s, note);
    json d = SessionDto(s);
    d["ok"] = ok;
    d["note"] = note;
    return d;
  }
  if (cmd == "auth:discordLogin") {
    bool ok=false; std::string err; auth::Session s;
    auth::DiscordLogin(ok, err, s);
    if (!ok) throw std::runtime_error(err);
    return SessionDto(s);
  }
  if (cmd == "auth:activateKey") {
    std::string k = ArgStr(args, "key");
    while (!k.empty() && isspace((unsigned char)k.front()))
      k.erase(k.begin());
    while (!k.empty() && isspace((unsigned char)k.back()))
      k.pop_back();
    if (k.empty())
      throw std::runtime_error("Paste a license key first.");
    bool ok = false;
    std::string err;
    auth::Session s;
    auth::Activate(k, ok, err, s);
    if (!ok)
      throw std::runtime_error(err);
    return SessionDto(s);
  }
  if (cmd == "auth:logout") {
    auth::Logout();
    return {{"message", "Signed out."}};
  }
  if (cmd == "auth:revalidate") {
    auth::Session s;
    auth::Revalidate(s);
    return SessionDto(s);
  }
  if (cmd == "sys.openUrl") {
    std::string url = ArgStr(args, "url");
    std::string l = url;
    for (auto &c : l) c = (char)tolower((unsigned char)c);
    if (l.rfind("https://", 0) != 0)
      throw std::runtime_error("Only https links allowed.");
    ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
    return {{"message", "Opened."}};
  }
  if (cmd == "aim.status") {
    return {{"hasBackup", aim::HasBackup()}};
  }
  if (cmd == "aimreg.apply") {
    RequirePremium(); // Advanced Aim Engine (spec §6)
    aim::ApplyAimReg();
    return {{"message",
             "AIM REG applied (your values). Log off + back on to load it."}};
  }
  if (cmd == "aimreg.revert") {
    if (!aim::Restore())
      throw std::runtime_error("No backup found — nothing to restore.");
    return {{"message",
             "Mouse settings restored. Log off + back on to load it."}};
  }
  if (cmd == "mouse.state") {
    bool a = true, p = true;
    aim::GetState(a, p);
    return {{"accel", a}, {"precision", p}};
  }
  if (cmd == "mouse.accel") {
    bool on = args.contains("on") ? args["on"].get<bool>() : true;
    aim::SetAccel(on);
    return {{"message", on ? "Acceleration curves on."
                           : "Acceleration OFF (flat 1:1). Log off to load."}};
  }
  if (cmd == "mouse.precision") {
    bool on = args.contains("on") ? args["on"].get<bool>() : true;
    aim::SetPrecision(on);
    return {{"message", on ? "Pointer precision on."
                           : "Pointer precision OFF. Log off to load."}};
  }
  if (cmd == "aim.apply") {
    RequirePremium(); // Advanced Aim Engine (spec §6)
    aim::Apply();
    return {{"message",
             "Aim smoothing applied. Log off + back on to load it."}};
  }
  if (cmd == "aim.restore") {
    if (!aim::Restore())
      throw std::runtime_error("No backup found — nothing to restore.");
    return {{"message",
             "Mouse settings restored. Log off + back on to load it."}};
  }
  if (cmd == "sys.stats") {
    return sys::SysStats();
  }
  if (cmd == "booster.run") {
    std::string msg = booster::Run(EngineProgress, nullptr);
    return {{"message", msg}};
  }
  if (cmd == "game.boost") {
    auto b = booster::PrioritizeGames();
    std::string who;
    if (b.empty())
      who = "no game running";
    else {
      for (size_t i = 0; i < b.size(); ++i) {
        if (i) who += ", ";
        who += b[i];
      }
    }
    return {{"message", "Priority boosted: " + who + "."}};
  }
  if (cmd == "clean.boost") {
    return CleanBoost();
  }
  if (cmd == "clean.adv") {
    sys::KillProcesses({L"OneDrive", L"GameBar", L"Widgets"});
    sys::FlushRam();
    sys::RestartExplorer();
    Sleep(1000);
    sys::FlushDns();
    sys::ClearArp();
    return {{"message", "Advanced Boost complete."}};
  }
  if (cmd == "clean.game") {
    sys::KillProcesses({L"chrome", L"msedge", L"firefox"});
    sys::KillProcesses({L"Spotify", L"Teams"});
    sys::FlushRam();
    return {{"message", "Game Boost complete — background apps closed."}};
  }
  if (cmd == "clean.deep") {
    const wchar_t *dirs[] = {nullptr, L"C:\\Windows\\Temp",
                             L"C:\\Windows\\Prefetch",
                             L"C:\\Windows\\SoftwareDistribution\\Download",
                             L"C:\\Windows\\Logs\\CBS",
                             L"C:\\Windows\\Minidump",
                             L"C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportArchive",
                             L"C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportQueue"};
    std::wstring tmp = sys::TempPath();
    int n = 0, i = 0, total = 8;
    n += cleaner::ForceCleanCount(tmp);
    PostProgress(++i * 100 / total, Narrow(L"Cleaning " + tmp + L"..."));
    for (int k = 1; k < 8; ++k) {
      n += cleaner::ForceCleanCount(dirs[k]);
      PostProgress(++i * 100 / total,
                   std::string("Cleaning ") + Narrow(dirs[k]) + "...");
    }
    // Local AppData caches
    wchar_t local[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local))) {
      std::wstring base = local;
      const wchar_t *subs[] = {
          L"\\Microsoft\\Windows\\INetCache",
          L"\\Microsoft\\Windows\\WER",
      };
      for (auto s : subs) {
        std::wstring p = base + s;
        n += cleaner::ForceCleanCount(p);
        PostProgress(++i * 100 / (total + 2), Narrow(L"Cleaning " + p + L"..."));
      }
      total += 2;
    }
    return {{"message",
             "Deep Clean complete - " + std::to_string(n) +
                 " items deleted (temp, prefetch, update cache, WER, logs)."}};
  }
  if (cmd == "hotkey.start") {
    int vk = args.contains("vk") ? args["vk"].get<int>() : 0;
    if (vk == 0)
      throw std::runtime_error("No key selected.");
    hotkey::SetCallback(
        [](void *) {
          // forward to the host trampoline (runs booster + notify)
          OnHotkeyPressed();
        },
        nullptr);
    std::string msg = hotkey::Start(vk);
    return {{"message", msg}};
  }
  if (cmd == "hotkey.stop") {
    std::string msg = hotkey::Stop();
    return {{"message", msg}};
  }
  if (cmd == "hotkey.status") {
    return {{"running", hotkey::IsRunning()},
            {"bound", hotkey::BoundName()}};
  }
  if (cmd == "tweaks.groups") {
    json arr = tweaks::ListGroups();
    // add per-group backup flag exactly like the C# projection
    for (auto &g : arr) {
      std::string id = g.value("id", "");
      g["backup"] = tweaks::HasBackup(Widen(id));
      g["premium"] = LockedCard(id, g.value("category", ""));
      g["action"] = tweaks::GroupAction(id);
    }
    return arr;
  }
  if (cmd == "tweaks.status") {
    return tweaks::StatusAll();
  }
  if (cmd == "tweaks.apply") {
    std::string id = ArgStr(args, "id");
    if (id.empty())
      throw std::runtime_error("No group id.");
    if (LockedGroup(id))
      RequirePremium();
    std::string msg = tweaks::ApplyGroup(Widen(id), EngineProgress, nullptr);
    return {{"message", msg}};
  }
  if (cmd == "tweaks.entries") {
    std::string id = ArgStr(args, "id");
    if (id.empty())
      throw std::runtime_error("No group id.");
    return tweaks::ListEntries(Widen(id));
  }
  if (cmd == "tweaks.applyEntries") {
    std::string id = ArgStr(args, "id");
    if (id.empty())
      throw std::runtime_error("No group id.");
    if (!args.contains("entries") || !args["entries"].is_array() ||
        args["entries"].empty())
      throw std::runtime_error("No entries selected.");
    if (LockedGroup(id))
      RequirePremium();
    std::string msg = tweaks::ApplyEntryList(Widen(id),
                                             args["entries"].dump(),
                                             EngineProgress, nullptr);
    return {{"message", msg}};
  }
  if (cmd == "tweaks.revert") {
    std::string id = ArgStr(args, "id");
    if (id.empty())
      throw std::runtime_error("No group id.");
    std::string msg = tweaks::RevertGroup(Widen(id), EngineProgress, nullptr);
    return {{"message", msg}};
  }
  if (cmd == "debloat.list") {
    return debloat::ListPackages();
  }
  if (cmd == "debloat.presets") {
    return debloat::Presets();
  }
  if (cmd == "debloat.remove") {
    if (!args.contains("packages") || !args["packages"].is_array() ||
        args["packages"].empty())
      throw std::runtime_error("No packages selected.");
    if (!IsAdmin())
      throw std::runtime_error(
          "Administrator required — run PRIMEx as admin to remove apps.");
    std::vector<std::string> pkgs;
    for (auto &p : args["packages"]) {
      if (p.is_string())
        pkgs.push_back(p.get<std::string>());
    }
    return debloat::Remove(pkgs, EngineProgress, nullptr);
  }
  if (cmd == "services.list") {
    return services::List();
  }
  if (cmd == "services.set") {
    std::string name = ArgStr(args, "name");
    std::string action = ArgStr(args, "action");
    if (name.empty())
      throw std::runtime_error("No service name.");
    if (action.empty())
      throw std::runtime_error("No service action.");
    if (!IsAdmin())
      throw std::runtime_error(
          "Administrator required — run PRIMEx as admin to change services.");
    return services::Configure(name, action);
  }
  if (cmd == "services.safeDisable") {
    if (!IsAdmin())
      throw std::runtime_error(
          "Administrator required — run PRIMEx as admin to disable services.");
    return services::SafeDisable(EngineProgress, nullptr);
  }
  if (cmd == "services.disablePack") {
    std::string pack = ArgStr(args, "pack");
    if (pack.empty())
      throw std::runtime_error("No service pack.");
    if (!IsAdmin())
      throw std::runtime_error(
          "Administrator required — run PRIMEx as admin to disable services.");
    return services::DisablePack(pack, EngineProgress, nullptr);
  }
  throw std::runtime_error("Unknown command '" + cmd + "'.");
}

void HandleWindowCommand(const std::string &cmd, const nlohmann::json &args,
                         HWND hwnd) {
  if (cmd == "win.min") {
    ShowWindow(hwnd, SW_MINIMIZE);
  } else if (cmd == "win.toggle") {
    if (IsZoomed(hwnd))
      ShowWindow(hwnd, SW_RESTORE);
    else
      ShowWindow(hwnd, SW_MAXIMIZE);
  } else if (cmd == "win.close") {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  } else if (cmd == "win.drag") {
    // classic borderless drag: release capture + send caption hit-test
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
  } else if (cmd == "win.dragstart") {
    if (IsZoomed(hwnd))
      return;
    RECT r{};
    GetWindowRect(hwnd, &r);
    long x = args.contains("x") ? args["x"].get<long>() : r.left;
    long y = args.contains("y") ? args["y"].get<long>() : r.top;
    g_dragOX = x - r.left;
    g_dragOY = y - r.top;
  } else if (cmd == "win.dragmove") {
    if (IsZoomed(hwnd))
      return;
    long x = args.contains("x") ? args["x"].get<long>() : 0;
    long y = args.contains("y") ? args["y"].get<long>() : 0;
    SetWindowPos(hwnd, nullptr, (int)(x - g_dragOX), (int)(y - g_dragOY), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  PostEvent("win.state", {{"maximized", IsZoomed(hwnd) == TRUE}});
}

} // namespace host
} // namespace px
