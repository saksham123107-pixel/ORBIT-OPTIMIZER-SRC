// services.h — Windows Service Control Manager (Services tab).
#pragma once
#include <string>
#include <vector>
#include "json.hpp"
#include "primex_core.h"

namespace px {
namespace services {

// All services: [{name, displayName, status, startType}]
nlohmann::json List();

// action: "start" | "stop" | "disable" | "manual" | "auto"
// disable/manual/auto only change start type (does not stop a running service).
nlohmann::json Configure(const std::string &name, const std::string &action);

// Apply the curated safe-disable start-type list (no service is force-stopped).
nlohmann::json SafeDisable(PxProgressCb cb, void *user);

// Named packs: telemetry | updates | xbox | store | print-sensor | remote | misc
nlohmann::json DisablePack(const std::string &pack, PxProgressCb cb, void *user);

} // namespace services
} // namespace px
