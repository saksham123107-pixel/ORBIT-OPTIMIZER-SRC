// system.h — RAM / resolution / process / stats helpers.
// Port of RamBooster.cs, Resolution.cs, SystemManager.cs, SysStats.cs.
#pragma once
#include <string>
#include <vector>
#include "json.hpp"

namespace px {
namespace sys {

// RAM
void FlushRam();

// Resolution
void ResolutionGet(int &w, int &h);
bool ResolutionSet(int w, int h);

// Process / network
void FlushDns();
void ClearArp();
void RestartExplorer();
void KillProcesses(const std::vector<std::wstring> &names);
std::vector<std::string> PrioritizeGames();

// Stats -> {cpu,cpuTemp,gpu,gpuTemp,gpuName,ramPct,ramGb,ramTotal}
// (temps may be null when sensors unavailable — same as C# nullable shape)
nlohmann::json SysStats();

// Misc
std::wstring TempPath();
unsigned long long TotalRamKb();

} // namespace sys
} // namespace px
