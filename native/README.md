# PRIMEx native backend (C++) — engine + Win32/WebView2 host

The C# backend (`shell/`, WPF + .NET 8) is replaced by pure Win32 C++17.
The Vite+TS UI (`frontend/`) is **untouched** — same `dist/`, same IPC
contract (`{id,cmd,args}` ↔ `{id,ok,data}` + `{type:event}`).

## Layout

```
native/
  include/primex_core.h   C ABI (unchanged contract)
  include/json.hpp        nlohmann/json (vendored)
  src/
    reg.h / util.h        registry + Win32 helpers
    aim.cpp               mouse/aim registry (AimRegistry.cs)
    cleaner.cpp           file cleaners (Cleaner.cs)
    system.cpp            RAM/resolution/process/stats
                          (RamBooster.cs, Resolution.cs,
                           SystemManager.cs, SysStats.cs)
    tweaks.cpp            registry packs (TweaksService.cs)
    booster.cpp           one-shot booster (Booster.cs)
    hotkey.cpp            global hotkey poller (HotkeyManager.cs)
    auth.cpp              KeyAuth 1.3 client (KeyAuthService.cs)
    primex_core.cpp       C ABI wrappers
  host/
    main.cpp              borderless Win32 window + WebView2
                          (MainWindow.xaml[.cs])
    ipc.cpp               IPC router — same commands, same shapes
    app.manifest          requireAdministrator (same as shell/)
  selftest.cpp            engine smoke test (no WebView2)
  CMakeLists.txt
```

## IPC parity (frontend needs zero changes)

All commands from `MainWindow.xaml.cs → DispatchAsync` exist with the
same names and shapes: `sys.getInfo`, `auth:*`, `sys.openUrl`,
`aim.*`, `mouse.*`, `sys.stats`, `booster.run`, `game.boost`,
`clean.*`, `hotkey.*`, `tweaks.*`, plus `win.*` chrome and
`progress` / `win.state` / `notify` events.

## Notes vs the C# backend

- `SysStats`: CPU (GetSystemTimes) + RAM (GlobalMemoryStatusEx) + GPU
  name (display device). Temps are `null` (no LibreHardwareMonitor in
  C++); the UI already handles nulls.
- `auth`: same KeyAuth 1.3 endpoint/fields/app credentials, DPAPI
  `session.dat`, tier map, 24h offline grace. Server authenticity via
  TLS chain validation; failures return errors — the process is never
  killed (the C# SDK called TerminateProcess).
- `tweaks.json` loads from disk (`Optimizer/tweaks.json` next to the
  exe); point elsewhere with `px_set_tweaks_path`. The C# build embedded
  it as a resource.

## Build

Engine + smoke test (any toolchain, e.g. MinGW):

```powershell
cd native
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\px_selftest.exe
```

Full host (needs MSVC Build Tools + WebView2 SDK):

```powershell
nuget install Microsoft.Web.WebView2 -OutputDirectory packages
cmake -S native -B native\build -DWEBVIEW2_ROOT="packages\Microsoft.Web.WebView2.<ver>"
cmake --build native\build --config Release
# copy frontend\dist next to primex-glass.exe (and Optimizer\tweaks.json)
```
