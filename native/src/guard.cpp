// guard.cpp — anti-debug + anti-tamper implementation.
#include "guard.h"
#include "crypto.h"
#include "util.h"

#include <windows.h>
#include <tlhelp32.h>

#include <fstream>
#include <string>
#include <vector>

namespace px {
namespace guard {
namespace {

// ── anti-tamper marker ────────────────────────────────────────────────────
// 16-byte magic + 32-byte SHA-256 digest in ONE packed object. Separate
// extern "C" arrays let the linker insert padding (e.g. 16 bytes) between
// them; the patcher then writes the hash into the gap and the runtime
// digest never matches. The digest is written over the 32 zeros by
// tools/selfhash/selfhash_tool.exe in a CMake POST_BUILD step.
// Check semantics: hash(exe bytes with the digest region zeroed) must equal
// the embedded digest. A zero digest means "not patched yet" (dev build) and
// skips the check so local builds keep working.
#pragma pack(push, 1)
struct SelfHashMarker {
  unsigned char magic[16];
  unsigned char digest[32];
};
#pragma pack(pop)

extern "C" const SelfHashMarker kSelfHashMarker = {
    {'P', 'X', 'S', 'H', '1', '7', '0', '0', 0x00, 0x00, 'Q', 0x7F, 'M',
     'K', '7', '7'},
    {0}};

constexpr size_t kMagicSize = 16;
constexpr size_t kDigestSize = 32;
static_assert(sizeof(SelfHashMarker) == kMagicSize + kDigestSize,
              "SelfHashMarker must be tightly packed");

bool DigestZeroed() {
  for (unsigned char b : kSelfHashMarker.digest)
    if (b != 0)
      return false;
  return true;
}

// Locate the magic inside raw exe memory; digest starts right after it.
bool FindMarker(const std::vector<unsigned char> &bytes, size_t &outOff) {
  if (bytes.size() < kMagicSize)
    return false;
  for (size_t i = 0; i + kMagicSize <= bytes.size(); ++i) {
    bool match = true;
    for (size_t k = 0; k < kMagicSize; ++k)
      if (bytes[i + k] != kSelfHashMarker.magic[k]) {
        match = false;
        break;
      }
    if (match) {
      outOff = i;
      return true;
    }
  }
  return false;
}

bool ReadOwnExe(std::vector<unsigned char> &out) {
  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::ifstream f(exe, std::ios::binary);
  if (!f)
    return false;
  out.assign(std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>());
  return !out.empty();
}

// 1 = ok, 0 = mismatch, -1 = not patched (skip)
int SelfIntegrity() {
  if (DigestZeroed())
    return -1; // dev build, patcher never ran
  std::vector<unsigned char> bytes;
  if (!ReadOwnExe(bytes))
    return -1;
  size_t off = 0;
  if (!FindMarker(bytes, off) || off + kMagicSize + kDigestSize > bytes.size())
    return 0; // marker deleted/modified => tampered
  for (size_t k = 0; k < kDigestSize; ++k)
    bytes[off + kMagicSize + k] = 0;
  std::string hash = Sha256Hex(std::string((const char *)bytes.data(), bytes.size()));
  if (hash.size() != kDigestSize * 2)
    return 0;
  for (size_t k = 0; k < kDigestSize; ++k) {
    static const char *d = "0123456789abcdef";
    char e0 = d[kSelfHashMarker.digest[k] >> 4];
    char e1 = d[kSelfHashMarker.digest[k] & 15];
    if (hash[k * 2] != e0 || hash[k * 2 + 1] != e1)
      return 0;
  }
  return 1;
}

// ── anti-debug ─────────────────────────────────────────────────────────────
// PEB BeingDebugged + NtGlobalFlag without ntdll exports.
bool PebDebugFlags() {
#if defined(_M_X64) || defined(__x86_64__)
  const auto peb = (const unsigned char *)__readgsqword(0x60);
#else
  const auto peb = (const unsigned char *)__readfsdword(0x30);
#endif
  if (!peb)
    return false;
  if (peb[2] & 0x01) // BeingDebugged
    return true;
  return false; // NtGlobalFlag check needs the offset; IsDebuggerPresent covers it
}

bool DirectDebugger() {
  if (IsDebuggerPresent())
    return true;
  if (PebDebugFlags())
    return true;
  BOOL remote = FALSE;
  if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote)
    return true;
  return false;
}

bool ToolRunning(const wchar_t *name) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  bool found = false;
  if (Process32FirstW(snap, &pe)) {
    do {
      std::wstring lower;
      for (auto c : std::wstring(pe.szExeFile))
        lower.push_back((wchar_t)towlower(c));
      if (lower == name) {
        found = true;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return found;
}

bool DebugToolsRunning() {
  // Known reverse-engineering / memory-editing tools. Lowercase matches.
  static const wchar_t *kTools[] = {
      L"x64dbg.exe", L"x32dbg.exe", L"ida.exe",   L"ida64.exe",
      L"idaq.exe",   L"idaq64.exe", L"ollydbg.exe", L"cheatengine.exe",
      L"cheatengine-x86_64.exe", L"scylla_x64.exe", L"scylla.exe"};
  for (auto t : kTools)
    if (ToolRunning(t))
      return true;
  return false;
}

} // namespace

CheckResult StartupCheck(std::string &reason) {
  // Anti-tamper first: a debugger can defeat the self-check, so a modified
  // binary must never even reach the debugger detection paths it could patch.
  int si = SelfIntegrity();
  if (si == 0) {
    reason = "This PRIMEx build has been modified and will not run.";
    return CheckResult::Tampered;
  }
  // Anti-debug: silent.
  if (DirectDebugger() || DebugToolsRunning())
    return CheckResult::SilentExit;
  reason.clear();
  return CheckResult::Ok;
}

bool Poll() {
  // Periodic re-check (debugger attach mid-session, tools spawned later).
  if (DirectDebugger() || DebugToolsRunning())
    return false;
  return true;
}

} // namespace guard
} // namespace px