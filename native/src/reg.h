// reg.h — small WinReg + JSON helpers shared by engine sources.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "json.hpp"
#include "primex_core.h"

namespace px {

using nlohmann::json;

inline char *DupJson(const json &o) {
  const std::string s = o.dump();
  char *p = static_cast<char *>(std::malloc(s.size() + 1));
  if (p)
    std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

inline char *OkMsg(const std::string &message) {
  return DupJson(json{{"ok", true}, {"message", message}});
}

inline char *ErrJson(const std::string &error) {
  return DupJson(json{{"ok", false}, {"error", error}});
}

inline std::wstring W(const wchar_t *p) { return p ? std::wstring(p) : L""; }

inline HKEY HiveOf(const std::string &hive) {
  if (hive == "HKLM")
    return HKEY_LOCAL_MACHINE;
  return HKEY_CURRENT_USER;
}

// hex string (no separators) -> bytes
inline std::vector<uint8_t> FromHex(const std::string &hex) {
  std::string h;
  h.reserve(hex.size());
  for (char c : hex) {
    if (std::isxdigit(static_cast<unsigned char>(c)))
      h.push_back(c);
  }
  if (h.size() % 2 == 1)
    h = "0" + h;
  std::vector<uint8_t> out;
  out.reserve(h.size() / 2);
  for (size_t i = 0; i < h.size(); i += 2)
    out.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
  return out;
}

inline std::string ToHex(const uint8_t *data, size_t len) {
  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(digits[data[i] >> 4]);
    out.push_back(digits[data[i] & 15]);
  }
  return out;
}

inline std::vector<std::wstring> SplitMulti(const std::wstring &s) {
  std::vector<std::wstring> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find(L'\0', start);
    if (end == std::wstring::npos)
      end = s.size();
    if (end > start)
      out.push_back(s.substr(start, end - start));
    if (end == s.size())
      break;
    start = end + 1;
  }
  return out;
}

inline void Emit(PxProgressCb cb, void *user, int pct,
                 const std::string &msg) {
  if (cb) {
    // px_free-compatible lifetime: callback must copy msg synchronously
    // (matches primex_core.h: cb(pct, msg_utf8, user)).
    cb(pct, msg.c_str(), user);
  }
}

} // namespace px
