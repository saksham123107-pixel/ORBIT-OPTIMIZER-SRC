// primex_core.h — PRIMEx native engine C ABI.
// Strings in: UTF-16 (wchar_t*). Strings out: malloc'd UTF-8 JSON,
// always {"ok":true,...} or {"ok":false,"error":"..."} — free with px_free().
// Progress callbacks fire synchronously on the calling thread.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PxProgressCb)(int pct, const char *msg_utf8, void *user);

/* JSON doc must stay valid (parsed once, embedded at build). */
int px_set_tweaks_path(const wchar_t *path);

/* tweaks groups */
int px_groups_json(char **out);
int px_entries_json(const wchar_t *group, char **out);
int px_apply_group(const wchar_t *group, PxProgressCb cb, void *user,
                   char **out);
int px_apply_entries(const wchar_t *group, const wchar_t *entries_json,
                     PxProgressCb cb, void *user, char **out);
int px_revert_group(const wchar_t *group, PxProgressCb cb, void *user,
                    char **out);
int px_has_backup(const wchar_t *group);

/* aim registry (personal values + smooth/raw controls) */
int px_aim_apply(char **out);
int px_aim_restore(char **out);
int px_aim_has_backup(void);
int px_mouse_state(char **out); /* {accel,precision} */
int px_mouse_accel(int on, char **out);
int px_mouse_precision(int on, char **out);

/* cleaners -> {"ok":true,"count":N} */
int px_clean_path(const wchar_t *path, char **out);
int px_force_clean(const wchar_t *path, char **out);

/* ram / resolution / system */
int px_ram_flush(char **out);
int px_resolution_get(char **out); /* {w,h} */
int px_resolution_set(int w, int h, char **out);
int px_flush_dns(char **out);
int px_clear_arp(char **out);
int px_restart_explorer(char **out);
int px_kill_processes(const wchar_t *names_json, char **out); /* ["a","b"] */
int px_prioritize_games(char **out); /* {boosted:[...]} */

/* booster (all-in-one, mirrors old TS flow) */
int px_booster(PxProgressCb cb, void *user, char **out);

/* live stats */
int px_sys_stats(char **out);

/* hotkey: C++ polls, fires registered C# callback on press */
typedef void (*PxHotkeyCb)(void *user);
int px_hotkey_set_callback(PxHotkeyCb cb, void *user);
int px_hotkey_start(int vk, char **out);
int px_hotkey_stop(char **out);
int px_hotkey_status(char **out); /* {running,vk} */

void px_free(char *p);

#ifdef __cplusplus
}
#endif
