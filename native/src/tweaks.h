// tweaks.h — curated registry packs (port of TweaksService.cs).
#pragma once
#include <string>
#include "json.hpp"
#include "primex_core.h"

namespace px {
namespace tweaks {

void SetTweaksPath(const std::wstring &path);

nlohmann::json ListGroups();              // [{id,title,desc,danger,count,...}]
nlohmann::json ListEntries(const std::wstring &groupId);
std::string ApplyGroup(const std::wstring &groupId, PxProgressCb cb, void *user);
std::string ApplyEntryList(const std::wstring &groupId,
                           const std::string &entriesJsonUtf8, PxProgressCb cb,
                           void *user);
std::string RevertGroup(const std::wstring &groupId, PxProgressCb cb, void *user);
bool HasBackup(const std::wstring &groupId);

// Live registry state per card: [{id,applied,matched,total}].
// "applied" = every entry already matches its target value (Already applied).
nlohmann::json StatusAll();
// Group json flags (premium packs + command/powershell action groups).
bool GroupPremium(const std::string &groupId);
std::string GroupAction(const std::string &groupId);
std::string CardCategory(const std::string &cardId);

} // namespace tweaks
} // namespace px
