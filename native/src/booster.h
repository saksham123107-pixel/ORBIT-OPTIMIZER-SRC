// booster.h — one-shot BOOSTER (port of shell/Services/Booster.cs).
#pragma once
#include <string>
#include <vector>
#include "primex_core.h"

namespace px {
namespace booster {

// Runs bloatware kill + RAM trim + clean + DNS + deep clean +
// background kill + game priority, with progress. Returns summary message.
std::string Run(PxProgressCb cb, void *user);
std::vector<std::string> PrioritizeGames();

} // namespace booster
} // namespace px
