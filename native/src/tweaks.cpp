// tweaks.cpp — port of shell/Services/TweaksService.cs to Win32 C++.
#include "tweaks.h"
#include "reg.h"
#include "system.h"
#include "util.h"

#include <windows.h>

#include <algorithm>
#include <codecvt>
#include <fstream>
#include <mutex>
#include <sstream>

namespace px {
namespace tweaks {
namespace {

std::wstring g_path;
nlohmann::json g_doc;
bool g_loaded = false;
std::mutex g_mu;

// Generated at build time (native/cmake/embed_tweaks.cmake):
// shell/Optimizer/tweaks.json hex-embedded into the exe so the shipped
// app has no external tweak file (spec §12: no external config files).
// kTweaksEmbeddedLen = decoded byte count; the C-string carries 2*N hex.
extern "C" const char kTweaksEmbeddedHex[];
extern "C" const std::size_t kTweaksEmbeddedLen;

std::string HexToStr(const std::string &hex) {
  std::string out;
  out.reserve(hex.size() / 2);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  for (size_t i = 0; i + 1 < hex.size(); i += 2)
    out.push_back((char)((nib(hex[i]) << 4) | nib(hex[i + 1])));
  return out;
}

std::wstring DefaultSearchPath() {
  // exe dir candidates (mirrors C# embedded resource -> now sidecar file).
  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring e = exe;
  auto slash = e.find_last_of(L"\\/");
  std::wstring dir = slash == std::wstring::npos ? L"." : e.substr(0, slash);
  const wchar_t *cands[] = {
      L"\\Optimizer\\tweaks.json",
      L"\\tweaks.json",
      L"\\dist\\tweaks.json",
  };
  for (auto c : cands) {
    std::wstring p = dir + c;
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
      return p;
  }
  // dev fallback: <repo>/shell/Optimizer/tweaks.json
  std::wstring dev = dir + L"\\..\\..\\shell\\Optimizer\\tweaks.json";
  if (GetFileAttributesW(dev.c_str()) != INVALID_FILE_ATTRIBUTES)
    return dev;
  return dir + L"\\Optimizer\\tweaks.json";
}

const nlohmann::json &Doc() {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_loaded)
    return g_doc;
  g_loaded = true;
  // 1) Explicit dev override (px_set_tweaks_path).
  if (!g_path.empty()) {
    std::ifstream f(g_path.c_str(), std::ios::binary);
    if (f) {
      std::stringstream ss;
      ss << f.rdbuf();
      g_doc = nlohmann::json::parse(ss.str());
      return g_doc;
    }
  }
  // 2) Build-embedded pack (production: shipped inside the exe).
  if (kTweaksEmbeddedLen > 0) {
    std::string json =
        HexToStr(std::string(kTweaksEmbeddedHex, kTweaksEmbeddedLen * 2));
    g_doc = nlohmann::json::parse(json);
    return g_doc;
  }
  // 3) Dev fallback: sidecar file next to the exe / repo.
  std::wstring path = DefaultSearchPath();
  std::ifstream f(path.c_str(), std::ios::binary);
  if (!f)
    throw std::runtime_error("tweaks pack missing: " + Narrow(path));
  std::stringstream ss;
  ss << f.rdbuf();
  g_doc = nlohmann::json::parse(ss.str());
  return g_doc;
}

std::string ToLower(std::string s) {
  for (auto &c : s) c = (char)tolower((unsigned char)c);
  return s;
}

bool RiskyCat(const std::string &cat) {
  static const char *k[] = {
      "MOUSE/KEYBOARD CLASS DRIVERS",
      "HD-PLAYER PRIORITY (IFEO)",
      "TELEMETRY / PRIVACY",
      "GAME MODE + GPU SCHEDULING + GAME DVR",
      "Power Tweaks",
      "Security",
      "Windows Update",
  };
  for (auto r : k)
    if (cat == r)
      return true;
  return false;
}

// Map pack title / entry cat → clean section category (never brand-prefixed).
std::string CleanCategory(const std::string &packTitle,
                          const std::string &entryCat) {
  if (entryCat == "MOUSE" || entryCat == "KEYBOARD" || entryCat == "DESKTOP" ||
      entryCat == "EXPLORER" || entryCat == "EXPLORER + TOUCH" ||
      entryCat == "VISUAL" || entryCat == "NOTIFICATIONS" || entryCat == "INPUT")
    return "Personalization";
  if (entryCat == "POWER" || entryCat == "CONTROLLER / USB / BT POWER")
    return "Power Tweaks";
  if (entryCat == "AUDIO LATENCY + MMCSS + GAMES TASK" ||
      entryCat == "GAMING" ||
      entryCat == "GAME MODE + GPU SCHEDULING + GAME DVR" ||
      entryCat == "MOUSE/KEYBOARD CLASS DRIVERS")
    return "Gaming";
  if (entryCat == "DPI / COMPAT FLAGS ON EMULATORS" ||
      entryCat == "BLUESTACKS REGISTRY" ||
      entryCat == "HD-PLAYER PRIORITY (IFEO)")
    return "Emulators";
  if (entryCat == "TELEMETRY / PRIVACY")
    return "Privacy";
  if (entryCat == "SYSTEM" || entryCat == "KERNEL / MEMORY" ||
      entryCat == "GPU / DISPLAY" || entryCat == "CPU / SCHEDULING" ||
      entryCat == "STARTUP")
    return "Core Optimizations";

  std::string p = packTitle;
  if (p.rfind("CPU Priority", 0) == 0)
    return "CPU Priority";
  std::string pl = ToLower(p);
  if (pl.find("network") != std::string::npos)
    return "Network";
  if (p.rfind("PRIMEx", 0) == 0)
    return "Core Optimizations";
  if (entryCat == "Input" || entryCat == "Windows Update" ||
      entryCat == "Security" || entryCat == "Bloat")
    return entryCat;
  return p.empty() ? std::string("Core Optimizations") : p;
}

struct Resolved {
  std::string hive, key, name;
  bool hasName = false;
  std::string type, cat;
  nlohmann::json value;
};

HKEY OpenHive(const std::string &hive) {
  if (hive == "HKLM")
    return HKEY_LOCAL_MACHINE;
  if (hive == "HKCU")
    return HKEY_CURRENT_USER;
  if (hive == "HKCR")
    return HKEY_CLASSES_ROOT;
  if (hive == "HKU")
    return HKEY_USERS;
  throw std::runtime_error("Unsupported hive '" + hive + "'.");
}

std::wstring BackupKeyFor(const std::wstring &group) {
  return L"SOFTWARE\\PRIMEx Optimizer\\TweaksBackup\\" + group;
}

std::vector<uint8_t> FromHex(const std::string &hexIn) {
  std::string h;
  for (char c : hexIn)
    if (isxdigit((unsigned char)c))
      h.push_back(c);
  if (h.size() % 2 == 1)
    h = "0" + h;
  std::vector<uint8_t> out;
  for (size_t i = 0; i < h.size(); i += 2)
    out.push_back((uint8_t)strtoul(h.substr(i, 2).c_str(), nullptr, 16));
  return out;
}

std::vector<std::wstring> DecodeMultiSz(const std::vector<uint8_t> &b) {
  std::vector<std::wstring> out;
  if (b.empty())
    return out;
  const wchar_t *p = (const wchar_t *)b.data();
  size_t n = b.size() / sizeof(wchar_t);
  size_t i = 0;
  while (i < n) {
    size_t j = i;
    while (j < n && p[j] != L'\0')
      ++j;
    if (j > i)
      out.emplace_back(p + i, p + j);
    i = j + 1;
    if (j < n && j + 1 < n && p[j + 1] == L'\0' && j == i - 1)
      break;
  }
  return out;
}

std::vector<std::wstring> NicSubkeys() {
  std::vector<std::wstring> out;
  HKEY k = nullptr;
  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
          0, KEY_ENUMERATE_SUB_KEYS, &k) == ERROR_SUCCESS) {
    wchar_t name[256]{};
    for (DWORD i = 0;; ++i) {
      DWORD n = 256;
      if (RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) !=
          ERROR_SUCCESS)
        break;
      out.emplace_back(name, n);
    }
    RegCloseKey(k);
  }
  return out;
}

// Find group by "id" or "id::Category".
const nlohmann::json *FindGroup(const std::string &id, std::string &catOut) {
  std::string base = id, cat;
  auto sep = id.find("::");
  if (sep != std::string::npos) {
    base = id.substr(0, sep);
    cat = id.substr(sep + 2);
  }
  for (auto &g : Doc()["groups"]) {
    if (g.value("id", "") == base) {
      catOut = cat;
      return &g;
    }
  }
  throw std::runtime_error("Unknown optimizer group '" + id + "'.");
}

std::vector<Resolved> Resolve(const nlohmann::json &group,
                              const std::string &onlyCat) {
  std::vector<Resolved> list;
  // NB: "dynamic" is null for static groups — value() would throw
  // type_error.302 on null, so read it defensively.
  std::string dyn;
  if (group.contains("dynamic") && group["dynamic"].is_string())
    dyn = group["dynamic"].get<std::string>();
  if (dyn == "svchost") {
    unsigned long long kb = sys::TotalRamKb();
    Resolved r;
    r.hive = "HKLM";
    r.key = "SYSTEM\\CurrentControlSet\\Control";
    r.name = "SvcHostSplitThresholdInKB";
    r.hasName = true;
    r.type = "REG_DWORD";
    r.value = kb;
    r.cat = "POWER";
    list.push_back(r);
    return list;
  }
  if (dyn == "nagle") {
    auto tmpl = Doc()["nagle"];
    auto nics = NicSubkeys();
    if (nics.empty())
      throw std::runtime_error("No network interfaces found.");
    for (auto &subW : nics) {
      std::string sub = Narrow(subW);
      for (auto &t : tmpl) {
        Resolved r;
        r.hive = "HKLM";
        r.key =
            "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" +
            sub;
        r.name = t.value("name", "");
        r.hasName = true;
        r.type = t.value("type", "REG_SZ");
        r.value = t["value"];
        r.cat = "NETWORK";
        list.push_back(r);
      }
    }
    return list;
  }
  for (auto &e : group["entries"]) {
    if (e.value("op", "") != "set")
      continue;
    std::string cat = e.value("cat", "MISC");
    if (!onlyCat.empty() && cat != onlyCat)
      continue;
    Resolved r;
    r.hive = e.value("hive", "HKCU");
    r.key = e.value("key", "");
    r.hasName = e.contains("name") && e["name"].is_string();
    r.name = r.hasName ? e["name"].get<std::string>() : "";
    r.type = e.value("type", "REG_SZ");
    r.value = e["value"];
    r.cat = cat;
    list.push_back(r);
  }
  return list;
}

void ApplyValue(HKEY root, const std::wstring &keyW, const Resolved &r) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(root, keyW.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                      nullptr, &k, nullptr) != ERROR_SUCCESS)
    throw std::runtime_error("cannot open key");
  std::wstring nameW = Widen(r.name);
  const wchar_t *nameP = r.hasName ? nameW.c_str() : L"";
  DWORD rc = ERROR_SUCCESS;
  if (r.type == "REG_SZ") {
    std::wstring v = Widen(r.value.get<std::string>());
    rc = RegSetValueExW(k, nameP, 0, REG_SZ, (const BYTE *)v.c_str(),
                        (DWORD)((v.size() + 1) * sizeof(wchar_t)));
  } else if (r.type == "REG_EXPAND_SZ") {
    std::wstring v = Widen(r.value.get<std::string>());
    rc = RegSetValueExW(k, nameP, 0, REG_EXPAND_SZ, (const BYTE *)v.c_str(),
                        (DWORD)((v.size() + 1) * sizeof(wchar_t)));
  } else if (r.type == "REG_DWORD") {
    DWORD v = (DWORD)r.value.get<unsigned long long>();
    rc = RegSetValueExW(k, nameP, 0, REG_DWORD, (BYTE *)&v, sizeof(v));
  } else if (r.type == "REG_QWORD") {
    unsigned long long v =
        strtoull(r.value.get<std::string>().c_str(), nullptr, 16);
    rc = RegSetValueExW(k, nameP, 0, REG_QWORD, (BYTE *)&v, sizeof(v));
  } else if (r.type == "REG_BINARY") {
    auto b = FromHex(r.value.get<std::string>());
    rc = RegSetValueExW(k, nameP, 0, REG_BINARY, b.data(), (DWORD)b.size());
  } else if (r.type == "HEX(7)") {
    auto b = FromHex(r.value.get<std::string>());
    auto parts = DecodeMultiSz(b);
    std::wstring multi;
    for (auto &p : parts) {
      multi += p;
      multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    rc = RegSetValueExW(k, nameP, 0, REG_MULTI_SZ, (const BYTE *)multi.c_str(),
                        (DWORD)(multi.size() * sizeof(wchar_t)));
  } else {
    RegCloseKey(k);
    throw std::runtime_error("Unsupported type '" + r.type + "'.");
  }
  RegCloseKey(k);
  if (rc != ERROR_SUCCESS)
    throw std::runtime_error("write failed");
}

// Run an imported action group payload: "command" via cmd.exe,
// "powershell" via a temp .ps1 (BOM'd UTF-8). Returns exit code, or -1
// with err set when the process could not be started/timed out.
int RunAction(const std::string &action, const std::string &payload,
              std::string &err) {
  err.clear();
  std::wstring line;
  std::wstring tmpFile;
  if (action == "powershell") {
    wchar_t tmpDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpDir);
    tmpFile = std::wstring(tmpDir) + L"px_" +
              std::to_wstring(GetCurrentProcessId()) + L"_" +
              std::to_wstring(GetTickCount()) + L".ps1";
    std::string data = "\xEF\xBB\xBF" + payload;
    HANDLE hf = CreateFileW(tmpFile.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
      err = "Could not write the temporary script.";
      return -1;
    }
    DWORD written = 0;
    WriteFile(hf, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(hf);
    line = L"powershell.exe -NoLogo -NoProfile -NonInteractive "
           L"-ExecutionPolicy Bypass -File \"" +
           tmpFile + L"\"";
  } else {
    // /s + outer quotes: run the payload literally even with inner quotes.
    line = L"cmd.exe /s /c \"" + Widen(payload) + L"\"";
  }
  std::vector<wchar_t> buf(line.begin(), line.end());
  buf.push_back(0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    err = "Could not start " + action + ".";
    if (!tmpFile.empty())
      DeleteFileW(tmpFile.c_str());
    return -1;
  }
  CloseHandle(pi.hThread);
  DWORD wait = WaitForSingleObject(pi.hProcess, 600000); // 10 min cap
  DWORD rc = 0;
  bool timedOut = (wait == WAIT_TIMEOUT);
  if (timedOut) {
    TerminateProcess(pi.hProcess, 1);
    err = "Timed out.";
  } else {
    GetExitCodeProcess(pi.hProcess, &rc);
  }
  CloseHandle(pi.hProcess);
  if (!tmpFile.empty())
    DeleteFileW(tmpFile.c_str());
  if (timedOut)
    return -1;
  return (int)rc;
}

// Apply an action group (imported command / powershell tweak).
std::string ApplyAction(const std::string &action, const nlohmann::json &g,
                        const std::wstring &groupId, PxProgressCb cb,
                        void *user) {
  std::string payload = g.value("command", "");
  if (payload.empty())
    throw std::runtime_error("Action is empty.");
  std::string title = g.value("title", Narrow(groupId));
  std::string revert = g.value("revert", "");
  if (!revert.empty()) {
    HKEY tmp = nullptr;
    std::wstring bk = BackupKeyFor(groupId);
    RegDeleteTreeW(HKEY_CURRENT_USER, bk.c_str());
    if (RegCreateKeyExW(HKEY_CURRENT_USER, bk.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &tmp, nullptr) ==
        ERROR_SUCCESS) {
      DWORD zero = 0;
      RegSetValueExW(tmp, L"count", 0, REG_DWORD, (BYTE *)&zero, sizeof(zero));
      std::wstring kindW = Widen(action);
      std::wstring cmdW = Widen(revert);
      RegSetValueExW(tmp, L"kind", 0, REG_SZ, (const BYTE *)kindW.c_str(),
                     (DWORD)((kindW.size() + 1) * sizeof(wchar_t)));
      RegSetValueExW(tmp, L"cmd", 0, REG_SZ, (const BYTE *)cmdW.c_str(),
                     (DWORD)((cmdW.size() + 1) * sizeof(wchar_t)));
      RegCloseKey(tmp);
    }
  }
  Emit(cb, user, 0, title);
  std::string err;
  int rc = RunAction(action, payload, err);
  Emit(cb, user, 100, title);
  if (rc < 0)
    throw std::runtime_error(err.empty() ? "Action failed." : err);
  if (rc != 0)
    return title + ": exited with code " + std::to_string(rc) + ".";
  return title + " applied." + (revert.empty() ? "" : " Revert available.");
}

// Does the live registry value already match the entry's target?
bool EntryApplied(const Resolved &e) {
  HKEY root = OpenHive(e.hive);
  HKEY k = nullptr;
  if (RegOpenKeyExW(root, Widen(e.key).c_str(), 0, KEY_QUERY_VALUE, &k) !=
      ERROR_SUCCESS)
    return false;
  std::wstring nameW = Widen(e.name);
  const wchar_t *np = e.hasName ? nameW.c_str() : L"";
  DWORD type = REG_NONE, sz = 0;
  if (RegQueryValueExW(k, np, nullptr, &type, nullptr, &sz) != ERROR_SUCCESS) {
    RegCloseKey(k);
    return false;
  }
  std::vector<BYTE> buf(sz + 2, 0);
  if (RegQueryValueExW(k, np, nullptr, &type, buf.data(), &sz) !=
      ERROR_SUCCESS) {
    RegCloseKey(k);
    return false;
  }
  RegCloseKey(k);
  if (e.type == "REG_DWORD")
    return type == REG_DWORD && sz >= 4 &&
           *(const DWORD *)buf.data() ==
               (DWORD)e.value.get<unsigned long long>();
  if (e.type == "REG_SZ" || e.type == "REG_EXPAND_SZ") {
    if (type != REG_SZ && type != REG_EXPAND_SZ)
      return false;
    std::wstring cur((const wchar_t *)buf.data(), sz / sizeof(wchar_t));
    while (!cur.empty() && cur.back() == L'\0')
      cur.pop_back();
    return _wcsicmp(cur.c_str(), Widen(e.value.get<std::string>()).c_str()) ==
           0;
  }
  if (e.type == "REG_BINARY" || e.type == "HEX(7)") {
    if (type != REG_BINARY && type != REG_MULTI_SZ)
      return false;
    std::string cur = ToLower(ToHex(buf.data(), sz));
    std::string want = e.value.get<std::string>();
    std::string norm;
    for (char c : want)
      if (isxdigit((unsigned char)c))
        norm.push_back((char)tolower((unsigned char)c));
    return cur == norm;
  }
  if (e.type == "REG_QWORD")
    return type == REG_QWORD && sz >= 8 &&
           *(const unsigned long long *)buf.data() ==
               strtoull(e.value.get<std::string>().c_str(), nullptr, 16);
  return false;
}

// Status of one card (id + optional "id::Category" split).
nlohmann::json CardStatus(const std::string &cardId, const nlohmann::json &g,
                          const std::string &onlyCat) {
  int matched = 0, total = 0;
  try {
    auto entries = Resolve(g, onlyCat);
    total = (int)entries.size();
    for (auto &e : entries)
      if (EntryApplied(e))
        ++matched;
  } catch (...) {
    total = 0;
    matched = 0;
  }
  return nlohmann::json{{"id", cardId},
                        {"applied", total > 0 && matched == total},
                        {"matched", matched},
                        {"total", total}};
}

std::string ApplyResolved(const std::wstring &groupIdW, const std::string &title,
                          std::vector<Resolved> &entries, PxProgressCb cb,
                          void *user) {
  if (entries.empty())
    throw std::runtime_error("Group is empty.");
  std::wstring groupId = groupIdW;
  // wipe previous backup + snapshot
  {
    HKEY tmp = nullptr;
    std::wstring bk = BackupKeyFor(groupId);
    RegDeleteTreeW(HKEY_CURRENT_USER, bk.c_str());
    if (RegCreateKeyExW(HKEY_CURRENT_USER, bk.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &tmp, nullptr) ==
        ERROR_SUCCESS) {
      for (size_t i = 0; i < entries.size(); ++i) {
        auto &e = entries[i];
        std::string si = std::to_string(i);
        HKEY root = OpenHive(e.hive);
        std::wstring keyW = Widen(e.key);
        HKEY k = nullptr;
        DWORD curType = REG_NONE;
        std::vector<uint8_t> cur;
        bool present = false;
        if (RegOpenKeyExW(root, keyW.c_str(), 0, KEY_QUERY_VALUE, &k) ==
            ERROR_SUCCESS) {
          std::wstring nameW = Widen(e.name);
          DWORD sz = 0;
          if (RegQueryValueExW(k, e.hasName ? nameW.c_str() : L"", nullptr,
                               &curType, nullptr, &sz) == ERROR_SUCCESS) {
            present = true;
            cur.resize(sz ? sz : 1);
            if (RegQueryValueExW(k, e.hasName ? nameW.c_str() : L"", nullptr,
                                 &curType, cur.data(), &sz) != ERROR_SUCCESS)
              present = false;
            else
              cur.resize(sz);
          }
          RegCloseKey(k);
        }
        auto setW = [&](const std::string &sfx, const std::wstring &v) {
          std::wstring n = Widen(si + sfx);
          RegSetValueExW(tmp, n.c_str(), 0, REG_SZ, (const BYTE *)v.c_str(),
                         (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        };
        auto setD = [&](const std::string &sfx, DWORD v) {
          std::wstring n = Widen(si + sfx);
          RegSetValueExW(tmp, n.c_str(), 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        };
        setD(".present", present ? 1 : 0);
        setW(".hive", Widen(e.hive));
        setW(".key", Widen(e.key));
        setW(".name", e.hasName ? Widen(e.name) : L"@");
        const char *kind = "Unknown";
        if (present) {
          switch (curType) {
          case REG_SZ: kind = "String"; break;
          case REG_EXPAND_SZ: kind = "ExpandString"; break;
          case REG_DWORD: kind = "DWord"; break;
          case REG_QWORD: kind = "QWord"; break;
          case REG_BINARY: kind = "Binary"; break;
          case REG_MULTI_SZ: kind = "MultiString"; break;
          }
        }
        setW(".kind", Widen(kind));
        if (present) {
          if (curType == REG_BINARY) {
            setW(".hex", Widen(ToHex(cur.data(), cur.size())));
          } else if (curType == REG_MULTI_SZ) {
            setW(".hex", Widen(ToHex(cur.data(), cur.size())));
          } else if (curType == REG_DWORD && cur.size() >= 4) {
            DWORD v = *(DWORD *)cur.data();
            setD(".dword", v);
          } else if (curType == REG_QWORD && cur.size() >= 8) {
            std::wstring n = Widen(si + ".qword");
            RegSetValueExW(tmp, n.c_str(), 0, REG_QWORD, cur.data(),
                           (DWORD)cur.size());
          } else {
            std::wstring s;
            if (curType == REG_SZ || curType == REG_EXPAND_SZ)
              s = std::wstring((wchar_t *)cur.data());
            else
              s = Widen(std::string((char *)cur.data(), cur.size()));
            setW(".str", s);
          }
        }
      }
      DWORD cnt = (DWORD)entries.size();
      RegSetValueExW(tmp, L"count", 0, REG_DWORD, (BYTE *)&cnt, sizeof(cnt));
      RegCloseKey(tmp);
    }
  }
  int done = 0, failed = 0;
  for (auto &e : entries) {
    try {
      ApplyValue(OpenHive(e.hive), Widen(e.key), e);
      ++done;
    } catch (...) {
      ++failed;
    }
    Emit(cb, user, entries.empty() ? 100 : (int)(done * 100 / entries.size()),
         std::to_string(done) + "/" + std::to_string(entries.size()));
  }
  return title + " applied: " + std::to_string(done) + " ok, " +
         std::to_string(failed) + " failed. Reboot recommended.";
}

} // namespace

void SetTweaksPath(const std::wstring &path) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_path = path;
  g_loaded = false;
  g_doc = nlohmann::json();
}

nlohmann::json ListGroups() {
  nlohmann::json out = nlohmann::json::array();
  for (auto &g : Doc()["groups"]) {
    std::string id = g.value("id", "");
    // Groups that carry an explicit category are one card each
    // (title = Content, desc = Description) — no per-entry split.
    if (g.contains("category") && g["category"].is_string()) {
      std::string cat = g["category"].get<std::string>();
      int n = g.contains("count") ? g["count"].get<int>()
                                  : (int)g["entries"].size();
      out.push_back({{"id", id},
                     {"title", g.value("title", id)},
                     {"desc", g.value("desc", "")},
                     {"category", cat},
                     {"danger", g.value("danger", false)},
                     {"count", n},
                     {"dynamic",
                      g.contains("dynamic") && g["dynamic"].is_string()
                          ? g["dynamic"]
                          : nlohmann::json(nullptr)},
                     {"backup", HasBackup(Widen(id))}});
      continue;
    }
    // split multi-category packs into one card per category (mirrors C#)
    std::vector<std::string> cats;
    for (auto &e : g["entries"]) {
      std::string c = e.value("cat", "MISC");
      if (std::find(cats.begin(), cats.end(), c) == cats.end())
        cats.push_back(c);
    }
    if (cats.size() > 1) {
      for (auto &cat : cats) {
        int n = 0;
        for (auto &e : g["entries"])
          if (e.value("cat", "MISC") == cat)
            ++n;
        bool danger = g.value("danger", false) && RiskyCat(cat);
        out.push_back({{"id", id + "::" + cat},
                       {"title", cat},
                       {"desc", std::to_string(n) + " keys"},
                       {"category", CleanCategory(g.value("title", id), cat)},
                       {"danger", danger},
                       {"count", n},
                       {"dynamic", nullptr},
                       {"backup", HasBackup(Widen(id + "::" + cat))}});
      }
      continue;
    }
    std::string singleEntry = cats.empty() ? std::string() : cats[0];
    out.push_back({{"id", id},
                   {"title", g.value("title", id)},
                   {"desc", g.value("desc", "")},
                   {"category", CleanCategory(g.value("title", ""), singleEntry)},
                   {"danger", g.value("danger", false)},
                   {"count", g.value("count", (int)g["entries"].size())},
                   {"dynamic",
                    g.contains("dynamic") && g["dynamic"].is_string()
                        ? g["dynamic"]
                        : nlohmann::json(nullptr)},
                   {"backup", HasBackup(Widen(id))}});
  }
  return out;
}

nlohmann::json ListEntries(const std::wstring &groupId) {
  std::string cat;
  const nlohmann::json *g = FindGroup(Narrow(groupId), cat);
  auto entries = Resolve(*g, cat);
  nlohmann::json out = nlohmann::json::array();
  for (auto &e : entries) {
    std::string display;
    if (e.type == "REG_DWORD") {
      unsigned long long v = e.value.get<unsigned long long>();
      char b[64]{};
      snprintf(b, sizeof(b), "0x%08llX (%llu)", v, v);
      display = b;
    } else if (e.type == "REG_SZ" || e.type == "REG_EXPAND_SZ") {
      display = e.value.get<std::string>();
    } else if (e.type == "REG_BINARY") {
      std::string h = e.value.get<std::string>();
      display =
          "hex:" + (h.size() > 48 ? h.substr(0, 48) + "..." : h);
    } else if (e.type == "HEX(7)") {
      auto parts = DecodeMultiSz(FromHex(e.value.get<std::string>()));
      display = "multi-sz: ";
      for (size_t i = 0; i < parts.size() && i < 3; ++i) {
        if (i) display += " | ";
        display += Narrow(parts[i]);
      }
    } else if (e.type == "REG_QWORD") {
      display = "0x" + e.value.get<std::string>();
    } else {
      display = e.value.dump();
    }
    nlohmann::json item;
    item["hive"] = e.hive;
    item["key"] = e.key;
    item["name"] = e.hasName ? nlohmann::json(e.name) : nlohmann::json(nullptr);
    item["type"] = e.type;
    item["cat"] = e.cat;
    item["value"] = e.value;
    item["display"] = display;
    out.push_back(item);
  }
  return out;
}

std::string ApplyGroup(const std::wstring &groupId, PxProgressCb cb, void *user) {
  std::string cat;
  const nlohmann::json *g = FindGroup(Narrow(groupId), cat);
  std::string action = g->value("action", "");
  if (!action.empty())
    return ApplyAction(action, *g, groupId, cb, user);
  auto entries = Resolve(*g, cat);
  std::string title;
  if (cat.empty())
    title = g->value("title", Narrow(groupId));
  else
    title = cat;
  return ApplyResolved(groupId, title, entries, cb, user);
}

std::string ApplyEntryList(const std::wstring &groupId,
                           const std::string &entriesJsonUtf8, PxProgressCb cb,
                           void *user) {
  auto arr = nlohmann::json::parse(entriesJsonUtf8);
  std::vector<Resolved> entries;
  for (auto &r : arr) {
    Resolved e;
    e.hive = r.value("hive", "HKCU");
    e.key = r.value("key", "");
    e.hasName = r.contains("name") && r["name"].is_string();
    e.name = e.hasName ? r["name"].get<std::string>() : "";
    e.type = r.value("type", "REG_SZ");
    e.value = r["value"];
    e.cat = r.value("cat", "MISC");
    entries.push_back(e);
  }
  if (entries.empty())
    throw std::runtime_error("No entries selected.");
  return ApplyResolved(groupId, "Custom selection", entries, cb, user);
}

bool HasBackup(const std::wstring &groupId) {
  HKEY k = nullptr;
  std::wstring bk = BackupKeyFor(groupId);
  if (RegOpenKeyExW(HKEY_CURRENT_USER, bk.c_str(), 0, KEY_QUERY_VALUE, &k) !=
      ERROR_SUCCESS)
    return false;
  DWORD type = 0, size = 0;
  // registry backups store per-entry snapshots (0.present + count);
  // action backups only store count/kind/cmd.
  bool ok =
      RegQueryValueExW(k, L"count", nullptr, &type, nullptr, &size) ==
          ERROR_SUCCESS ||
      RegQueryValueExW(k, L"0.present", nullptr, &type, nullptr, &size) ==
          ERROR_SUCCESS;
  RegCloseKey(k);
  return ok;
}

std::string RevertGroup(const std::wstring &groupId, PxProgressCb cb, void *user) {
  HKEY bak = nullptr;
  std::wstring bk = BackupKeyFor(groupId);
  if (RegOpenKeyExW(HKEY_CURRENT_USER, bk.c_str(), 0, KEY_QUERY_VALUE, &bak) !=
      ERROR_SUCCESS)
    throw std::runtime_error("No backup for this group — nothing to revert.");
  auto readRoot = [&](const wchar_t *nm) -> std::wstring {
    DWORD t = 0, sz = 0;
    if (RegQueryValueExW(bak, nm, nullptr, &t, nullptr, &sz) != ERROR_SUCCESS)
      return L"";
    if (t != REG_SZ && t != REG_EXPAND_SZ)
      return L"";
    std::vector<wchar_t> b(sz / sizeof(wchar_t) + 1, 0);
    if (RegQueryValueExW(bak, nm, nullptr, &t, (BYTE *)b.data(), &sz) !=
        ERROR_SUCCESS)
      return L"";
    return std::wstring(b.data());
  };
  // Action-group backups store kind + revert command instead of snapshots.
  std::wstring kindW = readRoot(L"kind");
  if (kindW == L"command" || kindW == L"powershell") {
    std::wstring cmdW = readRoot(L"cmd");
    RegCloseKey(bak);
    if (cmdW.empty())
      throw std::runtime_error("Stored revert action is missing.");
    std::string err;
    int rc = RunAction(Narrow(kindW), Narrow(cmdW), err);
    if (rc < 0)
      throw std::runtime_error(err.empty() ? "Revert action failed." : err);
    Emit(cb, user, 100, "reverted");
    return rc == 0 ? std::string("Reverted.")
                   : "Revert action exited with code " + std::to_string(rc) +
                         ".";
  }
  DWORD count = 0, size = sizeof(count);
  RegQueryValueExW(bak, L"count", nullptr, nullptr, (BYTE *)&count, &size);
  int done = 0;
  auto getW = [&](const std::string &sfx) -> std::wstring {
    wchar_t buf[1024]{};
    DWORD sz = sizeof(buf), t = 0;
    std::wstring n = Widen(std::to_string(0) + sfx); // placeholder
    (void)n;
    return L"";
  };
  (void)getW;
  auto readStr = [&](int i, const wchar_t *sfx) -> std::wstring {
    wchar_t buf[2048]{};
    DWORD sz = sizeof(buf), t = 0;
    std::wstring n = std::to_wstring(i) + sfx;
    if (RegQueryValueExW(bak, n.c_str(), nullptr, &t, (BYTE *)buf, &sz) ==
        ERROR_SUCCESS)
      return std::wstring(buf);
    return L"";
  };
  auto readDword = [&](int i, const wchar_t *sfx) -> DWORD {
    DWORD v = 0, sz = sizeof(v);
    std::wstring n = std::to_wstring(i) + sfx;
    RegQueryValueExW(bak, n.c_str(), nullptr, nullptr, (BYTE *)&v, &sz);
    return v;
  };
  for (DWORD i = 0; i < count; ++i) {
    try {
      std::string hive = Narrow(readStr((int)i, L".hive"));
      std::string key = Narrow(readStr((int)i, L".key"));
      std::wstring nameW = readStr((int)i, L".name");
      bool nullName = (nameW == L"@");
      int present = (int)readDword((int)i, L".present");
      std::string kind = Narrow(readStr((int)i, L".kind"));
      HKEY root = OpenHive(hive.empty() ? "HKCU" : hive);
      std::wstring keyW = Widen(key);
      if (present == 0) {
        HKEY k = nullptr;
        if (!nullName &&
            RegOpenKeyExW(root, keyW.c_str(), 0, KEY_SET_VALUE, &k) ==
                ERROR_SUCCESS) {
          RegDeleteValueW(k, nameW.c_str());
          RegCloseKey(k);
        }
      } else if (!nullName) {
        HKEY k = nullptr;
        if (RegCreateKeyExW(root, keyW.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                            nullptr, &k, nullptr) == ERROR_SUCCESS) {
          if (kind == "String")
            RegSetValueExW(
                k, nameW.c_str(), 0, REG_SZ,
                (const BYTE *)readStr((int)i, L".str").c_str(),
                (DWORD)((readStr((int)i, L".str").size() + 1) * sizeof(wchar_t)));
          else if (kind == "ExpandString") {
            std::wstring v = readStr((int)i, L".str");
            RegSetValueExW(k, nameW.c_str(), 0, REG_EXPAND_SZ,
                           (const BYTE *)v.c_str(),
                           (DWORD)((v.size() + 1) * sizeof(wchar_t)));
          } else if (kind == "DWord") {
            DWORD v = readDword((int)i, L".dword");
            RegSetValueExW(k, nameW.c_str(), 0, REG_DWORD, (BYTE *)&v,
                           sizeof(v));
          } else if (kind == "QWord") {
            uint64_t v = 0;
            DWORD sz = sizeof(v);
            std::wstring n = std::to_wstring(i) + L".qword";
            RegQueryValueExW(bak, n.c_str(), nullptr, nullptr, (BYTE *)&v,
                             &sz);
            RegSetValueExW(k, nameW.c_str(), 0, REG_QWORD, (BYTE *)&v,
                           sizeof(v));
          } else if (kind == "Binary" || kind == "MultiString") {
            DWORD rk = (kind == "Binary") ? REG_BINARY : REG_MULTI_SZ;
            auto b = FromHex(Narrow(readStr((int)i, L".hex")));
            RegSetValueExW(k, nameW.c_str(), 0, rk, b.data(), (DWORD)b.size());
          }
          RegCloseKey(k);
        }
      }
      ++done;
    } catch (...) {
    }
    Emit(cb, user, count == 0 ? 100 : (int)(done * 100 / (int)count),
         std::to_string(done) + "/" + std::to_string(count));
  }
  RegCloseKey(bak);
  return "Reverted. Reboot recommended.";
}

nlohmann::json StatusAll() {
  nlohmann::json out = nlohmann::json::array();
  for (auto &g : Doc()["groups"]) {
    std::string id = g.value("id", "");
    if (!g.value("action", "").empty()) {
      // command/powershell actions can't be probed → report as not applied.
      out.push_back({{"id", id},
                     {"applied", false},
                     {"matched", 0},
                     {"total", 0}});
      continue;
    }
    if (g.contains("category") && g["category"].is_string()) {
      out.push_back(CardStatus(id, g, ""));
      continue;
    }
    // mirror ListGroups: one card per distinct entry category
    std::vector<std::string> cats;
    if (g.contains("entries"))
      for (auto &e : g["entries"]) {
        std::string c = e.value("cat", "MISC");
        if (std::find(cats.begin(), cats.end(), c) == cats.end())
          cats.push_back(c);
      }
    if (cats.size() > 1) {
      for (auto &cat : cats)
        out.push_back(CardStatus(id + "::" + cat, g, cat));
    } else {
      out.push_back(CardStatus(id, g, ""));
    }
  }
  return out;
}

bool GroupPremium(const std::string &id) {
  try {
    std::string cat;
    const nlohmann::json *g = FindGroup(id, cat);
    return g->value("premium", false);
  } catch (...) {
    return false;
  }
}

std::string GroupAction(const std::string &id) {
  try {
    std::string cat;
    const nlohmann::json *g = FindGroup(id, cat);
    return g->value("action", "");
  } catch (...) {
    return "";
  }
}

std::string CardCategory(const std::string &cardId) {
  nlohmann::json arr = ListGroups();
  for (auto &g : arr)
    if (g.value("id", "") == cardId)
      return g.value("category", "");
  return "";
}

} // namespace tweaks
} // namespace px
