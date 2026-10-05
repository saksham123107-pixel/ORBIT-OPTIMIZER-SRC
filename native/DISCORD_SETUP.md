# PRIMEx Optimizer Discord authentication setup

## Discord Developer Portal

Register this exact redirect URI:

`http://127.0.0.1:48731/callback`

The application uses OAuth2 Authorization Code + PKCE and requests `identify`, `guilds.join`, and `guilds.members.read`.

The bot needs the **Server Members Intent** enabled in the Developer Portal (Bot → Privileged Gateway Intents) so guild-member endpoints can resolve roles.

## Trusted bot token

The bot token lives in `native/auth.env` (build input only) and is **hex-embedded into `PRIMEx Optimizer.exe` at build time** by `cmake/embed_auth.cmake`. No `auth.env` file is required or shipped next to the EXE.

- Runtime order: embedded token → optional `auth.env` beside the EXE (dev override) → `PRIMEX_DISCORD_BOT_TOKEN` env var.
- Do not commit the token to a public GitHub repo. Rotate any token that has previously been exposed.
- To change the token: edit `native/auth.env`, rebuild (CMake re-embeds on change).

The bot must be in guild `<YOUR_GUILD_ID>` and have permission to add OAuth-authorized users.

## Roles

- `<YOUR_TRIAL_ROLE_ID>` = 7-day access
- `<YOUR_PREMIUM_ROLE_ID>` = lifetime premium

Premium takes precedence when both roles exist.

## Session

Discord refresh tokens are stored locally using Windows DPAPI in `discord-session.dat`; the plaintext refresh token is never written to disk.

## KeyAuth

KeyAuth activation remains available and is treated as lifetime premium. The client no longer sends a locally generated HWID. If the KeyAuth application itself has server-side HWID enforcement enabled, disable that setting in KeyAuth as well.
