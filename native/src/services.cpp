// services.cpp — list/configure Windows services for the Services tab.
#include "services.h"
#include "util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>

#include <algorithm>

namespace px {
namespace services {
namespace {

void Emit(PxProgressCb cb, void *user, int pct, const std::string &msg) {
  if (cb)
    cb(pct, msg.c_str(), user);
}

const char *StartTypeName(DWORD t) {
#ifndef SERVICE_DELAYED_AUTO_START
#define SERVICE_DELAYED_AUTO_START 0x00000010
#endif
  switch (t) {
  case SERVICE_AUTO_START:
    return "auto";
  case SERVICE_DELAYED_AUTO_START:
    return "delayed";
  case SERVICE_DEMAND_START:
    return "manual";
  case SERVICE_DISABLED:
    return "disabled";
  default:
    return "unknown";
  }
}
#ifndef SC_ENUM_PROCESS_INFO
#define SC_ENUM_PROCESS_INFO 0
#endif

const char *StatusName(DWORD s) {
  switch (s) {
  case SERVICE_RUNNING:
    return "running";
  case SERVICE_STOPPED:
    return "stopped";
  case SERVICE_START_PENDING:
    return "starting";
  case SERVICE_STOP_PENDING:
    return "stopping";
  case SERVICE_PAUSED:
    return "paused";
  default:
    return "other";
  }
}

// Curated start-type disables only (never force-stops). Matches the user's
// sc config start=disabled list — safe because running services keep running
// until reboot/stop; new starts are blocked.
const char *kSafeDisable[] = {
    "WSearch",      "AxInstSV",           "BITS",
    "BrokerInfrastructure", "BDESVC",    "AssignedAccessManagerSvc",
    "DiagTrack",    "CryptSvc",           "diagsvc",
    "DPS",          "WdiServiceHost",     "lfsvc",
    "iphlpsvc",     "Netlogon",           "PhoneSvc",
    "Spooler",      "SessionEnv",         "TermService",
    "UmRdpService", "SensorService",      "SCardSvr",
    "ScDeviceEnum", "SCPolicySvc",        "WbioSrvc",
    "WerSvc",       "workfolderssvc",     "XboxGipSvc",
    // curated additions
    "dmwappushservice", "PcaSvc",         "SysMain",
    "Fax",          "MapsBroker",         "RetailDemo",
    "WMPNetworkSvc","TrkWks",             "StiSvc",
    "wisvc",        "RemoteRegistry",     "RpcLocator",
    "SNMPTRAP",     "vds",                "MSDTC",
    "SDRSVC",       "smphost",            "wbengine",
    "XblAuthManager", "XblGameSave",      "XboxNetApiSvc",
    "UsoSvc",       "WaaSMedicSvc",
};

const char *kPackTelemetry[] = {
    "DiagTrack", "dmwappushservice", "WerSvc", "PcaSvc", "DPS",
    "WdiServiceHost", "WdiSystemHost", "diagsvc",
    "diagnosticshub.standardcollector.service", "InventorySvc",
    "wercplsupport", "TroubleshootingSvc", "DcpSvc",
};
const char *kPackUpdates[] = {
    "wuauserv", "UsoSvc", "WaaSMedicSvc", "DoSvc", "BITS",
    "edgeupdate", "edgeupdatem", "MicrosoftEdgeElevationService",
};
const char *kPackXbox[] = {
    "XblAuthManager", "XblGameSave", "XboxNetApiSvc", "XboxGipSvc",
};
const char *kPackStore[] = {
    "InstallService", "PushToInstall", "OneSyncSvc", "UserDataSvc",
    "UnistoreSvc", "PimIndexMaintenanceSvc", "MessagingService",
    "BcastDVRUserService", "WSearch", "MapsBroker", "wisvc",
};
const char *kPackPrintSensor[] = {
    "Spooler", "PrintNotify", "StiSvc", "WiaRpc", "WbioSrvc",
    "SensorService", "SensorDataService", "SensrSvc", "FrameServer", "WPDBusEnum",
};
const char *kPackRemote[] = {
    "RemoteRegistry", "TermService", "SessionEnv", "UmRdpService",
    "WinRM", "RemoteAccess", "RasMan", "RasAuto",
};
const char *kPackMisc[] = {
    "SysMain", "Fax", "MapsBroker", "RetailDemo", "WMPNetworkSvc",
    "PhoneSvc", "TrkWks", "StiSvc", "wisvc", "TabletInputService",
    "RpcLocator", "SNMPTRAP", "vds", "MSDTC", "SDRSVC", "smphost", "wbengine",
    "VSS", "SwPrv", "ssh-agent", "WbioSrvc", "BDESVC", "iphlpsvc",
};

bool SetStartTypeRaw(const std::wstring &svc, DWORD type) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm)
    return false;
  SC_HANDLE h = OpenServiceW(scm, svc.c_str(), SERVICE_CHANGE_CONFIG);
  bool ok = false;
  if (h) {
    ok = ChangeServiceConfigW(h, SERVICE_NO_CHANGE, type, SERVICE_NO_CHANGE,
                              nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr) != FALSE;
    CloseServiceHandle(h);
  }
  CloseServiceHandle(scm);
  return ok;
}

bool ControlRaw(const std::wstring &svc, DWORD op) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm)
    return false;
  SC_HANDLE h =
      OpenServiceW(scm, svc.c_str(), SERVICE_START | SERVICE_STOP |
                                         SERVICE_QUERY_STATUS);
  bool ok = false;
  if (h) {
    SERVICE_STATUS st{};
    ok = ControlService(h, op, &st) != FALSE;
    if (op == SERVICE_CONTROL_STOP && !ok &&
        GetLastError() == ERROR_SERVICE_NOT_ACTIVE)
      ok = true;
    CloseServiceHandle(h);
  }
  CloseServiceHandle(scm);
  return ok;
}

} // namespace

nlohmann::json List() {
  nlohmann::json arr = nlohmann::json::array();
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE |
                                                       SC_MANAGER_CONNECT);
  if (!scm)
    return arr;

  DWORD bytesNeeded = 0, count = 0, resume = 0;
  EnumServicesStatusExW(scm, (SC_ENUM_TYPE)SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                        SERVICE_STATE_ALL, nullptr, 0, &bytesNeeded, &count,
                        &resume, nullptr);
  if (bytesNeeded == 0) {
    CloseServiceHandle(scm);
    return arr;
  }
  std::vector<BYTE> buf(bytesNeeded + 4096);
  resume = 0;
  if (!EnumServicesStatusExW(scm, (SC_ENUM_TYPE)SC_ENUM_PROCESS_INFO,
                             SERVICE_WIN32, SERVICE_STATE_ALL, buf.data(),
                             (DWORD)buf.size(), &bytesNeeded, &count, &resume,
                             nullptr)) {
    CloseServiceHandle(scm);
    return arr;
  }

  auto *entries = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW *>(buf.data());
  for (DWORD i = 0; i < count; ++i) {
    const ENUM_SERVICE_STATUS_PROCESSW &e = entries[i];
    DWORD startType = SERVICE_NO_CHANGE;
    SC_HANDLE h = OpenServiceW(scm, e.lpServiceName,
                               SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (h) {
      DWORD need = 0;
      QueryServiceConfigW(h, nullptr, 0, &need);
      if (need) {
        std::vector<BYTE> cfgBuf(need);
        auto *cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(cfgBuf.data());
        if (QueryServiceConfigW(h, cfg, need, &need))
          startType = cfg->dwStartType;
      }
      CloseServiceHandle(h);
    }
    nlohmann::json row;
    row["name"] = Narrow(e.lpServiceName);
    row["displayName"] =
        e.lpDisplayName ? Narrow(e.lpDisplayName) : Narrow(e.lpServiceName);
    row["status"] = StatusName(e.ServiceStatusProcess.dwCurrentState);
    row["startType"] =
        startType == SERVICE_NO_CHANGE ? "unknown" : StartTypeName(startType);
    arr.push_back(row);
  }
  CloseServiceHandle(scm);
  return arr;
}

nlohmann::json Configure(const std::string &name, const std::string &action) {
  if (name.empty())
    throw std::runtime_error("No service name.");
  std::wstring w = Widen(name);
  std::string a = action;
  for (auto &c : a)
    c = (char)tolower((unsigned char)c);

  if (a == "disable") {
    if (!SetStartTypeRaw(w, SERVICE_DISABLED))
      throw std::runtime_error(
          "Could not change start type (run as administrator).");
    return {{"message", name + " start type set to disabled."}};
  }
  if (a == "manual") {
    if (!SetStartTypeRaw(w, SERVICE_DEMAND_START))
      throw std::runtime_error(
          "Could not change start type (run as administrator).");
    return {{"message", name + " start type set to manual."}};
  }
  if (a == "auto") {
    if (!SetStartTypeRaw(w, SERVICE_AUTO_START))
      throw std::runtime_error(
          "Could not change start type (run as administrator).");
    return {{"message", name + " start type set to automatic."}};
  }
  if (a == "start") {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
      throw std::runtime_error("Service manager unavailable.");
    SC_HANDLE h = OpenServiceW(scm, w.c_str(), SERVICE_START);
    bool ok = false;
    if (h) {
      ok = StartServiceW(h, 0, nullptr) != FALSE;
      if (!ok && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
        ok = true;
      CloseServiceHandle(h);
    }
    CloseServiceHandle(scm);
    if (!ok)
      throw std::runtime_error("Could not start " + name +
                               " (administrator required).");
    return {{"message", name + " start requested."}};
  }
  if (a == "stop") {
    if (!ControlRaw(w, SERVICE_CONTROL_STOP))
      throw std::runtime_error("Could not stop " + name +
                               " (administrator required).");
    return {{"message", name + " stop requested."}};
  }
  throw std::runtime_error("Unknown service action: " + action);
}

nlohmann::json SafeDisable(PxProgressCb cb, void *user) {
  const int total = (int)(sizeof(kSafeDisable) / sizeof(kSafeDisable[0]));
  int ok = 0, fail = 0;
  std::vector<std::string> fails;
  for (int i = 0; i < total; ++i) {
    const char *svc = kSafeDisable[i];
    Emit(cb, user, i * 100 / total,
         std::string("Setting start=disabled: ") + svc);
    if (SetStartTypeRaw(Widen(svc), SERVICE_DISABLED))
      ++ok;
    else {
      // missing services on some SKUs are fine
      ++fail;
      fails.push_back(svc);
    }
  }
  Emit(cb, user, 100, "Safe disable complete.");
  std::string msg = "Safe disable: " + std::to_string(ok) + " services set " +
                    "to disabled start type";
  if (fail)
    msg += " (" + std::to_string(fail) + " not present/accessible)";
  msg += ". Running services stay up until stopped or rebooted.";
  return {{"message", msg}, {"okCount", ok}, {"failCount", fail}};
}

namespace {
template <size_t N>
nlohmann::json DisableList(const char *(&list)[N], const std::string &label,
                           PxProgressCb cb, void *user) {
  int ok = 0, fail = 0;
  for (size_t i = 0; i < N; ++i) {
    const char *svc = list[i];
    Emit(cb, user, (int)(i * 100 / N),
         std::string("Setting start=disabled: ") + svc);
    if (SetStartTypeRaw(Widen(svc), SERVICE_DISABLED))
      ++ok;
    else
      ++fail;
  }
  Emit(cb, user, 100, label + " complete.");
  std::string msg = label + ": " + std::to_string(ok) +
                    " services set to disabled start type";
  if (fail)
    msg += " (" + std::to_string(fail) + " not present/accessible)";
  msg += ". Running services stay up until stopped or rebooted.";
  return {{"message", msg}, {"okCount", ok}, {"failCount", fail}};
}
} // namespace

nlohmann::json DisablePack(const std::string &pack, PxProgressCb cb,
                           void *user) {
  if (pack == "telemetry")
    return DisableList(kPackTelemetry, "telemetry", cb, user);
  if (pack == "updates")
    return DisableList(kPackUpdates, "updates", cb, user);
  if (pack == "xbox")
    return DisableList(kPackXbox, "xbox", cb, user);
  if (pack == "store")
    return DisableList(kPackStore, "store", cb, user);
  if (pack == "print-sensor")
    return DisableList(kPackPrintSensor, "print-sensor", cb, user);
  if (pack == "remote")
    return DisableList(kPackRemote, "remote", cb, user);
  if (pack == "misc")
    return DisableList(kPackMisc, "misc", cb, user);
  throw std::runtime_error("Unknown service pack '" + pack + "'.");
}

} // namespace services
} // namespace px
