// hotkey.cpp — port of shell/Services/HotkeyManager.cs.
// Polls one bound VK with GetAsyncKeyState, fires the registered C callback.
#include "hotkey.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

namespace px {
namespace hotkey {
namespace {

std::atomic<bool> g_running{false};
std::atomic<int> g_vk{0};
std::string g_name;
PxHotkeyCb g_cb = nullptr;
void *g_user = nullptr;
std::mutex g_mu;
std::thread g_thread;

std::string VkName(int vk) {
  char b[32]{};
  snprintf(b, sizeof(b), "VK 0x%X", (unsigned)vk);
  return b;
}

} // namespace

void SetCallback(PxHotkeyCb cb, void *user) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_cb = cb;
  g_user = user;
}

std::string Start(int vk) {
  Stop();
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_vk = vk;
    g_name = VkName(vk);
    g_running = true;
  }
  g_thread = std::thread([] {
    while (g_running) {
      int vk = g_vk.load();
      if (vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0) {
        PxHotkeyCb cb = nullptr;
        void *user = nullptr;
        {
          std::lock_guard<std::mutex> lk(g_mu);
          cb = g_cb;
          user = g_user;
        }
        if (cb)
          cb(user);
        while ((GetAsyncKeyState(vk) & 0x8000) != 0)
          Sleep(10);
      }
      Sleep(50);
    }
  });
  g_thread.detach();
  int w = GetSystemMetrics(SM_CXSCREEN);
  int h = GetSystemMetrics(SM_CYSCREEN);
  char b[128]{};
  snprintf(b, sizeof(b), "Hotkey bound -> Aim Optimizer (%dx%d).", w, h);
  return b;
}

std::string Stop() {
  g_running = false;
  std::lock_guard<std::mutex> lk(g_mu);
  g_vk = 0;
  g_name.clear();
  return "Hotkey stopped.";
}

bool IsRunning() { return g_running; }
int BoundVk() { return g_vk.load(); }
std::string BoundName() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_name;
}

} // namespace hotkey
} // namespace px
