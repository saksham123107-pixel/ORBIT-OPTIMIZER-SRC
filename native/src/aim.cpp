// aim.cpp — port of shell/Services/AimRegistry.cs to Win32 C++.
#include "aim.h"
#include "reg.h"

#include <windows.h>

namespace px {
namespace aim {
namespace {

const wchar_t *kBackup = L"SOFTWARE\\PRIMEx Optimizer\\MouseBackup";
const wchar_t *kMouse = L"Control Panel\\Mouse";
const wchar_t *kMouClass =
    L"SYSTEM\\CurrentControlSet\\Services\\mouclass\\Parameters";

const uint8_t kDefaultX[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x15, 0x6E, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0xE9, 0x03, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kDefaultY[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0xB8, 0x0B, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0xCD, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0xE9, 0x03, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kFlatX[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0xC0, 0xCC, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x80, 0x99, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x40, 0x66, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x33, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kFlatY[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kAimX[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kAimY[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x66, 0xA6, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0xCD, 0x4C, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0xA0, 0x99, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x38, 0x33, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00};

const wchar_t *kMouseStrings[] = {
    L"MouseSpeed",        L"MouseThreshold1", L"MouseThreshold2",
    L"MouseSensitivity",  L"DoubleClickSpeed", L"MouseHoverTime",
    L"ActiveWindowTracking", L"Beep", L"DoubleClickHeight", L"DoubleClickWidth",
    L"ExtendedSounds", L"MouseHoverHeight", L"MouseHoverWidth", L"MouseTrails",
    L"SnapToDefaultButton", L"SwapMouseButtons"};

struct StrKV { const wchar_t *name; const wchar_t *value; };
const StrKV kAimRegStrings[] = {
    {L"Beep", L"No"}, {L"DoubleClickHeight", L"4"},
    {L"DoubleClickSpeed", L"500"}, {L"DoubleClickWidth", L"4"},
    {L"ExtendedSounds", L"No"}, {L"MouseHoverHeight", L"4"},
    {L"MouseHoverTime", L"9"}, {L"MouseHoverWidth", L"4"},
    {L"MouseSensitivity", L"6"}, {L"MouseSpeed", L"1"},
    {L"MouseThreshold1", L"6"}, {L"MouseThreshold2", L"10"},
    {L"MouseTrails", L"0"}, {L"SnapToDefaultButton", L"0"},
    {L"SwapMouseButtons", L"0"},
};

void SetBinary(HKEY root, const wchar_t *sub, const wchar_t *name,
               const uint8_t *data, DWORD len) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k,
                      nullptr) == ERROR_SUCCESS) {
    RegSetValueExW(k, name, 0, REG_BINARY, data, len);
    RegCloseKey(k);
  }
}

void SetString(HKEY root, const wchar_t *sub, const wchar_t *name,
               const wchar_t *value) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k,
                      nullptr) == ERROR_SUCCESS) {
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value,
                   (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
  }
}

void BackupCurrent() {
  HKEY src = nullptr;
  HKEY bak = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kBackup, 0, nullptr, 0, KEY_SET_VALUE,
                      nullptr, &bak, nullptr) != ERROR_SUCCESS)
    return;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kMouse, 0, KEY_QUERY_VALUE, &src) ==
      ERROR_SUCCESS) {
    for (const wchar_t *name : kMouseStrings) {
      DWORD type = 0, size = 0;
      if (RegQueryValueExW(src, name, nullptr, &type, nullptr, &size) !=
              ERROR_SUCCESS ||
          size == 0 || size > 4096)
        continue;
      std::vector<uint8_t> buf(size);
      if (RegQueryValueExW(src, name, nullptr, &type, buf.data(), &size) !=
          ERROR_SUCCESS)
        continue;
      RegSetValueExW(bak, name, 0, type == REG_DWORD ? REG_DWORD
                                     : type == REG_BINARY ? REG_BINARY
                                                          : REG_SZ,
                     buf.data(), size);
    }
    DWORD type = 0, size = 0;
    for (const wchar_t *curve : {L"SmoothMouseXCurve", L"SmoothMouseYCurve"}) {
      size = 0;
      if (RegQueryValueExW(src, curve, nullptr, &type, nullptr, &size) ==
              ERROR_SUCCESS &&
          size > 0 && size <= 4096) {
        std::vector<uint8_t> buf(size);
        if (RegQueryValueExW(src, curve, nullptr, nullptr, buf.data(),
                             &size) == ERROR_SUCCESS)
          RegSetValueExW(bak, curve, 0, REG_BINARY, buf.data(), size);
      }
    }
    RegCloseKey(src);
  }
  HKEY mq = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kMouClass, 0, KEY_QUERY_VALUE, &mq) ==
      ERROR_SUCCESS) {
    DWORD q = 0, size = sizeof(q), type = 0;
    if (RegQueryValueExW(mq, L"MouseDataQueueSize", nullptr, &type,
                         (BYTE *)&q, &size) == ERROR_SUCCESS)
      RegSetValueExW(bak, L"MouseDataQueueSize", 0, REG_DWORD, (BYTE *)&q,
                     sizeof(q));
    RegCloseKey(mq);
  }
  RegCloseKey(bak);
}

} // namespace

bool HasBackup() {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kBackup, 0, KEY_QUERY_VALUE, &k) !=
      ERROR_SUCCESS)
    return false;
  DWORD type = 0, size = 0;
  bool ok = RegQueryValueExW(k, L"MouseSpeed", nullptr, &type, nullptr,
                             &size) == ERROR_SUCCESS;
  RegCloseKey(k);
  return ok;
}

void ApplyAimReg() {
  BackupCurrent();
  for (const auto &kv : kAimRegStrings)
    SetString(HKEY_CURRENT_USER, kMouse, kv.name, kv.value);
  HKEY k = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kMouse, 0, nullptr, 0, KEY_SET_VALUE,
                      nullptr, &k, nullptr) == ERROR_SUCCESS) {
    DWORD zero = 0;
    RegSetValueExW(k, L"ActiveWindowTracking", 0, REG_DWORD, (BYTE *)&zero,
                   sizeof(zero));
    RegSetValueExW(k, L"SmoothMouseXCurve", 0, REG_BINARY, kAimX,
                   (DWORD)sizeof(kAimX));
    RegSetValueExW(k, L"SmoothMouseYCurve", 0, REG_BINARY, kAimY,
                   (DWORD)sizeof(kAimY));
    RegCloseKey(k);
  }
}

void Apply() {
  BackupCurrent();
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseSpeed", L"1");
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseThreshold1", L"6");
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseThreshold2", L"10");
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseSensitivity", L"10");
  SetBinary(HKEY_CURRENT_USER, kMouse, L"SmoothMouseXCurve", kDefaultX,
            (DWORD)sizeof(kDefaultX));
  SetBinary(HKEY_CURRENT_USER, kMouse, L"SmoothMouseYCurve", kDefaultY,
            (DWORD)sizeof(kDefaultY));
  HKEY k = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kMouClass, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
    DWORD q = 100;
    RegSetValueExW(k, L"MouseDataQueueSize", 0, REG_DWORD, (BYTE *)&q,
                   sizeof(q));
    RegCloseKey(k);
  }
}

void GetState(bool &accelOn, bool &precisionOn) {
  accelOn = true;
  precisionOn = true;
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kMouse, 0, KEY_QUERY_VALUE, &k) !=
      ERROR_SUCCESS)
    return;
  uint8_t buf[256]{};
  DWORD size = sizeof(buf), type = 0;
  if (RegQueryValueExW(k, L"SmoothMouseXCurve", nullptr, &type, buf, &size) ==
      ERROR_SUCCESS) {
    if (size == sizeof(kFlatX) && memcmp(buf, kFlatX, sizeof(kFlatX)) == 0)
      accelOn = false;
  }
  wchar_t sp[64]{};
  size = sizeof(sp);
  if (RegQueryValueExW(k, L"MouseSpeed", nullptr, &type, (BYTE *)sp, &size) ==
      ERROR_SUCCESS) {
    if (wcscmp(sp, L"0") == 0)
      precisionOn = false;
  }
  RegCloseKey(k);
}

void SetAccel(bool on) {
  BackupCurrent();
  SetBinary(HKEY_CURRENT_USER, kMouse, L"SmoothMouseXCurve",
            on ? kDefaultX : kFlatX, (DWORD)sizeof(kDefaultX));
  SetBinary(HKEY_CURRENT_USER, kMouse, L"SmoothMouseYCurve",
            on ? kDefaultY : kFlatY, (DWORD)sizeof(kDefaultY));
}

void SetPrecision(bool on) {
  BackupCurrent();
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseSpeed", on ? L"1" : L"0");
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseThreshold1", on ? L"6" : L"0");
  SetString(HKEY_CURRENT_USER, kMouse, L"MouseThreshold2", on ? L"10" : L"0");
}

bool Restore() {
  if (!HasBackup())
    return false;
  HKEY bak = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kBackup, 0, KEY_QUERY_VALUE, &bak) !=
      ERROR_SUCCESS)
    return false;
  for (const wchar_t *name : kMouseStrings) {
    uint8_t buf[1024]{};
    DWORD size = sizeof(buf), type = 0;
    if (RegQueryValueExW(bak, name, nullptr, &type, buf, &size) ==
        ERROR_SUCCESS) {
      HKEY m = nullptr;
      if (RegCreateKeyExW(HKEY_CURRENT_USER, kMouse, 0, nullptr, 0,
                          KEY_SET_VALUE, nullptr, &m, nullptr) ==
          ERROR_SUCCESS) {
        RegSetValueExW(m, name, 0, type, buf, size);
        RegCloseKey(m);
      }
    }
  }
  for (const wchar_t *curve : {L"SmoothMouseXCurve", L"SmoothMouseYCurve"}) {
    uint8_t buf[512]{};
    DWORD size = sizeof(buf), type = 0;
    if (RegQueryValueExW(bak, curve, nullptr, &type, buf, &size) ==
        ERROR_SUCCESS) {
      HKEY m = nullptr;
      if (RegCreateKeyExW(HKEY_CURRENT_USER, kMouse, 0, nullptr, 0,
                          KEY_SET_VALUE, nullptr, &m, nullptr) ==
          ERROR_SUCCESS) {
        RegSetValueExW(m, curve, 0, REG_BINARY, buf, size);
        RegCloseKey(m);
      }
    }
  }
  DWORD q = 0, size = sizeof(q), type = 0;
  if (RegQueryValueExW(bak, L"MouseDataQueueSize", nullptr, &type, (BYTE *)&q,
                       &size) == ERROR_SUCCESS) {
    HKEY m = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kMouClass, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &m, nullptr) ==
        ERROR_SUCCESS) {
      RegSetValueExW(m, L"MouseDataQueueSize", 0, REG_DWORD, (BYTE *)&q,
                     sizeof(q));
      RegCloseKey(m);
    }
  }
  RegCloseKey(bak);
  return true;
}

} // namespace aim
} // namespace px
