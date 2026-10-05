// host.h — native Win32 + WebView2 shell (C++ replacement for
// shell/MainWindow.xaml[.cs]). The Vite+TS UI is untouched: same dist/,
// same IPC contract ({id,cmd,args} <-> {id,ok,data} + {type:event}).
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <string>
#include "json.hpp"

namespace px {
namespace host {

// Thread-safe send to the page. Must be initialized by main.cpp.
void InitSender(std::function<void(const std::string &)> sender);
void Post(const nlohmann::json &payload);
void PostEvent(const std::string &name, const nlohmann::json &payload);
void PostProgress(int pct, const std::string &msg);
void PostNotify(const std::string &title, const std::string &body);
void Respond(long long id, const nlohmann::json &data, bool ok,
             const std::string &error = "");

// Auth init state (mirrors App.xaml.cs static flags).
struct AuthInit {
  bool done = false;
  bool ok = false;
  bool offline = false;
  bool versionMismatch = false;
  std::string error;
};
AuthInit GetAuthInit();
void StartAuthInitAsync(); // never blocks the UI thread

// Dispatch one IPC command on a worker thread. Throws on error with a
// human-readable message (surfaced to TS as a rejected promise).
// progressCb streams PostProgress; winAction runs window ops on the UI thread.
nlohmann::json Dispatch(const std::string &cmd, const nlohmann::json &args);

// Window ops executed on the UI thread by main.cpp.
void HandleWindowCommand(const std::string &cmd, const nlohmann::json &args,
                         HWND hwnd);

// Hotkey fire: run booster + notify (mirrors AimFixViaIpc).
void OnHotkeyPressed();

// Admin check for sys.getInfo.
bool IsAdmin();

} // namespace host
} // namespace px
