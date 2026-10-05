// guard.h — anti-debug + anti-tamper for the PRIMEx host.
//  - ANTI-DEBUG: live debugger / known reverse-engineering tools -> silent exit
//  - ANTI-TAMPER: the exe hashes itself at startup; modified -> refuse to run
//  - ANTI-CRACK lives in auth.cpp: tier/expiry/hwid re-fetched from GitHub.
#pragma once
#include <string>

namespace px {
namespace guard {

enum class CheckResult {
  Ok,          // all good, continue
  SilentExit,  // debugger / tamper tools detected — quit without a window
  Tampered,    // self-hash mismatch — show message and refuse to run
};

// Full startup check (debug + tamper). Runs once before the window shows.
CheckResult StartupCheck(std::string &reason);

// Lightweight re-check used by the periodic timer (debugger only).
bool Poll();

} // namespace guard
} // namespace px