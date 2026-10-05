// hotkey.h — global hotkey poller (port of HotkeyManager.cs).
#pragma once
#include <string>
#include "primex_core.h"

namespace px {
namespace hotkey {

void SetCallback(PxHotkeyCb cb, void *user);
std::string Start(int vk);
std::string Stop();
bool IsRunning();
int BoundVk();
std::string BoundName();

} // namespace hotkey
} // namespace px
