// primex_core.cpp — C ABI over the C++ engine (native/include/primex_core.h).
// Every out-string is malloc'd UTF-8 JSON, always {"ok":...}. Free with
// px_free(). Progress callbacks fire synchronously on the calling thread.
#include "primex_core.h"
#include "reg.h"

#include "aim.h"
#include "auth.h"
#include "booster.h"
#include "cleaner.h"
#include "hotkey.h"
#include "system.h"
#include "tweaks.h"
#include "util.h"

#include <mutex>

namespace {

std::mutex g_hotMutex;
PxHotkeyCb g_hostCb = nullptr;
void *g_hostUser = nullptr;

void HotkeyTrampoline(void *user) {
  PxHotkeyCb cb = nullptr;
  void *u = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_hotMutex);
    cb = g_hostCb;
    u = g_hostUser;
  }
  if (cb)
    cb(u);
  (void)user;
}

char *OkMsgDup(const std::string &message) { return px::OkMsg(message); }
char *ErrDup(const std::string &error) { return px::ErrJson(error); }
char *JsonDup(const nlohmann::json &j) { return px::DupJson(j); }

} // namespace

int px_set_tweaks_path(const wchar_t *path) {
  try {
    px::tweaks::SetTweaksPath(px::W(path));
    return 1;
  } catch (...) {
    return 0;
  }
}

int px_groups_json(char **out) {
  try {
    nlohmann::json g = px::tweaks::ListGroups();
    *out = JsonDup(nlohmann::json{{"ok", true}, {"groups", g}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_entries_json(const wchar_t *group, char **out) {
  try {
    nlohmann::json e = px::tweaks::ListEntries(px::W(group));
    *out = JsonDup(nlohmann::json{{"ok", true}, {"entries", e}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_apply_group(const wchar_t *group, PxProgressCb cb, void *user,
                   char **out) {
  try {
    std::string msg = px::tweaks::ApplyGroup(px::W(group), cb, user);
    *out = OkMsgDup(msg);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_apply_entries(const wchar_t *group, const wchar_t *entries_json,
                     PxProgressCb cb, void *user, char **out) {
  try {
    std::string msg = px::tweaks::ApplyEntryList(
        px::W(group), px::Narrow(px::W(entries_json)), cb, user);
    *out = OkMsgDup(msg);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_revert_group(const wchar_t *group, PxProgressCb cb, void *user,
                    char **out) {
  try {
    std::string msg = px::tweaks::RevertGroup(px::W(group), cb, user);
    *out = OkMsgDup(msg);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_has_backup(const wchar_t *group) {
  try {
    return px::tweaks::HasBackup(px::W(group)) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int px_aim_apply(char **out) {
  try {
    px::aim::ApplyAimReg();
    *out = OkMsgDup("AIM REG applied (your values). Log off + back on to load it.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_aim_restore(char **out) {
  try {
    if (!px::aim::Restore()) {
      *out = ErrDup("No backup found — nothing to restore.");
      return 0;
    }
    *out = OkMsgDup("Mouse settings restored. Log off + back on to load it.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_aim_has_backup(void) { return px::aim::HasBackup() ? 1 : 0; }

int px_mouse_state(char **out) {
  try {
    bool accel = true, prec = true;
    px::aim::GetState(accel, prec);
    *out = JsonDup(nlohmann::json{{"ok", true}, {"accel", accel}, {"precision", prec}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_mouse_accel(int on, char **out) {
  try {
    px::aim::SetAccel(on != 0);
    *out = OkMsgDup(on ? "Acceleration curves on."
                       : "Acceleration OFF (flat 1:1). Log off to load.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_mouse_precision(int on, char **out) {
  try {
    px::aim::SetPrecision(on != 0);
    *out = OkMsgDup(on ? "Pointer precision on."
                       : "Pointer precision OFF. Log off to load.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_clean_path(const wchar_t *path, char **out) {
  try {
    int n = px::cleaner::CleanPath(px::W(path));
    *out = JsonDup(nlohmann::json{{"ok", true}, {"count", n}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_force_clean(const wchar_t *path, char **out) {
  try {
    int n = px::cleaner::ForceCleanCount(px::W(path));
    *out = JsonDup(nlohmann::json{{"ok", true}, {"count", n}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_ram_flush(char **out) {
  try {
    px::sys::FlushRam();
    *out = OkMsgDup("RAM trimmed.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_resolution_get(char **out) {
  try {
    int w = 0, h = 0;
    px::sys::ResolutionGet(w, h);
    *out = JsonDup(nlohmann::json{{"ok", true}, {"w", w}, {"h", h}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_resolution_set(int w, int h, char **out) {
  try {
    if (!px::sys::ResolutionSet(w, h)) {
      *out = ErrDup("Resolution change rejected by the driver.");
      return 0;
    }
    *out = OkMsgDup("Resolution changed.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_flush_dns(char **out) {
  try {
    px::sys::FlushDns();
    *out = OkMsgDup("DNS flushed.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_clear_arp(char **out) {
  try {
    px::sys::ClearArp();
    *out = OkMsgDup("ARP cache cleared.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_restart_explorer(char **out) {
  try {
    px::sys::RestartExplorer();
    *out = OkMsgDup("Explorer restarted.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_kill_processes(const wchar_t *names_json, char **out) {
  try {
    auto arr = nlohmann::json::parse(px::Narrow(px::W(names_json)));
    std::vector<std::wstring> names;
    for (auto &n : arr)
      names.push_back(px::Widen(n.get<std::string>()));
    px::sys::KillProcesses(names);
    *out = OkMsgDup("Done.");
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_prioritize_games(char **out) {
  try {
    auto b = px::sys::PrioritizeGames();
    *out = JsonDup(nlohmann::json{{"ok", true}, {"boosted", b}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_booster(PxProgressCb cb, void *user, char **out) {
  try {
    std::string msg = px::booster::Run(cb, user);
    *out = OkMsgDup(msg);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_sys_stats(char **out) {
  try {
    nlohmann::json s = px::sys::SysStats();
    s["ok"] = true;
    *out = JsonDup(s);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_hotkey_set_callback(PxHotkeyCb cb, void *user) {
  try {
    {
      std::lock_guard<std::mutex> lk(g_hotMutex);
      g_hostCb = cb;
      g_hostUser = user;
    }
    px::hotkey::SetCallback(HotkeyTrampoline, nullptr);
    return 1;
  } catch (...) {
    return 0;
  }
}

int px_hotkey_start(int vk, char **out) {
  try {
    if (vk == 0) {
      *out = ErrDup("No key selected.");
      return 0;
    }
    std::string msg = px::hotkey::Start(vk);
    *out = OkMsgDup(msg);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_hotkey_stop(char **out) {
  try {
    std::string msg = px::hotkey::Stop();
    *out = OkMsgDup(msg);
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

int px_hotkey_status(char **out) {
  try {
    *out = JsonDup(nlohmann::json{{"ok", true},
                                  {"running", px::hotkey::IsRunning()},
                                  {"vk", px::hotkey::BoundVk()},
                                  {"bound", px::hotkey::BoundName()}});
    return 1;
  } catch (std::exception &e) {
    *out = ErrDup(e.what());
    return 0;
  }
}

void px_free(char *p) { std::free(p); }
