// selftest.cpp — console smoke test for the C++ engine (no WebView2).
// Exercises resolution, stats, tweaks load, aim state. Used to validate
// MinGW/MSVC builds of primex_core without the full host.
#include <conio.h>
#include <cstdio>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "primex_core.h"
#include "tweaks.h"

static void Show(const char *label, char *p) {
  std::printf("[%s] %s\n", label, p ? p : "(null)");
  px_free(p);
}

int main() {
  char *o = nullptr;
  // tweaks.json candidates depend on the working directory; try each.
  const wchar_t *cands[] = {
      L"shell\\Optimizer\\tweaks.json",       // repo root
      L"..\\shell\\Optimizer\\tweaks.json",   // native/build
      L"Optimizer\\tweaks.json",              // beside the exe
      L"dist\\tweaks.json",
  };
  for (auto c : cands) {
    px_set_tweaks_path(c);
    char *g = nullptr;
    if (px_groups_json(&g)) {
      std::printf("[tweaks-path] %ls\n", c);
      Show("groups", g);
      break;
    }
    px_free(g);
    if (c == cands[3])
      Show("groups-ERR", g);
  }
  if (px_resolution_get(&o))
    Show("resolution", o);
  else
    Show("resolution-ERR", o);
  if (px_sys_stats(&o))
    Show("stats", o);
  else
    Show("stats-ERR", o);
  if (px_mouse_state(&o))
    Show("mouse", o);
  else
    Show("mouse-ERR", o);
  if (px_hotkey_status(&o))
    Show("hotkey", o);
  else
    Show("hotkey-ERR", o);
  std::printf("aim backup: %d\n", px_aim_has_backup());
  // Live registry status + imported pack flags (free/paid + action groups).
  int rc = 0;
  {
    auto cards = px::tweaks::ListGroups();
    auto st = px::tweaks::StatusAll();
    int applied = 0, capCount = -1, recycleGone = 1;
    for (auto &s : st)
      if (s.value("applied", false))
        ++applied;
    for (auto &c : cards) {
      if (c.value("id", "") == "capabilities")
        capCount = c.value("count", -1);
      if (c.value("id", "") == "cmd-recycle")
        recycleGone = 0;
    }
    if (px::tweaks::GroupAction("cmd-recycle") != "")
      recycleGone = 0;
    std::string catCpu = px::tweaks::CardCategory("cpu-amd");
    std::string catMouse = px::tweaks::CardCategory("primex-full::MOUSE");
    std::printf(
        "[tweaks] cards=%zu status=%zu applied=%d prem-qos=%d hibernate=%d\n"
        "[tweaks] cap-count=%d cap-cat=%s cat-cpu=[%s] cat-mouse=[%s] "
        "recycle-removed=%d\n",
        cards.size(), st.size(), applied,
        px::tweaks::GroupPremium("prem-qos-dscp-priority") ? 1 : 0,
        px::tweaks::GroupAction("power-hibernate-off") == "command" ? 1 : 0,
        capCount, capCount == 36 ? "ok" : "BAD", catCpu.c_str(),
        catMouse.c_str(), recycleGone);
    if (cards.size() != st.size()) {
      std::printf("[tweaks] MISMATCH card/status counts!\n");
      rc = 1;
    }
    if (capCount != 36 || catCpu != "CPU Priority" ||
        catMouse != "Personalization" || !recycleGone)
      rc = 1;
  }
  std::puts("selftest done.");
  // Keep the window open when launched from Explorer (double-click).
  // _getch() reads the console directly (not stdin), so this works even
  // if stdin is closed. Skipped when there is no console at all or stdin
  // is piped, so scripts/automation never hang.
  if (GetConsoleWindow() != nullptr && _isatty(_fileno(stdin))) {
    std::puts("Press any key to exit...");
    fflush(stdout);
    _getch();
  }
  return rc;
}
