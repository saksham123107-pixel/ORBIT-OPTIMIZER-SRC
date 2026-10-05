// debloat.h — AppX / bundled-app removal (PRIMEx Debloat tab).
#pragma once
#include <string>
#include <vector>
#include "json.hpp"
#include "primex_core.h"

namespace px {
namespace debloat {

// Installed packages for current user: [{name, fullName, displayName}]
nlohmann::json ListPackages();

// Remove selected package full names. Returns {message, removed, failed}
nlohmann::json Remove(const std::vector<std::string> &packageFullNames,
                      PxProgressCb cb, void *user);

// Common bloat package name patterns (for frontend presets).
nlohmann::json Presets();

} // namespace debloat
} // namespace px
