// aim.h — mouse / aim registry tweaks (port of shell/Services/AimRegistry.cs).
#pragma once
#include <string>
#include "json.hpp"

namespace px {
namespace aim {

bool HasBackup();
void ApplyAimReg();   // user's personal tuned values
void Apply();         // smoothing defaults (DefaultX/DefaultY)
bool Restore();
void GetState(bool &accelOn, bool &precisionOn);
void SetAccel(bool on);
void SetPrecision(bool on);

} // namespace aim
} // namespace px
