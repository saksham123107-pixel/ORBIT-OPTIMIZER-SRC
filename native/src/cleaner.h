// cleaner.h — file cleaners (port of shell/Services/Cleaner.cs).
#pragma once
#include <string>

namespace px {
namespace cleaner {

// Best-effort delete; returns deleted item count, skips locked files.
int CleanPath(const std::wstring &path);
int ForceCleanCount(const std::wstring &path);

} // namespace cleaner
} // namespace px
