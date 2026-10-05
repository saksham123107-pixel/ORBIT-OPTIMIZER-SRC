// util.h — shared helpers for the PRIMEx C++ engine (Win32, C++17).
// Frontend UI is untouched: this layer only implements the logic that
// used to live in shell/Services/*.cs, behind native/include/primex_core.h.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "reg.h" // px::W(), DupJson/OkMsg/ErrJson

namespace px {

// --- string conversions ------------------------------------------------
inline std::string Narrow(const std::wstring &w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n) - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
  return s;
}

inline std::wstring Widen(const std::string &s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n) - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

// --- admin --------------------------------------------------------------
inline bool IsAdmin() {
  BOOL admin = FALSE;
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  PSID group = nullptr;
  if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
    CheckTokenMembership(nullptr, group, &admin);
    FreeSid(group);
  }
  return admin == TRUE;
}

// --- hidden process runner (mirrors SystemManager.Run) ------------------
inline void RunHidden(const std::wstring &exe, const std::wstring &args) {
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, 8000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// --- local app-data path: %LOCALAPPDATA%\PRIMEx Optimizer ----------------
inline std::wstring AppDataDir() {
  wchar_t *p = nullptr;
  std::wstring dir;
  // Use SHGetKnownFolderPath without extra linkage: fallback to env.
  wchar_t buf[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH)
    dir = std::wstring(buf) + L"\\PRIMEx Optimizer";
  else
    dir = L".\\PRIMEx Optimizer";
  CreateDirectoryW(dir.c_str(), nullptr);
  (void)p;
  return dir;
}

} // namespace px
