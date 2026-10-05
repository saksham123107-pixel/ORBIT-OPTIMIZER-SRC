// system.cpp — port of RamBooster / Resolution / SystemManager / SysStats.
#include "system.h"
#include "util.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <pdhmsg.h>

#ifndef PDH_SUCCESS
#define PDH_SUCCESS ERROR_SUCCESS
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

namespace px {
namespace sys {
namespace {

std::wstring Lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), ::towlower);
  return s;
}

std::wstring StripExe(std::wstring n) {
  auto l = Lower(n);
  if (l.size() > 4 && l.compare(l.size() - 4, 4, L".exe") == 0)
    return n.substr(0, n.size() - 4);
  return n;
}

// CPU load via GetSystemTimes delta (0..100), null-able.
bool CpuLoad(double &out) {
  static ULONGLONG prevIdle = 0, prevKernel = 0, prevUser = 0;
  static bool first = true;
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user))
    return false;
  auto toUll = [](const FILETIME &f) {
    ULARGE_INTEGER u{};
    u.LowPart = f.dwLowDateTime;
    u.HighPart = f.dwHighDateTime;
    return u.QuadPart;
  };
  ULONGLONG i = toUll(idle), k = toUll(kernel), u = toUll(user);
  if (first) {
    prevIdle = i;
    prevKernel = k;
    prevUser = u;
    first = false;
    return false;
  }
  ULONGLONG sys = (k - prevKernel) + (u - prevUser);
  ULONGLONG idleD = i - prevIdle;
  prevIdle = i;
  prevKernel = k;
  prevUser = u;
  if (sys == 0)
    return false;
  out = (double)(sys - idleD) * 100.0 / (double)sys;
  if (out < 0) out = 0;
  if (out > 100) out = 100;
  // warm the second sample quickly on first real call
  return true;
}

std::string GpuName() {
  // Lightweight: first display device string (no LibreHardwareMonitor
  // dependency in C++; temps via PDH when available).
  DISPLAY_DEVICEW dd{};
  dd.cb = sizeof(dd);
  for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
    if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
      char buf[256]{};
      WideCharToMultiByte(CP_UTF8, 0, dd.DeviceString, -1, buf, sizeof(buf),
                          nullptr, nullptr);
      std::string s = buf;
      if (!s.empty())
        return s;
    }
  }
  return {};
}

// Per-counter PDH slot (each path needs its own query/counter).
struct PdhSlot {
  PDH_HQUERY q = nullptr;
  PDH_HCOUNTER c = nullptr;
  bool primed = false;
  bool failed = false;
};

bool PdhMax(PdhSlot &slot, const wchar_t *path, double &out,
            bool kelvinToC = false) {
  if (slot.failed)
    return false;
  if (!slot.q) {
    if (PdhOpenQueryW(nullptr, 0, &slot.q) != ERROR_SUCCESS) {
      slot.failed = true;
      return false;
    }
    if (PdhAddEnglishCounterW(slot.q, path, 0, &slot.c) != PDH_SUCCESS) {
      PdhCloseQuery(slot.q);
      slot.q = nullptr;
      slot.failed = true;
      return false;
    }
    // Warm two samples so the first real read can succeed immediately.
    PdhCollectQueryData(slot.q);
    Sleep(80);
    PdhCollectQueryData(slot.q);
    slot.primed = true;
  }
  if (PdhCollectQueryData(slot.q) != PDH_SUCCESS)
    return false;
  DWORD bufSize = 0, count = 0;
  PdhGetFormattedCounterArrayW(slot.c, PDH_FMT_DOUBLE, &bufSize, &count,
                               nullptr);
  if (bufSize == 0 || count == 0)
    return false;
  std::vector<BYTE> raw(bufSize);
  auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(raw.data());
  if (PdhGetFormattedCounterArrayW(slot.c, PDH_FMT_DOUBLE, &bufSize, &count,
                                   items) != PDH_SUCCESS)
    return false;
  double maxv = 0;
  bool any = false;
  for (DWORD i = 0; i < count; ++i) {
    if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
        items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
      continue;
    double v = items[i].FmtValue.doubleValue;
    if (!any || v > maxv) {
      maxv = v;
      any = true;
    }
  }
  if (!any)
    return false;
  if (kelvinToC) {
    if (maxv < 50.0 || maxv > 400.0)
      return false; // not Kelvin
    maxv = maxv - 273.15;
    if (maxv < 0 || maxv > 150)
      return false;
  }
  out = maxv;
  return true;
}

bool GpuLoad(double &out) {
  static PdhSlot slot;
  if (!PdhMax(slot, L"\\GPU Engine(*)\\Utilization Percentage", out))
    return false;
  if (out < 0) out = 0;
  if (out > 100) out = 100;
  return true;
}

bool CpuTemp(double &out) {
  static PdhSlot slot;
  if (!PdhMax(slot, L"\\Thermal Zone Information(*)\\Temperature", out, true))
    return false;
  return out >= 5 && out <= 120;
}

bool GpuTemp(double &out) {
  // Best-effort; many systems lack this counter — UI keeps null.
  static PdhSlot slot;
  if (PdhMax(slot, L"\\GPU Thermal Monitor(*)\\Temperature", out, true) &&
      out >= 0 && out <= 120)
    return true;
  return false;
}

} // namespace

void FlushRam() {
  // Mirror RamBooster.FlushCore: trim every working set via EmptyWorkingSet.
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
      do {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA,
                               FALSE, pe.th32ProcessID);
        if (h) {
          EmptyWorkingSet(h);
          CloseHandle(h);
        }
      } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
  }
  // Also ask the OS to trim our own working set.
  SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

void ResolutionGet(int &w, int &h) {
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
    w = (int)dm.dmPelsWidth;
    h = (int)dm.dmPelsHeight;
    return;
  }
  w = 1920;
  h = 1080;
}

bool ResolutionSet(int w, int h) {
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
    return false;
  dm.dmPelsWidth = (DWORD)w;
  dm.dmPelsHeight = (DWORD)h;
  dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
  return ChangeDisplaySettingsW(&dm, 0) == DISP_CHANGE_SUCCESSFUL;
}

void FlushDns() { RunHidden(L"ipconfig", L"/flushdns"); }
void ClearArp() { RunHidden(L"arp", L"-d *"); }

void RestartExplorer() {
  RunHidden(L"cmd.exe", L"/c taskkill /f /im explorer.exe & start explorer.exe");
}

void KillProcesses(const std::vector<std::wstring> &names) {
  std::vector<std::wstring> want;
  for (auto n : names)
    want.push_back(Lower(StripExe(n)));
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      std::wstring exe = Lower(pe.szExeFile);
      // strip .exe for comparison
      if (exe.size() > 4 && exe.compare(exe.size() - 4, 4, L".exe") == 0)
        exe = exe.substr(0, exe.size() - 4);
      for (auto &w : want) {
        if (_wcsicmp(exe.c_str(), w.c_str()) == 0) {
          HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
          if (h) {
            TerminateProcess(h, 0);
            CloseHandle(h);
          }
          break;
        }
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
}

std::vector<std::string> PrioritizeGames() {
  // Same order as Booster.cs — HD-Player (emulator) first.
  static const wchar_t *kGames[] = {
      L"HD-Player", L"cs2", L"VALORANT-Win64-Shipping",
      L"FortniteClient-Win64-Shipping", L"r5apex", L"GTA5", L"RainbowSix",
      L"ModernWarfare", L"Overwatch", L"RocketLeague"};
  std::vector<std::string> boosted;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return boosted;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      std::wstring exe = pe.szExeFile;
      if (exe.size() > 4 &&
          _wcsicmp(exe.substr(exe.size() - 4).c_str(), L".exe") == 0)
        exe = exe.substr(0, exe.size() - 4);
      for (auto g : kGames) {
        if (_wcsicmp(exe.c_str(), g) == 0) {
          HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                 pe.th32ProcessID);
          if (h) {
            SetPriorityClass(h, HIGH_PRIORITY_CLASS);
            CloseHandle(h);
          }
          std::string n = Narrow(g);
          if (std::find(boosted.begin(), boosted.end(), n) == boosted.end())
            boosted.push_back(n);
        }
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return boosted;
}

nlohmann::json SysStats() {
  using nlohmann::json;
  // Prime CPU sampler (first call returns null like a cold sensor read).
  double cpu = 0;
  bool hasCpu = CpuLoad(cpu);
  // second immediate sample for a stable reading
  if (!hasCpu) {
    Sleep(120);
    hasCpu = CpuLoad(cpu);
  }

  MEMORYSTATUSEX m{};
  m.dwLength = sizeof(m);
  double ramPct = 0, ramUsedGb = 0, ramTotalGb = 0;
  bool hasRam = false;
  if (GlobalMemoryStatusEx(&m) && m.ullTotalPhys > 0) {
    hasRam = true;
    ramTotalGb = (double)m.ullTotalPhys / 1073741824.0;
    ramUsedGb = (double)(m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0;
    ramPct = (double)m.dwMemoryLoad;
  }

  std::string gpu = GpuName();
  double gpuPct = 0, cpuT = 0, gpuT = 0;
  bool hasGpu = GpuLoad(gpuPct);
  bool hasCpuT = CpuTemp(cpuT);
  bool hasGpuT = GpuTemp(gpuT);

  json o;
  o["cpu"] = hasCpu ? json(cpu) : json(nullptr);
  o["cpuTemp"] = hasCpuT ? json(cpuT) : json(nullptr);
  o["gpu"] = hasGpu ? json(gpuPct) : json(nullptr);
  o["gpuTemp"] = hasGpuT ? json(gpuT) : json(nullptr);
  o["gpuName"] = gpu.empty() ? json(nullptr) : json(gpu);
  o["ramPct"] = hasRam ? json(ramPct) : json(nullptr);
  o["ramGb"] = hasRam ? json(ramUsedGb) : json(nullptr);
  o["ramTotal"] = hasRam ? json(ramTotalGb) : json(nullptr);
  return o;
}

std::wstring TempPath() {
  wchar_t buf[MAX_PATH]{};
  DWORD n = GetTempPathW(MAX_PATH, buf);
  if (n == 0 || n >= MAX_PATH)
    return L"C:\\Windows\\Temp\\";
  return buf;
}

unsigned long long TotalRamKb() {
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof(m);
  if (GlobalMemoryStatusEx(&m) && m.ullTotalPhys > 0)
    return m.ullTotalPhys / 1024;
  return 8388608ULL; // fallback 8 GB (mirrors C#)
}

} // namespace sys
} // namespace px
