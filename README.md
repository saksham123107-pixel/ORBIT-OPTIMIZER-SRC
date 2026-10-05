# PRIMEx Optimizer (ORBIT OPTIMIZER)

Windows PC optimizer with Discord OAuth authentication and KeyAuth license system.

## Architecture

| Layer | Tech | Location |
|-------|------|----------|
| **Native Core** | C++ (CMake, Win32) | `native/` |
| **Shell / UI** | C# WPF (.NET 8) | `shell/` |
| **Frontend** | TypeScript + Vite | `frontend/` |
| **Installer** | C++ (MinGW) | `installer/` |

## Features

- **PC Cleaning** - temp files, registry junk, browser caches
- **Debloat** - remove unwanted Windows services/app packages
- **RAM Booster** - working-set optimization
- **Tweaks** - configurable registry tweaks (applied/reverted)
- **Resolution / Boost** - display and game-aim helpers
- **Hotkey Manager** - global keyboard shortcuts
- **System Stats** - CPU, GPU, RAM monitoring (LibreHardwareMonitor)
- **Discord OAuth2** - PKCE login with guild role gating (Trial / Lifetime)
- **KeyAuth** - legacy license-key backend

## Environment Variables

Configure these before building/running (never hard-code them):

| Variable | Purpose |
|----------|---------|
| `PRIMEX_DISCORD_CLIENT_ID` | Discord Application Client ID |
| `PRIMEX_DISCORD_GUILD_ID` | Discord server ID for role checks |
| `PRIMEX_DISCORD_BOT_TOKEN` | (optional) bot token for server-join API |
| `PRIMEX_TRIAL_ROLE` | Trial role ID in your Discord guild |
| `PRIMEX_PREMIUM_ROLE` | Premium role ID in your Discord guild |
| `PRIMEX_KEYAUTH_OWNER_ID` | KeyAuth owner/application ID |

## Building

### Native Core (C++)
```bash
cd native
cmake -B build2 -G "MinGW Makefiles"
cmake --build build2 --config Release
```

### Shell (C# WPF)
```bash
cd shell
dotnet build -c Release
```

### Frontend (TypeScript / Vite)
```bash
cd frontend
npm install
npm run build
```

### Installer
```bash
cd installer
pwsh ./build_setup.ps1
```

## Tech Stack

- **C++** - Win32 API, WinHTTP, DPAPI, nlohmann/json, CMake
- **C#** - .NET 8 WPF, Microsoft.Web.WebView2, System.Text.Json
- **TypeScript** - Vite bundler, embedded inside WebView2
- **Authentication** - Discord OAuth2 (PKCE), KeyAuth 1.3 (Ed25519)

## License

Proprietary. All rights reserved.
