// booster.cpp — port of shell/Services/Booster.cs.
#include "booster.h"
#include "cleaner.h"
#include "reg.h"
#include "system.h"
#include "util.h"

namespace px {
namespace booster {

std::vector<std::string> PrioritizeGames() { return sys::PrioritizeGames(); }

std::string Run(PxProgressCb cb, void *user) {
  auto report = [&](int pct, const std::string &msg) { Emit(cb, user, pct, msg); };
  report(5, "Killing bloatware...");
  sys::KillProcesses({L"OneDrive", L"GameBar", L"Widgets"});

  report(20, "Trimming RAM...");
  sys::FlushRam();

  report(35, "Cleaning temp + prefetch...");
  int n = 0;
  n += cleaner::CleanPath(sys::TempPath());
  n += cleaner::CleanPath(L"C:\\Windows\\Temp");
  n += cleaner::CleanPath(L"C:\\Windows\\Prefetch");

  report(50, "DNS flush...");
  sys::FlushDns();

  report(60, "Deep clean...");
  n += cleaner::ForceCleanCount(sys::TempPath());
  n += cleaner::ForceCleanCount(L"C:\\Windows\\Temp");
  n += cleaner::ForceCleanCount(L"C:\\Windows\\Prefetch");

  report(75, "Closing background apps...");
  sys::KillProcesses({L"chrome", L"msedge", L"firefox", L"Spotify", L"Teams"});

  report(88, "Boosting running games...");
  auto boosted = sys::PrioritizeGames();

  report(100, "Booster complete.");
  std::string who;
  if (boosted.empty())
    who = "no game running";
  else {
    for (size_t i = 0; i < boosted.size(); ++i) {
      if (i) who += ", ";
      who += boosted[i];
    }
  }
  return "Booster complete - " + std::to_string(n) +
         " items cleaned. Priority: " + who + ".";
}

} // namespace booster
} // namespace px
