// debloat.cpp — list/remove AppX packages for the Debloat tab.
#include "debloat.h"
#include "util.h"

#include <windows.h>

#include <algorithm>
#include <sstream>

namespace px {
namespace debloat {
namespace {

// Capture hidden PowerShell stdout (UTF-8). Empty string on failure.
std::string RunPowerShell(const std::wstring &script, DWORD timeoutMs = 60000) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE outR = nullptr, outW = nullptr;
  if (!CreatePipe(&outR, &outW, &sa, 0))
    return {};
  SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = outW;
  si.hStdError = outW;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};

  std::wstring cmd =
      L"powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy "
      L"Bypass -Command \"" +
      script + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);

  BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(outW);
  if (!ok) {
    CloseHandle(outR);
    return {};
  }

  std::string out;
  char tmp[4096];
  DWORD n = 0;
  while (ReadFile(outR, tmp, sizeof(tmp), &n, nullptr) && n > 0)
    out.append(tmp, n);
  WaitForSingleObject(pi.hProcess, timeoutMs);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(outR);

  // strip UTF-8 BOM
  if (out.rfind("\xEF\xBB\xBF", 0) == 0)
    out.erase(0, 3);
  return out;
}

std::string EscapeSingle(const std::string &s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '\'')
      o += "''";
    else
      o += c;
  }
  return o;
}

void Emit(PxProgressCb cb, void *user, int pct, const std::string &msg) {
  if (cb)
    cb(pct, msg.c_str(), user);
}

} // namespace

nlohmann::json Presets() {
  // Dynamic patterns for live package scanning (frontend matches against installed packages).
  return nlohmann::json::array({
      {
          {"id", "xbox"},
          {"label", "Xbox & gaming stubs"},
          {"patterns",
           {"Microsoft.GamingApp", "Microsoft.XboxApp", "Microsoft.XboxGamingOverlay",
            "Microsoft.XboxIdentityProvider", "Microsoft.XboxSpeechToTextOverlay",
            "Microsoft.XboxTcUI", "Microsoft.GamingServices", "Microsoft.XboxGameCallableUI"}},
      },
      {
          {"id", "social"},
          {"label", "Social & phone-link stubs"},
          {"patterns",
           {"Microsoft.SkypeApp", "Microsoft.People", "Microsoft.YourPhone",
            "Microsoft.WindowsCommunicationsApps", "MicrosoftTeams"}},
      },
      {
          {"id", "bing"},
          {"label", "Bing content apps"},
          {"patterns",
           {"Microsoft.BingNews", "Microsoft.BingWeather", "Microsoft.BingFinance",
            "Microsoft.BingSports", "Microsoft.BingFoodAndDrink", "Microsoft.BingHealthAndFitness",
            "Microsoft.BingTravel", "Microsoft.GetHelp", "Microsoft.Getstarted",
            "Microsoft.MicrosoftOfficeHub", "Microsoft.MicrosoftSolitaireCollection",
            "Microsoft.WindowsFeedbackHub", "Microsoft.WindowsMaps", "Microsoft.WindowsAlarms",
            "Microsoft.WindowsSoundRecorder", "Microsoft.PowerAutomateDesktop", "Microsoft.Todos",
            "Microsoft.Clipchamp", "Microsoft.MicrosoftStickyNotes", "Microsoft.OutlookForWindows"}},
      },
      {
          {"id", "cortana-ai"},
          {"label", "Cortana / Windows AI stubs"},
          {"patterns",
           {"Microsoft.549981C3F5F10", "Microsoft.Windows.Copilot", "Microsoft.Copilot",
            "Microsoft.MicrosoftPCManager"}},
      },
  });
}

nlohmann::json ListPackages() {
  const wchar_t *ps =
      L"$ErrorActionPreference='SilentlyContinue'; "
      L"Get-AppxPackage | Select-Object Name,PackageFullName,DisplayName | "
      L"ConvertTo-Json -Compress -Depth 3";
  std::string raw = RunPowerShell(ps, 45000);
  nlohmann::json arr = nlohmann::json::array();
  if (raw.empty())
    return arr;
  try {
    auto j = nlohmann::json::parse(raw);
    if (j.is_object())
      j = nlohmann::json::array({j});
    if (!j.is_array())
      return arr;
    for (auto &o : j) {
      if (!o.is_object())
        continue;
      nlohmann::json row;
      row["name"] = o.value("Name", "");
      row["fullName"] = o.value("PackageFullName", "");
      row["displayName"] =
          o.value("DisplayName", o.value("Name", std::string()));
      if (row["fullName"].get<std::string>().empty())
        continue;
      arr.push_back(row);
    }
  } catch (...) {
    // non-JSON (localized error) — return empty
  }
  return arr;
}

nlohmann::json Remove(const std::vector<std::string> &packageFullNames,
                      PxProgressCb cb, void *user) {
  if (packageFullNames.empty())
    throw std::runtime_error("No packages selected.");
  int removed = 0, failed = 0;
  int total = (int)packageFullNames.size();
  std::vector<std::string> fails;
  for (int i = 0; i < total; ++i) {
    const std::string &fn = packageFullNames[i];
    Emit(cb, user, i * 100 / (total ? total : 1),
         "Removing " + fn.substr(0, 80) + "...");
    std::wstring ps =
        L"$ErrorActionPreference='Stop'; try { Remove-AppxPackage -Package '" +
        Widen(EscapeSingle(fn)) + L"' -ErrorAction Stop; 'OK' } catch { 'ERR:' + "
        L"$_.Exception.Message }";
    std::string out = RunPowerShell(ps, 90000);
    bool ok = out.find("OK") != std::string::npos &&
              out.find("ERR:") == std::string::npos;
    // empty output can mean package already gone / no UI access
    if (ok)
      ++removed;
    else {
      ++failed;
      fails.push_back(fn);
    }
  }
  Emit(cb, user, 100,
       "Removed " + std::to_string(removed) + "/" + std::to_string(total));
  std::string msg = "Debloat complete: " + std::to_string(removed) +
                    " removed, " + std::to_string(failed) + " failed.";
  if (!fails.empty()) {
    msg += " Failed: ";
    for (size_t i = 0; i < fails.size() && i < 5; ++i) {
      if (i)
        msg += ", ";
      msg += fails[i];
    }
    if (fails.size() > 5)
      msg += "…";
  }
  return {{"message", msg},
          {"removed", removed},
          {"failed", failed}};
}

} // namespace debloat
} // namespace px
